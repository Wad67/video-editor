#include "media/AudioFrameQueue.h"

AudioFrameQueue::AudioFrameQueue() {
    for (auto& e : m_ring) {
        e.frame = av_frame_alloc();
        e.serial = -1;
    }
}

AudioFrameQueue::~AudioFrameQueue() {
    for (auto& e : m_ring) {
        av_frame_free(&e.frame);
    }
}

bool AudioFrameQueue::push(AVFrame* frame, int serial) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condWrite.wait(lock, [this] { return m_count < CAPACITY || m_abort.load(); });
    if (m_abort.load()) return false;

    av_frame_unref(m_ring[m_writeIdx].frame);
    av_frame_move_ref(m_ring[m_writeIdx].frame, frame);
    m_ring[m_writeIdx].serial = serial;
    m_writeIdx = (m_writeIdx + 1) % CAPACITY;
    m_count++;

    lock.unlock();
    m_condRead.notify_one();
    return true;
}

AVFrame* AudioFrameQueue::peek(int* outSerial) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == 0) return nullptr;
    if (outSerial) *outSerial = m_ring[m_readIdx].serial;
    return m_ring[m_readIdx].frame;
}

void AudioFrameQueue::pop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == 0) return;
    av_frame_unref(m_ring[m_readIdx].frame);
    m_ring[m_readIdx].serial = -1;
    m_readIdx = (m_readIdx + 1) % CAPACITY;
    m_count--;
    m_condWrite.notify_one();
}

void AudioFrameQueue::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < m_count; i++) {
        int idx = (m_readIdx + i) % CAPACITY;
        av_frame_unref(m_ring[idx].frame);
        m_ring[idx].serial = -1;
    }
    m_readIdx = 0;
    m_writeIdx = 0;
    m_count = 0;
    m_condWrite.notify_all();
}

void AudioFrameQueue::abort() {
    m_abort.store(true);
    m_condRead.notify_all();
    m_condWrite.notify_all();
}

void AudioFrameQueue::start() {
    m_abort.store(false);
}

size_t AudioFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

bool AudioFrameQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count == 0;
}
