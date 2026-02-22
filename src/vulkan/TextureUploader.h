#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include "vulkan/Swapchain.h"

struct VulkanContext;
class VideoTexture;

class TextureUploader {
public:
    bool init(VulkanContext& ctx, uint32_t maxWidth, uint32_t maxHeight);
    void shutdown(VulkanContext& ctx);

    // Stage RGBA data into the per-frame staging buffer (CPU side only).
    // frameIndex is the swapchain frame-in-flight index (0 or 1).
    void stage(VulkanContext& ctx, int frameIndex,
               const uint8_t* data, uint32_t width, uint32_t height);

    // Record the copy + barrier commands into an existing command buffer.
    void recordUpload(VkCommandBuffer cmd, int frameIndex,
                      VideoTexture& texture, int slot,
                      uint32_t width, uint32_t height);

    bool ensureCapacity(VulkanContext& ctx, uint32_t width, uint32_t height);

private:
    // One staging buffer per frame-in-flight to avoid GPU/CPU race
    VkBuffer m_stagingBuffers[Swapchain::MAX_FRAMES_IN_FLIGHT]{};
    VmaAllocation m_stagingAllocations[Swapchain::MAX_FRAMES_IN_FLIGHT]{};
    VkDeviceSize m_stagingSize = 0;
};
