#pragma once

#include <cstdint>

class Timeline;

class ClipPropertiesUI {
public:
    void render(Timeline& timeline, uint32_t selectedClipId, double fps);
};
