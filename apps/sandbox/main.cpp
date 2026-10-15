// Standalone runtime: plays a project's main scene without the editor.
// `sandbox [--project] <dir>`; defaults to the repo's demo project.
#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Settings.h"
#include "audio/Audio.h"
#include "ecs/Registry.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/CameraFollow.h"
#include "gameplay/GameSession.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"
#include "scripting/LuaVM.h"

#include <chrono>
#include <iostream>
#include <string>

#ifndef AIGE_DEFAULT_PROJECT
#define AIGE_DEFAULT_PROJECT "."
#endif

int main(int argc, char** argv) {
    try {
        std::filesystem::path root = AIGE_DEFAULT_PROJECT;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--project" && i + 1 < argc) {
                root = argv[++i];
            } else if (!arg.empty() && arg[0] != '-') {
                root = arg;
            }
        }
        aige::Project project{std::filesystem::absolute(root)};
        aige::LoadSettings(aige::EngineReflection(), project.SettingsPath().string());
        const aige::ProjectSettings& settings = aige::ActiveSettings();

        aige::Window::Desc desc;
        aige::Window window(desc);
        aige::VulkanRHI rhi(window);
        aige::Renderer2D renderer(rhi, rhi.RenderPass()); // draws straight to the swapchain pass

        aige::AssetDatabase assets(project);
        renderer.SetTextureLoader([&assets](const std::string& path) { return assets.LoadTexture(path); });
        aige::AudioEngine audio;
        aige::PrefabLibrary prefabs(project);
        prefabs.Refresh();

        // lua must outlive registry: LuaScript components store a
        // sol::environment referencing lua's Lua state, so registry (and
        // anything in it) has to be destroyed before lua closes that state.
        aige::LuaVM lua;
        aige::Registry registry;
        auto loadLevel = [&]() {
            if (aige::LoadScene(registry, project.MainScenePath().string())) {
                std::cout << "Loaded " << project.MainScenePath().string() << std::endl;
            } else {
                std::cout << "No scene at " << project.MainScenePath().string() << "; playing the bootstrap level"
                          << std::endl;
                registry.clear();
                aige::BuildBootstrapScene(registry, "scripts/npc_wander.lua");
            }
            prefabs.SyncAll(registry); // prefab edits since the scene was saved apply here too
        };
        loadLevel();

        aige::GameSession session(registry, lua, aige::BootstrapIso(), project.root);
        session.SetSoundPlayer([&](const std::string& path) { audio.Play(project.Resolve(path)); });
        session.SetSpawner([&](const std::string& prefab, float x, float y) {
            return prefabs.Instantiate(registry, prefab, x, y);
        });
        auto isKeyDown = [&window](aige::Key key) { return window.IsKeyDown(key); };

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        double restartIn = -1.0; // counts down after the player dies or wins

        // The camera follows the player; snapped into place whenever a level
        // (re)starts so it doesn't sweep across the map.
        aige::Camera2D camera;
        camera.zoom = settings.camera.playZoom;
        auto snapCamera = [&]() {
            aige::UpdateWorldToScreen(registry, aige::BootstrapIso());
            if (auto player = aige::FindPlayer(registry)) {
                VkExtent2D extent = rhi.SwapchainExtent();
                aige::SnapCameraTo(camera, registry, *player, static_cast<float>(extent.width),
                                   static_cast<float>(extent.height));
            }
        };
        snapCamera();

        while (!window.ShouldClose()) {
            window.PollEvents();
            if (window.IsKeyDown(aige::Key::Escape)) {
                break;
            }

            auto currentTime = Clock::now();
            double frameSeconds = std::chrono::duration<double>(currentTime - previousTime).count();
            previousTime = currentTime;
            session.Advance(frameSeconds, isKeyDown);

            // Player death: a short pause, then restart the level.
            for (const auto& event : session.Events()) {
                auto player = aige::FindPlayer(registry);
                if (event.type == aige::GameEvent::Type::Death && player && event.entity == *player) {
                    std::cout << "You died. Restarting..." << std::endl;
                    restartIn = 2.0;
                } else if (event.type == aige::GameEvent::Type::Victory) {
                    std::cout << "Victory! All enemies defeated. Restarting..." << std::endl;
                    restartIn = 3.0;
                }
            }
            if (restartIn >= 0.0) {
                restartIn -= frameSeconds;
                if (restartIn < 0.0) {
                    loadLevel();
                    session.ResetClock();
                    snapCamera();
                }
            }

            if (auto player = aige::FindPlayer(registry)) {
                VkExtent2D extent = rhi.SwapchainExtent();
                aige::FollowWithCamera(camera, registry, *player, static_cast<float>(extent.width),
                                       static_cast<float>(extent.height), static_cast<float>(frameSeconds),
                                       settings.camera.followStiffness);
            }

            std::vector<aige::OverlayLine> overlay;
            aige::AppendHealthBars(registry, overlay);

            rhi.SetClearColor(settings.renderer.clearR, settings.renderer.clearG, settings.renderer.clearB);
            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
                renderer.Render(cmd, registry, camera, overlay);
                rhi.EndFrame();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
