#include "ecs/Registry.h"
#include "platform/Window.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"

#include <iostream>

namespace {

// UV sub-rects into Renderer2D's 2x2 procedural atlas (Renderer2D.cpp).
constexpr float kGrassUV[4] = {0.0f, 0.0f, 0.5f, 0.5f};
constexpr float kDirtUV[4] = {0.5f, 0.0f, 1.0f, 0.5f};
constexpr float kPlayerUV[4] = {0.0f, 0.5f, 0.5f, 1.0f};
constexpr float kNpcUV[4] = {0.5f, 0.5f, 1.0f, 1.0f};

// Builds an isometric tile grid plus a couple of "character" entities, all as
// plain (Transform2D, Sprite) entities — the renderer has no concept of
// "tiles" at all, tilemap-ness is purely a content-layer construct built from
// the same sprite primitive (development_plan.md milestone 0.3).
void BuildScene(aige::Registry& registry, int viewportWidth) {
    constexpr int kGridSize = 8;
    constexpr float kTileWidth = 64.0f;
    constexpr float kTileHeight = 32.0f;

    const float originX = static_cast<float>(viewportWidth) * 0.5f;
    const float originY = 96.0f;

    for (int gridX = 0; gridX < kGridSize; ++gridX) {
        for (int gridY = 0; gridY < kGridSize; ++gridY) {
            auto entity = registry.create();

            float screenX = originX + static_cast<float>(gridX - gridY) * (kTileWidth * 0.5f);
            float screenY = originY + static_cast<float>(gridX + gridY) * (kTileHeight * 0.5f);
            float depth = static_cast<float>(gridX + gridY);

            registry.emplace<aige::Transform2D>(entity, screenX, screenY, depth);

            bool isPath = (gridX == kGridSize / 2) || (gridY == kGridSize / 2);
            const float* uv = isPath ? kDirtUV : kGrassUV;
            registry.emplace<aige::Sprite>(entity, kTileWidth, kTileHeight, uv[0], uv[1], uv[2], uv[3], 1.0f, 1.0f,
                                            1.0f, 1.0f);
        }
    }

    auto SpawnCharacter = [&](int gridX, int gridY, const float* uv) {
        auto entity = registry.create();
        float screenX = originX + static_cast<float>(gridX - gridY) * (kTileWidth * 0.5f);
        float screenY = originY + static_cast<float>(gridX + gridY) * (kTileHeight * 0.5f) - kTileHeight * 0.5f;
        // Slightly higher depth than its own tile so it draws after (on top
        // of) the ground it stands on, per stable_sort ordering.
        float depth = static_cast<float>(gridX + gridY) + 0.5f;

        registry.emplace<aige::Transform2D>(entity, screenX, screenY, depth);
        registry.emplace<aige::Sprite>(entity, 32.0f, 48.0f, uv[0], uv[1], uv[2], uv[3], 1.0f, 1.0f, 1.0f, 1.0f);
    };

    SpawnCharacter(3, 3, kPlayerUV);
    SpawnCharacter(5, 2, kNpcUV);
    SpawnCharacter(2, 5, kNpcUV);
}

} // namespace

int main() {
    try {
        aige::Window::Desc desc;
        aige::Window window(desc);
        aige::VulkanRHI rhi(window);
        aige::Renderer2D renderer(rhi);

        aige::Registry registry;
        BuildScene(registry, static_cast<int>(desc.width));

        while (!window.ShouldClose()) {
            window.PollEvents();
            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                renderer.Render(cmd, registry);
                rhi.EndFrame();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
