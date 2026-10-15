#pragma once

#include "ecs/Registry.h"
#include "renderer2d/Camera2D.h"
#include "rhi/VulkanRHI.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace aige {

// A solid-color line drawn on top of every sprite: editor selection outlines,
// hover markers, debug shapes. Endpoints are in scene pixels (the camera
// applies); thickness is in viewport pixels, so outlines stay crisp at any
// zoom.
struct OverlayLine {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float thickness = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// Sprite batcher built on top of VulkanRHI's frame lifecycle (technical.md §8,
// §5). Owns its own pipeline/descriptor set/vertex buffers and a single
// procedurally-generated placeholder atlas texture — real texture import is
// development_plan.md Phase 1 milestone 1.3, out of scope here. Draws every
// entity with (Transform2D, Sprite) in the given registry, painter's-algorithm
// sorted by Transform2D::depth.
class Renderer2D {
public:
    // `targetRenderPass` is whichever pass this renderer's pipeline draws
    // into — callers begin that specific pass (VulkanRHI::BeginOffscreenPass
    // or BeginSwapchainPass) before calling Render(). Explicit rather than
    // assumed, since an editor draws into the offscreen target while a
    // non-editor app (apps/sandbox) can draw straight to the swapchain pass.
    Renderer2D(VulkanRHI& rhi, VkRenderPass targetRenderPass);
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Must be called with `cmd` from an active VulkanRHI frame, inside the
    // render pass this Renderer2D was constructed with (already begun,
    // viewport/scissor already set). Sprites are drawn through `camera`, then
    // `overlay` lines on top.
    void Render(VkCommandBuffer cmd, Registry& registry, const Camera2D& camera = {},
                std::span<const OverlayLine> overlay = {});

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr;
    };

    void CreateAtlasTexture();
    void CreateDescriptorSet();
    void CreatePipeline();
    void CreateVertexBuffers();
    void RunOneTimeCommands(const std::function<void(VkCommandBuffer)>& recordFn);

    VulkanRHI& rhi_;
    VkRenderPass targetRenderPass_;

    VkImage atlasImage_ = VK_NULL_HANDLE;
    VmaAllocation atlasAllocation_ = nullptr;
    VkImageView atlasImageView_ = VK_NULL_HANDLE;
    VkSampler atlasSampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    static constexpr uint32_t kMaxSprites = 4096;
    static constexpr uint32_t kVerticesPerSprite = 6;
    std::vector<Buffer> vertexBuffers_; // one per frame-in-flight
};

} // namespace aige
