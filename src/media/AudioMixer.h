#pragma once

#include "media/AudioFrameQueue.h"
#include "media/Clock.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>

extern "C" {
#include <libavutil/rational.h>
}

struct Clip;
struct Track;

// A single audio source feeding into the mixer.
struct AudioMixSource {
    AudioFrameQueue* queue = nullptr;
    const Clip* clip = nullptr;         // for time mapping
    const Track* track = nullptr;       // for volume/mute
    AVRational timeBase{};
    uint32_t clipId = 0;

    // Per-source read state (owned by mixer, only touched under lock)
    AVFrame* currentFrame = nullptr;
    int frameByteOffset = 0;
};

// Mixes multiple AudioFrameQueue sources into a single interleaved float buffer.
// Called from the SDL audio callback thread.
class AudioMixer {
public:
    static constexpr int OUTPUT_SAMPLE_RATE = 48000;
    static constexpr int OUTPUT_CHANNELS = 2;

    ~AudioMixer();

    // Replace the active set of sources. Called from main thread when
    // active clips change. Thread-safe with the audio callback.
    void setSources(std::vector<AudioMixSource> sources);

    // Clear all sources. Called on stop.
    void clearSources();

    // Lock the master clock after a seek. While locked, readSource() will
    // only update the clock once the audio PTS is within tolerance of the
    // target (meaning the seek has been processed). Auto-unlocks after 500ms.
    void lockClockForSeek(double targetTime);

    // Called from SDL audio thread to fill output buffer.
    // Mixes all sources, updates master clock from the first source with data.
    void fillBuffer(float* out, int frames, Clock& masterClock);

    bool hasSources() const;

private:
    // Read up to `frames` samples from one source into `buf`.
    // Returns number of frames actually read.
    int readSource(AudioMixSource& src, float* buf, int frames, Clock& masterClock);

    std::vector<AudioMixSource> m_sources;
    std::mutex m_mutex;

    // Clock lock state — prevents stale audio from overwriting seek target
    bool m_clockLocked = false;
    double m_seekTargetTime = 0.0;
    std::chrono::steady_clock::time_point m_clockLockTime;

    // Temp mixing buffer (reused across calls)
    std::vector<float> m_tempBuf;
};
