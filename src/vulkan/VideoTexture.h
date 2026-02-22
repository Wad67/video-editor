#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

struct VulkanContext;

class VideoTexture {
public:
    static constexpr int SLOT_COUNT = 3;

    bool init(VulkanContext& ctx, uint32_t width, uint32_t height);
    void shutdown(VulkanContext& ctx);

    // Reinitialize with new dimensions
    bool resize(VulkanContext& ctx, uint32_t width, uint32_t height);

    // Get the current display slot's descriptor set for ImGui::Image()
    VkDescriptorSet getDisplayDescriptor() const { return m_descriptorSets[m_displaySlot]; }

    // Get upload slot index and advance
    int acquireUploadSlot();
    void promoteUploadSlot();

    VkImage getImage(int slot) const { return m_images[slot]; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }

private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    VkImage m_images[SLOT_COUNT]{};
    VmaAllocation m_allocations[SLOT_COUNT]{};
    VkImageView m_imageViews[SLOT_COUNT]{};
    VkDescriptorSet m_descriptorSets[SLOT_COUNT]{};
    VkSampler m_sampler = VK_NULL_HANDLE;

    int m_uploadSlot = 0;
    int m_displaySlot = 0;

    bool createResources(VulkanContext& ctx);
    void destroyResources(VulkanContext& ctx);
};
