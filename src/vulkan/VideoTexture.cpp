#include "vulkan/VideoTexture.h"
#include "vulkan/VulkanContext.h"
#include <cstdio>

bool VideoTexture::init(VulkanContext& ctx, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create sampler\n");
        return false;
    }

    return createResources(ctx);
}

void VideoTexture::shutdown(VulkanContext& ctx) {
    destroyResources(ctx);
    if (m_sampler) { vkDestroySampler(ctx.device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
}

bool VideoTexture::resize(VulkanContext& ctx, uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) return true;
    vkDeviceWaitIdle(ctx.device);
    destroyResources(ctx);
    m_width = width;
    m_height = height;
    m_uploadSlot = 0;
    m_displaySlot = 0;
    return createResources(ctx);
}

int VideoTexture::acquireUploadSlot() {
    // Use the slot after the display slot, wrapping around
    m_uploadSlot = (m_displaySlot + 1) % SLOT_COUNT;
    return m_uploadSlot;
}

void VideoTexture::promoteUploadSlot() {
    m_displaySlot = m_uploadSlot;
}

bool VideoTexture::createResources(VulkanContext& ctx) {
    for (int i = 0; i < SLOT_COUNT; i++) {
        // Create image
        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgInfo.extent = {m_width, m_height, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(ctx.allocator, &imgInfo, &allocInfo,
                           &m_images[i], &m_allocations[i], nullptr) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create video texture image %d\n", i);
            return false;
        }

        // Transition to shader read optimal
        VkCommandBuffer cmd = ctx.beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_images[i];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        ctx.endSingleTimeCommands(cmd);

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create video texture image view %d\n", i);
            return false;
        }

        // Register with ImGui
        m_descriptorSets[i] = ImGui_ImplVulkan_AddTexture(
            m_sampler, m_imageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (!m_descriptorSets[i]) {
            fprintf(stderr, "Failed to register texture %d with ImGui\n", i);
            return false;
        }
    }
    return true;
}

void VideoTexture::destroyResources(VulkanContext& ctx) {
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (m_descriptorSets[i]) {
            ImGui_ImplVulkan_RemoveTexture(m_descriptorSets[i]);
            m_descriptorSets[i] = VK_NULL_HANDLE;
        }
        if (m_imageViews[i]) {
            vkDestroyImageView(ctx.device, m_imageViews[i], nullptr);
            m_imageViews[i] = VK_NULL_HANDLE;
        }
        if (m_images[i]) {
            vmaDestroyImage(ctx.allocator, m_images[i], m_allocations[i]);
            m_images[i] = VK_NULL_HANDLE;
            m_allocations[i] = VK_NULL_HANDLE;
        }
    }
}
