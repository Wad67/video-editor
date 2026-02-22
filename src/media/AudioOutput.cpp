#include "media/AudioOutput.h"
#include "media/AudioFrameQueue.h"
#include "media/AudioMixer.h"
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
}

AudioOutput::~AudioOutput() {
    shutdown();
}

bool AudioOutput::init(int sampleRate, int channels) {
    m_sampleRate = sampleRate;
    m_channels = 2; // Always output stereo

    SDL_AudioSpec spec;
    spec.freq = sampleRate;
    spec.format = SDL_AUDIO_F32;
    spec.channels = m_channels;

    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &spec, audioCallback, this);
    if (!m_stream) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

void AudioOutput::shutdown() {
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_frameQueue = nullptr;
    m_audioClock = nullptr;
    m_mixer = nullptr;
    m_masterClock = nullptr;
    m_mixerMode = false;
}

void AudioOutput::start(AudioFrameQueue& frameQueue, Clock& audioClock, AVRational timeBase) {
    m_frameQueue = &frameQueue;
    m_audioClock = &audioClock;
    m_timeBase = timeBase;
    m_frameByteOffset = 0;
    m_mixerMode = false;
    m_mixer = nullptr;
    m_masterClock = nullptr;
}

void AudioOutput::startWithMixer(AudioMixer& mixer, Clock& masterClock) {
    m_mixer = &mixer;
    m_masterClock = &masterClock;
    m_mixerMode = true;
    m_frameQueue = nullptr;
    m_audioClock = nullptr;
    m_frameByteOffset = 0;
}

void AudioOutput::pause() {
    m_paused.store(true);
    if (m_stream) {
        SDL_PauseAudioStreamDevice(m_stream);
        SDL_ClearAudioStream(m_stream);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameByteOffset = 0;
}

void AudioOutput::resume() {
    m_paused.store(false);
    if (m_stream) {
        SDL_ResumeAudioStreamDevice(m_stream);
    }
}

double AudioOutput::getPlaybackClock() const {
    Clock* clock = m_mixerMode ? m_masterClock : m_audioClock;
    if (!clock) return 0.0;

    double rawClock = clock->get();

    // Subtract the latency of audio data buffered in SDL but not yet played.
    if (m_stream && m_sampleRate > 0) {
        int queued = SDL_GetAudioStreamQueued(m_stream);
        double bufferedSeconds = static_cast<double>(queued) /
            (m_sampleRate * m_channels * sizeof(float));
        rawClock -= bufferedSeconds;
    }

    return rawClock;
}

void AudioOutput::audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount) {
    auto* self = static_cast<AudioOutput*>(userdata);
    self->fillBuffer(stream, additionalAmount);
}

void AudioOutput::fillBuffer(SDL_AudioStream* stream, int additionalAmount) {
    if (m_paused.load()) return;

    if (m_mixerMode) {
        // Mixer mode: delegate to AudioMixer
        if (!m_mixer || !m_masterClock) return;

        int bytesPerFrame = m_channels * sizeof(float);
        int frames = additionalAmount / bytesPerFrame;
        if (frames <= 0) return;

        std::vector<float> buf(frames * m_channels);
        m_mixer->fillBuffer(buf.data(), frames, *m_masterClock);
        SDL_PutAudioStreamData(stream, buf.data(), frames * bytesPerFrame);
        return;
    }

    // Single-source mode (legacy)
    if (!m_frameQueue || !m_audioClock) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    int bytesNeeded = additionalAmount;

    while (bytesNeeded > 0) {
        AVFrame* frame = m_frameQueue->peek();
        if (!frame) {
            // Underrun — push silence
            std::vector<uint8_t> silence(bytesNeeded, 0);
            SDL_PutAudioStreamData(stream, silence.data(), bytesNeeded);
            return;
        }

        int frameBytes = frame->nb_samples * m_channels * sizeof(float);
        int remaining = frameBytes - m_frameByteOffset;

        // Update audio clock with this frame's PTS (at start of frame)
        if (frame->pts != AV_NOPTS_VALUE && m_frameByteOffset == 0) {
            double pts = frame->pts * av_q2d(m_timeBase);
            m_audioClock->set(pts);
        }

        if (remaining <= bytesNeeded) {
            SDL_PutAudioStreamData(stream, frame->data[0] + m_frameByteOffset, remaining);
            bytesNeeded -= remaining;
            m_frameByteOffset = 0;
            m_frameQueue->pop();
        } else {
            SDL_PutAudioStreamData(stream, frame->data[0] + m_frameByteOffset, bytesNeeded);
            m_frameByteOffset += bytesNeeded;
            bytesNeeded = 0;
        }
    }
}
