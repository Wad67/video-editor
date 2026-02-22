#include "timeline/ClipPlayer.h"
#include "media/AudioDecoder.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

extern "C" {
#include <libavutil/imgutils.h>
}

ClipPlayer::~ClipPlayer() {
    close();
    if (m_currentFrameBuffer) {
        av_free(m_currentFrameBuffer);
        m_currentFrameBuffer = nullptr;
    }
}

bool ClipPlayer::open(const std::string& path, bool needVideo, bool needAudio,
                       int outputSampleRate) {
    close();

    if (!m_mediaFile.open(path)) return false;

    // Only track the streams we actually need
    m_videoStreamIdx = needVideo ? m_mediaFile.getVideoStreamIndex() : -1;
    m_audioStreamIdx = needAudio ? m_mediaFile.getAudioStreamIndex() : -1;

    if (m_videoStreamIdx >= 0) {
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

        if (!m_videoFrameQueue.allocate(w, h)) {
            fprintf(stderr, "ClipPlayer: failed to allocate video frame queue\n");
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
        if (!m_audioDecoder->init(par, tb, outputSampleRate)) {
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
            m_audioStreamIdx = -1;
        }
    }

    return (m_videoDecoder != nullptr) || (m_audioDecoder != nullptr);
}

void ClipPlayer::close() {
    stop();
    stopThreads();

    delete m_videoDecoder;
    m_videoDecoder = nullptr;

    delete m_audioDecoder;
    m_audioDecoder = nullptr;

    m_mediaFile.close();
    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;

    if (m_currentFrameBuffer) {
        av_free(m_currentFrameBuffer);
        m_currentFrameBuffer = nullptr;
    }
    m_firstFrameReceived = false;
    m_active.store(false);
}

void ClipPlayer::play() {
    if (m_videoDecoder) {
        m_videoPacketQueue.start();
        m_videoFrameQueue.start();
        m_videoDecoder->start(m_videoPacketQueue, m_videoFrameQueue);
    }
    if (m_audioDecoder) {
        m_audioPacketQueue.start();
        m_audioFrameQueue.start();
        m_audioDecoder->start(m_audioPacketQueue, m_audioFrameQueue);
    }

    m_demuxRunning.store(true);
    m_demuxThread = std::thread(&ClipPlayer::demuxLoop, this);
    m_active.store(true);
    m_firstFrameReceived = false;
}

void ClipPlayer::pause() {
    // ClipPlayer doesn't manage its own clock —
    // pausing just stops the demux thread from reading ahead
}

void ClipPlayer::resume() {
    // Resume is a no-op — the master clock drives frame selection
}

void ClipPlayer::stop() {
    stopThreads();
    m_active.store(false);
    m_firstFrameReceived = false;
}

void ClipPlayer::seek(double sourceSeconds) {
    if (!m_mediaFile.isOpen()) return;
    double duration = m_mediaFile.getDuration();
    sourceSeconds = std::max(0.0, std::min(sourceSeconds, duration));
    m_seekTarget.store(sourceSeconds);
    m_seekRequested.store(true);
}

void ClipPlayer::stopThreads() {
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

const uint8_t* ClipPlayer::getVideoFrameAtTime(double targetPts, int& width, int& height,
                                                 bool* isNewFrame) {
    if (isNewFrame) *isNewFrame = false;

    if (!m_videoDecoder) {
        width = 0;
        height = 0;
        return nullptr;
    }

    width = m_currentFrameWidth;
    height = m_currentFrameHeight;

    // Peek at next frame and advance to match targetPts
    int64_t pts;
    int linesize;
    const uint8_t* data = m_videoFrameQueue.peek(&pts, &linesize);

    if (!data) {
        return m_currentFrameBuffer; // Hold last frame
    }

    double ptsSec = pts * av_q2d(m_videoDecoder->getTimeBase());
    double frameDuration = 1.0 / m_videoDecoder->getFrameRate();

    // Skip frames that are behind the target
    while (ptsSec < targetPts - frameDuration * 2.0) {
        m_videoFrameQueue.pop();
        data = m_videoFrameQueue.peek(&pts, &linesize);
        if (!data) return m_currentFrameBuffer;
        ptsSec = pts * av_q2d(m_videoDecoder->getTimeBase());
    }

    // If the next frame is in the future, hold current
    if (ptsSec > targetPts + frameDuration * 0.5) {
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
    m_videoFrameQueue.pop();
    m_firstFrameReceived = true;
    if (isNewFrame) *isNewFrame = true;

    return m_currentFrameBuffer;
}

int ClipPlayer::getVideoWidth() const {
    return m_videoDecoder ? m_videoDecoder->getWidth() : 0;
}

int ClipPlayer::getVideoHeight() const {
    return m_videoDecoder ? m_videoDecoder->getHeight() : 0;
}

int ClipPlayer::getAudioSampleRate() const {
    return m_audioDecoder ? m_audioDecoder->getSampleRate() : 48000;
}

int ClipPlayer::getAudioChannels() const {
    return m_audioDecoder ? m_audioDecoder->getChannels() : 2;
}

AVRational ClipPlayer::getAudioTimeBase() const {
    if (m_audioDecoder) return m_audioDecoder->getTimeBase();
    return {1, 48000};
}

void ClipPlayer::demuxLoop() {
    AVPacket* packet = av_packet_alloc();

    while (m_demuxRunning.load()) {
        if (m_seekRequested.load()) {
            double target = m_seekTarget.load();
            int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);

            avformat_seek_file(m_mediaFile.getFormatContext(), -1,
                               INT64_MIN, ts, INT64_MAX, 0);

            if (m_videoDecoder) {
                m_videoPacketQueue.flush();
                m_videoFrameQueue.flush();
            }
            if (m_audioDecoder) {
                m_audioPacketQueue.flush();
                m_audioFrameQueue.flush();
            }

            m_firstFrameReceived = false;
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

        // Only route packets to streams we're actually decoding
        if (packet->stream_index == m_videoStreamIdx && m_videoDecoder) {
            AVPacket* clone = av_packet_clone(packet);
            m_videoPacketQueue.push(clone);
        } else if (packet->stream_index == m_audioStreamIdx && m_audioDecoder) {
            AVPacket* clone = av_packet_clone(packet);
            m_audioPacketQueue.push(clone);
        }
        // Packets for streams we don't need are silently dropped
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
}
