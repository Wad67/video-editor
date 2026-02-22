#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

class PacketQueue {
public:
    PacketQueue() = default;
    ~PacketQueue();

    // Push a packet (takes ownership). Returns false if aborted.
    bool push(AVPacket* packet);

    // Pop a packet. Returns nullptr if aborted or empty after timeout.
    AVPacket* pop(int timeoutMs = 100);

    // Flush all packets and increment serial
    void flush();

    // Signal abort to unblock waiting threads
    void abort();

    // Reset abort state
    void start();

    int getSerial() const { return m_serial.load(); }
    size_t size() const;

private:
    struct Entry {
        AVPacket* packet;
        int serial;
    };

    std::queue<Entry> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<int> m_serial{0};
    std::atomic<bool> m_abort{false};
    static constexpr size_t MAX_SIZE = 256;
};
