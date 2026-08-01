#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "editor/ImGuiLayer.h"
#include "physics2d/Components.h"
#include "platform/Window.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"
#include "scripting/ScriptSystem.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <entt/entt.hpp>
#include <iostream>
#include <string>

#ifndef AIGE_EDITOR_SANDBOX_SCENE_PATH
#define AIGE_EDITOR_SANDBOX_SCENE_PATH "."
#endif

namespace {

// Milestone 1.1 has no tagging/prefab system yet, so entity "kind" in the
// Scene Tree is inferred from which components are present — the same kind
// of heuristic apps/sandbox/main.cpp uses to find "the player" after a load.
std::string DescribeEntity(aige::Registry& registry, aige::Entity entity) {
    if (registry.all_of<aige::LuaScript>(entity)) {
        return "Scripted NPC";
    }
    if (registry.all_of<aige::Velocity2D, aige::Collider2D>(entity)) {
        return "Player";
    }
    if (registry.all_of<aige::WorldTransform2D>(entity)) {
        return "NPC";
    }
    return "Tile";
}

// Used only if no apps/sandbox save file exists yet — just enough content to
// exercise the Scene Tree/Inspector before you've ever run the sandbox.
void BuildFallbackScene(aige::Registry& registry) {
    for (int i = 0; i < 4; ++i) {
        auto tile = registry.create();
        registry.emplace<aige::ScreenTransform2D>(tile, 600.0f + static_cast<float>(i) * 40.0f, 300.0f,
                                                   static_cast<float>(i));
        registry.emplace<aige::Sprite>(tile, 64.0f, 32.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    auto player = registry.create();
    registry.emplace<aige::WorldTransform2D>(player, 3.5f, 3.5f);
    registry.emplace<aige::Velocity2D>(player);
    registry.emplace<aige::Collider2D>(player, 0.3f, 0.3f);
    registry.emplace<aige::ScreenTransform2D>(player, 700.0f, 300.0f, 10.0f);
    registry.emplace<aige::Sprite>(player, 32.0f, 48.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}

void DrawSceneTreePanel(aige::Registry& registry, aige::Entity& selected) {
    ImGui::Begin("Scene Tree");
    for (auto entity : registry.view<entt::entity>()) {
        std::string label =
            DescribeEntity(registry, entity) + " ##" + std::to_string(static_cast<uint32_t>(entity));
        bool isSelected = (entity == selected);
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            selected = entity;
        }
    }
    ImGui::End();
}

// Hand-wired per component type for now (development_plan.md milestone
// 1.1) — becomes generated from the Reflection & Command Registry once that
// exists (Phase 2), at which point this whole function goes away.
void DrawInspectorPanel(aige::Registry& registry, aige::Entity selected) {
    ImGui::Begin("Inspector");
    if (selected == entt::null || !registry.valid(selected)) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    if (auto* c = registry.try_get<aige::WorldTransform2D>(selected)) {
        if (ImGui::CollapsingHeader("WorldTransform2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("x##world", &c->x, 0.05f);
            ImGui::DragFloat("y##world", &c->y, 0.05f);
        }
    }
    if (auto* c = registry.try_get<aige::Velocity2D>(selected)) {
        if (ImGui::CollapsingHeader("Velocity2D")) {
            ImGui::DragFloat("x##vel", &c->x, 0.05f);
            ImGui::DragFloat("y##vel", &c->y, 0.05f);
        }
    }
    if (auto* c = registry.try_get<aige::Collider2D>(selected)) {
        if (ImGui::CollapsingHeader("Collider2D")) {
            ImGui::DragFloat("halfWidth##col", &c->halfWidth, 0.01f, 0.01f, 5.0f);
            ImGui::DragFloat("halfHeight##col", &c->halfHeight, 0.01f, 0.01f, 5.0f);
        }
    }
    if (auto* c = registry.try_get<aige::ScreenTransform2D>(selected)) {
        if (ImGui::CollapsingHeader("ScreenTransform2D", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("x##screen", &c->x, 1.0f);
            ImGui::DragFloat("y##screen", &c->y, 1.0f);
            ImGui::DragFloat("depth##screen", &c->depth, 0.1f);
        }
    }
    if (auto* c = registry.try_get<aige::Sprite>(selected)) {
        if (ImGui::CollapsingHeader("Sprite")) {
            ImGui::DragFloat("width##spr", &c->width, 1.0f, 1.0f, 512.0f);
            ImGui::DragFloat("height##spr", &c->height, 1.0f, 1.0f, 512.0f);
            ImGui::DragFloat4("uv (u0,v0,u1,v1)##spr", &c->u0, 0.01f, 0.0f, 1.0f);
            ImGui::ColorEdit4("tint##spr", &c->r);
        }
    }
    if (auto* c = registry.try_get<aige::LuaScript>(selected)) {
        if (ImGui::CollapsingHeader("LuaScript", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("%s", c->path.c_str());
        }
    }
    ImGui::End();
}

} // namespace

int main() {
    try {
        aige::Window::Desc desc;
        desc.title = "AI-Native Game Engine - Editor";
        aige::Window window(desc);
        aige::VulkanRHI rhi(window);
        // Draws into the offscreen target, not the swapchain pass directly —
        // the Viewport panel below displays it via ImGui::Image().
        aige::Renderer2D renderer(rhi, rhi.OffscreenRenderPass());
        aige::ImGuiLayer imguiLayer(window, rhi);

        // lua must outlive registry — see the same note in apps/sandbox/main.cpp.
        aige::LuaVM lua;
        aige::Registry registry;

        std::string sandboxScenePath = std::string(AIGE_EDITOR_SANDBOX_SCENE_PATH) + "/scene_save.aige";
        if (aige::LoadScene(registry, sandboxScenePath)) {
            std::cout << "Loaded scene from " << sandboxScenePath << std::endl;
        } else {
            BuildFallbackScene(registry);
        }

        aige::ScriptBinder scriptBinder = [&registry](sol::environment& env, aige::Entity entity) {
            env["get_position"] = [&registry, entity]() {
                auto& world = registry.get<aige::WorldTransform2D>(entity);
                return std::make_tuple(world.x, world.y);
            };
            env["set_position"] = [&registry, entity](float x, float y) {
                auto& world = registry.get<aige::WorldTransform2D>(entity);
                world.x = x;
                world.y = y;
            };
        };

        aige::Entity selectedEntity{entt::null};
        bool dockLayoutBuilt = false;

        VkImageView lastOffscreenView = VK_NULL_HANDLE;
        VkDescriptorSet viewportTextureId = VK_NULL_HANDLE;

        using Clock = std::chrono::steady_clock;
        constexpr double kFixedDt = 1.0 / 60.0;
        auto previousTime = Clock::now();
        double accumulator = 0.0;

        while (!window.ShouldClose()) {
            window.PollEvents();

            auto currentTime = Clock::now();
            double frameSeconds = std::chrono::duration<double>(currentTime - previousTime).count();
            previousTime = currentTime;
            frameSeconds = std::min(frameSeconds, 0.25);

            accumulator += frameSeconds;
            while (accumulator >= kFixedDt) {
                // No player-input-driven movement in the editor — that's
                // Playtest/Preview (product.md), a distinct future mode.
                // Lua-scripted entities still update, so the viewport shows
                // genuinely live RHI output rather than a frozen frame.
                aige::UpdateScripts(registry, lua, static_cast<float>(kFixedDt), scriptBinder);
                accumulator -= kFixedDt;
            }

            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd == VK_NULL_HANDLE) {
                continue;
            }

            if (rhi.OffscreenColorView() != lastOffscreenView) {
                if (viewportTextureId != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RemoveTexture(viewportTextureId);
                }
                viewportTextureId = ImGui_ImplVulkan_AddTexture(rhi.OffscreenSampler(), rhi.OffscreenColorView(),
                                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                lastOffscreenView = rhi.OffscreenColorView();
            }

            rhi.BeginOffscreenPass();
            renderer.Render(cmd, registry);
            rhi.EndOffscreenPass();

            rhi.BeginSwapchainPass();

            ImGuiID dockspaceId = imguiLayer.BeginFrame();
            if (!dockLayoutBuilt) {
                dockLayoutBuilt = true;
                ImGui::DockBuilderRemoveNode(dockspaceId);
                ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

                ImGuiID rightId = 0;
                ImGuiID centerId = 0;
                ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.28f, &rightId, &centerId);
                ImGuiID inspectorId = 0;
                ImGuiID sceneTreeId = 0;
                ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.5f, &inspectorId, &sceneTreeId);

                ImGui::DockBuilderDockWindow("Viewport", centerId);
                ImGui::DockBuilderDockWindow("Scene Tree", sceneTreeId);
                ImGui::DockBuilderDockWindow("Inspector", inspectorId);
                ImGui::DockBuilderFinish(dockspaceId);
            }

            ImGui::Begin("Viewport");
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 0.0f && avail.y > 0.0f && viewportTextureId != VK_NULL_HANDLE) {
                ImGui::Image(reinterpret_cast<ImTextureID>(viewportTextureId), avail);
            }
            ImGui::End();

            DrawSceneTreePanel(registry, selectedEntity);
            DrawInspectorPanel(registry, selectedEntity);

            imguiLayer.EndFrame(cmd);
            rhi.EndFrame();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
