#include "timeline/Timeline.h"
#include <algorithm>
#include <cstdio>
#include <cctype>

extern "C" {
#include <libavformat/avformat.h>
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"

Timeline::Timeline() = default;

uint32_t Timeline::addTrack(const std::string& name, TrackType type) {
    uint32_t id = m_nextTrackId++;
    Track track;
    track.id = id;
    track.name = name;
    track.type = type;
    m_tracks[id] = std::move(track);
    m_trackOrder.push_back(id);
    return id;
}

Track* Timeline::getTrack(uint32_t trackId) {
    auto it = m_tracks.find(trackId);
    return it != m_tracks.end() ? &it->second : nullptr;
}

const Track* Timeline::getTrack(uint32_t trackId) const {
    auto it = m_tracks.find(trackId);
    return it != m_tracks.end() ? &it->second : nullptr;
}

uint32_t Timeline::addAsset(MediaAsset asset) {
    uint32_t id = m_nextAssetId++;
    asset.id = id;
    m_assets[id] = std::move(asset);
    return id;
}

MediaAsset* Timeline::getAsset(uint32_t assetId) {
    auto it = m_assets.find(assetId);
    return it != m_assets.end() ? &it->second : nullptr;
}

const MediaAsset* Timeline::getAsset(uint32_t assetId) const {
    auto it = m_assets.find(assetId);
    return it != m_assets.end() ? &it->second : nullptr;
}

uint32_t Timeline::addClip(uint32_t trackId, uint32_t assetId,
                            double timelineStart, double sourceIn, double sourceOut) {
    auto* track = getTrack(trackId);
    if (!track) return 0;

    uint32_t id = m_nextClipId++;
    Clip clip;
    clip.id = id;
    clip.assetId = assetId;
    clip.trackId = trackId;
    clip.timelineStart = timelineStart;
    clip.sourceIn = sourceIn;
    clip.sourceOut = sourceOut;

    m_clips[id] = clip;
    track->clipIds.push_back(id);
    sortTrackClips(trackId);
    return id;
}

Clip* Timeline::getClip(uint32_t clipId) {
    auto it = m_clips.find(clipId);
    return it != m_clips.end() ? &it->second : nullptr;
}

const Clip* Timeline::getClip(uint32_t clipId) const {
    auto it = m_clips.find(clipId);
    return it != m_clips.end() ? &it->second : nullptr;
}

void Timeline::removeClip(uint32_t clipId) {
    auto it = m_clips.find(clipId);
    if (it == m_clips.end()) return;

    uint32_t trackId = it->second.trackId;
    m_clips.erase(it);

    auto* track = getTrack(trackId);
    if (track) {
        auto& ids = track->clipIds;
        ids.erase(std::remove(ids.begin(), ids.end(), clipId), ids.end());
    }
}

void Timeline::moveClip(uint32_t clipId, uint32_t newTrackId, double newTimelineStart) {
    auto* clip = getClip(clipId);
    if (!clip) return;

    // Remove from old track
    if (clip->trackId != newTrackId) {
        auto* oldTrack = getTrack(clip->trackId);
        if (oldTrack) {
            auto& ids = oldTrack->clipIds;
            ids.erase(std::remove(ids.begin(), ids.end(), clipId), ids.end());
        }

        // Add to new track
        auto* newTrack = getTrack(newTrackId);
        if (newTrack) {
            newTrack->clipIds.push_back(clipId);
            clip->trackId = newTrackId;
            sortTrackClips(newTrackId);
        }
    }

    clip->timelineStart = newTimelineStart;
    sortTrackClips(clip->trackId);
}

const Clip* Timeline::getActiveClipOnTrack(uint32_t trackId, double time) const {
    const auto* track = getTrack(trackId);
    if (!track) return nullptr;

    for (uint32_t clipId : track->clipIds) {
        const auto* clip = getClip(clipId);
        if (clip && clip->containsTime(time)) return clip;
    }
    return nullptr;
}

std::vector<const Clip*> Timeline::getActiveClips(double time) const {
    std::vector<const Clip*> result;
    for (uint32_t trackId : m_trackOrder) {
        const auto* clip = getActiveClipOnTrack(trackId, time);
        if (clip) result.push_back(clip);
    }
    return result;
}

double Timeline::getTotalDuration() const {
    double maxEnd = 0.0;
    for (auto& [id, clip] : m_clips) {
        double end = clip.getTimelineEnd();
        if (end > maxEnd) maxEnd = end;
    }
    return maxEnd;
}

static bool isImageExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = std::tolower(c);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".tga";
}

uint32_t Timeline::importFile(const std::string& path) {
    // Check if it's an image file
    if (isImageExtension(path)) {
        return importImage(path);
    }

    // Probe the file with FFmpeg to get metadata
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        fprintf(stderr, "Timeline: could not open %s\n", path.c_str());
        return 0;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return 0;
    }

    MediaAsset asset;
    asset.filePath = path;

    int videoIdx = -1, audioIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        auto ct = fmt->streams[i]->codecpar->codec_type;
        if (ct == AVMEDIA_TYPE_VIDEO && videoIdx < 0) videoIdx = i;
        else if (ct == AVMEDIA_TYPE_AUDIO && audioIdx < 0) audioIdx = i;
    }

    if (videoIdx >= 0) {
        auto* par = fmt->streams[videoIdx]->codecpar;
        auto fr = fmt->streams[videoIdx]->avg_frame_rate;
        asset.hasVideo = true;
        asset.width = par->width;
        asset.height = par->height;
        asset.fps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 30.0;
    }
    if (audioIdx >= 0) {
        auto* par = fmt->streams[audioIdx]->codecpar;
        asset.hasAudio = true;
        asset.sampleRate = par->sample_rate;
        asset.channels = par->ch_layout.nb_channels;
    }

    if (fmt->duration != AV_NOPTS_VALUE) {
        asset.duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
    }

    asset.type = asset.hasVideo ? MediaType::Video : MediaType::Audio;
    avformat_close_input(&fmt);

    uint32_t assetId = addAsset(std::move(asset));
    const auto* a = getAsset(assetId);

    // Find the current end of timeline for placing new clips
    double placeAt = getTotalDuration();

    // Auto-create clips: video on first video track, audio on first audio track
    if (a->hasVideo) {
        uint32_t vTrack = findTrackByType(TrackType::Video);
        if (vTrack) {
            addClip(vTrack, assetId, placeAt, 0.0, a->duration);
        }
    }
    if (a->hasAudio) {
        uint32_t aTrack = findTrackByType(TrackType::Audio);
        if (aTrack) {
            addClip(aTrack, assetId, placeAt, 0.0, a->duration);
        }
    }

    return assetId;
}

uint32_t Timeline::importImage(const std::string& path) {
    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4); // Force RGBA
    if (!pixels) {
        fprintf(stderr, "Timeline: failed to load image %s: %s\n",
                path.c_str(), stbi_failure_reason());
        return 0;
    }

    MediaAsset asset;
    asset.filePath = path;
    asset.type = MediaType::Image;
    asset.width = w;
    asset.height = h;
    asset.duration = 5.0;  // Default 5-second duration for images
    asset.hasVideo = false;
    asset.hasAudio = false;

    // Store pre-decoded RGBA pixels
    size_t dataSize = static_cast<size_t>(w) * h * 4;
    asset.imageData.assign(pixels, pixels + dataSize);
    stbi_image_free(pixels);

    uint32_t assetId = addAsset(std::move(asset));

    // Create or find an Image track
    uint32_t imgTrack = findTrackByType(TrackType::Image);
    if (!imgTrack) {
        imgTrack = addTrack("Image 1", TrackType::Image);
    }

    double placeAt = getTotalDuration();
    const auto* a = getAsset(assetId);
    addClip(imgTrack, assetId, placeAt, 0.0, a->duration);

    return assetId;
}

uint32_t Timeline::findTrackByType(TrackType type) const {
    for (uint32_t trackId : m_trackOrder) {
        const auto* track = getTrack(trackId);
        if (track && track->type == type) return trackId;
    }
    return 0;
}

void Timeline::sortTrackClips(uint32_t trackId) {
    auto* track = getTrack(trackId);
    if (!track) return;
    std::sort(track->clipIds.begin(), track->clipIds.end(),
              [this](uint32_t a, uint32_t b) {
                  const auto* ca = getClip(a);
                  const auto* cb = getClip(b);
                  if (!ca || !cb) return false;
                  return ca->timelineStart < cb->timelineStart;
              });
}

void Timeline::swapTracks(int indexA, int indexB) {
    if (indexA < 0 || indexB < 0 ||
        indexA >= (int)m_trackOrder.size() || indexB >= (int)m_trackOrder.size())
        return;
    std::swap(m_trackOrder[indexA], m_trackOrder[indexB]);
}
