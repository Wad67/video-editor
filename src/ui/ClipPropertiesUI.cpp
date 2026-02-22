#include "ui/ClipPropertiesUI.h"
#include "timeline/Timeline.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

static const char* mediaTypeName(MediaType type) {
    switch (type) {
        case MediaType::Video: return "Video";
        case MediaType::Audio: return "Audio";
        case MediaType::Image: return "Image";
    }
    return "Unknown";
}

static const char* trackTypeName(TrackType type) {
    switch (type) {
        case TrackType::Video: return "Video";
        case TrackType::Audio: return "Audio";
        case TrackType::Image: return "Image";
    }
    return "Unknown";
}

void ClipPropertiesUI::render(Timeline& timeline, uint32_t selectedClipId, double fps) {
    ImGui::Begin("Clip Properties");

    if (selectedClipId == 0) {
        ImGui::TextDisabled("No clip selected");
        ImGui::End();
        return;
    }

    auto* clip = timeline.getClip(selectedClipId);
    if (!clip) {
        ImGui::TextDisabled("Clip not found");
        ImGui::End();
        return;
    }

    const auto* track = timeline.getTrack(clip->trackId);
    const auto* asset = timeline.getAsset(clip->assetId);

    double frameDuration = (fps > 0.0) ? (1.0 / fps) : (1.0 / 30.0);

    // --- Timing ---
    if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
        double timelineStart = clip->timelineStart;
        double sourceIn = clip->sourceIn;
        double sourceOut = clip->sourceOut;
        double duration = clip->getDuration();
        double timelineEnd = clip->getTimelineEnd();

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputDouble("Timeline Start", &timelineStart, 0.0, 0.0, "%.3f")) {
            clip->timelineStart = std::max(0.0, timelineStart);
        }

        // Nudge buttons for frame-accurate positioning
        ImGui::SameLine();
        if (ImGui::SmallButton("-##nudgeL")) {
            clip->timelineStart = std::max(0.0, clip->timelineStart - frameDuration);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+##nudgeR")) {
            clip->timelineStart += frameDuration;
        }

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputDouble("Source In", &sourceIn, 0.0, 0.0, "%.3f")) {
            double maxIn = clip->sourceOut - 0.01;
            clip->sourceIn = std::clamp(sourceIn, 0.0, maxIn);
        }

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputDouble("Source Out", &sourceOut, 0.0, 0.0, "%.3f")) {
            double maxOut = asset ? asset->duration : 3600.0;
            clip->sourceOut = std::clamp(sourceOut, clip->sourceIn + 0.01, maxOut);
        }

        // Read-only derived values
        ImGui::Text("Duration:     %.3f s", duration);
        ImGui::Text("Timeline End: %.3f s", timelineEnd);
    }

    // --- Track ---
    if (track && ImGui::CollapsingHeader("Track", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Name: %s", track->name.c_str());
        ImGui::Text("Type: %s", trackTypeName(track->type));
    }

    // --- Media ---
    if (asset && ImGui::CollapsingHeader("Media", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Show just the filename
        const auto& path = asset->filePath;
        size_t lastSlash = path.find_last_of('/');
        const char* filename = (lastSlash != std::string::npos)
                                 ? path.c_str() + lastSlash + 1 : path.c_str();
        ImGui::Text("File: %s", filename);
        ImGui::Text("Type: %s", mediaTypeName(asset->type));
        ImGui::Text("Duration: %.3f s", asset->duration);

        if (asset->hasVideo) {
            ImGui::Text("Dimensions: %dx%d", asset->width, asset->height);
            ImGui::Text("FPS: %.2f", asset->fps);
        }
        if (asset->hasAudio) {
            ImGui::Text("Sample Rate: %d Hz", asset->sampleRate);
            ImGui::Text("Channels: %d", asset->channels);
        }
    }

    ImGui::End();
}
