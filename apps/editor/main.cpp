// Editor host: owns the windows and the frame loop, and forwards IPC and
// viewport input to EditorApp. The Vue frontend (apps/editor/ui) hosts every
// panel; the native Vulkan viewport is positioned "on top" of the WebView2
// surface at whatever rect the Vue app's viewport placeholder reports
// ("hole punching", docs/technical.md §4). The WebView2 control always
// fills the client area; only the viewport window's bounds are dynamic.
#include "EditorApp.h"

#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "editor/ViewportWindow.h"
#include "editor/WebViewHost.h"
#include "gameplay/BootstrapLevel.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"
#include "scripting/LuaVM.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <combaseapi.h>

#include <chrono>
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
aige::EditorApp* g_app = nullptr;

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

        // lua must outlive registry: LuaScript components hold
        // sol::environments referencing lua's state.
        aige::LuaVM lua;
        aige::Registry registry;
        const std::string sandboxDir = AIGE_EDITOR_SANDBOX_SCENE_PATH;
        const std::string scenePath = sandboxDir + "/scene_save.aige";
        if (!aige::LoadScene(registry, scenePath)) {
            aige::BuildBootstrapScene(registry, sandboxDir + "/scripts/npc_wander.lua");
        }

        aige::WebViewHost webView(mainWindow);
        g_webView = &webView;

        aige::EditorApp app(mainWindow, viewportWindow, registry, lua, scenePath,
                            [&webView](const nlohmann::json& message) {
                                webView.PostMessageToPage(WidenAscii(message.dump()));
                            });
        g_app = &app;

        viewportWindow.SetMouseCallback([&app](const aige::ViewportMouseEvent& event) { app.OnViewportMouse(event); });
        viewportWindow.SetKeyDownCallback(
            [&app](int virtualKey, bool ctrl, bool shift) { app.OnViewportKey(virtualKey, ctrl, shift); });

        webView.SetBounds(0, 0, clientWidth, clientHeight);
        webView.NavigateToVirtualHost(kVirtualHostName, WidenAscii(AIGE_EDITOR_UI_DIST_DIR), L"index.html");
        webView.SetMessageHandler([&app](std::wstring json) {
            try {
                app.OnWebMessage(nlohmann::json::parse(NarrowAscii(json)));
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

            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.BeginSwapchainPass();
                renderer.Render(cmd, registry, app.Camera(), app.BuildOverlay());
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
