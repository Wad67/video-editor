#include "media/Clock.h"

void Clock::set(double pts) {
    m_pts.store(pts);
    m_lastUpdate.store(now());
}

double Clock::get() const {
    if (m_paused.load()) return m_pts.load();
    double elapsed = now() - m_lastUpdate.load();
    return m_pts.load() + elapsed;
}

void Clock::pause() {
    m_pts.store(get());
    m_lastUpdate.store(now());
    m_paused.store(true);
}

void Clock::resume() {
    m_lastUpdate.store(now());
    m_paused.store(false);
}

double Clock::now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
