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
// RHI does NOT own any pipeline or draw calls. BeginFrame() only acquires an
// image and begins the command buffer — no render pass is active yet.
// Callers explicitly begin/end whichever pass(es) they need this frame:
//   - BeginOffscreenPass()/EndOffscreenPass(): a sampleable color target
//     (OffscreenRenderPass()/OffscreenColorView()) meant for a renderer
//     (Renderer2D, later Renderer3D) to draw a scene into, which an editor
//     then displays inside an ImGui::Image() viewport panel.
//   - BeginSwapchainPass(): the render pass that actually targets the
//     window's swapchain framebuffer — where ImGui itself draws, or where a
//     non-editor app (apps/sandbox) draws directly with nothing further.
// EndFrame() ends whichever pass is currently open, then submits/presents.
class VulkanRHI {
public:
    explicit VulkanRHI(Window& window);
    ~VulkanRHI();

    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    // Waits on the in-flight fence, acquires a swapchain image, and begins
    // the command buffer (no render pass active yet). Returns VK_NULL_HANDLE
    // if the frame must be skipped (swapchain was out of date and got
    // recreated) — callers must not record anything in that case.
    VkCommandBuffer BeginFrame();

    void BeginOffscreenPass();
    void EndOffscreenPass();
    void BeginSwapchainPass();

    // Ends the currently-active render pass (if any), submits, and presents.
    void EndFrame();

    VkInstance Instance() const { return instance_; }
    VkDevice Device() const { return device_; }
    VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
    VkQueue GraphicsQueue() const { return graphicsQueue_; }
    uint32_t GraphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkCommandPool CommandPool() const { return commandPool_; }
    VkRenderPass RenderPass() const { return renderPass_; }
    VkExtent2D SwapchainExtent() const { return swapchainExtent_; }
    uint32_t SwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages_.size()); }
    VkFormat SwapchainFormat() const { return swapchainFormat_; }
    VmaAllocator Allocator() const { return allocator_; }
    uint32_t CurrentFrameIndex() const { return static_cast<uint32_t>(currentFrame_); }
    static constexpr int MaxFramesInFlight() { return kMaxFramesInFlight; }

    VkRenderPass OffscreenRenderPass() const { return offscreenRenderPass_; }
    VkImageView OffscreenColorView() const { return offscreenImageView_; }
    VkSampler OffscreenSampler() const { return offscreenSampler_; }
    VkExtent2D OffscreenExtent() const { return swapchainExtent_; } // same size for now, see §17 open question

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
    void CreateFrameSyncObjects();
    void CreateRenderFinishedSemaphores();
    void CreateOffscreenRenderPass();
    void CreateOffscreenSampler();
    void CreateOffscreenTarget();
    void CleanupOffscreenTarget();

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
    // Per frame-in-flight (size kMaxFramesInFlight): safe to index by
    // currentFrame_ because CreateSyncObjects's fence wait already guarantees
    // the previous use of that slot is done before it's reused.
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkFence> inFlightFences_;
    // Per swapchain image (size swapchainImages_.size(), NOT kMaxFramesInFlight):
    // must be indexed by the acquired image index, not currentFrame_ — the
    // presentation engine's use of this semaphore is tied to the image, and
    // frame count != image count in general (we recreate this whenever the
    // swapchain does). Indexing it by frame instead is a well-known footgun
    // in the "naive" synchronization pattern: validation flags it as a real
    // spec violation, and it manifested here as a hang under sustained input.
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    size_t currentFrame_ = 0;

    // Created once (format/device-only, not extent-dependent).
    VkRenderPass offscreenRenderPass_ = VK_NULL_HANDLE;
    VkSampler offscreenSampler_ = VK_NULL_HANDLE;
    // Extent-dependent: recreated alongside the swapchain on resize.
    VkImage offscreenImage_ = VK_NULL_HANDLE;
    VmaAllocation offscreenAllocation_ = nullptr;
    VkImageView offscreenImageView_ = VK_NULL_HANDLE;
    VkFramebuffer offscreenFramebuffer_ = VK_NULL_HANDLE;

    VkCommandBuffer currentCommandBuffer_ = VK_NULL_HANDLE;
    uint32_t currentImageIndex_ = 0;
    bool frameActive_ = false;
    bool renderPassActive_ = false;

    bool framebufferResized_ = false;
};

} // namespace aige
