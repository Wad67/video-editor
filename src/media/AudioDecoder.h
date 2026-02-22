#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include <thread>
#include <atomic>

class PacketQueue;
class AudioFrameQueue;

class AudioDecoder {
public:
    ~AudioDecoder();

    bool init(AVCodecParameters* codecPar, AVRational timeBase, int outputSampleRate = 0);
    void start(PacketQueue& packetQueue, AudioFrameQueue& frameQueue);
    void stop();

    int getSampleRate() const { return m_sampleRate; }
    int getChannels() const { return m_channels; }
    AVRational getTimeBase() const { return m_timeBase; }
    AVCodecContext* getCodecContext() const { return m_codecCtx; }

private:
    void decodeLoop(PacketQueue& packetQueue, AudioFrameQueue& frameQueue);

    AVCodecContext* m_codecCtx = nullptr;
    SwrContext* m_swrCtx = nullptr;
    AVRational m_timeBase{};
    int m_sampleRate = 0;
    int m_channels = 0;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
};
