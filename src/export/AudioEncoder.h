#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include "export/ExportSettings.h"
#include <functional>
#include <vector>

class AudioEncoder {
public:
    ~AudioEncoder();

    bool init(const ExportSettings& settings, int muxerFlags = 0);
    void shutdown();

    using PacketCallback = std::function<void(AVPacket* pkt)>;

    // Feed interleaved float stereo samples. Buffers internally until
    // codec frame_size is reached, then encodes.
    bool encode(const float* samples, int numFrames, PacketCallback cb);

    bool flush(PacketCallback cb);

    AVCodecContext* getCodecContext() { return m_codecCtx; }
    int getFrameSize() const;

private:
    bool encodeBuffered(PacketCallback& cb);
    bool drainPackets(PacketCallback& cb);

    AVCodecContext* m_codecCtx = nullptr;
    SwrContext* m_swrCtx = nullptr;
    AVFrame* m_frame = nullptr;
    int m_channels = 2;
    int64_t m_nextPts = 0;
    std::vector<float> m_inputBuffer;
    int m_samplesBuffered = 0;
};
