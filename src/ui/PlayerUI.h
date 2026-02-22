#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <functional>
#include <cstdint>

struct LayerInfo;

class PlayerUI {
public:
    // Callbacks for transport controls
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onStop;
    std::function<void(double)> onSeek;

    // Stats for viewport overlay (set by Application each frame)
    double videoFps = 0.0;
    int layerCount = 0;
    size_t activeClips = 0;

    void render(const std::vector<LayerInfo>& layers,
                double currentTime, double duration, bool playing);

private:
    void renderViewport(const std::vector<LayerInfo>& layers);
    void renderTransportControls(double currentTime, double duration, bool playing);

    static const char* formatTime(double seconds);
};
