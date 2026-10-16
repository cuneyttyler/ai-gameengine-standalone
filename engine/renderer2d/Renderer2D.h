#pragma once

#include "ecs/Registry.h"
#include "renderer2d/Camera2D.h"
#include "renderer2d/ImageData.h"
#include "renderer2d/SpriteCulling.h"
#include "renderer2d/Text.h"
#include "rhi/VulkanRHI.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
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

// Sprite batcher built on top of VulkanRHI's frame lifecycle (technical.md §8).
// Draws every entity with (ScreenTransform2D, Sprite), painter's-algorithm
// sorted by depth. Each Sprite names its texture by project-relative path;
// textures are loaded lazily through the app-provided TextureLoader (the
// asset pipeline), and an empty path means the built-in placeholder atlas.
class Renderer2D {
public:
    // `targetRenderPass` is whichever pass this renderer's pipeline draws
    // into; callers begin it before calling Render().
    Renderer2D(VulkanRHI& rhi, VkRenderPass targetRenderPass);
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    using TextureLoader = std::function<std::optional<ImageData>(const std::string& path)>;
    void SetTextureLoader(TextureLoader loader);

    // Drops a cached texture so the next use reloads it (asset reimport).
    void InvalidateTexture(const std::string& path);

    // Uploads a texture made in code (a font atlas) under `name`, once; later
    // calls with the same name do nothing. UiQuads refer to it by name.
    void RegisterTexture(const std::string& name, const ImageData& image);

    struct TextureSize {
        uint32_t width;
        uint32_t height;
    };
    // Loads the texture if needed; nullopt for the built-in atlas or a
    // texture that failed to load.
    std::optional<TextureSize> TextureSizeOf(const std::string& path);

    // Must be called with `cmd` from an active VulkanRHI frame, inside the
    // render pass this Renderer2D was constructed with (already begun,
    // viewport/scissor already set). Sprites are drawn through `camera`, then
    // `overlay` lines on top, then `ui` quads in viewport pixels (no camera).
    // `extra` (optional) is drawn with the scene, in the same depth order and
    // in front of it at equal depth: the editor's brush preview (5.6).
    void Render(VkCommandBuffer cmd, Registry& registry, const Camera2D& camera = {},
                std::span<const OverlayLine> overlay = {}, std::span<const UiQuad> ui = {},
                Registry* extra = nullptr);

    // What the last Render drew (the F3 stats overlay).
    const RenderStats& LastStats() const { return stats_; }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr;
        uint32_t capacity = 0; // in vertices
    };

    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    void CreateSampler();
    void CreateDescriptorLayoutAndPool();
    void CreatePipeline();
    void CreateVertexBuffers();
    // Grows (never shrinks) this frame's vertex buffer to hold `vertices`. The
    // frame's previous use has finished (its fence was waited on), so the old
    // buffer can go right away.
    void EnsureCapacity(Buffer& buffer, uint32_t vertices);
    void CreateVertexBuffer(Buffer& buffer, uint32_t vertices);
    void RunOneTimeCommands(const std::function<void(VkCommandBuffer)>& recordFn);

    Texture UploadTexture(const ImageData& image);
    void DestroyTexture(Texture& texture);

    // The texture to draw a sprite with: cached, freshly loaded, or the
    // built-in atlas as a fallback. Never fails.
    const Texture& Resolve(const std::string& path);

    VulkanRHI& rhi_;
    VkRenderPass targetRenderPass_;

    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    Texture builtinAtlas_;
    TextureLoader loader_;
    // A failed load is cached as an empty Texture (image == VK_NULL_HANDLE).
    std::unordered_map<std::string, Texture> textures_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    static constexpr uint32_t kInitialSprites = 4096; // buffers grow past this as needed (4.6)
    static constexpr uint32_t kVerticesPerSprite = 6;
    std::vector<Buffer> vertexBuffers_; // one per frame-in-flight
    RenderStats stats_;
};

} // namespace aige
