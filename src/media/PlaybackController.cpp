#include "media/PlaybackController.h"
#include "media/AudioDecoder.h"
#include "media/AudioOutput.h"
#include "media/DebugStats.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>

extern "C" {
#include <libavutil/imgutils.h>
}

static double wallClock() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

PlaybackController::~PlaybackController() {
    closeFile();
    if (m_currentFrameBuffer) {
        av_free(m_currentFrameBuffer);
        m_currentFrameBuffer = nullptr;
    }
}

bool PlaybackController::openFile(const std::string& path) {
    closeFile();

    if (!m_mediaFile.open(path)) return false;

    m_audioStreamIdx = m_mediaFile.getAudioStreamIndex();

    if (m_mediaFile.getVideoStreamIndex() >= 0) {
        AVStream* vstream = m_mediaFile.getVideoStream();
        auto* par = vstream->codecpar;
        auto tb = vstream->time_base;
        auto fr = vstream->avg_frame_rate;

        m_videoDecoder = new VideoDecoder();
        if (!m_videoDecoder->init(par, tb, fr)) {
            delete m_videoDecoder;
            m_videoDecoder = nullptr;
            return false;
        }

        int w = m_videoDecoder->getWidth();
        int h = m_videoDecoder->getHeight();

        // Pre-allocate video frame queue ring buffers
        if (!m_videoFrameQueue.allocate(w, h)) {
            fprintf(stderr, "Failed to allocate video frame queue\n");
            delete m_videoDecoder;
            m_videoDecoder = nullptr;
            return false;
        }

        int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
        m_currentFrameBuffer = (uint8_t*)av_malloc(bufSize);
        m_currentFrameWidth = w;
        m_currentFrameHeight = h;
    }

    if (m_audioStreamIdx >= 0) {
        auto* par = m_mediaFile.getAudioCodecPar();
        auto tb = m_mediaFile.getFormatContext()->streams[m_audioStreamIdx]->time_base;

        m_audioDecoder = new AudioDecoder();
        if (!m_audioDecoder->init(par, tb)) {
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
            m_audioStreamIdx = -1;
        }
    }

    return true;
}

void PlaybackController::closeFile() {
    stop();
    stopThreads();

    delete m_videoDecoder;
    m_videoDecoder = nullptr;

    delete m_audioDecoder;
    m_audioDecoder = nullptr;

    m_mediaFile.close();
    m_audioStreamIdx = -1;

    if (m_currentFrameBuffer) {
        av_free(m_currentFrameBuffer);
        m_currentFrameBuffer = nullptr;
    }
    m_timerInitialized = false;
    m_audioStarted = false;
}

void PlaybackController::play() {
    if (!m_mediaFile.isOpen()) return;
    if (m_state.load() == State::Playing) return;

    if (m_state.load() == State::Paused) {
        // Recalculate wall-clock reference so PTS pacing resumes correctly
        double now = wallClock();
        double currentPts = m_videoClock.get();
        m_streamStartWall = now - (currentPts - m_streamStartPts);

        m_videoClock.resume();
        m_audioClock.resume();
        if (m_audioOutput && m_audioStarted) m_audioOutput->resume();
        m_state.store(State::Playing);
        return;
    }

    // Start from stopped
    m_videoPacketQueue.start();
    m_audioPacketQueue.start();
    m_videoFrameQueue.start();
    m_audioFrameQueue.start();

    if (m_videoDecoder) {
        m_videoDecoder->start(m_videoPacketQueue, m_videoFrameQueue);
    }
    if (m_audioDecoder) {
        m_audioDecoder->start(m_audioPacketQueue, m_audioFrameQueue);
    }

    m_demuxRunning.store(true);
    m_demuxThread = std::thread(&PlaybackController::demuxLoop, this);

    // DON'T start audio yet — we'll start it when the first video frame arrives
    m_audioStarted = false;
    m_timerInitialized = false;

    m_videoClock.set(0.0);
    m_videoClock.resume();
    m_audioClock.set(0.0);
    m_audioClock.resume();

    m_stats = {};
    m_fpsCounterFrames = 0;
    m_fpsCounterStart = wallClock();
    g_stats.reset();

    m_state.store(State::Playing);
}

void PlaybackController::pause() {
    if (m_state.load() != State::Playing) return;
    m_videoClock.pause();
    m_audioClock.pause();
    if (m_audioOutput && m_audioStarted) m_audioOutput->pause();
    m_state.store(State::Paused);
}

void PlaybackController::togglePlayPause() {
    if (m_state.load() == State::Playing) pause();
    else play();
}

void PlaybackController::stop() {
    if (m_state.load() == State::Stopped) return;

    stopThreads();

    m_videoClock.set(0.0);
    m_videoClock.pause();
    m_audioClock.set(0.0);
    m_audioClock.pause();
    m_timerInitialized = false;
    m_audioStarted = false;

    if (m_audioOutput) m_audioOutput->pause();

    m_state.store(State::Stopped);
}

void PlaybackController::seek(double seconds) {
    if (!m_mediaFile.isOpen()) return;
    seconds = std::max(0.0, std::min(seconds, getDuration()));
    m_seekTarget.store(seconds);
    m_seekRequested.store(true);
}

void PlaybackController::stopThreads() {
    m_demuxRunning.store(false);
    m_videoPacketQueue.abort();
    m_audioPacketQueue.abort();
    m_videoFrameQueue.abort();
    m_audioFrameQueue.abort();

    if (m_videoDecoder) m_videoDecoder->stop();
    if (m_audioDecoder) m_audioDecoder->stop();
    if (m_demuxThread.joinable()) m_demuxThread.join();

    m_videoPacketQueue.flush();
    m_audioPacketQueue.flush();
    m_videoFrameQueue.flush();
    m_audioFrameQueue.flush();
}

const uint8_t* PlaybackController::getVideoFrame(int& width, int& height) {
    if (!m_videoDecoder || m_state.load() == State::Stopped) {
        width = 0;
        height = 0;
        return nullptr;
    }

    width = m_currentFrameWidth;
    height = m_currentFrameHeight;

    if (m_state.load() == State::Paused) {
        return m_currentFrameBuffer;
    }

    double now = wallClock();
    m_stats.queueDepth = static_cast<int>(m_videoFrameQueue.size());
    g_stats.videoFrameQueueDepth.store(m_stats.queueDepth);
    g_stats.videoPacketQueueDepth.store(static_cast<int>(m_videoPacketQueue.size()));
    g_stats.tick();

    // Wait for first decoded frame to arrive before starting anything
    if (!m_timerInitialized) {
        int64_t firstPts;
        const uint8_t* firstData = m_videoFrameQueue.peek(&firstPts);
        if (!firstData) {
            m_stats.repeated++;
            return m_currentFrameBuffer;
        }

        double firstPtsSec = firstPts * av_q2d(m_videoDecoder->getTimeBase());
        m_streamStartPts = firstPtsSec;
        m_streamStartWall = now;
        m_videoClock.set(firstPtsSec);
        m_audioClock.set(firstPtsSec);
        m_timerInitialized = true;

        // Start audio synchronized with the first video frame
        if (m_audioOutput && !m_audioStarted) {
            m_audioOutput->resume();
            m_audioStarted = true;
        }
    }

    // PTS-based pacing: how far into the stream should we be based on wall clock?
    double streamElapsed = now - m_streamStartWall;
    double targetPts = m_streamStartPts + streamElapsed;

    // Peek at next frame
    int64_t pts;
    int linesize;
    g_stats.mainPeekCalls++;
    const uint8_t* data = m_videoFrameQueue.peek(&pts, &linesize);
    if (!data) {
        m_stats.repeated++;
        g_stats.mainPeekNull++;
        g_stats.mainFramesRepeated++;
        return m_currentFrameBuffer;
    }

    double ptsSec = pts * av_q2d(m_videoDecoder->getTimeBase());

    double frameDuration = 1.0 / m_videoDecoder->getFrameRate();

    // Frame is in the future — not time yet, hold current frame
    if (ptsSec > targetPts + frameDuration * 0.5) {
        m_stats.repeated++;
        return m_currentFrameBuffer;
    }

    // If we're seriously behind (>3 frames), skip to catch up.
    // This only happens after seeks or stalls, not during normal playback.
    double skipThreshold = frameDuration * 3.0;
    while (ptsSec < targetPts - skipThreshold) {
        m_videoFrameQueue.pop();
        g_stats.mainFramesSkipped++;
        data = m_videoFrameQueue.peek(&pts, &linesize);
        if (!data) break;
        ptsSec = pts * av_q2d(m_videoDecoder->getTimeBase());
    }

    if (!data) {
        m_stats.repeated++;
        return m_currentFrameBuffer;
    }

    // Copy into display buffer
    int w = m_currentFrameWidth;
    int h = m_currentFrameHeight;
    for (int y = 0; y < h; y++) {
        memcpy(m_currentFrameBuffer + y * w * 4,
               data + y * linesize,
               w * 4);
    }

    m_videoClock.set(ptsSec);
    m_videoFrameQueue.pop();
    m_stats.displayed++;
    g_stats.mainFramesDisplayed++;

    // Measure video FPS
    m_fpsCounterFrames++;
    double fpsElapsed = now - m_fpsCounterStart;
    if (fpsElapsed >= 0.5) {
        m_stats.videoFps = m_fpsCounterFrames / fpsElapsed;
        g_stats.overlayFps.store(m_stats.videoFps);
        m_fpsCounterFrames = 0;
        m_fpsCounterStart = now;
    }

    return m_currentFrameBuffer;
}

double PlaybackController::getCurrentTime() const {
    return m_videoClock.get();
}

double PlaybackController::getDuration() const {
    return m_mediaFile.getDuration();
}

int PlaybackController::getVideoWidth() const {
    return m_videoDecoder ? m_videoDecoder->getWidth() : 0;
}

int PlaybackController::getVideoHeight() const {
    return m_videoDecoder ? m_videoDecoder->getHeight() : 0;
}

int PlaybackController::getAudioSampleRate() const {
    return m_audioDecoder ? m_audioDecoder->getSampleRate() : 44100;
}

AVRational PlaybackController::getAudioTimeBase() const {
    if (m_audioDecoder) return m_audioDecoder->getTimeBase();
    return {1, 44100};
}

void PlaybackController::demuxLoop() {
    AVPacket* packet = av_packet_alloc();

    while (m_demuxRunning.load()) {
        if (m_seekRequested.load()) {
            double target = m_seekTarget.load();
            int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);

            // Pause audio output and clear SDL buffer before flushing
            if (m_audioOutput && m_audioStarted) {
                m_audioOutput->pause();
            }

            avformat_seek_file(m_mediaFile.getFormatContext(), -1,
                               INT64_MIN, ts, INT64_MAX, 0);

            // Flush all queues — drops stale packets and frames
            m_videoPacketQueue.flush();
            m_audioPacketQueue.flush();
            m_videoFrameQueue.flush();
            m_audioFrameQueue.flush();

            // Codec flush is handled by the decoder threads themselves:
            // PacketQueue::flush() increments the serial number. When the decoder
            // thread pops the next packet and sees a serial mismatch, it flushes
            // its own codec context. This avoids a cross-thread race on the codec.

            m_videoClock.set(target);
            m_audioClock.set(target);
            // Reset timer — will re-sync wall clock to the first post-seek frame PTS
            m_timerInitialized = false;
            // Audio will be resumed when the first video frame arrives
            m_audioStarted = false;

            m_seekRequested.store(false);
        }

        int ret = av_read_frame(m_mediaFile.getFormatContext(), packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                SDL_Delay(10);
                continue;
            }
            break;
        }

        if (packet->stream_index == m_mediaFile.getVideoStreamIndex()) {
            AVPacket* clone = av_packet_clone(packet);
            m_videoPacketQueue.push(clone);
            g_stats.videoPacketsPushed++;
        } else if (packet->stream_index == m_audioStreamIdx) {
            AVPacket* clone = av_packet_clone(packet);
            m_audioPacketQueue.push(clone);
            g_stats.audioPacketsPushed++;
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
}
