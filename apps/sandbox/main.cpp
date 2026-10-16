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
#include "gameplay/Benchmark.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/CameraFollow.h"
#include "gameplay/GameSession.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "renderer2d/Renderer2D.h"
#include "ui/GameUi.h"
#include "world/WorldBible.h"
#include "rhi/VulkanRHI.h"
#include "scripting/LuaVM.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#ifndef AIGE_DEFAULT_PROJECT
#define AIGE_DEFAULT_PROJECT "."
#endif

int main(int argc, char** argv) {
    try {
        std::filesystem::path root = AIGE_DEFAULT_PROJECT;
        bool uiPreview = false;   // --ui-preview: sample game UI over the level (UI development)
        double exitAfter = -1.0;  // --exit-after <seconds>: quit by itself (smoke runs)
        bool bench = false;       // --bench: 10k tiles + 500 characters, stats on (development_plan.md 4.6)
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--project" && i + 1 < argc) {
                root = argv[++i];
            } else if (arg == "--bench") {
                bench = true;
            } else if (arg == "--ui-preview") {
                uiPreview = true;
            } else if (arg == "--exit-after" && i + 1 < argc) {
                exitAfter = std::stod(argv[++i]);
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
        aige::RegisterGameUiFonts(renderer);

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
            if (bench) {
                registry.clear();
                aige::BuildBenchmarkScene(registry);
                std::cout << "Benchmark scene: 10000 tiles, 500 characters" << std::endl;
                return;
            }
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
        // Dialogue and quests, with names from the World Bible.
        aige::StoryLibrary story(project.root);
        story.Refresh();
        aige::WorldBible world(project);
        world.Refresh();
        session.SetStory(&story);
        aige::ScheduleLibrary schedules(project.root);
        schedules.Refresh();
        session.SetSchedules(&schedules);
        session.SetNameResolver([&world](const std::string& key) {
            const aige::WorldEntry* entry = world.Find(key);
            return entry ? entry->name : std::string();
        });
        auto isKeyDown = [&window](aige::Key key) { return window.IsKeyDown(key); };

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        double restartIn = -1.0; // counts down after the player dies or wins
        std::pair<std::string, std::string> banner; // title, subtitle; drawn by the game while set

        // The camera follows the player; snapped into place whenever a level
        // (re)starts so it doesn't sweep across the map.
        aige::Camera2D camera;
        camera.zoom = settings.camera.playZoom;
        if (bench) {
            camera.zoom = 0.3f; // most of the field on screen: far past the old 4096-sprite limit
        }
        auto snapCamera = [&]() {
            aige::UpdateWorldToScreen(registry, aige::BootstrapIso());
            if (auto player = aige::FindPlayer(registry)) {
                VkExtent2D extent = rhi.SwapchainExtent();
                aige::SnapCameraTo(camera, registry, *player, static_cast<float>(extent.width),
                                   static_cast<float>(extent.height));
            }
        };
        snapCamera();

        // F3: frame stats (smoothed), on from the start in --bench.
        bool showStats = bench;
        bool f3WasDown = false;
        double frameMs = 0.0, updateMs = 0.0, benchFrames = 0.0, benchFrameMs = 0.0, benchUpdateMs = 0.0;

        const auto startTime = Clock::now();
        while (!window.ShouldClose()) {
            window.PollEvents();
            if (exitAfter >= 0.0 && std::chrono::duration<double>(Clock::now() - startTime).count() > exitAfter) {
                break;
            }
            if (window.IsKeyDown(aige::Key::Escape)) {
                break;
            }

            auto currentTime = Clock::now();
            double frameSeconds = std::chrono::duration<double>(currentTime - previousTime).count();
            previousTime = currentTime;
            const auto updateStart = Clock::now();
            session.Advance(frameSeconds, isKeyDown);
            const double stepMs = std::chrono::duration<double, std::milli>(Clock::now() - updateStart).count();
            frameMs = frameMs * 0.9 + frameSeconds * 1000.0 * 0.1;
            updateMs = updateMs * 0.9 + stepMs * 0.1;
            if (bench && std::chrono::duration<double>(currentTime - startTime).count() > 1.0) { // after warm-up
                benchFrames += 1.0;
                benchFrameMs += frameSeconds * 1000.0;
                benchUpdateMs += stepMs;
            }
            const bool f3Down = window.IsKeyDown(aige::Key::F3);
            if (f3Down && !f3WasDown) {
                showStats = !showStats;
            }
            f3WasDown = f3Down;

            // Player death: a short pause, then restart the level.
            for (const auto& event : session.Events()) {
                auto player = aige::FindPlayer(registry);
                if (event.type == aige::GameEvent::Type::Death && player && event.entity == *player) {
                    std::cout << "You died. Restarting..." << std::endl;
                    banner = {"You died", "Back to the start in a moment"};
                    restartIn = 2.0;
                } else if (event.type == aige::GameEvent::Type::Victory) {
                    std::cout << "Victory! All enemies defeated. Restarting..." << std::endl;
                    banner = {"Victory!", "All enemies defeated"};
                    restartIn = 3.0;
                }
            }
            if (restartIn >= 0.0) {
                restartIn -= frameSeconds;
                if (restartIn < 0.0) {
                    banner = {};
                    loadLevel();
                    session.SetStory(&story); // a fresh run: quests and flags start over
                    session.SetClockMinutes(-1.0); // and the day starts again
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

            std::vector<aige::UiQuad> ui;
            {
                VkExtent2D extent = rhi.SwapchainExtent();
                const float width = static_cast<float>(extent.width);
                const float height = static_cast<float>(extent.height);
                session.BuildUi(ui, width, height);
                if (!banner.first.empty()) {
                    aige::AddBanner(ui, banner.first, banner.second, width, height);
                }
                if (showStats) {
                    const aige::RenderStats& stats = renderer.LastStats();
                    char lines[3][96];
                    std::snprintf(lines[0], sizeof(lines[0]), "frame %.1f ms (%.0f fps)", frameMs,
                                  frameMs > 0.0 ? 1000.0 / frameMs : 0.0);
                    std::snprintf(lines[1], sizeof(lines[1]), "update %.2f ms", updateMs);
                    std::snprintf(lines[2], sizeof(lines[2]), "sprites %zu drawn, %zu culled - %zu draw calls",
                                  stats.sprites, stats.culled, stats.drawCalls);
                    const aige::Font& font = aige::Font::Builtin(14.0f);
                    float textWidth = 0.0f;
                    for (const char* line : lines) textWidth = std::max(textWidth, font.Measure(line));
                    aige::AddUiRect(ui, 8.0f, 8.0f, 20.0f + textWidth, 14.0f + font.LineHeight() * 3.0f,
                                    {0.0f, 0.0f, 0.0f, 0.65f});
                    for (int i = 0; i < 3; ++i) {
                        font.Draw(ui, lines[i], 14.0f, 11.0f + font.LineHeight() * static_cast<float>(i),
                                  {0.85f, 0.95f, 0.85f, 1.0f});
                    }
                }
                if (uiPreview) {
                    aige::AddObjective(ui, "Clear the courtyard", "Skeletons defeated: 2/4", width, height);
                    aige::DialogueView view;
                    view.speaker = "Old Tamsin";
                    view.text = "Another Warden? The last one went up that hill and came back in a sack. "
                                "Take a draught, at least.";
                    view.choices = {"I'll be careful.", "What happened to the last one?", "Goodbye."};
                    view.highlighted = 1;
                    aige::AddDialogueBox(ui, view, width, height);
                }
            }

            rhi.SetClearColor(settings.renderer.clearR, settings.renderer.clearG, settings.renderer.clearB);
            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
                renderer.Render(cmd, registry, camera, overlay, ui);
                rhi.EndFrame();
            }
        }
        if (bench && benchFrames > 0.0) {
            std::cout << "Benchmark: " << benchFrames << " frames, average frame " << benchFrameMs / benchFrames
                      << " ms, update " << benchUpdateMs / benchFrames << " ms" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
