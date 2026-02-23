#pragma once

#include <SDL3/SDL.h>
#include <string>
#include "vulkan/VulkanContext.h"
#include "vulkan/Swapchain.h"
#include "ui/ImGuiLayer.h"
#include "ui/PlayerUI.h"
#include "ui/FileDialog.h"
#include "ui/TimelineUI.h"
#include "ui/ClipPropertiesUI.h"
#include "ui/ExportDialog.h"
#include "export/ExportSession.h"
#include "export/ExportSettings.h"
#include "media/AudioOutput.h"
#include "timeline/Timeline.h"
#include "timeline/TimelinePlayback.h"
#include <memory>

class Application {
public:
    Application();
    ~Application();

    void setVerbose(bool v) { m_verbose = v; }
    bool init(const std::string& filePath = "");
    void run();
    void shutdown();

private:
    void processEvents();
    bool renderFrame();
    void handleResize();
    void importToTimeline(const std::string& path);

    SDL_Window* m_window = nullptr;
    VulkanContext m_vkCtx;
    Swapchain m_swapchain;
    ImGuiLayer m_imguiLayer;

    // Audio output (shared — initialized once at 48kHz)
    AudioOutput m_audioOutput;

    // Timeline data model + orchestrator
    Timeline m_timeline;
    TimelinePlayback m_timelinePlayback;

    // UI
    PlayerUI m_playerUI;
    FileDialog m_fileDialog;
    TimelineUI m_timelineUI;
    ClipPropertiesUI m_clipPropertiesUI;
    ExportDialog m_exportDialog;
    std::unique_ptr<ExportSession> m_exportSession;
    ExportSettings m_exportSettings;
    bool m_showExportDialog = false;

    bool m_running = false;
    bool m_minimized = false;
    bool m_resizeNeeded = false;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    std::string m_filePath;
    bool m_verbose = false;
};
