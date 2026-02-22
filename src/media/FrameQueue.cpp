#include "media/FrameQueue.h"
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
}

FrameQueue::FrameQueue() = default;

FrameQueue::~FrameQueue() {
    for (auto& s : m_ring) {
        av_free(s.data);
    }
}

bool FrameQueue::allocate(int width, int height) {
    m_width = width;
    m_height = height;
    int linesize = width * 4; // RGBA, packed
    for (auto& s : m_ring) {
        av_free(s.data);
        s.data = (uint8_t*)av_malloc(linesize * height);
        if (!s.data) return false;
        s.linesize = linesize;
        s.pts = 0;
        s.serial = -1;
    }
    return true;
}

uint8_t* FrameQueue::getWriteBuffer(int& linesize) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condWrite.wait(lock, [this] { return m_count < CAPACITY || m_abort.load(); });
    if (m_abort.load()) { linesize = 0; return nullptr; }
    linesize = m_ring[m_writeIdx].linesize;
    return m_ring[m_writeIdx].data;
}

void FrameQueue::push(int64_t pts, int serial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ring[m_writeIdx].pts = pts;
    m_ring[m_writeIdx].serial = serial;
    m_writeIdx = (m_writeIdx + 1) % CAPACITY;
    m_count++;
    m_condRead.notify_one();
}

const uint8_t* FrameQueue::peek(int64_t* outPts, int* outLinesize) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == 0) return nullptr;
    if (outPts) *outPts = m_ring[m_readIdx].pts;
    if (outLinesize) *outLinesize = m_ring[m_readIdx].linesize;
    return m_ring[m_readIdx].data;
}

void FrameQueue::pop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == 0) return;
    m_readIdx = (m_readIdx + 1) % CAPACITY;
    m_count--;
    m_condWrite.notify_one();
}

void FrameQueue::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_readIdx = 0;
    m_writeIdx = 0;
    m_count = 0;
    m_condWrite.notify_all();
}

void FrameQueue::abort() {
    m_abort.store(true);
    m_condRead.notify_all();
    m_condWrite.notify_all();
}

void FrameQueue::start() {
    m_abort.store(false);
}

size_t FrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

bool FrameQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count == 0;
}
