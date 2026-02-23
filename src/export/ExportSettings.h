#pragma once

#include <string>

enum class VideoCodecChoice {
    H264_Software,   // libx264
    H265_Software,   // libx265
    H264_VAAPI       // h264_vaapi (hardware)
};

struct ExportSettings {
    std::string outputPath = "output.mp4";

    // Video
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    int videoBitrate = 8000000;     // 8 Mbps
    VideoCodecChoice videoCodec = VideoCodecChoice::H264_Software;
    int crf = 23;

    // Audio
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitrate = 192000;      // 192 kbps AAC

    // Range
    double startTime = 0.0;
    double endTime = -1.0;          // -1 means entire timeline
};
