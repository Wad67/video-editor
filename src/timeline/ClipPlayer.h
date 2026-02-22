#pragma once

#include "media/MediaFile.h"
#include "media/PacketQueue.h"
#include "media/FrameQueue.h"
#include "media/AudioFrameQueue.h"
#include "media/VideoDecoder.h"
#include "media/Clock.h"
#include <thread>
#include <atomic>
#include <string>

class AudioDecoder;

// Lightweight per-clip decoder. Wraps existing media pipeline components.
// Driven by target source time from the master clock (no wall-clock pacing of its own).
class ClipPlayer {
public:
    ~ClipPlayer();

    // Open a file. Only decode the streams you need:
    //   needVideo=true  → creates video decoder pipeline
    //   needAudio=true  → creates audio decoder pipeline
    // Skipping unneeded streams prevents pipeline deadlocks.
    bool open(const std::string& path, bool needVideo, bool needAudio,
              int outputSampleRate = 0);
    void close();

    void play();
    void pause();
    void resume();
    void stop();
    void seek(double sourceSeconds);

    // Get the current video frame for a target source time.
    // Returns RGBA pixel data, or nullptr if no frame available.
    // Sets *isNewFrame = true if this is a newly decoded frame (not a hold).
    const uint8_t* getVideoFrameAtTime(double targetPts, int& width, int& height,
                                        bool* isNewFrame = nullptr);

    bool hasVideo() const { return m_videoDecoder != nullptr; }
    bool hasAudio() const { return m_audioDecoder != nullptr; }
    int getVideoWidth() const;
    int getVideoHeight() const;

    AudioFrameQueue& getAudioFrameQueue() { return m_audioFrameQueue; }
    int getAudioSampleRate() const;
    int getAudioChannels() const;
    AVRational getAudioTimeBase() const;

    // Queue depth queries for debug stats
    size_t getVideoFrameQueueSize() const { return m_videoFrameQueue.size(); }
    size_t getVideoPacketQueueSize() const { return m_videoPacketQueue.size(); }
    size_t getAudioFrameQueueSize() const { return m_audioFrameQueue.size(); }
    size_t getAudioPacketQueueSize() const { return m_audioPacketQueue.size(); }

    bool isActive() const { return m_active.load(); }
    void setActive(bool active) { m_active.store(active); }

private:
    void demuxLoop();
    void stopThreads();

    MediaFile m_mediaFile;
    int m_videoStreamIdx = -1;
    int m_audioStreamIdx = -1;

    PacketQueue m_videoPacketQueue;
    FrameQueue m_videoFrameQueue;
    VideoDecoder* m_videoDecoder = nullptr;

    PacketQueue m_audioPacketQueue;
    AudioFrameQueue m_audioFrameQueue;
    AudioDecoder* m_audioDecoder = nullptr;

    std::thread m_demuxThread;
    std::atomic<bool> m_demuxRunning{false};

    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekTarget{0.0};

    std::atomic<bool> m_active{false};

    uint8_t* m_currentFrameBuffer = nullptr;
    int m_currentFrameWidth = 0;
    int m_currentFrameHeight = 0;

    bool m_firstFrameReceived = false;
};
