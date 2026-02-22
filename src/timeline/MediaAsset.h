#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class MediaType {
    Video,
    Audio,
    Image
};

// Shared reference to a source file. Caches metadata.
// Multiple clips can reference the same asset.
struct MediaAsset {
    uint32_t id = 0;
    std::string filePath;
    MediaType type = MediaType::Video;

    // Video/audio metadata
    double duration = 0.0;      // seconds
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int sampleRate = 0;
    int channels = 0;
    bool hasVideo = false;
    bool hasAudio = false;

    // Image: pre-decoded RGBA pixels (decoded once at import)
    std::vector<uint8_t> imageData;
};
