#pragma once

#include <vulkan/vulkan.h>
#include <vector>

struct VulkanContext;

class Swapchain {
public:
    bool init(VulkanContext& ctx, uint32_t width, uint32_t height);
    void shutdown(VkDevice device);
    bool recreate(VulkanContext& ctx, uint32_t width, uint32_t height);

    VkResult acquireNextImage(VkDevice device, VkSemaphore semaphore, uint32_t& imageIndex);
    VkResult present(VkQueue queue, VkSemaphore waitSemaphore, uint32_t imageIndex);

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;

    // Per-frame sync (double buffered)
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT]{};
    VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT]{};
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT]{};
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT]{};
    uint32_t currentFrame = 0;

    bool createSyncObjects(VkDevice device, VkCommandPool commandPool);

private:
    bool createSwapchain(VulkanContext& ctx, uint32_t width, uint32_t height);
    bool createImageViews(VkDevice device);
    bool createRenderPass(VkDevice device);
    bool createFramebuffers(VkDevice device);
    void cleanupSwapchain(VkDevice device);
};
