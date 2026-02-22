#include "ui/TimelineUI.h"
#include "timeline/Timeline.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

// Color constants
static const ImU32 COL_VIDEO_CLIP    = IM_COL32(70, 130, 200, 255);
static const ImU32 COL_AUDIO_CLIP    = IM_COL32(70, 180, 100, 255);
static const ImU32 COL_IMAGE_CLIP    = IM_COL32(200, 180, 60, 255);
static const ImU32 COL_CLIP_SELECTED = IM_COL32(255, 255, 255, 80);
static const ImU32 COL_CLIP_BORDER   = IM_COL32(255, 255, 255, 100);
static const ImU32 COL_PLAYHEAD      = IM_COL32(220, 50, 50, 255);
static const ImU32 COL_RULER_BG      = IM_COL32(40, 40, 45, 255);
static const ImU32 COL_RULER_TICK    = IM_COL32(180, 180, 180, 255);
static const ImU32 COL_RULER_TEXT    = IM_COL32(180, 180, 180, 255);
static const ImU32 COL_TRACK_BG_EVEN = IM_COL32(35, 35, 40, 255);
static const ImU32 COL_TRACK_BG_ODD  = IM_COL32(40, 40, 48, 255);
static const ImU32 COL_HEADER_BG     = IM_COL32(50, 50, 58, 255);
static const ImU32 COL_SCROLLBAR_BG  = IM_COL32(30, 30, 35, 255);
static const ImU32 COL_SCROLLBAR_THUMB = IM_COL32(80, 80, 90, 255);
static const ImU32 COL_SCROLLBAR_THUMB_HOVER = IM_COL32(100, 100, 115, 255);

static const char* formatTimeShort(double seconds) {
    static char buf[32];
    if (seconds < 0) seconds = 0;
    int m = static_cast<int>(seconds) / 60;
    int s = static_cast<int>(seconds) % 60;
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

static ImU32 getClipColor(TrackType type) {
    switch (type) {
        case TrackType::Video: return COL_VIDEO_CLIP;
        case TrackType::Audio: return COL_AUDIO_CLIP;
        case TrackType::Image: return COL_IMAGE_CLIP;
    }
    return COL_VIDEO_CLIP;
}

static const char* getTrackTypePrefix(TrackType type) {
    switch (type) {
        case TrackType::Video: return "V";
        case TrackType::Audio: return "A";
        case TrackType::Image: return "I";
    }
    return "?";
}

void TimelineUI::render(Timeline& timeline, double currentTime, double totalDuration) {
    ImGui::Begin("Timeline", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 contentMin = ImGui::GetCursorScreenPos();
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    if (contentSize.x < 100 || contentSize.y < 50) {
        ImGui::End();
        return;
    }

    // Auto-fit view to content (only if user hasn't manually zoomed/panned)
    double effectiveDuration = std::max(totalDuration, 10.0);
    if (!m_viewInitialized || m_viewDuration <= 0.0) {
        m_viewStart = 0.0;
        m_viewDuration = effectiveDuration * 1.1;
        m_viewInitialized = true;
    }
    // If new content was added (duration grew) and user hasn't zoomed, expand to fit
    if (!m_userHasZoomed && effectiveDuration > m_lastKnownDuration * 1.01) {
        m_viewDuration = effectiveDuration * 1.1;
    }
    m_lastKnownDuration = effectiveDuration;
    if (m_viewDuration < 1.0) m_viewDuration = 1.0;

    // Reset snap state each frame
    m_snapActive = false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    float laneX = contentMin.x + TRACK_HEADER_WIDTH;
    float laneWidth = contentSize.x - TRACK_HEADER_WIDTH;
    if (laneWidth < 50) laneWidth = 50;

    auto& trackOrder = timeline.getTrackOrder();
    float totalTrackHeight = trackOrder.size() * TRACK_HEIGHT;
    float totalHeight = RULER_HEIGHT + totalTrackHeight;

    // ---- Place an InvisibleButton covering the entire lane area ----
    ImGui::SetCursorScreenPos(ImVec2(laneX, contentMin.y));
    ImGui::InvisibleButton("##timeline_lanes", ImVec2(laneWidth, totalHeight + 2.0f + SCROLLBAR_HEIGHT));
    bool lanesHovered = ImGui::IsItemHovered();
    bool lanesActive = ImGui::IsItemActive();

    ImVec2 mousePos = ImGui::GetMousePos();
    float rulerY = contentMin.y;

    // ---- Draw: Header corner ----
    drawList->AddRectFilled(
        ImVec2(contentMin.x, rulerY),
        ImVec2(laneX, rulerY + RULER_HEIGHT),
        COL_HEADER_BG);

    // ---- Draw: Time ruler ----
    renderTimeRuler(laneX, rulerY, laneWidth, RULER_HEIGHT, effectiveDuration);

    // ---- Draw: Track headers + lanes ----
    float trackY = rulerY + RULER_HEIGHT;
    for (size_t i = 0; i < trackOrder.size(); i++) {
        uint32_t trackId = trackOrder[i];

        renderTrackHeader(contentMin.x, trackY, TRACK_HEADER_WIDTH, TRACK_HEIGHT,
                          timeline, trackId, (int)i, (int)trackOrder.size());

        ImU32 bgColor = (i % 2 == 0) ? COL_TRACK_BG_EVEN : COL_TRACK_BG_ODD;
        drawList->AddRectFilled(
            ImVec2(laneX, trackY),
            ImVec2(laneX + laneWidth, trackY + TRACK_HEIGHT),
            bgColor);

        renderTrackLane(laneX, trackY, laneWidth, TRACK_HEIGHT,
                        timeline, trackId, effectiveDuration);

        trackY += TRACK_HEIGHT;
    }

    // ---- Draw: Playhead ----
    renderPlayhead(laneX, rulerY, totalHeight, currentTime, effectiveDuration, laneWidth);

    // ---- Draw: Snap indicator ----
    if (m_snapActive && m_viewDuration > 0) {
        double snapFrac = (m_snapTime - m_viewStart) / m_viewDuration;
        if (snapFrac >= 0 && snapFrac <= 1) {
            float snapPx = laneX + static_cast<float>(snapFrac * laneWidth);
            ImU32 snapColor = IM_COL32(255, 220, 50, 200);
            ImGui::GetForegroundDrawList()->AddLine(
                ImVec2(snapPx, rulerY), ImVec2(snapPx, rulerY + totalHeight),
                snapColor, 1.5f);
        }
    }

    // ---- Draw: Scrollbar ----
    float scrollbarY = contentMin.y + totalHeight + 2;
    renderScrollbar(laneX, scrollbarY, laneWidth, SCROLLBAR_HEIGHT, effectiveDuration);

    // ---- Input: all interaction goes through the InvisibleButton ----
    if (lanesHovered) {
        // Mouse wheel: zoom (scroll) / pan (shift+scroll)
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            m_userHasZoomed = true;
            if (ImGui::GetIO().KeyShift) {
                // Shift + scroll = pan
                double panAmount = m_viewDuration * 0.1 * (-wheel);
                m_viewStart += panAmount;
                m_viewStart = std::max(0.0, m_viewStart);
            } else {
                // Scroll = zoom centered on mouse
                double mouseFrac = (mousePos.x - laneX) / laneWidth;
                double mouseTime = m_viewStart + mouseFrac * m_viewDuration;
                double zoomFactor = (wheel > 0) ? 0.85 : 1.15;
                m_viewDuration *= zoomFactor;
                m_viewDuration = std::clamp(m_viewDuration, 0.5, effectiveDuration * 3.0);
                m_viewStart = mouseTime - mouseFrac * m_viewDuration;
                m_viewStart = std::max(0.0, m_viewStart);
            }
        }

        // Middle-click = start pan drag
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_draggingPan = true;
            m_panStartMouseX = mousePos.x;
            m_panStartViewStart = m_viewStart;
            m_userHasZoomed = true;
        }

        // Left click — determine what was hit
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            bool hitRuler = mousePos.y >= rulerY && mousePos.y < rulerY + RULER_HEIGHT;
            bool hitClip = false;

            if (hitRuler) {
                m_draggingRuler = true;
                double frac = (mousePos.x - laneX) / laneWidth;
                m_seekTime = m_viewStart + frac * m_viewDuration;
                m_seekTime = std::clamp(m_seekTime, 0.0, effectiveDuration);
                m_seekRequested = true;
            } else {
                // Check for clip hit
                float clickTrackY = rulerY + RULER_HEIGHT;
                for (size_t i = 0; i < trackOrder.size(); i++) {
                    uint32_t trackId = trackOrder[i];
                    if (mousePos.y < clickTrackY || mousePos.y >= clickTrackY + TRACK_HEIGHT) {
                        clickTrackY += TRACK_HEIGHT;
                        continue;
                    }

                    const auto* track = timeline.getTrack(trackId);
                    if (!track) { clickTrackY += TRACK_HEIGHT; continue; }

                    for (uint32_t clipId : track->clipIds) {
                        const auto* clip = timeline.getClip(clipId);
                        if (!clip) continue;

                        float startFrac = static_cast<float>((clip->timelineStart - m_viewStart) / m_viewDuration);
                        float endFrac = static_cast<float>((clip->getTimelineEnd() - m_viewStart) / m_viewDuration);
                        float clipX1 = laneX + startFrac * laneWidth;
                        float clipX2 = laneX + endFrac * laneWidth;

                        if (mousePos.x >= clipX1 && mousePos.x <= clipX2) {
                            hitClip = true;
                            m_selectedClipId = clipId;

                            float edgeZone = 8.0f;
                            if (mousePos.x - clipX1 < edgeZone) {
                                m_draggingEdge = -1;
                                m_trimClipId = clipId;
                                m_trimOrigSourceIn = clip->sourceIn;
                                m_trimOrigTimelineStart = clip->timelineStart;
                            } else if (clipX2 - mousePos.x < edgeZone) {
                                m_draggingEdge = 1;
                                m_trimClipId = clipId;
                                m_trimOrigSourceOut = clip->sourceOut;
                            } else {
                                m_draggingClip = true;
                                m_dragClipId = clipId;
                                m_dragOrigTrackId = clip->trackId;
                                double mouseTime = m_viewStart + ((mousePos.x - laneX) / laneWidth) * m_viewDuration;
                                m_dragStartOffset = mouseTime - clip->timelineStart;
                            }
                            break;
                        }
                    }
                    break; // Only check the track the mouse is in
                }

                if (!hitClip) {
                    m_selectedClipId = 0;
                }
            }
        }

        // Right click on selected clip — context menu
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && m_selectedClipId != 0) {
            ImGui::OpenPopup("ClipContextMenu");
        }
    }

    // ---- Input: drag handling (active, mouse may leave hover area) ----
    if (lanesActive || m_draggingClip || m_draggingEdge != 0 || m_draggingRuler || m_draggingPan) {
        // Middle-click pan drag
        if (m_draggingPan && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            float dx = mousePos.x - m_panStartMouseX;
            double timeDelta = -(dx / laneWidth) * m_viewDuration;
            m_viewStart = m_panStartViewStart + timeDelta;
            m_viewStart = std::max(0.0, m_viewStart);
        }

        // Ruler drag-seek
        if (m_draggingRuler && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double frac = (mousePos.x - laneX) / laneWidth;
            m_seekTime = m_viewStart + frac * m_viewDuration;
            m_seekTime = std::clamp(m_seekTime, 0.0, effectiveDuration);
            m_seekRequested = true;
        }

        // Clip drag
        if (m_draggingClip && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double mouseTime = m_viewStart + ((mousePos.x - laneX) / laneWidth) * m_viewDuration;
            double newStart = mouseTime - m_dragStartOffset;
            newStart = std::max(0.0, newStart);
            auto* clip = timeline.getClip(m_dragClipId);
            if (clip) {
                double clipDur = clip->getDuration();

                // Determine which track the mouse is over (for cross-track drag)
                float clickTrackY = rulerY + RULER_HEIGHT;
                uint32_t targetTrackId = clip->trackId;
                for (size_t i = 0; i < trackOrder.size(); i++) {
                    if (mousePos.y >= clickTrackY && mousePos.y < clickTrackY + TRACK_HEIGHT) {
                        const auto* candidateTrack = timeline.getTrack(trackOrder[i]);
                        const auto* currentTrack = timeline.getTrack(clip->trackId);
                        if (candidateTrack && currentTrack &&
                            candidateTrack->type == currentTrack->type) {
                            targetTrackId = trackOrder[i];
                        }
                        break;
                    }
                    clickTrackY += TRACK_HEIGHT;
                }

                // Snap threshold: 5 pixels worth of time at current zoom
                double snapThreshold = (5.0 / laneWidth) * m_viewDuration;
                double bestSnapDist = snapThreshold;
                double bestSnapStart = newStart;
                double bestSnapIndicator = -1.0;

                // Snap clip start to targets
                auto trySnapStart = [&](double targetTime) {
                    double dist = std::abs(newStart - targetTime);
                    if (dist < bestSnapDist) {
                        bestSnapDist = dist;
                        bestSnapStart = targetTime;
                        bestSnapIndicator = targetTime;
                    }
                };

                // Snap clip end to targets
                auto trySnapEnd = [&](double targetTime) {
                    double proposedStart = targetTime - clipDur;
                    double dist = std::abs((newStart + clipDur) - targetTime);
                    if (dist < bestSnapDist) {
                        bestSnapDist = dist;
                        bestSnapStart = proposedStart;
                        bestSnapIndicator = targetTime;
                    }
                };

                // Snap to playhead
                trySnapStart(currentTime);
                trySnapEnd(currentTime);

                // Snap to all clip edges on all tracks
                for (uint32_t tId : trackOrder) {
                    const auto* t = timeline.getTrack(tId);
                    if (!t) continue;
                    for (uint32_t cId : t->clipIds) {
                        if (cId == m_dragClipId) continue;
                        const auto* other = timeline.getClip(cId);
                        if (!other) continue;
                        trySnapStart(other->timelineStart);
                        trySnapStart(other->getTimelineEnd());
                        trySnapEnd(other->timelineStart);
                        trySnapEnd(other->getTimelineEnd());
                    }
                }

                if (bestSnapDist < snapThreshold) {
                    newStart = std::max(0.0, bestSnapStart);
                    m_snapActive = true;
                    m_snapTime = bestSnapIndicator;
                }

                // Overlap prevention: push to nearest gap on target track
                const auto* targetTrack = timeline.getTrack(targetTrackId);
                if (targetTrack) {
                    for (uint32_t otherId : targetTrack->clipIds) {
                        if (otherId == m_dragClipId) continue;
                        const auto* other = timeline.getClip(otherId);
                        if (!other) continue;

                        double otherStart = other->timelineStart;
                        double otherEnd = other->getTimelineEnd();
                        double newEnd = newStart + clipDur;

                        if (newStart < otherEnd && newEnd > otherStart) {
                            double snapLeft = otherStart - clipDur;
                            double snapRight = otherEnd;
                            if (std::abs(newStart - snapLeft) < std::abs(newStart - snapRight)) {
                                newStart = std::max(0.0, snapLeft);
                            } else {
                                newStart = snapRight;
                            }
                        }
                    }
                }

                if (targetTrackId != clip->trackId) {
                    timeline.moveClip(m_dragClipId, targetTrackId, newStart);
                } else {
                    clip->timelineStart = newStart;
                }
            }
        }

        // Edge trim (with snap to playhead)
        if (m_draggingEdge != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            double mouseTime = m_viewStart + ((mousePos.x - laneX) / laneWidth) * m_viewDuration;

            // Snap trim point to playhead
            double snapThreshold = (5.0 / laneWidth) * m_viewDuration;
            if (std::abs(mouseTime - currentTime) < snapThreshold) {
                mouseTime = currentTime;
                m_snapActive = true;
                m_snapTime = currentTime;
            }

            auto* clip = timeline.getClip(m_trimClipId);
            const auto* asset = clip ? timeline.getAsset(clip->assetId) : nullptr;
            if (clip && asset) {
                if (m_draggingEdge == -1) {
                    double delta = mouseTime - m_trimOrigTimelineStart;
                    double newSourceIn = m_trimOrigSourceIn + delta;
                    newSourceIn = std::clamp(newSourceIn, 0.0, clip->sourceOut - 0.1);
                    clip->sourceIn = newSourceIn;
                    clip->timelineStart = m_trimOrigTimelineStart + (newSourceIn - m_trimOrigSourceIn);
                } else {
                    double clipEndTime = mouseTime;
                    double newSourceOut = clip->sourceIn + (clipEndTime - clip->timelineStart);
                    double maxDuration = (asset->type == MediaType::Image)
                        ? 3600.0  // Images: allow up to 1 hour
                        : asset->duration;
                    newSourceOut = std::clamp(newSourceOut, clip->sourceIn + 0.1, maxDuration);
                    clip->sourceOut = newSourceOut;
                }
            }
        }

        // Release all drags on mouse up
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_draggingClip = false;
            m_dragClipId = 0;
            m_draggingEdge = 0;
            m_trimClipId = 0;
            m_draggingRuler = false;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            m_draggingPan = false;
        }
    }

    // ---- Context menu ----
    if (ImGui::BeginPopup("ClipContextMenu")) {
        if (ImGui::MenuItem("Delete Clip")) {
            if (m_selectedClipId != 0) {
                timeline.removeClip(m_selectedClipId);
                m_selectedClipId = 0;
            }
        }
        if (ImGui::MenuItem("Split at Playhead", "S")) {
            if (m_selectedClipId != 0) {
                auto* clip = timeline.getClip(m_selectedClipId);
                if (clip && clip->containsTime(m_currentPlayheadTime)) {
                    double splitSource = clip->toSourceTime(m_currentPlayheadTime);
                    // Create new clip for the right half
                    double rightStart = m_currentPlayheadTime;
                    double rightSourceIn = splitSource;
                    double rightSourceOut = clip->sourceOut;
                    uint32_t rightId = timeline.addClip(
                        clip->trackId, clip->assetId,
                        rightStart, rightSourceIn, rightSourceOut);
                    // Trim left clip
                    clip->sourceOut = splitSource;
                    (void)rightId;
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Fit View to Content")) {
            m_viewStart = 0.0;
            m_viewDuration = effectiveDuration * 1.1;
            m_userHasZoomed = false;
        }
        ImGui::EndPopup();
    }

    // ---- Keyboard shortcut: S = split selected clip at playhead ----
    if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (m_selectedClipId != 0) {
            auto* clip = timeline.getClip(m_selectedClipId);
            if (clip && clip->containsTime(m_currentPlayheadTime)) {
                double splitSource = clip->toSourceTime(m_currentPlayheadTime);
                double rightStart = m_currentPlayheadTime;
                double rightSourceIn = splitSource;
                double rightSourceOut = clip->sourceOut;
                timeline.addClip(clip->trackId, clip->assetId,
                                  rightStart, rightSourceIn, rightSourceOut);
                clip->sourceOut = splitSource;
            }
        }
    }

    ImGui::End();
}

void TimelineUI::renderTimeRuler(float x, float y, float width, float height,
                                  double totalDuration) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), COL_RULER_BG);

    double pixelsPerSecond = width / m_viewDuration;
    double tickInterval = 1.0;
    if (pixelsPerSecond < 5) tickInterval = 30.0;
    else if (pixelsPerSecond < 10) tickInterval = 15.0;
    else if (pixelsPerSecond < 20) tickInterval = 10.0;
    else if (pixelsPerSecond < 50) tickInterval = 5.0;
    else if (pixelsPerSecond < 100) tickInterval = 2.0;

    double startTick = std::floor(m_viewStart / tickInterval) * tickInterval;
    for (double t = startTick; t <= m_viewStart + m_viewDuration; t += tickInterval) {
        if (t < 0) continue;
        double frac = (t - m_viewStart) / m_viewDuration;
        float px = x + static_cast<float>(frac * width);
        if (px < x || px > x + width) continue;

        drawList->AddLine(ImVec2(px, y + height * 0.5f), ImVec2(px, y + height), COL_RULER_TICK);
        const char* label = formatTimeShort(t);
        drawList->AddText(ImVec2(px + 2, y + 2), COL_RULER_TEXT, label);
    }

    // Minor ticks
    double minorInterval = tickInterval / 2.0;
    if (minorInterval >= 0.5 && pixelsPerSecond >= 10) {
        double minorStart = std::floor(m_viewStart / minorInterval) * minorInterval;
        for (double t = minorStart; t <= m_viewStart + m_viewDuration; t += minorInterval) {
            if (t < 0) continue;
            double remainder = std::fmod(t, tickInterval);
            if (remainder < 0.01 || remainder > tickInterval - 0.01) continue;

            double frac = (t - m_viewStart) / m_viewDuration;
            float px = x + static_cast<float>(frac * width);
            if (px < x || px > x + width) continue;

            drawList->AddLine(ImVec2(px, y + height * 0.7f), ImVec2(px, y + height),
                              IM_COL32(120, 120, 120, 255));
        }
    }
}

void TimelineUI::renderTrackHeader(float x, float y, float width, float height,
                                    Timeline& timeline, uint32_t trackId,
                                    int trackIndex, int trackCount) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    auto* track = timeline.getTrack(trackId);
    if (!track) return;

    drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), COL_HEADER_BG);
    drawList->AddLine(ImVec2(x + width, y), ImVec2(x + width, y + height),
                      IM_COL32(70, 70, 80, 255));

    char label[64];
    snprintf(label, sizeof(label), "%s:%s", getTrackTypePrefix(track->type), track->name.c_str());

    ImVec2 textSize = ImGui::CalcTextSize(label);
    float textY = y + (height - textSize.y) * 0.5f;
    drawList->AddText(ImVec2(x + 6, textY), IM_COL32(220, 220, 220, 255), label);

    // Buttons area — right side of header
    float btnX = x + width - 66;
    ImGui::SetCursorScreenPos(ImVec2(btnX, y + 4));
    ImGui::PushID(trackId);

    // Track reorder buttons
    if (trackIndex > 0) {
        if (ImGui::SmallButton("^")) {
            timeline.swapTracks(trackIndex, trackIndex - 1);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        ImGui::SmallButton("^");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (trackIndex < trackCount - 1) {
        if (ImGui::SmallButton("v")) {
            timeline.swapTracks(trackIndex, trackIndex + 1);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        ImGui::SmallButton("v");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();

    if (track->type == TrackType::Audio) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            track->muted ? ImVec4(0.7f, 0.2f, 0.2f, 1.0f) : ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        if (ImGui::SmallButton("M")) {
            track->muted = !track->muted;
        }
        ImGui::PopStyleColor();
    }

    if (track->type == TrackType::Video || track->type == TrackType::Image) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            track->visible ? ImVec4(0.3f, 0.3f, 0.35f, 1.0f) : ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::SmallButton("V")) {
            track->visible = !track->visible;
        }
        ImGui::PopStyleColor();
    }

    ImGui::PopID();
}

void TimelineUI::renderTrackLane(float x, float y, float width, float height,
                                  Timeline& timeline, uint32_t trackId, double totalDuration) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    auto* track = timeline.getTrack(trackId);
    if (!track) return;

    for (uint32_t clipId : track->clipIds) {
        const auto* clip = timeline.getClip(clipId);
        if (!clip) continue;

        double clipStart = clip->timelineStart;
        double clipEnd = clip->getTimelineEnd();

        if (clipEnd < m_viewStart || clipStart > m_viewStart + m_viewDuration) continue;

        float startFrac = static_cast<float>((clipStart - m_viewStart) / m_viewDuration);
        float endFrac = static_cast<float>((clipEnd - m_viewStart) / m_viewDuration);

        float clipX1 = x + startFrac * width;
        float clipX2 = x + endFrac * width;
        clipX1 = std::max(clipX1, x);
        clipX2 = std::min(clipX2, x + width);

        if (clipX2 - clipX1 < 2) continue;

        float clipY1 = y + 2;
        float clipY2 = y + height - 2;

        ImU32 color = getClipColor(track->type);
        drawList->AddRectFilled(ImVec2(clipX1, clipY1), ImVec2(clipX2, clipY2), color, 3.0f);

        if (clipId == m_selectedClipId) {
            drawList->AddRectFilled(ImVec2(clipX1, clipY1), ImVec2(clipX2, clipY2),
                                     COL_CLIP_SELECTED, 3.0f);
        }

        drawList->AddRect(ImVec2(clipX1, clipY1), ImVec2(clipX2, clipY2),
                          COL_CLIP_BORDER, 3.0f);

        const auto* asset = timeline.getAsset(clip->assetId);
        if (asset && (clipX2 - clipX1) > 30) {
            const auto& path = asset->filePath;
            size_t lastSlash = path.find_last_of('/');
            const char* filename = (lastSlash != std::string::npos)
                                     ? path.c_str() + lastSlash + 1 : path.c_str();

            ImVec2 textPos(clipX1 + 4, clipY1 + 2);
            drawList->PushClipRect(ImVec2(clipX1, clipY1), ImVec2(clipX2, clipY2), true);
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 220), filename);
            drawList->PopClipRect();
        }
    }
}

void TimelineUI::renderPlayhead(float x, float y, float height,
                                 double currentTime, double totalDuration, float laneWidth) {
    if (m_viewDuration <= 0) return;

    double frac = (currentTime - m_viewStart) / m_viewDuration;
    if (frac < 0 || frac > 1) return;

    float px = x + static_cast<float>(frac * laneWidth);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    drawList->AddLine(ImVec2(px, y), ImVec2(px, y + height), COL_PLAYHEAD, 2.0f);

    float triSize = 6.0f;
    drawList->AddTriangleFilled(
        ImVec2(px - triSize, y),
        ImVec2(px + triSize, y),
        ImVec2(px, y + triSize * 1.5f),
        COL_PLAYHEAD);
}

void TimelineUI::renderScrollbar(float x, float y, float width, float height,
                                   double totalDuration) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    double maxTime = std::max(totalDuration * 1.2, m_viewStart + m_viewDuration);
    if (maxTime <= 0) maxTime = 10.0;

    // Background
    drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height),
                             COL_SCROLLBAR_BG, 3.0f);

    // Thumb
    float thumbStartFrac = static_cast<float>(m_viewStart / maxTime);
    float thumbEndFrac = static_cast<float>((m_viewStart + m_viewDuration) / maxTime);
    thumbStartFrac = std::clamp(thumbStartFrac, 0.0f, 1.0f);
    thumbEndFrac = std::clamp(thumbEndFrac, 0.0f, 1.0f);

    float thumbX1 = x + thumbStartFrac * width;
    float thumbX2 = x + thumbEndFrac * width;
    float minThumb = 20.0f;
    if (thumbX2 - thumbX1 < minThumb) thumbX2 = thumbX1 + minThumb;

    ImVec2 mousePos = ImGui::GetMousePos();
    bool thumbHovered = mousePos.x >= thumbX1 && mousePos.x <= thumbX2 &&
                        mousePos.y >= y && mousePos.y <= y + height;

    ImU32 thumbColor = thumbHovered || m_draggingScrollbar
        ? COL_SCROLLBAR_THUMB_HOVER : COL_SCROLLBAR_THUMB;
    drawList->AddRectFilled(ImVec2(thumbX1, y + 2), ImVec2(thumbX2, y + height - 2),
                             thumbColor, 3.0f);

    // Scrollbar interaction
    bool scrollbarHovered = mousePos.x >= x && mousePos.x <= x + width &&
                            mousePos.y >= y && mousePos.y <= y + height;

    if (scrollbarHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (thumbHovered) {
            m_draggingScrollbar = true;
            m_scrollbarDragStartX = mousePos.x;
            m_scrollbarDragStartView = m_viewStart;
            m_userHasZoomed = true;
        } else {
            // Click outside thumb: jump to that position
            float clickFrac = (mousePos.x - x) / width;
            m_viewStart = clickFrac * maxTime - m_viewDuration * 0.5;
            m_viewStart = std::max(0.0, m_viewStart);
            m_userHasZoomed = true;
        }
    }

    if (m_draggingScrollbar) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float dx = mousePos.x - m_scrollbarDragStartX;
            double timeDelta = (dx / width) * maxTime;
            m_viewStart = m_scrollbarDragStartView + timeDelta;
            m_viewStart = std::max(0.0, m_viewStart);
        } else {
            m_draggingScrollbar = false;
        }
    }
}
