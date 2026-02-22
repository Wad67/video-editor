#pragma once

extern "C" {
#include <libavutil/frame.h>
}

#include <mutex>
#include <condition_variable>
#include <atomic>

// AVFrame-based ring buffer for audio decoded frames.
class AudioFrameQueue {
public:
    static constexpr int CAPACITY = 32;

    AudioFrameQueue();
    ~AudioFrameQueue();

    bool push(AVFrame* frame, int serial);
    AVFrame* peek(int* outSerial = nullptr);
    void pop();

    void flush();
    void abort();
    void start();

    size_t size() const;
    bool empty() const;

private:
    struct Entry {
        AVFrame* frame;
        int serial;
    };

    Entry m_ring[CAPACITY]{};
    int m_readIdx = 0;
    int m_writeIdx = 0;
    int m_count = 0;
    mutable std::mutex m_mutex;
    std::condition_variable m_condRead;
    std::condition_variable m_condWrite;
    std::atomic<bool> m_abort{false};
};
