#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "export/ExportSettings.h"
#include <functional>

class AVFormatContext;

class VideoEncoder {
public:
    ~VideoEncoder();

    // Init encoder. Pass muxer flags so we can set GLOBAL_HEADER if needed.
    bool init(const ExportSettings& settings, int muxerFlags = 0);
    void shutdown();

    using PacketCallback = std::function<void(AVPacket* pkt)>;
    bool encodeFrame(const uint8_t* rgbaData, int width, int height,
                     int64_t frameIndex, PacketCallback cb);
    bool flush(PacketCallback cb);

    AVCodecContext* getCodecContext() { return m_codecCtx; }

private:
    bool drainPackets(PacketCallback& cb);

    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    int m_width = 0;
    int m_height = 0;
};
