#include "ui/PlayerUI.h"
#include "timeline/TimelinePlayback.h"
#include <imgui.h>
#include <cstdio>
#include <algorithm>

void PlayerUI::render(const std::vector<LayerInfo>& layers,
                       double currentTime, double duration, bool playing) {
    renderViewport(layers);
    renderTransportControls(currentTime, duration, playing);
}

void PlayerUI::renderViewport(const std::vector<LayerInfo>& layers) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (!layers.empty()) {
        // Draw each layer as a full-size quad (bottom-to-top, last drawn wins)
        ImVec2 basePos = ImGui::GetCursorPos();
        for (const auto& layer : layers) {
            if (!layer.descriptorSet || layer.width <= 0 || layer.height <= 0) continue;

            float aspect = static_cast<float>(layer.width) / layer.height;
            float displayW = avail.x;
            float displayH = avail.x / aspect;
            if (displayH > avail.y) {
                displayH = avail.y;
                displayW = avail.y * aspect;
            }

            float offsetX = (avail.x - displayW) * 0.5f;
            float offsetY = (avail.y - displayH) * 0.5f;

            // Reset cursor for each layer (they stack in the same position)
            ImGui::SetCursorPos(ImVec2(basePos.x + offsetX, basePos.y + offsetY));
            ImGui::Image((ImTextureID)layer.descriptorSet, ImVec2(displayW, displayH));
        }

        // FPS / stats overlay — top-right corner
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        char fpsText[256];
        snprintf(fpsText, sizeof(fpsText),
                 "Render: %.1f fps\n"
                 "Video:  %.1f fps\n"
                 "Layers: %d  Clips: %zu",
                 ImGui::GetIO().Framerate,
                 videoFps,
                 (int)layers.size(),
                 activeClips);

        ImVec2 textSize = ImGui::CalcTextSize(fpsText);
        float pad = 6.0f;
        ImVec2 overlayPos(winPos.x + winSize.x - textSize.x - pad * 2,
                          winPos.y + ImGui::GetFrameHeight() + pad);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            ImVec2(overlayPos.x - pad, overlayPos.y - pad),
            ImVec2(overlayPos.x + textSize.x + pad, overlayPos.y + textSize.y + pad),
            IM_COL32(0, 0, 0, 160), 4.0f);
        drawList->AddText(overlayPos, IM_COL32(255, 255, 255, 255), fpsText);

    } else {
        ImVec2 textSize = ImGui::CalcTextSize("No video loaded");
        ImGui::SetCursorPos(ImVec2((avail.x - textSize.x) * 0.5f,
                                    (avail.y - textSize.y) * 0.5f));
        ImGui::TextDisabled("No video loaded");
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void PlayerUI::renderTransportControls(double currentTime, double duration, bool playing) {
    ImGui::Begin("Controls", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::Button("|<")) {
        if (onSeek) onSeek(0.0);
    }
    ImGui::SameLine();

    if (playing) {
        if (ImGui::Button("||")) {
            if (onPause) onPause();
        }
    } else {
        if (ImGui::Button(">")) {
            if (onPlay) onPlay();
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("[]")) {
        if (onStop) onStop();
    }
    ImGui::SameLine();

    const char* currentStr = formatTime(currentTime);
    char currentBuf[64];
    snprintf(currentBuf, sizeof(currentBuf), "%s", currentStr);
    ImGui::Text("%s / %s", currentBuf, formatTime(duration));

    float progress = (duration > 0.0) ? static_cast<float>(currentTime / duration) : 0.0f;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##scrub", &progress, 0.0f, 1.0f, "")) {
        if (onSeek) onSeek(progress * duration);
    }

    ImGui::End();
}

const char* PlayerUI::formatTime(double seconds) {
    static char buf[64];
    if (seconds < 0) seconds = 0;
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    if (h > 0)
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else
        snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}
