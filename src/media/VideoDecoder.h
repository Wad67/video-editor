#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <thread>
#include <atomic>

class PacketQueue;
class FrameQueue;

class VideoDecoder {
public:
    ~VideoDecoder();

    bool init(AVCodecParameters* codecPar, AVRational timeBase, AVRational frameRate);
    void start(PacketQueue& packetQueue, FrameQueue& frameQueue);
    void stop();

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    AVRational getTimeBase() const { return m_timeBase; }
    double getFrameRate() const { return m_frameRate; }
    AVCodecContext* getCodecContext() const { return m_codecCtx; }

private:
    void decodeLoop(PacketQueue& packetQueue, FrameQueue& frameQueue);

    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVRational m_timeBase{};
    int m_width = 0;
    int m_height = 0;
    double m_frameRate = 30.0;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};
