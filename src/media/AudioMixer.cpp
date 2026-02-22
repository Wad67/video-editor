#include "media/AudioMixer.h"
#include "timeline/Timeline.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
}

AudioMixer::~AudioMixer() {
    clearSources();
}

void AudioMixer::setSources(std::vector<AudioMixSource> sources) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& src : m_sources) {
        src.currentFrame = nullptr;
        src.frameByteOffset = 0;
    }
    m_sources = std::move(sources);
}

void AudioMixer::clearSources() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& src : m_sources) {
        src.currentFrame = nullptr;
        src.frameByteOffset = 0;
    }
    m_sources.clear();
}

bool AudioMixer::hasSources() const {
    return !m_sources.empty();
}

void AudioMixer::lockClockForSeek(double targetTime) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clockLocked = true;
    m_seekTargetTime = targetTime;
    m_clockLockTime = std::chrono::steady_clock::now();
}

void AudioMixer::fillBuffer(float* out, int frames, Clock& masterClock) {
    std::lock_guard<std::mutex> lock(m_mutex);

    int totalSamples = frames * OUTPUT_CHANNELS;
    memset(out, 0, totalSamples * sizeof(float));

    if (m_sources.empty()) return;

    if ((int)m_tempBuf.size() < totalSamples) {
        m_tempBuf.resize(totalSamples);
    }

    for (auto& src : m_sources) {
        if (!src.queue) continue;
        if (src.track && src.track->muted) continue;

        float volume = (src.track) ? src.track->volume : 1.0f;

        int framesRead = readSource(src, m_tempBuf.data(), frames, masterClock);
        if (framesRead <= 0) continue;

        int samplesToMix = framesRead * OUTPUT_CHANNELS;
        for (int i = 0; i < samplesToMix; i++) {
            out[i] += m_tempBuf[i] * volume;
        }
    }

    // Clamp to [-1, 1]
    for (int i = 0; i < totalSamples; i++) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
}

int AudioMixer::readSource(AudioMixSource& src, float* buf, int frames, Clock& masterClock) {
    if (!src.queue) return 0;

    int framesWritten = 0;
    int bytesPerFrame = OUTPUT_CHANNELS * sizeof(float);

    while (framesWritten < frames) {
        AVFrame* frame = src.queue->peek();
        if (!frame) break;

        src.currentFrame = frame;

        if (src.frameByteOffset == 0 && frame->pts != AV_NOPTS_VALUE) {
            double pts = frame->pts * av_q2d(src.timeBase);

            // Skip frames before the clip's source range. After a seek,
            // FFmpeg decodes from the nearest keyframe which may be seconds
            // before sourceIn. These pre-roll frames must not be played.
            if (src.clip && pts < src.clip->sourceIn - 0.05) {
                src.queue->pop();
                src.frameByteOffset = 0;
                continue;
            }

            double timelineTime = pts;
            if (src.clip) {
                timelineTime = (pts - src.clip->sourceIn) + src.clip->timelineStart;
            }

            // During an explicit seek, discard stale pre-seek frames
            // (from before the demux thread processes the seek request).
            if (m_clockLocked) {
                auto elapsed = std::chrono::steady_clock::now() - m_clockLockTime;
                bool timedOut = elapsed > std::chrono::milliseconds(1000);
                // Accept frames at or past the seek target (with tolerance
                // for keyframe-based seeking landing a bit before target).
                bool ptsReasonable = timelineTime >= m_seekTargetTime - 3.0;
                if (ptsReasonable || timedOut) {
                    m_clockLocked = false;
                    masterClock.set(timelineTime);
                } else {
                    // Definitely stale — discard
                    src.queue->pop();
                    src.frameByteOffset = 0;
                    continue;
                }
            } else {
                // Normal playback — update clock but never jump backward.
                masterClock.setIfForward(timelineTime);
            }
        }

        int frameSamples = frame->nb_samples;
        int frameBytes = frameSamples * bytesPerFrame;
        int remaining = frameBytes - src.frameByteOffset;
        int needed = (frames - framesWritten) * bytesPerFrame;

        if (remaining <= needed) {
            memcpy(reinterpret_cast<uint8_t*>(buf) + framesWritten * bytesPerFrame,
                   frame->data[0] + src.frameByteOffset,
                   remaining);
            framesWritten += remaining / bytesPerFrame;
            src.frameByteOffset = 0;
            src.queue->pop();
        } else {
            memcpy(reinterpret_cast<uint8_t*>(buf) + framesWritten * bytesPerFrame,
                   frame->data[0] + src.frameByteOffset,
                   needed);
            src.frameByteOffset += needed;
            framesWritten += needed / bytesPerFrame;
        }
    }

    if (framesWritten < frames) {
        memset(reinterpret_cast<uint8_t*>(buf) + framesWritten * bytesPerFrame,
               0, (frames - framesWritten) * bytesPerFrame);
    }

    return framesWritten;
}
