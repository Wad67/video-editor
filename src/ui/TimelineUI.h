#pragma once

#include <cstdint>

class Timeline;

class TimelineUI {
public:
    void render(Timeline& timeline, double currentTime, double totalDuration);

    // Get the playhead position (if user dragged it)
    bool hasSeekRequest() const { return m_seekRequested; }
    double getSeekTime() const { m_seekRequested = false; return m_seekTime; }

    // Selection
    uint32_t getSelectedClipId() const { return m_selectedClipId; }

    // Drag state
    bool isDraggingClip() const { return m_draggingClip; }
    bool isDraggingEdge() const { return m_draggingEdge != 0; }

    // Pass current playhead time for split-at-playhead
    void setCurrentTime(double t) { m_currentPlayheadTime = t; }

private:
    void renderTimeRuler(float x, float y, float width, float height,
                         double totalDuration);
    void renderTrackHeader(float x, float y, float width, float height,
                           Timeline& timeline, uint32_t trackId, int trackIndex, int trackCount);
    void renderTrackLane(float x, float y, float width, float height,
                         Timeline& timeline, uint32_t trackId, double totalDuration);
    void renderPlayhead(float x, float y, float height, double currentTime,
                        double totalDuration, float laneWidth);
    void renderScrollbar(float x, float y, float width, float height,
                         double totalDuration);

    // View state
    double m_viewStart = 0.0;         // leftmost visible time
    double m_viewDuration = 0.0;      // visible time span (0 = auto-fit)
    double m_lastKnownDuration = 0.0; // for detecting content changes
    bool m_viewInitialized = false;
    bool m_userHasZoomed = false;      // once user zooms/pans, stop auto-fitting

    // Interaction state
    mutable bool m_seekRequested = false;
    double m_seekTime = 0.0;
    uint32_t m_selectedClipId = 0;
    double m_currentPlayheadTime = 0.0;

    // Clip dragging
    bool m_draggingClip = false;
    uint32_t m_dragClipId = 0;
    double m_dragStartOffset = 0.0;  // offset from clip start to mouse at drag begin
    uint32_t m_dragOrigTrackId = 0;

    // Middle-click pan
    bool m_draggingPan = false;
    float m_panStartMouseX = 0.0f;
    double m_panStartViewStart = 0.0;

    // Ruler drag-seek
    bool m_draggingRuler = false;

    // Scrollbar drag
    bool m_draggingScrollbar = false;
    float m_scrollbarDragStartX = 0.0f;
    double m_scrollbarDragStartView = 0.0;

    // Edge trimming: -1 = left edge, 1 = right edge, 0 = none
    int m_draggingEdge = 0;
    uint32_t m_trimClipId = 0;
    double m_trimOrigSourceIn = 0.0;
    double m_trimOrigSourceOut = 0.0;
    double m_trimOrigTimelineStart = 0.0;

    // Snap state
    bool m_snapActive = false;
    double m_snapTime = 0.0;

    // Layout constants
    static constexpr float TRACK_HEADER_WIDTH = 120.0f;
    static constexpr float TRACK_HEIGHT = 40.0f;
    static constexpr float RULER_HEIGHT = 24.0f;
    static constexpr float MIN_TRACK_HEIGHT = 30.0f;
    static constexpr float SCROLLBAR_HEIGHT = 14.0f;
};
