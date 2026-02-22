#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>

// Global pipeline counters — updated by each thread, printed periodically.
struct DebugStats {
    // Demux thread
    std::atomic<uint64_t> videoPacketsPushed{0};
    std::atomic<uint64_t> audioPacketsPushed{0};

    // Video decoder thread
    std::atomic<uint64_t> videoPacketsPopped{0};
    std::atomic<uint64_t> videoFramesDecoded{0};
    std::atomic<uint64_t> videoFramesPushed{0};
    std::atomic<uint64_t> decoderGetBufferCalls{0};
    std::atomic<uint64_t> decoderSwsScaleCalls{0};

    // Main thread
    std::atomic<uint64_t> mainPeekCalls{0};
    std::atomic<uint64_t> mainPeekNull{0};
    std::atomic<uint64_t> mainFramesDisplayed{0};
    std::atomic<uint64_t> mainFramesRepeated{0};
    std::atomic<uint64_t> mainFramesSkipped{0};
    std::atomic<double> overlayFps{0.0};

    // Queue depths (snapshot)
    std::atomic<int> videoPacketQueueDepth{0};
    std::atomic<int> videoFrameQueueDepth{0};

    double lastPrintTime = 0.0;

    void reset() {
        videoPacketsPushed = 0; audioPacketsPushed = 0;
        videoPacketsPopped = 0; videoFramesDecoded = 0; videoFramesPushed = 0;
        decoderGetBufferCalls = 0; decoderSwsScaleCalls = 0;
        mainPeekCalls = 0; mainPeekNull = 0;
        mainFramesDisplayed = 0; mainFramesRepeated = 0; mainFramesSkipped = 0;
        videoPacketQueueDepth = 0; videoFrameQueueDepth = 0;
        lastPrintTime = now();
    }

    // Call from main thread each frame. Prints to stderr every second.
    void tick() {
        double t = now();
        double elapsed = t - lastPrintTime;
        if (elapsed < 1.0) return;

        fprintf(stderr,
            "[PIPELINE %.1fs] "
            "Demux: v_pkt=%llu a_pkt=%llu | "
            "VDec: popped=%llu decoded=%llu pushed=%llu | "
            "Main: peek=%llu null=%llu displayed=%llu skipped=%llu repeat=%llu fps=%.1f | "
            "Queues: pkt=%d frm=%d\n",
            elapsed,
            (unsigned long long)videoPacketsPushed.load(),
            (unsigned long long)audioPacketsPushed.load(),
            (unsigned long long)videoPacketsPopped.load(),
            (unsigned long long)videoFramesDecoded.load(),
            (unsigned long long)videoFramesPushed.load(),
            (unsigned long long)mainPeekCalls.load(),
            (unsigned long long)mainPeekNull.load(),
            (unsigned long long)mainFramesDisplayed.load(),
            (unsigned long long)mainFramesSkipped.load(),
            (unsigned long long)mainFramesRepeated.load(),
            overlayFps.load(),
            videoPacketQueueDepth.load(),
            videoFrameQueueDepth.load()
        );
        fflush(stderr);

        // Reset for next interval
        videoPacketsPushed = 0; audioPacketsPushed = 0;
        videoPacketsPopped = 0; videoFramesDecoded = 0; videoFramesPushed = 0;
        decoderGetBufferCalls = 0; decoderSwsScaleCalls = 0;
        mainPeekCalls = 0; mainPeekNull = 0;
        mainFramesDisplayed = 0; mainFramesRepeated = 0;
        lastPrintTime = t;
    }

    static double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
};

// Single global instance
inline DebugStats g_stats;
