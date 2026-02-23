#pragma once

#include "export/ExportSettings.h"
#include "export/VideoEncoder.h"
#include "export/AudioEncoder.h"
#include "export/Muxer.h"
#include "timeline/Timeline.h"
#include "timeline/ClipPlayer.h"
#include "media/AudioMixer.h"
#include "media/Clock.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

class ExportSession {
public:
    enum class State {
        Idle,
        Running,
        Completed,
        Failed,
        Cancelled
    };

    ~ExportSession();

    bool start(const Timeline& timeline, const ExportSettings& settings);
    void cancel();

    State getState() const { return m_state.load(); }
    double getProgress() const { return m_progress.load(); }
    int64_t getFramesEncoded() const { return m_framesEncoded.load(); }
    int64_t getTotalFrames() const { return m_totalFrames.load(); }
    std::string getErrorMessage() const;

    void wait();

private:
    void exportLoop();
    void fail(const std::string& msg);

    void updateActiveClips(double time);
    void activateClip(uint32_t clipId);
    void deactivateClip(uint32_t clipId);
    void rebuildAudioSources();

    void compositeFrame(double time, uint8_t* outputRGBA, int width, int height);
    void encodeAudioForFrame(double frameDuration);

    Timeline m_timelineCopy;
    ExportSettings m_settings;

    std::unordered_map<uint32_t, std::unique_ptr<ClipPlayer>> m_clipPlayers;
    std::unordered_set<uint32_t> m_activeClipIds;
    AudioMixer m_audioMixer;
    Clock m_exportClock;

    VideoEncoder m_videoEncoder;
    AudioEncoder m_audioEncoder;
    Muxer m_muxer;

    // Audio buffer reused each frame
    std::vector<float> m_audioBuffer;

    std::thread m_thread;
    std::atomic<State> m_state{State::Idle};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<double> m_progress{0.0};
    std::atomic<int64_t> m_framesEncoded{0};
    std::atomic<int64_t> m_totalFrames{0};

    mutable std::mutex m_errorMutex;
    std::string m_errorMessage;
};
