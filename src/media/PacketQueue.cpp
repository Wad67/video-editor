#include "media/PacketQueue.h"

PacketQueue::~PacketQueue() {
    flush();
}

bool PacketQueue::push(AVPacket* packet) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait(lock, [this] { return m_queue.size() < MAX_SIZE || m_abort.load(); });

    if (m_abort.load()) {
        av_packet_free(&packet);
        return false;
    }

    m_queue.push({packet, m_serial.load()});
    lock.unlock();
    m_cond.notify_one();
    return true;
}

AVPacket* PacketQueue::pop(int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_cond.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                          [this] { return !m_queue.empty() || m_abort.load(); })) {
        return nullptr;
    }

    if (m_abort.load() || m_queue.empty()) return nullptr;

    auto entry = m_queue.front();
    m_queue.pop();
    lock.unlock();
    m_cond.notify_one();

    // Check if packet is stale (from before a flush)
    if (entry.serial != m_serial.load()) {
        av_packet_free(&entry.packet);
        return pop(timeoutMs); // try again
    }

    return entry.packet;
}

void PacketQueue::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_queue.empty()) {
        av_packet_free(&m_queue.front().packet);
        m_queue.pop();
    }
    m_serial++;
    m_cond.notify_all();
}

void PacketQueue::abort() {
    m_abort.store(true);
    m_cond.notify_all();
}

void PacketQueue::start() {
    m_abort.store(false);
}

size_t PacketQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}
