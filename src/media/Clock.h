#pragma once

#include <atomic>
#include <chrono>

class Clock {
public:
    void set(double pts);
    // Only update if pts >= current time - tolerance.
    // Prevents the audio thread from ever jumping the clock backward.
    void setIfForward(double pts, double tolerance = 0.1);
    double get() const;
    void pause();
    void resume();
    bool isPaused() const { return m_paused.load(); }

private:
    std::atomic<double> m_pts{0.0};
    std::atomic<double> m_lastUpdate{0.0};
    std::atomic<bool> m_paused{false};

    static double now();
};
