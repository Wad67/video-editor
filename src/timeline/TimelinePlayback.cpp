#include "timeline/TimelinePlayback.h"
#include "media/AudioOutput.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

static double wallClock() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

TimelinePlayback::~TimelinePlayback() {
    shutdown();
}

void TimelinePlayback::init(VulkanContext& ctx) {
    m_vkCtx = &ctx;
}

void TimelinePlayback::shutdown() {
    stop();

    m_clipPlayers.clear();
    m_activeClipIds.clear();

    if (m_vkCtx) {
        for (auto& [trackId, state] : m_trackStates) {
            state.uploader.shutdown(*m_vkCtx);
            state.texture.shutdown(*m_vkCtx);
        }
    }
    m_trackStates.clear();
    m_pendingUploads.clear();
    m_vkCtx = nullptr;
}

void TimelinePlayback::play() {
    if (!m_timeline) return;

    if (m_state == State::Paused) {
        m_masterClock.resume();
        for (auto& [clipId, player] : m_clipPlayers) {
            player->resume();
        }
        if (m_audioOutput && m_audioStarted) {
            m_audioOutput->resume();
        }
        m_state = State::Playing;
        return;
    }

    if (m_state == State::Playing) return;

    // Start from stopped — preserve scrubbed position (set by seek())
    double startPos = m_masterClock.get();
    double duration = getDuration();
    if (startPos < 0.0 || (duration > 0.0 && startPos >= duration)) {
        startPos = 0.0;
    }
    m_masterClock.set(startPos);
    m_masterClock.resume();
    m_firstFrameReceived = false;
    m_audioStarted = false;
    m_debugLastPrint = wallClock();
    m_debugNewFrames = 0;
    m_debugHeldFrames = 0;
    m_debugSkippedFrames = 0;
    m_fpsCounterStart = wallClock();
    m_fpsCounterFrames = 0;

    if (m_audioOutput) {
        m_audioOutput->startWithMixer(m_audioMixer, m_masterClock);
    }

    m_state = State::Playing;

    // First update() will activate the right clips
    update();

    // Start audio after clips are activated
    if (m_audioOutput && m_audioMixer.hasSources()) {
        m_audioOutput->resume();
        m_audioStarted = true;
    }
}

void TimelinePlayback::pause() {
    if (m_state != State::Playing) return;

    m_masterClock.pause();
    for (auto& [clipId, player] : m_clipPlayers) {
        player->pause();
    }
    if (m_audioOutput && m_audioStarted) {
        m_audioOutput->pause();
    }
    m_state = State::Paused;
}

void TimelinePlayback::togglePlayPause() {
    if (m_state == State::Playing) pause();
    else play();
}

void TimelinePlayback::stop() {
    if (m_state == State::Stopped) return;

    for (auto& [clipId, player] : m_clipPlayers) {
        player->stop();
    }
    m_clipPlayers.clear();
    m_activeClipIds.clear();

    m_audioMixer.clearSources();

    if (m_audioOutput) {
        m_audioOutput->pause();
    }

    m_masterClock.set(0.0);
    m_masterClock.pause();
    m_audioStarted = false;
    m_firstFrameReceived = false;

    m_state = State::Stopped;
}

void TimelinePlayback::seek(double timelineSeconds) {
    if (!m_timeline) return;

    double duration = getDuration();
    timelineSeconds = std::clamp(timelineSeconds, 0.0, duration);

    if (m_audioOutput && m_audioStarted) {
        m_audioOutput->pause();
    }

    m_masterClock.set(timelineSeconds);

    for (auto& [clipId, player] : m_clipPlayers) {
        player->stop();
    }
    m_clipPlayers.clear();
    m_activeClipIds.clear();
    m_audioMixer.clearSources();

    m_firstFrameReceived = false;

    // Lock the clock so stale audio frames from newly activated players
    // don't overwrite the seek target before their seek completes.
    m_audioMixer.lockClockForSeek(timelineSeconds);

    if (m_state != State::Stopped) {
        update();

        if (m_audioOutput && m_state == State::Playing && m_audioMixer.hasSources()) {
            m_audioOutput->resume();
            m_audioStarted = true;
        }
    }
}

void TimelinePlayback::update() {
    if (!m_timeline || m_state == State::Stopped) return;

    // Use raw master clock for clip management decisions — NOT getPlaybackClock()
    // which subtracts SDL buffer latency and can report a time before a clip
    // transition point, causing the transition to immediately reverse.
    double currentTime = m_masterClock.get();
    double lookahead = currentTime + 1.0;

    std::unordered_set<uint32_t> neededClipIds;

    for (uint32_t trackId : m_timeline->getTrackOrder()) {
        const auto* track = m_timeline->getTrack(trackId);
        if (!track) continue;
        if (!track->visible && track->type != TrackType::Audio) continue;

        for (uint32_t clipId : track->clipIds) {
            const auto* clip = m_timeline->getClip(clipId);
            if (!clip) continue;

            double clipStart = clip->timelineStart;
            double clipEnd = clip->getTimelineEnd();

            if (clipEnd > currentTime && clipStart < lookahead) {
                if (track->type == TrackType::Image) continue;
                neededClipIds.insert(clipId);
            }
        }
    }

    std::vector<uint32_t> toRemove;
    for (uint32_t clipId : m_activeClipIds) {
        if (neededClipIds.find(clipId) == neededClipIds.end()) {
            toRemove.push_back(clipId);
        }
    }

    // Clear mixer sources BEFORE destroying any players to prevent the audio
    // callback from accessing freed AudioFrameQueue memory (use-after-free).
    if (!toRemove.empty()) {
        m_audioMixer.clearSources();
    }

    for (uint32_t clipId : toRemove) {
        deactivateClip(clipId);
    }

    bool sourcesChanged = !toRemove.empty();
    for (uint32_t clipId : neededClipIds) {
        if (m_activeClipIds.find(clipId) == m_activeClipIds.end()) {
            activateClip(clipId);
            sourcesChanged = true;
        }
    }

    if (sourcesChanged) {
        // No clock lock needed here — readSource uses:
        //   1) sourceIn check to discard pre-roll frames from keyframe seeking
        //   2) setIfForward() to prevent the clock from ever jumping backward
        // The clock lock is only engaged by explicit seek() calls.
        rebuildAudioSources();

        if (m_audioOutput && !m_audioStarted && m_state == State::Playing && m_audioMixer.hasSources()) {
            m_audioOutput->resume();
            m_audioStarted = true;
        }
    }
}

std::vector<LayerInfo> TimelinePlayback::prepareFrame(int swapchainFrameIndex) {
    std::vector<LayerInfo> layers;
    m_pendingUploads.clear();

    if (!m_timeline || !m_vkCtx) return layers;

    double currentTime = getCurrentTime();

    for (uint32_t trackId : m_timeline->getTrackOrder()) {
        const auto* track = m_timeline->getTrack(trackId);
        if (!track || !track->visible) continue;
        if (track->type == TrackType::Audio) continue;

        const auto* clip = m_timeline->getActiveClipOnTrack(trackId, currentTime);
        if (!clip) continue;

        const auto* asset = m_timeline->getAsset(clip->assetId);
        if (!asset) continue;

        if (track->type == TrackType::Image) {
            if (asset->imageData.empty() || asset->width <= 0 || asset->height <= 0) continue;

            auto& state = ensureTrackRenderState(trackId, asset->width, asset->height);

            int uploadSlot = state.texture.acquireUploadSlot();
            state.uploader.stage(*m_vkCtx, swapchainFrameIndex,
                                  asset->imageData.data(), asset->width, asset->height);
            state.texture.promoteUploadSlot();

            PendingUpload pu;
            pu.trackId = trackId;
            pu.uploadSlot = uploadSlot;
            pu.width = asset->width;
            pu.height = asset->height;
            m_pendingUploads.push_back(pu);

            LayerInfo layer;
            layer.descriptorSet = state.texture.getDisplayDescriptor();
            layer.width = asset->width;
            layer.height = asset->height;
            layer.trackId = trackId;
            layers.push_back(layer);

        } else if (track->type == TrackType::Video) {
            auto it = m_clipPlayers.find(clip->id);
            if (it == m_clipPlayers.end()) continue;

            auto& player = it->second;
            double sourceTime = clip->toSourceTime(currentTime);

            int w = 0, h = 0;
            bool isNewFrame = false;
            const uint8_t* frameData = player->getVideoFrameAtTime(sourceTime, w, h, &isNewFrame);

            if (!frameData || w <= 0 || h <= 0) {
                // No frame yet — show last texture if available
                auto stateIt = m_trackStates.find(trackId);
                if (stateIt != m_trackStates.end() && stateIt->second.initialized) {
                    LayerInfo layer;
                    layer.descriptorSet = stateIt->second.texture.getDisplayDescriptor();
                    layer.width = stateIt->second.lastWidth;
                    layer.height = stateIt->second.lastHeight;
                    layer.trackId = trackId;
                    layers.push_back(layer);
                }
                m_debugHeldFrames++;
                continue;
            }

            if (isNewFrame) {
                m_debugNewFrames++;
                m_fpsCounterFrames++;
            } else {
                m_debugHeldFrames++;
            }

            if (!isNewFrame) {
                // Still showing held frame — add existing texture as layer, skip re-upload
                auto stateIt = m_trackStates.find(trackId);
                if (stateIt != m_trackStates.end() && stateIt->second.initialized) {
                    LayerInfo layer;
                    layer.descriptorSet = stateIt->second.texture.getDisplayDescriptor();
                    layer.width = stateIt->second.lastWidth;
                    layer.height = stateIt->second.lastHeight;
                    layer.trackId = trackId;
                    layers.push_back(layer);
                }
                continue;
            }

            if (!m_firstFrameReceived) {
                m_firstFrameReceived = true;
            }

            auto& state = ensureTrackRenderState(trackId, w, h);

            int uploadSlot = state.texture.acquireUploadSlot();
            state.uploader.stage(*m_vkCtx, swapchainFrameIndex, frameData, w, h);
            state.texture.promoteUploadSlot();

            PendingUpload pu;
            pu.trackId = trackId;
            pu.uploadSlot = uploadSlot;
            pu.width = w;
            pu.height = h;
            m_pendingUploads.push_back(pu);

            LayerInfo layer;
            layer.descriptorSet = state.texture.getDisplayDescriptor();
            layer.width = w;
            layer.height = h;
            layer.trackId = trackId;
            layers.push_back(layer);
        }
    }

    // Video FPS measurement (new unique frames only)
    {
        double now = wallClock();
        double fpsElapsed = now - m_fpsCounterStart;
        if (fpsElapsed >= 0.5) {
            m_videoFps = m_fpsCounterFrames / fpsElapsed;
            m_fpsCounterFrames = 0;
            m_fpsCounterStart = now;
        }
    }

    // Periodic debug output
    if (m_state == State::Playing) {
        double now = wallClock();
        if (now - m_debugLastPrint >= 1.0) {
            fprintf(stderr,
                "[TIMELINE] t=%.2f/%.2f | clips=%zu layers=%zu | "
                "video=%.1ffps new=%llu held=%llu | audio=%s",
                currentTime, getDuration(),
                m_activeClipIds.size(), layers.size(),
                m_videoFps,
                (unsigned long long)m_debugNewFrames,
                (unsigned long long)m_debugHeldFrames,
                m_audioStarted ? "on" : "off");

            // Verbose: per-clip queue depths
            if (m_verbose) {
                for (auto& [clipId, player] : m_clipPlayers) {
                    const auto* clip = m_timeline->getClip(clipId);
                    const auto* track = clip ? m_timeline->getTrack(clip->trackId) : nullptr;
                    const char* tname = track ? track->name.c_str() : "?";
                    fprintf(stderr, "\n  clip%u(%s) vpkt=%zu vfrm=%zu apkt=%zu afrm=%zu",
                            clipId, tname,
                            player->getVideoPacketQueueSize(),
                            player->getVideoFrameQueueSize(),
                            player->getAudioPacketQueueSize(),
                            player->getAudioFrameQueueSize());
                }
            }
            fprintf(stderr, "\n");
            fflush(stderr);

            m_debugNewFrames = 0;
            m_debugHeldFrames = 0;
            m_debugLastPrint = now;
        }
    }

    return layers;
}

void TimelinePlayback::recordUploads(VkCommandBuffer cmd, int swapchainFrameIndex) {
    for (auto& pu : m_pendingUploads) {
        auto it = m_trackStates.find(pu.trackId);
        if (it == m_trackStates.end()) continue;

        it->second.uploader.recordUpload(cmd, swapchainFrameIndex,
                                          it->second.texture, pu.uploadSlot,
                                          pu.width, pu.height);
    }
    m_pendingUploads.clear();
}

double TimelinePlayback::getCurrentTime() const {
    if (m_audioOutput && m_audioStarted) {
        return m_audioOutput->getPlaybackClock();
    }
    return m_masterClock.get();
}

double TimelinePlayback::getDuration() const {
    if (!m_timeline) return 0.0;
    return m_timeline->getTotalDuration();
}

TrackRenderState& TimelinePlayback::ensureTrackRenderState(uint32_t trackId, int width, int height) {
    auto& state = m_trackStates[trackId];

    if (!state.initialized) {
        state.texture.init(*m_vkCtx, width, height);
        state.uploader.init(*m_vkCtx, width, height);
        state.initialized = true;
        state.lastWidth = width;
        state.lastHeight = height;
    } else if (state.lastWidth != width || state.lastHeight != height) {
        state.texture.resize(*m_vkCtx, width, height);
        state.uploader.ensureCapacity(*m_vkCtx, width, height);
        state.lastWidth = width;
        state.lastHeight = height;
    }

    return state;
}

void TimelinePlayback::activateClip(uint32_t clipId) {
    const auto* clip = m_timeline->getClip(clipId);
    if (!clip) return;

    const auto* track = m_timeline->getTrack(clip->trackId);
    if (!track) return;

    const auto* asset = m_timeline->getAsset(clip->assetId);
    if (!asset) return;

    // Determine which streams to decode based on track type
    // This prevents unconsumed streams from blocking the pipeline
    bool needVideo = (track->type == TrackType::Video) && asset->hasVideo;
    bool needAudio = (track->type == TrackType::Audio) && asset->hasAudio;

    if (!needVideo && !needAudio) return;

    auto player = std::make_unique<ClipPlayer>();
    if (!player->open(asset->filePath, needVideo, needAudio, AudioMixer::OUTPUT_SAMPLE_RATE)) {
        fprintf(stderr, "TimelinePlayback: failed to open clip %u: %s\n",
                clipId, asset->filePath.c_str());
        return;
    }

    player->play();

    // Seek to the right source position based on current timeline time
    double currentTime = m_masterClock.get();
    if (currentTime >= clip->timelineStart) {
        double sourceTime = clip->toSourceTime(currentTime);
        player->seek(sourceTime);
    }

    if (m_verbose) {
        fprintf(stderr, "[TIMELINE] Activate clip %u on %s (video=%d audio=%d)\n",
                clipId, track->name.c_str(), needVideo, needAudio);
    }

    m_clipPlayers[clipId] = std::move(player);
    m_activeClipIds.insert(clipId);
}

void TimelinePlayback::deactivateClip(uint32_t clipId) {
    auto it = m_clipPlayers.find(clipId);
    if (it != m_clipPlayers.end()) {
        if (m_verbose) {
            fprintf(stderr, "[TIMELINE] Deactivate clip %u\n", clipId);
        }
        it->second->stop();
        m_clipPlayers.erase(it);
    }
    m_activeClipIds.erase(clipId);
}

void TimelinePlayback::rebuildAudioSources() {
    if (!m_timeline) return;

    std::vector<AudioMixSource> sources;

    for (auto& [clipId, player] : m_clipPlayers) {
        if (!player->hasAudio()) continue;

        const auto* clip = m_timeline->getClip(clipId);
        if (!clip) continue;

        const auto* track = m_timeline->getTrack(clip->trackId);
        if (!track || track->type != TrackType::Audio) continue;

        AudioMixSource src;
        src.queue = &player->getAudioFrameQueue();
        src.clip = clip;
        src.track = track;
        src.timeBase = player->getAudioTimeBase();
        src.clipId = clipId;
        sources.push_back(src);
    }

    m_audioMixer.setSources(std::move(sources));
}
