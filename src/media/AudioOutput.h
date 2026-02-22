#pragma once

#include <SDL3/SDL.h>
#include "media/Clock.h"
#include <mutex>
#include <atomic>
#include <functional>

extern "C" {
#include <libavutil/rational.h>
}

class AudioFrameQueue;
class AudioMixer;

class AudioOutput {
public:
    ~AudioOutput();

    bool init(int sampleRate, int channels);
    void shutdown();

    // Single-source mode (legacy — used by PlaybackController)
    void start(AudioFrameQueue& frameQueue, Clock& audioClock, AVRational timeBase);

    // Mixer mode — multiple sources mixed by AudioMixer
    void startWithMixer(AudioMixer& mixer, Clock& masterClock);

    void pause();
    void resume();

    double getPlaybackClock() const;
    int getSampleRate() const { return m_sampleRate; }
    int getChannels() const { return m_channels; }

private:
    static void audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount);
    void fillBuffer(SDL_AudioStream* stream, int additionalAmount);

    SDL_AudioStream* m_stream = nullptr;

    // Single-source mode
    AudioFrameQueue* m_frameQueue = nullptr;
    Clock* m_audioClock = nullptr;
    AVRational m_timeBase{};

    // Mixer mode
    AudioMixer* m_mixer = nullptr;
    Clock* m_masterClock = nullptr;
    bool m_mixerMode = false;

    int m_sampleRate = 0;
    int m_channels = 2;
    std::atomic<bool> m_paused{true};

    int m_frameByteOffset = 0;
    std::mutex m_mutex;
};
