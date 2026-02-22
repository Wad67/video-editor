#pragma once

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

struct VulkanContext;
class Swapchain;

class ImGuiLayer {
public:
    bool init(SDL_Window* window, VulkanContext& ctx, Swapchain& swapchain);
    void shutdown();

    void processEvent(const SDL_Event& event);
    void beginFrame();
    void endFrame();
    void render(VkCommandBuffer cmd);

private:
    bool m_initialized = false;
};
