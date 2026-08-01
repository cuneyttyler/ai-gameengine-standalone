#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

namespace aige {

class Window;

// Bootstrap Vulkan RHI (technical.md §8): owns the instance/device/swapchain and
// can draw a single hardcoded triangle. Deliberately concrete (no backend
// abstraction yet) — the interface seam is the `rhi` module boundary itself;
// virtual dispatch gets introduced only once a second backend actually exists.
class VulkanRHI {
public:
    explicit VulkanRHI(Window& window);
    ~VulkanRHI();

    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    // Acquires an image, records the triangle draw, submits, and presents.
    // Handles swapchain recreation internally on resize/out-of-date.
    void DrawFrame();

private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    void CleanupSwapchain();
    void RecreateSwapchain();

    VkShaderModule LoadShaderModule(const char* spirvFileName) const;

    Window& window_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t presentQueueFamily_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr int kMaxFramesInFlight = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    size_t currentFrame_ = 0;

    bool framebufferResized_ = false;
};

} // namespace aige
