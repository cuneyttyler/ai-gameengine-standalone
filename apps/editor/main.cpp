// Editor host: owns the windows and the frame loop, and forwards IPC and
// viewport input to EditorApp. The Vue frontend (apps/editor/ui) hosts every
// panel; the native Vulkan viewport is positioned "on top" of the WebView2
// surface at whatever rect the Vue app's viewport placeholder reports
// ("hole punching", docs/technical.md §4). The WebView2 control always
// fills the client area; only the viewport window's bounds are dynamic.
#include "EditorApp.h"
#include "Win32Util.h"

#include "assets/AssetDatabase.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "gameplay/EngineReflection.h"
#include "modules/Modules.h"

#include <iostream>
#include "gameplay/Settings.h"
#include "audio/Audio.h"
#include "ecs/Registry.h"
#include "editor/ViewportWindow.h"
#include "editor/WebViewHost.h"
#include "gameplay/BootstrapLevel.h"
#include "renderer2d/Renderer2D.h"
#include "ui/GameUi.h"
#include "rhi/VulkanRHI.h"
#include "scripting/LuaVM.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <combaseapi.h>

#include <chrono>
#include <filesystem>
#include <shellapi.h>
#include <string>

#ifndef AIGE_EDITOR_DEFAULT_PROJECT
#define AIGE_EDITOR_DEFAULT_PROJECT "."
#endif
#ifndef AIGE_EDITOR_AI_SERVICE_DIR
#define AIGE_EDITOR_AI_SERVICE_DIR ""
#endif
#ifndef AIGE_EDITOR_UI_DIST_DIR
#define AIGE_EDITOR_UI_DIST_DIR "."
#endif

namespace {

const wchar_t* kMainWindowClass = L"AIGEEditorMainWindow";
const wchar_t* kVirtualHostName = L"aige-editor.local";
// The open project folder, so the UI can show asset thumbnails
// (apps/editor/ui/src/components/AssetBrowser.vue uses the same name).
const wchar_t* kProjectHostName = L"aige-project.local";

aige::WebViewHost* g_webView = nullptr;
aige::EditorApp* g_app = nullptr;

// `editor.exe [--project] <dir>`; defaults to the repo's demo project.
std::filesystem::path ProjectRootFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::filesystem::path root = aige::Utf8ToWide(AIGE_EDITOR_DEFAULT_PROJECT);
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--project" && i + 1 < argc) {
            root = argv[++i];
        } else if (!arg.empty() && arg[0] != L'-') {
            root = arg;
        }
    }
    LocalFree(argv);
    return root;
}

// Asks about unsaved edits. Returns false if closing should be cancelled.
bool ConfirmClose(HWND hwnd) {
    if (g_app == nullptr || !g_app->IsDirty()) {
        return true;
    }
    int answer = MessageBoxW(hwnd, L"Save changes to the scene before closing?", L"Unsaved changes",
                             MB_YESNOCANCEL | MB_ICONWARNING);
    if (answer == IDCANCEL) {
        return false;
    }
    if (answer == IDYES) {
        g_app->Save();
        return !g_app->IsDirty(); // stay open if the save failed
    }
    return true;
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE: {
            if (g_webView != nullptr) {
                g_webView->SetBounds(0, 0, LOWORD(lparam), HIWORD(lparam));
            }
            return 0;
        }
        case WM_ACTIVATEAPP:
            if (wparam != FALSE && g_app != nullptr) {
                g_app->OnAppActivated(); // pick up assets edited in other programs
            }
            return 0;
        case WM_CLOSE:
            if (ConfirmClose(hwnd)) {
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
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
        aige::RegisterGameUiFonts(renderer);

        aige::Project project{std::filesystem::absolute(ProjectRootFromCommandLine())};
        aige::LoadSettings(aige::EngineReflection(), project.SettingsPath().string());
        aige::RegisterEditingCommands(aige::EngineReflection());
        // The project's Lua modules (4.8), before any scene is read.
        for (const auto& error : aige::LoadModules(aige::EngineReflection(), project.root).errors) {
            std::cerr << "[modules] " << error << std::endl;
        }
        aige::AssetDatabase assets(project);
        assets.Refresh();
        renderer.SetTextureLoader([&assets](const std::string& path) { return assets.LoadTexture(path); });
        aige::AudioEngine audio;

        // lua must outlive registry: LuaScript components hold
        // sol::environments referencing lua's state.
        aige::LuaVM lua;
        aige::Registry registry;
        const std::filesystem::path scenePath = project.MainScenePath();
        if (!aige::LoadScene(registry, scenePath.string())) {
            // A new project: start from the bootstrap level and save it, so
            // the project is complete on disk from the first launch.
            aige::BuildBootstrapScene(registry, "scripts/npc_wander.lua");
            std::filesystem::create_directories(scenePath.parent_path());
            aige::SaveScene(registry, scenePath.string());
        }

        aige::WebViewHost webView(mainWindow);
        g_webView = &webView;

        aige::EditorApp app(mainWindow, viewportWindow, registry, lua, assets, audio, renderer,
                            [&webView](const nlohmann::json& message) {
                                webView.PostMessageToPage(aige::Utf8ToWide(message.dump()));
                            });
        g_app = &app;
        app.StartAi(AIGE_EDITOR_AI_SERVICE_DIR);

        viewportWindow.SetMouseCallback([&app](const aige::ViewportMouseEvent& event) { app.OnViewportMouse(event); });
        viewportWindow.SetKeyDownCallback(
            [&app](int virtualKey, bool ctrl, bool shift) { app.OnViewportKey(virtualKey, ctrl, shift); });

        webView.SetBounds(0, 0, clientWidth, clientHeight);
        webView.MapVirtualHostToFolder(kProjectHostName, project.root.wstring());
        webView.NavigateToVirtualHost(kVirtualHostName, aige::Utf8ToWide(AIGE_EDITOR_UI_DIST_DIR), L"index.html");
        webView.SetMessageHandler([&app](std::wstring json) {
            try {
                app.OnWebMessage(nlohmann::json::parse(aige::WideToUtf8(json)));
            } catch (const nlohmann::json::exception&) {
                // Malformed/unrecognized message — ignore rather than crash the editor.
            }
        });

        ShowWindow(mainWindow, SW_SHOW);
        UpdateWindow(mainWindow);

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();

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
            app.Tick(std::chrono::duration<double>(currentTime - previousTime).count());
            previousTime = currentTime;

            const auto& background = aige::ActiveSettings().renderer;
            rhi.SetClearColor(background.clearR, background.clearG, background.clearB);
            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
                renderer.Render(cmd, registry, app.Camera(), app.BuildOverlay(), app.BuildUi());
                rhi.EndFrame();
            }
        }
        g_app = nullptr;
        g_webView = nullptr;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Fatal error", MB_ICONERROR);
        return 1;
    }
    return 0;
}
