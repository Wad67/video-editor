#include "export/ExportSession.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libswscale/swscale.h>
}

ExportSession::~ExportSession() {
    cancel();
    wait();
}

bool ExportSession::start(const Timeline& timeline, const ExportSettings& settings) {
    State cur = m_state.load();
    if (cur == State::Running) return false;

    if (m_thread.joinable()) m_thread.join();

    m_timelineCopy = timeline;
    m_settings = settings;
    m_state.store(State::Running);
    m_cancelRequested.store(false);
    m_progress.store(0.0);
    m_framesEncoded.store(0);
    {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_errorMessage.clear();
    }

    m_thread = std::thread(&ExportSession::exportLoop, this);
    return true;
}

void ExportSession::cancel() {
    m_cancelRequested.store(true);
}

std::string ExportSession::getErrorMessage() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_errorMessage;
}

void ExportSession::wait() {
    if (m_thread.joinable()) m_thread.join();
}

void ExportSession::fail(const std::string& msg) {
    fprintf(stderr, "[EXPORT] FAILED: %s\n", msg.c_str());
    std::lock_guard<std::mutex> lock(m_errorMutex);
    m_errorMessage = msg;
    m_state.store(State::Failed);
}

void ExportSession::exportLoop() {
    fprintf(stderr, "[EXPORT] Starting export to %s\n", m_settings.outputPath.c_str());

    // 1. Open muxer
    if (!m_muxer.open(m_settings.outputPath)) {
        fail("Cannot open output file: " + m_settings.outputPath);
        return;
    }

    int muxerFlags = m_muxer.getFormatContext()->oformat->flags;

    // 2. Init video encoder
    if (!m_videoEncoder.init(m_settings, muxerFlags)) {
        fail("Video encoder initialization failed");
        m_muxer.close();
        return;
    }
    int videoIdx = m_muxer.addVideoStream(m_videoEncoder.getCodecContext());
    if (videoIdx < 0) {
        fail("Cannot add video stream");
        m_videoEncoder.shutdown();
        m_muxer.close();
        return;
    }

    // 3. Init audio encoder
    if (!m_audioEncoder.init(m_settings, muxerFlags)) {
        fail("Audio encoder initialization failed");
        m_videoEncoder.shutdown();
        m_muxer.close();
        return;
    }
    int audioIdx = m_muxer.addAudioStream(m_audioEncoder.getCodecContext());
    if (audioIdx < 0) {
        fail("Cannot add audio stream");
        m_audioEncoder.shutdown();
        m_videoEncoder.shutdown();
        m_muxer.close();
        return;
    }

    // 4. Write header
    if (!m_muxer.writeHeader()) {
        fail("Cannot write container header");
        m_audioEncoder.shutdown();
        m_videoEncoder.shutdown();
        m_muxer.close();
        return;
    }

    // 5. Compute frame count
    double duration = m_timelineCopy.getTotalDuration();
    if (m_settings.endTime > 0 && m_settings.endTime < duration)
        duration = m_settings.endTime;
    double startTime = m_settings.startTime;
    double exportDuration = duration - startTime;
    if (exportDuration <= 0) {
        fail("Export range is empty");
        m_muxer.writeTrailer();
        m_muxer.close();
        return;
    }

    int64_t totalFrames = static_cast<int64_t>(exportDuration * m_settings.fps);
    m_totalFrames.store(totalFrames);

    // 6. Allocate composite buffer
    std::vector<uint8_t> compositeBuffer(m_settings.width * m_settings.height * 4);

    // 7. Setup export clock (paused — we manually set it)
    m_exportClock.set(startTime);
    m_exportClock.pause();

    double frameDuration = 1.0 / m_settings.fps;
    int audioSamplesPerFrame = static_cast<int>(
        m_settings.audioSampleRate * frameDuration) + 1;
    m_audioBuffer.resize(audioSamplesPerFrame * m_settings.audioChannels);

    fprintf(stderr, "[EXPORT] Exporting %lld frames (%.2fs @ %.1f fps)\n",
            (long long)totalFrames, exportDuration, m_settings.fps);

    auto exportStart = std::chrono::steady_clock::now();

    // 8. Main export loop
    for (int64_t frame = 0; frame < totalFrames; frame++) {
        if (m_cancelRequested.load()) {
            m_state.store(State::Cancelled);
            fprintf(stderr, "[EXPORT] Cancelled at frame %lld/%lld\n",
                    (long long)frame, (long long)totalFrames);
            break;
        }

        double currentTime = startTime + frame * frameDuration;
        m_exportClock.set(currentTime);

        // Activate/deactivate clips for this time
        updateActiveClips(currentTime);

        bool verbose = (frame < 5 || frame % 500 == 0);

        if (verbose) {
            fprintf(stderr, "[EXPORT] Frame %lld t=%.3f active_clips=%zu audio_sources=%d\n",
                    (long long)frame, currentTime,
                    m_activeClipIds.size(), m_audioMixer.hasSources() ? 1 : 0);
            for (auto& [clipId, player] : m_clipPlayers) {
                fprintf(stderr, "[EXPORT]   clip%u: video=%d(frm=%zu) audio=%d(frm=%zu)\n",
                        clipId, player->hasVideo(),
                        player->getVideoFrameQueueSize(),
                        player->hasAudio(),
                        player->getAudioFrameQueueSize());
            }
        }

        // Composite video layers
        compositeFrame(currentTime, compositeBuffer.data(),
                       m_settings.width, m_settings.height);

        // Encode video frame
        m_videoEncoder.encodeFrame(
            compositeBuffer.data(), m_settings.width, m_settings.height, frame,
            [&](AVPacket* pkt) {
                av_packet_rescale_ts(pkt,
                    m_videoEncoder.getCodecContext()->time_base,
                    m_muxer.getVideoStream()->time_base);
                pkt->stream_index = videoIdx;
                m_muxer.writePacket(pkt);
                if (verbose) {
                    fprintf(stderr, "[EXPORT]   video pkt: pts=%lld dts=%lld size=%d\n",
                            (long long)pkt->pts, (long long)pkt->dts, pkt->size);
                }
            });

        // Encode audio for this frame's duration
        encodeAudioForFrame(frameDuration);

        m_framesEncoded.store(frame + 1);
        m_progress.store(static_cast<double>(frame + 1) / totalFrames);

        // Progress logging every 100 frames
        if ((frame + 1) % 100 == 0 || frame + 1 == totalFrames) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - exportStart).count();
            double fps = (frame + 1) / elapsed;
            double eta = (totalFrames - frame - 1) / fps;
            fprintf(stderr, "[EXPORT] Frame %lld/%lld (%.1f%%) - %.1f fps - ETA %.0fs\n",
                    (long long)(frame + 1), (long long)totalFrames,
                    100.0 * (frame + 1) / totalFrames, fps, eta);
        }
    }

    // 9. Flush encoders
    m_videoEncoder.flush([&](AVPacket* pkt) {
        av_packet_rescale_ts(pkt,
            m_videoEncoder.getCodecContext()->time_base,
            m_muxer.getVideoStream()->time_base);
        pkt->stream_index = videoIdx;
        m_muxer.writePacket(pkt);
    });

    m_audioEncoder.flush([&](AVPacket* pkt) {
        av_packet_rescale_ts(pkt,
            m_audioEncoder.getCodecContext()->time_base,
            m_muxer.getAudioStream()->time_base);
        pkt->stream_index = audioIdx;
        m_muxer.writePacket(pkt);
    });

    // 10. Finalize
    m_muxer.writeTrailer();

    // Cleanup
    for (auto& [id, player] : m_clipPlayers) player->stop();
    m_clipPlayers.clear();
    m_activeClipIds.clear();
    m_audioMixer.clearSources();
    m_audioEncoder.shutdown();
    m_videoEncoder.shutdown();
    m_muxer.close();

    if (m_state.load() == State::Running) {
        m_state.store(State::Completed);
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - exportStart).count();
        fprintf(stderr, "[EXPORT] Complete! %.1fs total (%.1f fps avg)\n",
                elapsed, totalFrames / elapsed);
    }
}

void ExportSession::updateActiveClips(double time) {
    double lookahead = time + 0.5;

    std::unordered_set<uint32_t> neededClipIds;

    for (uint32_t trackId : m_timelineCopy.getTrackOrder()) {
        const auto* track = m_timelineCopy.getTrack(trackId);
        if (!track) continue;
        if (!track->visible && track->type != TrackType::Audio) continue;
        if (track->type == TrackType::Image) continue;

        for (uint32_t clipId : track->clipIds) {
            const auto* clip = m_timelineCopy.getClip(clipId);
            if (!clip) continue;
            if (clip->getTimelineEnd() > time && clip->timelineStart < lookahead) {
                neededClipIds.insert(clipId);
            }
        }
    }

    // Deactivate clips no longer needed
    std::vector<uint32_t> toRemove;
    for (uint32_t clipId : m_activeClipIds) {
        if (neededClipIds.find(clipId) == neededClipIds.end()) {
            toRemove.push_back(clipId);
        }
    }
    if (!toRemove.empty()) {
        m_audioMixer.clearSources();
    }
    for (uint32_t clipId : toRemove) {
        deactivateClip(clipId);
    }

    // Activate new clips
    bool sourcesChanged = !toRemove.empty();
    for (uint32_t clipId : neededClipIds) {
        if (m_activeClipIds.find(clipId) == m_activeClipIds.end()) {
            activateClip(clipId);
            sourcesChanged = true;
        }
    }

    if (sourcesChanged) {
        rebuildAudioSources();
    }
}

void ExportSession::activateClip(uint32_t clipId) {
    const auto* clip = m_timelineCopy.getClip(clipId);
    if (!clip) return;
    const auto* track = m_timelineCopy.getTrack(clip->trackId);
    if (!track) return;
    const auto* asset = m_timelineCopy.getAsset(clip->assetId);
    if (!asset) return;

    bool needVideo = (track->type == TrackType::Video) && asset->hasVideo;
    bool needAudio = (track->type == TrackType::Audio) && asset->hasAudio;
    if (!needVideo && !needAudio) return;

    auto player = std::make_unique<ClipPlayer>();
    if (!player->open(asset->filePath, needVideo, needAudio,
                      AudioMixer::OUTPUT_SAMPLE_RATE)) {
        fprintf(stderr, "[EXPORT] Failed to open clip %u\n", clipId);
        return;
    }

    player->play();

    double currentTime = m_exportClock.get();
    if (currentTime >= clip->timelineStart) {
        double sourceTime = clip->toSourceTime(currentTime);
        player->seek(sourceTime);
    }

    m_clipPlayers[clipId] = std::move(player);
    m_activeClipIds.insert(clipId);
}

void ExportSession::deactivateClip(uint32_t clipId) {
    auto it = m_clipPlayers.find(clipId);
    if (it != m_clipPlayers.end()) {
        it->second->stop();
        m_clipPlayers.erase(it);
    }
    m_activeClipIds.erase(clipId);
}

void ExportSession::rebuildAudioSources() {
    std::vector<AudioMixSource> sources;

    for (auto& [clipId, player] : m_clipPlayers) {
        if (!player->hasAudio()) continue;
        const auto* clip = m_timelineCopy.getClip(clipId);
        if (!clip) continue;
        const auto* track = m_timelineCopy.getTrack(clip->trackId);
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

void ExportSession::compositeFrame(double time, uint8_t* outputRGBA,
                                    int outW, int outH) {
    memset(outputRGBA, 0, outW * outH * 4);

    for (uint32_t trackId : m_timelineCopy.getTrackOrder()) {
        const auto* track = m_timelineCopy.getTrack(trackId);
        if (!track || !track->visible) continue;
        if (track->type == TrackType::Audio) continue;

        const auto* clip = m_timelineCopy.getActiveClipOnTrack(trackId, time);
        if (!clip) continue;
        const auto* asset = m_timelineCopy.getAsset(clip->assetId);
        if (!asset) continue;

        const uint8_t* srcPixels = nullptr;
        int srcW = 0, srcH = 0;

        if (track->type == TrackType::Image) {
            if (asset->imageData.empty()) continue;
            srcPixels = asset->imageData.data();
            srcW = asset->width;
            srcH = asset->height;
        } else if (track->type == TrackType::Video) {
            auto it = m_clipPlayers.find(clip->id);
            if (it == m_clipPlayers.end()) continue;

            double sourceTime = clip->toSourceTime(time);

            // Wait for decode with retries
            for (int attempt = 0; attempt < 50; attempt++) {
                bool isNew = false;
                srcPixels = it->second->getVideoFrameAtTime(
                    sourceTime, srcW, srcH, &isNew);
                if (srcPixels && srcW > 0 && srcH > 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        if (!srcPixels || srcW <= 0 || srcH <= 0) continue;

        if (srcW == outW && srcH == outH) {
            memcpy(outputRGBA, srcPixels, outW * outH * 4);
        } else {
            // Resize via swscale
            SwsContext* resizeCtx = sws_getContext(
                srcW, srcH, AV_PIX_FMT_RGBA,
                outW, outH, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (resizeCtx) {
                const uint8_t* srcSlice[1] = { srcPixels };
                int srcStride[1] = { srcW * 4 };
                uint8_t* dstSlice[1] = { outputRGBA };
                int dstStride[1] = { outW * 4 };
                sws_scale(resizeCtx, srcSlice, srcStride, 0, srcH,
                          dstSlice, dstStride);
                sws_freeContext(resizeCtx);
            }
        }
    }
}

void ExportSession::encodeAudioForFrame(double frameDuration) {
    static int64_t s_audioFrameCount = 0;

    int numSamples = static_cast<int>(
        m_settings.audioSampleRate * frameDuration);
    if (numSamples <= 0) return;

    if ((int)m_audioBuffer.size() < numSamples * m_settings.audioChannels) {
        m_audioBuffer.resize(numSamples * m_settings.audioChannels);
    }

    // Wait for audio frames to be available in the mixer sources.
    // The ClipPlayer decode threads run async and may not have produced
    // frames yet when we first try to read.
    bool hasAudioSources = m_audioMixer.hasSources();
    if (hasAudioSources) {
        int waitAttempts = 0;
        for (int attempt = 0; attempt < 100; attempt++) {
            bool hasFrames = false;
            for (auto& [clipId, player] : m_clipPlayers) {
                if (player->hasAudio() && player->getAudioFrameQueueSize() > 0) {
                    hasFrames = true;
                    break;
                }
            }
            if (hasFrames) break;
            waitAttempts++;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (s_audioFrameCount < 5 && waitAttempts > 0) {
            fprintf(stderr, "[EXPORT] Audio: waited %d attempts for frames\n", waitAttempts);
        }
    }

    m_audioMixer.fillBuffer(m_audioBuffer.data(), numSamples, m_exportClock);

    // Debug: check if audio has actual data
    if (s_audioFrameCount < 5) {
        float maxSample = 0.0f;
        for (int i = 0; i < numSamples * m_settings.audioChannels; i++) {
            float v = std::abs(m_audioBuffer[i]);
            if (v > maxSample) maxSample = v;
        }
        fprintf(stderr, "[EXPORT] Audio frame %lld: %d samples, max=%.6f, sources=%d\n",
                (long long)s_audioFrameCount, numSamples, maxSample,
                hasAudioSources ? 1 : 0);
    }

    int audioIdx = m_muxer.getAudioStreamIndex();
    m_audioEncoder.encode(m_audioBuffer.data(), numSamples,
        [&](AVPacket* pkt) {
            av_packet_rescale_ts(pkt,
                m_audioEncoder.getCodecContext()->time_base,
                m_muxer.getAudioStream()->time_base);
            pkt->stream_index = audioIdx;
            m_muxer.writePacket(pkt);
        });

    s_audioFrameCount++;
}
