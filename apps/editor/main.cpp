// Milestone 1.1b: the Vue frontend (apps/editor/ui) hosts every panel
// (Scene Tree, Inspector, and everything else, per the approved WebView2/Vue
// pivot plan); the native Vulkan viewport is positioned "on top" of the
// WebView2 surface at whatever rect the Vue app's viewport placeholder div
// reports over IPC ("HTML hole punching" — see docs/development_plan.md's
// approved plan for this pivot). The WebView2 control always fills the
// entire client area; only the native viewport window's bounds are dynamic.
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "editor/ViewportWindow.h"
#include "editor/WebViewHost.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <combaseapi.h>

#include <chrono>
#include <optional>
#include <string>

#ifndef AIGE_EDITOR_SANDBOX_SCENE_PATH
#define AIGE_EDITOR_SANDBOX_SCENE_PATH "."
#endif
#ifndef AIGE_EDITOR_UI_DIST_DIR
#define AIGE_EDITOR_UI_DIST_DIR "."
#endif

namespace {

const wchar_t* kMainWindowClass = L"AIGEEditorMainWindow";
const wchar_t* kVirtualHostName = L"aige-editor.local";

aige::WebViewHost* g_webView = nullptr;

// Pure-ASCII only (source tree/build paths, and our own JSON IPC payloads) —
// a full MultiByteToWideChar/WideCharToMultiByte conversion isn't needed for
// what these strings ever actually contain. Written as explicit per-char
// casts rather than the range constructor to avoid MSVC's (correct, but
// irrelevant here) narrowing-conversion warning.
std::wstring WidenAscii(const std::string& s) {
    std::wstring result(s.size(), L'\0');
    for (size_t i = 0; i < s.size(); ++i) {
        result[i] = static_cast<wchar_t>(s[i]);
    }
    return result;
}

std::string NarrowAscii(const std::wstring& s) {
    std::string result(s.size(), '\0');
    for (size_t i = 0; i < s.size(); ++i) {
        result[i] = static_cast<char>(s[i]);
    }
    return result;
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE: {
            if (g_webView != nullptr) {
                g_webView->SetBounds(0, 0, LOWORD(lparam), HIWORD(lparam));
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

// No tagging/prefab system yet, so entity "kind" in the Scene Tree is
// inferred from which components are present — the same heuristic the
// (now-deleted) ImGui editor shell used.
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

nlohmann::json BuildSceneJson(aige::Registry& registry) {
    nlohmann::json entities = nlohmann::json::array();
    auto view = registry.view<entt::entity>();
    for (auto entity : view) {
        entities.push_back({{"id", static_cast<uint32_t>(entity)}, {"kind", DescribeEntity(registry, entity)}});
    }
    return {{"type", "scene:update"}, {"entities", entities}};
}

nlohmann::json BuildInspectorJson(aige::Registry& registry, aige::Entity entity) {
    nlohmann::json components = nlohmann::json::object();
    if (auto* c = registry.try_get<aige::WorldTransform2D>(entity)) {
        components["WorldTransform2D"] = {{"x", c->x}, {"y", c->y}};
    }
    if (auto* c = registry.try_get<aige::Velocity2D>(entity)) {
        components["Velocity2D"] = {{"x", c->x}, {"y", c->y}};
    }
    if (auto* c = registry.try_get<aige::Collider2D>(entity)) {
        components["Collider2D"] = {{"halfWidth", c->halfWidth}, {"halfHeight", c->halfHeight}};
    }
    if (auto* c = registry.try_get<aige::ScreenTransform2D>(entity)) {
        components["ScreenTransform2D"] = {{"x", c->x}, {"y", c->y}, {"depth", c->depth}};
    }
    if (auto* c = registry.try_get<aige::Sprite>(entity)) {
        components["Sprite"] = {{"width", c->width},   {"height", c->height}, {"u0", c->u0}, {"v0", c->v0},
                                 {"u1", c->u1},         {"v1", c->v1},         {"r", c->r},   {"g", c->g},
                                 {"b", c->b},           {"a", c->a}};
    }
    if (auto* c = registry.try_get<aige::LuaScript>(entity)) {
        components["LuaScript"] = {{"path", c->path}};
    }
    return {{"type", "inspector:data"}, {"id", static_cast<uint32_t>(entity)}, {"components", components}};
}

// Direct component field writes, mirroring what the deleted ImGui editor's
// DragFloat callbacks did — hand-wired per component/field until Phase 2's
// reflection-driven component authoring lands.
void ApplySetField(aige::Registry& registry, aige::Entity entity, const std::string& component,
                    const std::string& field, float value) {
    if (component == "WorldTransform2D") {
        if (auto* c = registry.try_get<aige::WorldTransform2D>(entity)) {
            if (field == "x") c->x = value;
            else if (field == "y") c->y = value;
        }
    } else if (component == "Velocity2D") {
        if (auto* c = registry.try_get<aige::Velocity2D>(entity)) {
            if (field == "x") c->x = value;
            else if (field == "y") c->y = value;
        }
    } else if (component == "Collider2D") {
        if (auto* c = registry.try_get<aige::Collider2D>(entity)) {
            if (field == "halfWidth") c->halfWidth = value;
            else if (field == "halfHeight") c->halfHeight = value;
        }
    } else if (component == "ScreenTransform2D") {
        if (auto* c = registry.try_get<aige::ScreenTransform2D>(entity)) {
            if (field == "x") c->x = value;
            else if (field == "y") c->y = value;
            else if (field == "depth") c->depth = value;
        }
    } else if (component == "Sprite") {
        if (auto* c = registry.try_get<aige::Sprite>(entity)) {
            if (field == "width") c->width = value;
            else if (field == "height") c->height = value;
            else if (field == "u0") c->u0 = value;
            else if (field == "v0") c->v0 = value;
            else if (field == "u1") c->u1 = value;
            else if (field == "v1") c->v1 = value;
            else if (field == "r") c->r = value;
            else if (field == "g") c->g = value;
            else if (field == "b") c->b = value;
            else if (field == "a") c->a = value;
        }
    }
}

nlohmann::json BuildPlayStateJson(bool playing) {
    return {{"type", "play:state"}, {"playing", playing}};
}

} // namespace

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, LPWSTR, int) {
    try {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &MainWndProc;
        wc.hInstance = hinstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kMainWindowClass;
        RegisterClassExW(&wc);

        HWND mainWindow = CreateWindowExW(0, kMainWindowClass, L"AI-Native Game Engine - Editor",
                                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900, nullptr,
                                          nullptr, hinstance, nullptr);
        if (mainWindow == nullptr) {
            throw std::runtime_error("Failed to create main window");
        }

        RECT clientRect{};
        GetClientRect(mainWindow, &clientRect);
        int clientWidth = clientRect.right - clientRect.left;
        int clientHeight = clientRect.bottom - clientRect.top;

        // Native viewport: sits "on top" of the WebView2 surface (created
        // after it, so later in z-order) at whatever rect the Vue app
        // reports for its placeholder div. Starts filling the whole window
        // so something renders before the first IPC bounds message arrives.
        aige::ViewportWindow viewportWindow(mainWindow, hinstance, 0, 0, clientWidth, clientHeight);

        aige::VulkanRHI rhi(
            viewportWindow.Handle(), viewportWindow.Instance(),
            [&viewportWindow](int& w, int& h) { viewportWindow.GetFramebufferSize(w, h); },
            [&viewportWindow](std::function<void(int, int)> cb) {
                viewportWindow.SetResizeCallback(std::move(cb));
            });
        aige::Renderer2D renderer(rhi, rhi.RenderPass());

        // lua must outlive registry: LuaScript components hold
        // sol::environments referencing lua's state.
        aige::LuaVM lua;
        aige::Registry registry;
        const std::string sandboxDir = AIGE_EDITOR_SANDBOX_SCENE_PATH;
        if (!aige::LoadScene(registry, sandboxDir + "/scene_save.aige")) {
            aige::BuildBootstrapScene(registry, sandboxDir + "/scripts/npc_wander.lua");
        }

        const aige::IsoParams iso = aige::BootstrapIso();
        aige::GameSession session(registry, lua, iso, &aige::IsBootstrapSolidCell);
        auto isKeyDown = [&viewportWindow](aige::Key key) { return viewportWindow.IsKeyDown(key); };

        aige::WebViewHost webView(mainWindow);
        g_webView = &webView;

        // Play mode: the scene is snapshotted on Play and restored on Stop, so
        // a playtest (and any inspector edits made during it) never changes
        // the edited scene. Restoring recreates every entity, so entity ids
        // the UI holds go stale — the selection is dropped and the Scene Tree
        // refreshed.
        bool playing = false;
        std::string playSnapshot;
        std::optional<aige::Entity> selected;

        auto postJson = [&webView](const nlohmann::json& message) {
            webView.PostMessageToPage(WidenAscii(message.dump()));
        };
        auto startPlay = [&]() {
            if (playing) {
                return;
            }
            playSnapshot = aige::SerializeScene(registry);
            session.ResetClock();
            playing = true;
            viewportWindow.Focus(); // so WASD works without first clicking the viewport
            postJson(BuildPlayStateJson(true));
        };
        auto stopPlay = [&]() {
            if (!playing) {
                return;
            }
            aige::DeserializeScene(registry, playSnapshot);
            playSnapshot.clear();
            playing = false;
            selected.reset();
            postJson(BuildPlayStateJson(false));
            postJson(BuildSceneJson(registry));
            postJson({{"type", "inspector:clear"}});
        };

        webView.SetBounds(0, 0, clientWidth, clientHeight);
        webView.NavigateToVirtualHost(kVirtualHostName, WidenAscii(AIGE_EDITOR_UI_DIST_DIR), L"index.html");
        webView.SetMessageHandler([&](std::wstring json) {
            std::string narrow = NarrowAscii(json);
            try {
                auto message = nlohmann::json::parse(narrow);
                std::string type = message.value("type", "");
                if (type == "viewport:bounds") {
                    int x = message.value("x", 0);
                    int y = message.value("y", 0);
                    int width = message.value("width", 0);
                    int height = message.value("height", 0);
                    if (width > 0 && height > 0) {
                        viewportWindow.SetBounds(x, y, width, height);
                    }
                } else if (type == "scene:request") {
                    postJson(BuildSceneJson(registry));
                    postJson(BuildPlayStateJson(playing));
                } else if (type == "entity:select") {
                    auto entity = static_cast<aige::Entity>(message.value("id", 0u));
                    if (registry.valid(entity)) {
                        selected = entity;
                        postJson(BuildInspectorJson(registry, entity));
                    }
                } else if (type == "play:start") {
                    startPlay();
                } else if (type == "play:stop") {
                    stopPlay();
                } else if (type == "inspector:setField") {
                    auto entity = static_cast<aige::Entity>(message.value("id", 0u));
                    if (registry.valid(entity)) {
                        ApplySetField(registry, entity, message.value("component", ""), message.value("field", ""),
                                      message.value("value", 0.0f));
                    }
                }
            } catch (const nlohmann::json::exception&) {
                // Malformed/unrecognized message — ignore rather than crash the editor.
            }
        });

        ShowWindow(mainWindow, SW_SHOW);
        UpdateWindow(mainWindow);

        using Clock = std::chrono::steady_clock;
        constexpr double kInspectorRefreshSeconds = 0.1;
        auto previousTime = Clock::now();
        double sinceInspectorRefresh = 0.0;

        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running) {
                break;
            }

            auto currentTime = Clock::now();
            double frameSeconds = std::chrono::duration<double>(currentTime - previousTime).count();
            previousTime = currentTime;

            if (playing) {
                if (viewportWindow.IsKeyDown(aige::Key::Escape)) {
                    stopPlay();
                } else {
                    session.Advance(frameSeconds, isKeyDown);

                    // Live values for the selected entity while it moves.
                    sinceInspectorRefresh += frameSeconds;
                    if (sinceInspectorRefresh >= kInspectorRefreshSeconds) {
                        sinceInspectorRefresh = 0.0;
                        if (selected && registry.valid(*selected)) {
                            postJson(BuildInspectorJson(registry, *selected));
                        }
                    }
                }
            } else {
                // Edit mode runs no simulation, only the projection, so
                // inspector edits to WorldTransform2D show up immediately.
                aige::UpdateWorldToScreen(registry, iso);
            }

            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
                renderer.Render(cmd, registry);
                rhi.EndFrame();
            }
        }
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Fatal error", MB_ICONERROR);
        return 1;
    }
    return 0;
}
