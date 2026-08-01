#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace aige {

class Window;

// Vulkan RHI (technical.md §8): owns the instance/device/swapchain/frame
// lifecycle and the shared VMA allocator. Deliberately concrete (no backend
// abstraction yet) — the interface seam is the `rhi` module boundary itself;
// virtual dispatch gets introduced only once a second backend actually exists.
//
// RHI does NOT own any pipeline or draw calls — it hands a begun command
// buffer (inside an already-begun render pass) to BeginFrame()'s caller, who
// records whatever they want (Renderer2D, later Renderer3D, ...) and calls
// EndFrame() to submit/present.
class VulkanRHI {
public:
    explicit VulkanRHI(Window& window);
    ~VulkanRHI();

    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    // Waits on the in-flight fence, acquires a swapchain image, and begins
    // the command buffer + render pass (viewport/scissor already set to the
    // full swapchain extent). Returns VK_NULL_HANDLE if the frame must be
    // skipped (swapchain was out of date and got recreated) — callers must
    // not record anything in that case.
    VkCommandBuffer BeginFrame();

    // Ends the render pass/command buffer, submits, and presents.
    void EndFrame();

    VkDevice Device() const { return device_; }
    VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
    VkQueue GraphicsQueue() const { return graphicsQueue_; }
    uint32_t GraphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkCommandPool CommandPool() const { return commandPool_; }
    VkRenderPass RenderPass() const { return renderPass_; }
    VkExtent2D SwapchainExtent() const { return swapchainExtent_; }
    VmaAllocator Allocator() const { return allocator_; }
    uint32_t CurrentFrameIndex() const { return static_cast<uint32_t>(currentFrame_); }
    static constexpr int MaxFramesInFlight() { return kMaxFramesInFlight; }

private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();

    void CleanupSwapchain();
    void RecreateSwapchain();

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

    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> swapchainFramebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    static constexpr int kMaxFramesInFlight = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    size_t currentFrame_ = 0;

    VkCommandBuffer currentCommandBuffer_ = VK_NULL_HANDLE;
    uint32_t currentImageIndex_ = 0;
    bool frameActive_ = false;

    bool framebufferResized_ = false;
};

} // namespace aige
