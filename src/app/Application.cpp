#include "app/Application.h"
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <cstdio>
#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
}

Application::Application() = default;

Application::~Application() {
    shutdown();
}

bool Application::init(const std::string& filePath) {
    m_filePath = filePath;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    m_window = SDL_CreateWindow(
        "Video Editor",
        m_windowWidth, m_windowHeight,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    if (!m_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    if (!m_vkCtx.init(m_window)) return false;
    if (!m_swapchain.init(m_vkCtx, m_windowWidth, m_windowHeight)) return false;
    if (!m_imguiLayer.init(m_window, m_vkCtx, m_swapchain)) return false;

    // Initialize audio output at fixed 48kHz stereo
    if (!m_audioOutput.init(AudioMixer::OUTPUT_SAMPLE_RATE, AudioMixer::OUTPUT_CHANNELS)) {
        fprintf(stderr, "Failed to initialize audio output\n");
    }

    // Create default timeline tracks
    m_timeline.addTrack("Video 1", TrackType::Video);
    m_timeline.addTrack("Audio 1", TrackType::Audio);

    // Initialize TimelinePlayback orchestrator
    m_timelinePlayback.setTimeline(&m_timeline);
    m_timelinePlayback.setAudioOutput(&m_audioOutput);
    m_timelinePlayback.setVerbose(m_verbose);
    m_timelinePlayback.init(m_vkCtx);

    if (m_verbose) {
        fprintf(stderr, "[APP] Verbose logging enabled\n");
    }

    // Wire PlayerUI transport callbacks to TimelinePlayback
    m_playerUI.onPlay = [this]() { m_timelinePlayback.play(); };
    m_playerUI.onPause = [this]() { m_timelinePlayback.pause(); };
    m_playerUI.onStop = [this]() { m_timelinePlayback.stop(); };
    m_playerUI.onSeek = [this](double t) { m_timelinePlayback.seek(t); };

    // Setup file dialog callback
    m_fileDialog.setCallback([this](const std::string& path) {
        importToTimeline(path);
    });

    // Open file if provided on command line
    if (!filePath.empty()) {
        importToTimeline(filePath);
    }

    m_running = true;
    return true;
}

void Application::importToTimeline(const std::string& path) {
    uint32_t assetId = m_timeline.importFile(path);
    if (assetId == 0) {
        fprintf(stderr, "Failed to import to timeline: %s\n", path.c_str());
        return;
    }

    if (m_verbose) {
        const auto* asset = m_timeline.getAsset(assetId);
        if (asset) {
            fprintf(stderr, "[APP] Imported asset %u: %s (type=%d video=%d audio=%d "
                    "dur=%.2fs %dx%d %.1ffps sr=%d ch=%d)\n",
                    assetId, path.c_str(), (int)asset->type,
                    asset->hasVideo, asset->hasAudio,
                    asset->duration, asset->width, asset->height,
                    asset->fps, asset->sampleRate, asset->channels);
        }
        // Log clips created
        for (auto& [clipId, clip] : m_timeline.getAllClips()) {
            if (clip.assetId == assetId) {
                const auto* track = m_timeline.getTrack(clip.trackId);
                fprintf(stderr, "[APP]   clip %u on track '%s' [%.2f - %.2f] src[%.2f - %.2f]\n",
                        clipId, track ? track->name.c_str() : "?",
                        clip.timelineStart, clip.getTimelineEnd(),
                        clip.sourceIn, clip.sourceOut);
            }
        }
    }

    if (m_timelinePlayback.getState() == TimelinePlayback::State::Stopped) {
        m_timelinePlayback.play();
    }
}

void Application::run() {
    while (m_running) {
        processEvents();

        if (m_minimized) {
            SDL_Delay(16);
            continue;
        }

        if (m_resizeNeeded) {
            handleResize();
            m_resizeNeeded = false;
        }

        if (!renderFrame()) {
            m_resizeNeeded = true;
        }
    }

    vkDeviceWaitIdle(m_vkCtx.device);
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        m_imguiLayer.processEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                m_running = false;
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                m_minimized = true;
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                m_minimized = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                m_resizeNeeded = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!ImGui::GetIO().WantCaptureKeyboard) {
                    switch (event.key.key) {
                        case SDLK_ESCAPE:
                            m_running = false;
                            break;
                        case SDLK_SPACE:
                            m_timelinePlayback.togglePlayPause();
                            break;
                        case SDLK_LEFT:
                            m_timelinePlayback.seek(
                                m_timelinePlayback.getCurrentTime() - 5.0);
                            break;
                        case SDLK_RIGHT:
                            m_timelinePlayback.seek(
                                m_timelinePlayback.getCurrentTime() + 5.0);
                            break;
                        default:
                            break;
                    }
                }
                break;
            case SDL_EVENT_DROP_FILE:
                importToTimeline(event.drop.data);
                break;
            default:
                break;
        }
    }
}

bool Application::renderFrame() {
    auto& sc = m_swapchain;
    uint32_t frame = sc.currentFrame;

    vkWaitForFences(m_vkCtx.device, 1, &sc.inFlightFences[frame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = sc.acquireNextImage(m_vkCtx.device, sc.imageAvailableSemaphores[frame], imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return false;

    vkResetFences(m_vkCtx.device, 1, &sc.inFlightFences[frame]);

    // Update timeline playback state (activate/deactivate ClipPlayers)
    m_timelinePlayback.update();

    // Prepare video layers (stages GPU data, returns layer descriptors)
    auto layers = m_timelinePlayback.prepareFrame(frame);

    // Build ImGui frame
    m_imguiLayer.beginFrame();

    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open", "Ctrl+O")) {
                m_fileDialog.open();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                m_running = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {}
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Fullscreen", "F11", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            if (ImGui::MenuItem("Play/Pause", "Space")) {
                m_timelinePlayback.togglePlayPause();
            }
            if (ImGui::MenuItem("Stop")) {
                m_timelinePlayback.stop();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Seek Back 5s", "Left")) {
                m_timelinePlayback.seek(
                    m_timelinePlayback.getCurrentTime() - 5.0);
            }
            if (ImGui::MenuItem("Seek Forward 5s", "Right")) {
                m_timelinePlayback.seek(
                    m_timelinePlayback.getCurrentTime() + 5.0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Player UI — driven by TimelinePlayback
    double currentTime = m_timelinePlayback.getCurrentTime();
    double totalDuration = m_timelinePlayback.getDuration();
    bool playing = m_timelinePlayback.isPlaying();
    m_playerUI.videoFps = m_timelinePlayback.getVideoFps();
    m_playerUI.activeClips = m_timelinePlayback.getActiveClipCount();
    m_playerUI.render(layers, currentTime, totalDuration, playing);

    // Timeline UI
    m_timelineUI.setCurrentTime(currentTime);
    m_timelineUI.render(m_timeline, currentTime, totalDuration);

    // Handle timeline seek requests (clicks on ruler/playhead)
    if (m_timelineUI.hasSeekRequest()) {
        double seekTime = m_timelineUI.getSeekTime();
        m_timelinePlayback.seek(seekTime);
    }

    // Clip Properties panel
    double clipFps = 30.0;
    uint32_t selClipId = m_timelineUI.getSelectedClipId();
    if (selClipId != 0) {
        const auto* selClip = m_timeline.getClip(selClipId);
        if (selClip) {
            const auto* selAsset = m_timeline.getAsset(selClip->assetId);
            if (selAsset && selAsset->fps > 0.0) clipFps = selAsset->fps;
        }
    }
    m_clipPropertiesUI.render(m_timeline, selClipId, clipFps);

    // File dialog
    m_fileDialog.render();

    m_imguiLayer.endFrame();

    // Record command buffer
    VkCommandBuffer cmd = sc.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Record GPU uploads for all tracks (before render pass)
    m_timelinePlayback.recordUploads(cmd, frame);

    VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = sc.renderPass;
    rpBegin.framebuffer = sc.framebuffers[imageIndex];
    rpBegin.renderArea.extent = sc.extent;
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    m_imguiLayer.render(cmd);
    vkCmdEndRenderPass(cmd);

    vkEndCommandBuffer(cmd);

    // Submit
    VkSemaphore waitSemaphores[] = {sc.imageAvailableSemaphores[frame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {sc.renderFinishedSemaphores[frame]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(m_vkCtx.graphicsQueue, 1, &submitInfo, sc.inFlightFences[frame]);

    // Present
    result = sc.present(m_vkCtx.presentQueue, sc.renderFinishedSemaphores[frame], imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        m_resizeNeeded = true;
    }

    sc.currentFrame = (frame + 1) % Swapchain::MAX_FRAMES_IN_FLIGHT;
    return true;
}

void Application::handleResize() {
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w == 0 || h == 0) {
        m_minimized = true;
        return;
    }
    m_minimized = false;
    m_windowWidth = w;
    m_windowHeight = h;
    m_swapchain.recreate(m_vkCtx, w, h);
}

void Application::shutdown() {
    m_timelinePlayback.stop();
    m_audioOutput.shutdown();

    if (m_vkCtx.device) vkDeviceWaitIdle(m_vkCtx.device);

    m_timelinePlayback.shutdown();
    m_imguiLayer.shutdown();
    m_swapchain.shutdown(m_vkCtx.device);
    m_vkCtx.shutdown();

    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}
