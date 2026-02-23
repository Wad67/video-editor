#include "ui/ExportDialog.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

bool ExportDialog::render(ExportSettings& settings, bool& visible) {
    if (!visible) return false;

    bool startExport = false;

    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Export Video", &visible)) {
        // Output path
        ImGui::InputText("Output File", m_outputPath, sizeof(m_outputPath));

        ImGui::Separator();
        ImGui::Text("Video");

        // Resolution
        const char* resOptions[] = { "Source", "1920x1080", "1280x720", "640x480" };
        ImGui::Combo("Resolution", &m_resIndex, resOptions, 4);

        // FPS
        const char* fpsOptions[] = { "Source", "60", "30", "24" };
        ImGui::Combo("Frame Rate", &m_fpsIndex, fpsOptions, 4);

        // Codec
        const char* codecOptions[] = { "H.264 (libx264)", "H.265 (libx265)", "H.264 VAAPI" };
        ImGui::Combo("Codec", &m_codecIndex, codecOptions, 3);

        // CRF (only for software codecs)
        if (m_codecIndex < 2) {
            ImGui::SliderInt("Quality (CRF)", &settings.crf, 0, 51);
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Lower = better quality, larger file.\n"
                                  "18-23 is visually lossless for most content.");
            }
        } else {
            int brMbps = settings.videoBitrate / 1000000;
            if (ImGui::SliderInt("Bitrate (Mbps)", &brMbps, 1, 50)) {
                settings.videoBitrate = brMbps * 1000000;
            }
        }

        ImGui::Separator();
        ImGui::Text("Audio");

        const char* abrOptions[] = { "128 kbps", "192 kbps", "256 kbps", "320 kbps" };
        int abrIndex = 1;
        if (settings.audioBitrate <= 128000) abrIndex = 0;
        else if (settings.audioBitrate <= 192000) abrIndex = 1;
        else if (settings.audioBitrate <= 256000) abrIndex = 2;
        else abrIndex = 3;
        if (ImGui::Combo("Audio Bitrate", &abrIndex, abrOptions, 4)) {
            const int rates[] = { 128000, 192000, 256000, 320000 };
            settings.audioBitrate = rates[abrIndex];
        }

        ImGui::Separator();

        // Apply resolution/fps selections
        switch (m_resIndex) {
            case 0: settings.width = m_sourceWidth; settings.height = m_sourceHeight; break;
            case 1: settings.width = 1920; settings.height = 1080; break;
            case 2: settings.width = 1280; settings.height = 720; break;
            case 3: settings.width = 640; settings.height = 480; break;
        }
        if (settings.width <= 0) settings.width = 1920;
        if (settings.height <= 0) settings.height = 1080;
        // Ensure even dimensions for YUV420P
        settings.width &= ~1;
        settings.height &= ~1;

        switch (m_fpsIndex) {
            case 0: settings.fps = m_sourceFps > 0 ? m_sourceFps : 30.0; break;
            case 1: settings.fps = 60.0; break;
            case 2: settings.fps = 30.0; break;
            case 3: settings.fps = 24.0; break;
        }

        switch (m_codecIndex) {
            case 0: settings.videoCodec = VideoCodecChoice::H264_Software; break;
            case 1: settings.videoCodec = VideoCodecChoice::H265_Software; break;
            case 2: settings.videoCodec = VideoCodecChoice::H264_VAAPI; break;
        }

        settings.outputPath = m_outputPath;

        ImGui::Text("Output: %dx%d @ %.0f fps", settings.width, settings.height, settings.fps);

        ImGui::Spacing();
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            startExport = true;
            visible = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            visible = false;
        }
    }
    ImGui::End();

    return startExport;
}

void ExportDialog::renderProgress(ExportSession& session) {
    auto state = session.getState();
    if (state != ExportSession::State::Running &&
        state != ExportSession::State::Completed &&
        state != ExportSession::State::Failed &&
        state != ExportSession::State::Cancelled) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(350, 140), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Export Progress", &open)) {
        if (state == ExportSession::State::Running) {
            float progress = static_cast<float>(session.getProgress());
            ImGui::ProgressBar(progress, ImVec2(-1, 0));

            int64_t encoded = session.getFramesEncoded();
            int64_t total = session.getTotalFrames();
            ImGui::Text("Frame %lld / %lld", (long long)encoded, (long long)total);

            if (ImGui::Button("Cancel Export")) {
                session.cancel();
            }
        } else if (state == ExportSession::State::Completed) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Export complete!");
            ImGui::Text("%lld frames exported", (long long)session.getFramesEncoded());
        } else if (state == ExportSession::State::Failed) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Export failed!");
            ImGui::TextWrapped("%s", session.getErrorMessage().c_str());
        } else if (state == ExportSession::State::Cancelled) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Export cancelled");
            ImGui::Text("%lld frames exported before cancel",
                        (long long)session.getFramesEncoded());
        }
    }
    ImGui::End();
}

