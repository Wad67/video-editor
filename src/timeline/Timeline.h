#pragma once

#include "timeline/MediaAsset.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>

// A segment of a MediaAsset placed on the timeline.
struct Clip {
    uint32_t id = 0;
    uint32_t assetId = 0;       // references MediaAsset
    uint32_t trackId = 0;       // which track this clip is on

    double timelineStart = 0.0; // where the clip starts on the timeline (seconds)
    double sourceIn = 0.0;      // source start offset (seconds)
    double sourceOut = 0.0;     // source end offset (seconds)

    // Derived: duration on timeline = sourceOut - sourceIn
    double getDuration() const { return sourceOut - sourceIn; }

    // Map timeline time to source time
    double toSourceTime(double timelineTime) const {
        return (timelineTime - timelineStart) + sourceIn;
    }

    // Check if timeline time falls within this clip
    bool containsTime(double timelineTime) const {
        return timelineTime >= timelineStart &&
               timelineTime < timelineStart + getDuration();
    }

    double getTimelineEnd() const { return timelineStart + getDuration(); }
};

enum class TrackType {
    Video,
    Audio,
    Image
};

struct Track {
    uint32_t id = 0;
    std::string name;
    TrackType type = TrackType::Video;

    std::vector<uint32_t> clipIds;  // ordered by timelineStart

    bool muted = false;
    bool visible = true;
    float volume = 1.0f;           // 0.0 - 1.0, for audio tracks
};

// Owns all assets, tracks, clips. Provides timeline queries.
class Timeline {
public:
    Timeline();

    // Track management
    uint32_t addTrack(const std::string& name, TrackType type);
    Track* getTrack(uint32_t trackId);
    const Track* getTrack(uint32_t trackId) const;
    const std::vector<uint32_t>& getTrackOrder() const { return m_trackOrder; }
    void swapTracks(int indexA, int indexB);

    // Asset management
    uint32_t addAsset(MediaAsset asset);
    MediaAsset* getAsset(uint32_t assetId);
    const MediaAsset* getAsset(uint32_t assetId) const;

    // Clip management
    uint32_t addClip(uint32_t trackId, uint32_t assetId,
                     double timelineStart, double sourceIn, double sourceOut);
    Clip* getClip(uint32_t clipId);
    const Clip* getClip(uint32_t clipId) const;
    void removeClip(uint32_t clipId);
    void moveClip(uint32_t clipId, uint32_t newTrackId, double newTimelineStart);

    // Queries
    const Clip* getActiveClipOnTrack(uint32_t trackId, double time) const;
    std::vector<const Clip*> getActiveClips(double time) const;
    double getTotalDuration() const;

    // Import a media file, auto-creating appropriate clips
    // Returns the asset ID (0 on failure)
    uint32_t importFile(const std::string& path);

    // Find first track of a given type, or 0 if none
    uint32_t findTrackByType(TrackType type) const;

    // Get all tracks/clips/assets
    const std::unordered_map<uint32_t, Track>& getAllTracks() const { return m_tracks; }
    const std::unordered_map<uint32_t, Clip>& getAllClips() const { return m_clips; }
    const std::unordered_map<uint32_t, MediaAsset>& getAllAssets() const { return m_assets; }

private:
    uint32_t importImage(const std::string& path);
    void sortTrackClips(uint32_t trackId);

    uint32_t m_nextAssetId = 1;
    uint32_t m_nextTrackId = 1;
    uint32_t m_nextClipId = 1;

    std::unordered_map<uint32_t, MediaAsset> m_assets;
    std::unordered_map<uint32_t, Track> m_tracks;
    std::unordered_map<uint32_t, Clip> m_clips;
    std::vector<uint32_t> m_trackOrder;  // display order
};
