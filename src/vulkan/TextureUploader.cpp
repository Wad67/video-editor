#include "vulkan/TextureUploader.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/VideoTexture.h"
#include <cstring>
#include <cstdio>

bool TextureUploader::init(VulkanContext& ctx, uint32_t maxWidth, uint32_t maxHeight) {
    return ensureCapacity(ctx, maxWidth, maxHeight);
}

void TextureUploader::shutdown(VulkanContext& ctx) {
    for (int i = 0; i < Swapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_stagingBuffers[i]) {
            vmaDestroyBuffer(ctx.allocator, m_stagingBuffers[i], m_stagingAllocations[i]);
            m_stagingBuffers[i] = VK_NULL_HANDLE;
            m_stagingAllocations[i] = VK_NULL_HANDLE;
        }
    }
    m_stagingSize = 0;
}

bool TextureUploader::ensureCapacity(VulkanContext& ctx, uint32_t width, uint32_t height) {
    VkDeviceSize needed = static_cast<VkDeviceSize>(width) * height * 4;
    if (needed <= m_stagingSize) return true;

    // Free old buffers
    for (int i = 0; i < Swapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_stagingBuffers[i]) {
            vmaDestroyBuffer(ctx.allocator, m_stagingBuffers[i], m_stagingAllocations[i]);
            m_stagingBuffers[i] = VK_NULL_HANDLE;
            m_stagingAllocations[i] = VK_NULL_HANDLE;
        }
    }

    for (int i = 0; i < Swapchain::MAX_FRAMES_IN_FLIGHT; i++) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = needed;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(ctx.allocator, &bufInfo, &allocInfo,
                            &m_stagingBuffers[i], &m_stagingAllocations[i], nullptr) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create staging buffer %d\n", i);
            return false;
        }
    }

    m_stagingSize = needed;
    return true;
}

void TextureUploader::stage(VulkanContext& ctx, int frameIndex,
                             const uint8_t* data, uint32_t width, uint32_t height) {
    VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;

    void* mapped;
    vmaMapMemory(ctx.allocator, m_stagingAllocations[frameIndex], &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(ctx.allocator, m_stagingAllocations[frameIndex]);
    vmaFlushAllocation(ctx.allocator, m_stagingAllocations[frameIndex], 0, size);
}

void TextureUploader::recordUpload(VkCommandBuffer cmd, int frameIndex,
                                    VideoTexture& texture, int slot,
                                    uint32_t width, uint32_t height) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.getImage(slot);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, m_stagingBuffers[frameIndex], texture.getImage(slot),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}
