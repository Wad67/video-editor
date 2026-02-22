#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <libavutil/frame.h>
}

// Ring buffer of decoded video frames with pre-allocated RGBA storage.
// The decoder writes directly into the next slot's buffer via getWriteBuffer(),
// then commits with push(). The consumer peeks/pops from the read side.
class FrameQueue {
public:
    static constexpr int CAPACITY = 16;

    FrameQueue();
    ~FrameQueue();

    // Allocate RGBA storage for all slots. Call once after knowing frame dimensions.
    bool allocate(int width, int height);

    // --- Producer (decoder thread) ---

    // Get a pointer to the next writable slot's RGBA buffer.
    // Blocks until a slot is free. Returns nullptr if aborted.
    uint8_t* getWriteBuffer(int& linesize);

    // Commit the current write slot with its PTS.
    void push(int64_t pts, int serial);

    // --- Consumer (main thread) ---

    // Peek at the front slot. Returns nullptr if empty.
    // Sets outPts/outLinesize if non-null.
    const uint8_t* peek(int64_t* outPts = nullptr, int* outLinesize = nullptr);

    void pop();

    // --- Control ---
    void flush();
    void abort();
    void start();

    size_t size() const;
    bool empty() const;

private:
    struct Slot {
        uint8_t* data = nullptr;
        int linesize = 0;
        int64_t pts = 0;
        int serial = -1;
    };

    Slot m_ring[CAPACITY]{};
    int m_readIdx = 0;
    int m_writeIdx = 0;
    int m_count = 0;
    int m_width = 0;
    int m_height = 0;
    mutable std::mutex m_mutex;
    std::condition_variable m_condRead;
    std::condition_variable m_condWrite;
    std::atomic<bool> m_abort{false};
};
