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
class AudioOutput;

class PlaybackController {
public:
    enum class State { Stopped, Playing, Paused };

    ~PlaybackController();

    bool openFile(const std::string& path);
    void closeFile();

    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(double seconds);

    const uint8_t* getVideoFrame(int& width, int& height);

    State getState() const { return m_state.load(); }
    double getCurrentTime() const;
    double getDuration() const;
    bool hasVideo() const { return m_videoDecoder != nullptr; }
    bool hasAudio() const { return m_audioStreamIdx >= 0; }
    int getVideoWidth() const;
    int getVideoHeight() const;

    int getAudioSampleRate() const;
    AVRational getAudioTimeBase() const;

    void setAudioOutput(AudioOutput* audio) { m_audioOutput = audio; }
    AudioFrameQueue& getAudioFrameQueue() { return m_audioFrameQueue; }
    PacketQueue& getAudioPacketQueue() { return m_audioPacketQueue; }
    Clock& getVideoClock() { return m_videoClock; }
    Clock& getAudioClock() { return m_audioClock; }

    MediaFile& getMediaFile() { return m_mediaFile; }
    VideoDecoder* getVideoDecoder() { return m_videoDecoder; }
    AudioDecoder* getAudioDecoder() { return m_audioDecoder; }

    struct FrameStats {
        uint64_t displayed = 0;
        uint64_t repeated = 0;
        double   videoFps = 0.0;
        int      queueDepth = 0;
    };
    const FrameStats& getFrameStats() const { return m_stats; }

private:
    void demuxLoop();
    void stopThreads();

    MediaFile m_mediaFile;
    int m_audioStreamIdx = -1;

    PacketQueue m_videoPacketQueue;
    FrameQueue m_videoFrameQueue;      // pre-allocated RGBA ring for video
    VideoDecoder* m_videoDecoder = nullptr;

    PacketQueue m_audioPacketQueue;
    AudioFrameQueue m_audioFrameQueue; // AVFrame ring for audio
    AudioDecoder* m_audioDecoder = nullptr;
    AudioOutput* m_audioOutput = nullptr;

    Clock m_videoClock;
    Clock m_audioClock;

    std::thread m_demuxThread;
    std::atomic<bool> m_demuxRunning{false};

    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekTarget{0.0};

    std::atomic<State> m_state{State::Stopped};

    uint8_t* m_currentFrameBuffer = nullptr;
    int m_currentFrameWidth = 0;
    int m_currentFrameHeight = 0;

    // PTS-based pacing: map wall clock to stream PTS
    double m_streamStartWall = 0.0;  // wall time when stream started/seeked
    double m_streamStartPts = 0.0;   // PTS of first frame after start/seek
    bool m_timerInitialized = false;
    bool m_audioStarted = false;

    FrameStats m_stats;
    uint64_t m_fpsCounterFrames = 0;
    double m_fpsCounterStart = 0.0;
};
