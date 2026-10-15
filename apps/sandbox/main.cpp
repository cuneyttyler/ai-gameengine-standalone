#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"
#include "scripting/LuaVM.h"

#include <chrono>
#include <iostream>

#ifndef AIGE_SANDBOX_SOURCE_DIR
#define AIGE_SANDBOX_SOURCE_DIR "."
#endif

int main() {
    try {
        aige::Window::Desc desc;
        aige::Window window(desc);
        aige::VulkanRHI rhi(window);
        aige::Renderer2D renderer(rhi, rhi.RenderPass()); // draws straight to the swapchain pass

        // lua must outlive registry: LuaScript components store a
        // sol::environment referencing lua's Lua state, so registry (and
        // anything in it) has to be destroyed before lua closes that state.
        aige::LuaVM lua;
        aige::Registry registry;

        const std::string saveFilePath = std::string(AIGE_SANDBOX_SOURCE_DIR) + "/scene_save.aige";
        if (aige::LoadScene(registry, saveFilePath)) {
            std::cout << "Loaded scene from " << saveFilePath << std::endl;
        } else {
            aige::BuildBootstrapScene(registry, std::string(AIGE_SANDBOX_SOURCE_DIR) + "/scripts/npc_wander.lua");
        }

        aige::GameSession session(registry, lua, aige::BootstrapIso(), &aige::IsBootstrapSolidCell);
        auto isKeyDown = [&window](aige::Key key) { return window.IsKeyDown(key); };

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        bool f5WasDown = false;

        while (!window.ShouldClose()) {
            window.PollEvents();
            if (window.IsKeyDown(aige::Key::Escape)) {
                break;
            }

            bool f5IsDown = window.IsKeyDown(aige::Key::F5);
            if (f5IsDown && !f5WasDown) {
                aige::SaveScene(registry, saveFilePath);
                std::cout << "Saved scene to " << saveFilePath << std::endl;
            }
            f5WasDown = f5IsDown;

            auto currentTime = Clock::now();
            session.Advance(std::chrono::duration<double>(currentTime - previousTime).count(), isKeyDown);
            previousTime = currentTime;

            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
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
