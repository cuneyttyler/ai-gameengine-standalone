#include <gtest/gtest.h>

#include "ecs/Registry.h"
#include "platform/Window.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"

TEST(Renderer2DTest, RendersSpriteEntitiesWithoutThrowing) {
    aige::Window window({});
    aige::VulkanRHI rhi(window);

    EXPECT_NO_THROW({
        aige::Renderer2D renderer(rhi, rhi.RenderPass());

        aige::Registry registry;
        auto tile = registry.create();
        registry.emplace<aige::ScreenTransform2D>(tile, 640.0f, 200.0f, 0.0f);
        registry.emplace<aige::Sprite>(tile, 64.0f, 32.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);

        auto character = registry.create();
        registry.emplace<aige::ScreenTransform2D>(character, 640.0f, 190.0f, 1.0f);
        registry.emplace<aige::Sprite>(character, 32.0f, 48.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

        for (int i = 0; i < 5; ++i) {
            window.PollEvents();
            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
                renderer.Render(cmd, registry);
                rhi.EndFrame();
            }
        }
    });
}

TEST(Renderer2DTest, RendersEmptyRegistryWithoutThrowing) {
    aige::Window window({});
    aige::VulkanRHI rhi(window);

    EXPECT_NO_THROW({
        aige::Renderer2D renderer(rhi, rhi.RenderPass());
        aige::Registry registry;

        VkCommandBuffer cmd = rhi.BeginFrame();
        if (cmd != VK_NULL_HANDLE) {
            rhi.BeginSwapchainPass();
            renderer.Render(cmd, registry);
            rhi.EndFrame();
        }
    });
}
