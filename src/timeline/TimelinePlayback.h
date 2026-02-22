#pragma once

#include "timeline/Timeline.h"
#include "timeline/ClipPlayer.h"
#include "media/Clock.h"
#include "media/AudioMixer.h"
#include "vulkan/VideoTexture.h"
#include "vulkan/TextureUploader.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <cstdint>

struct VulkanContext;
class AudioOutput;

// Per-track GPU resources for video/image rendering.
struct TrackRenderState {
    VideoTexture texture;
    TextureUploader uploader;
    bool initialized = false;
    int lastWidth = 0;
    int lastHeight = 0;
};

// Info about a single compositing layer, returned by prepareFrame().
struct LayerInfo {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
    uint32_t trackId = 0;
};

// Upload recorded for a single track during prepareFrame().
struct PendingUpload {
    uint32_t trackId = 0;
    int uploadSlot = 0;
    int width = 0;
    int height = 0;
};

// Central orchestrator: owns ClipPlayer pool, per-track GPU resources,
// AudioMixer, and master Clock. Makes the timeline the authoritative
// source for playback.
class TimelinePlayback {
public:
    enum class State { Stopped, Playing, Paused };

    ~TimelinePlayback();

    void setTimeline(Timeline* timeline) { m_timeline = timeline; }

    void init(VulkanContext& ctx);
    void shutdown();

    void setAudioOutput(AudioOutput* ao) { m_audioOutput = ao; }
    void setVerbose(bool v) { m_verbose = v; }

    // Transport controls
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(double timelineSeconds);

    // Called each frame: activate/deactivate ClipPlayers based on playhead.
    void update();

    // Prepare video layers for rendering. Returns layer list (bottom-to-top).
    std::vector<LayerInfo> prepareFrame(int swapchainFrameIndex);

    // Record GPU uploads for all tracks that have pending data.
    void recordUploads(VkCommandBuffer cmd, int swapchainFrameIndex);

    // Accessors
    State getState() const { return m_state; }
    double getCurrentTime() const;
    double getDuration() const;
    bool isPlaying() const { return m_state == State::Playing; }
    Clock& getMasterClock() { return m_masterClock; }

    // Stats for viewport overlay
    double getVideoFps() const { return m_videoFps; }
    size_t getActiveClipCount() const { return m_activeClipIds.size(); }

private:
    TrackRenderState& ensureTrackRenderState(uint32_t trackId, int width, int height);
    void activateClip(uint32_t clipId);
    void deactivateClip(uint32_t clipId);
    void rebuildAudioSources();

    Timeline* m_timeline = nullptr;
    VulkanContext* m_vkCtx = nullptr;
    AudioOutput* m_audioOutput = nullptr;

    State m_state = State::Stopped;
    Clock m_masterClock;
    bool m_audioStarted = false;
    bool m_verbose = false;

    std::unordered_map<uint32_t, std::unique_ptr<ClipPlayer>> m_clipPlayers;
    std::unordered_map<uint32_t, TrackRenderState> m_trackStates;
    std::vector<PendingUpload> m_pendingUploads;
    AudioMixer m_audioMixer;
    std::unordered_set<uint32_t> m_activeClipIds;

    bool m_firstFrameReceived = false;

    // Stats
    double m_debugLastPrint = 0.0;
    uint64_t m_debugNewFrames = 0;
    uint64_t m_debugHeldFrames = 0;
    uint64_t m_debugSkippedFrames = 0;
    double m_fpsCounterStart = 0.0;
    uint64_t m_fpsCounterFrames = 0;
    double m_videoFps = 0.0;
};
