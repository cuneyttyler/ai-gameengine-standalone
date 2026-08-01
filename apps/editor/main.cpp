// Milestone 1.1a de-risking spike: confirm a WebView2 control and a native
// Vulkan-rendered child window can coexist as siblings in one top-level
// window without fighting each other (z-order, paint, resize). No Vue
// frontend and no IPC yet — that's 1.1b/1.1c once this is confirmed.
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "editor/ViewportWindow.h"
#include "editor/WebViewHost.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"

#include <windows.h>

#include <combaseapi.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

#ifndef AIGE_EDITOR_SANDBOX_SCENE_PATH
#define AIGE_EDITOR_SANDBOX_SCENE_PATH "."
#endif

namespace {

const wchar_t* kMainWindowClass = L"AIGEEditorMainWindow";

// Fraction of the client width given to the native viewport (right side);
// the rest goes to the WebView2 control (left side). Hardcoded for this
// spike — 1.1b replaces this with bounds reported by the Vue viewport
// placeholder div over IPC.
constexpr double kViewportFraction = 0.3;

aige::WebViewHost* g_webView = nullptr;
aige::ViewportWindow* g_viewportWindow = nullptr;

void LayoutChildren(int clientWidth, int clientHeight) {
    int viewportWidth = static_cast<int>(clientWidth * kViewportFraction);
    int webViewWidth = clientWidth - viewportWidth;

    if (g_webView != nullptr) {
        g_webView->SetBounds(0, 0, webViewWidth, clientHeight);
    }
    if (g_viewportWindow != nullptr) {
        g_viewportWindow->SetBounds(webViewWidth, 0, viewportWidth, clientHeight);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_SIZE: {
            LayoutChildren(LOWORD(lparam), HIWORD(lparam));
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void BuildFallbackScene(aige::Registry& registry) {
    for (int i = 0; i < 4; ++i) {
        auto tile = registry.create();
        registry.emplace<aige::ScreenTransform2D>(tile, 150.0f + static_cast<float>(i) * 40.0f, 200.0f,
                                                   static_cast<float>(i));
        registry.emplace<aige::Sprite>(tile, 64.0f, 32.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    auto player = registry.create();
    registry.emplace<aige::ScreenTransform2D>(player, 250.0f, 200.0f, 10.0f);
    registry.emplace<aige::Sprite>(player, 32.0f, 48.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
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

        HWND mainWindow =
            CreateWindowExW(0, kMainWindowClass, L"AI-Native Game Engine - Editor (1.1a spike)",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900, nullptr, nullptr,
                             hinstance, nullptr);
        if (mainWindow == nullptr) {
            throw std::runtime_error("Failed to create main window");
        }

        aige::WebViewHost webView(mainWindow);
        g_webView = &webView;
        webView.NavigateToHtml(
            LR"(<html><body style="background:#1e1e2e;color:#eee;font-family:sans-serif;padding:24px;">
<h1>WebView2 chrome (placeholder)</h1>
<p>This region will become the Vue app (milestone 1.1b). The panel to the
right is a native Vulkan viewport rendering directly into its own child
window — not a texture displayed here.</p>
</body></html>)");

        RECT clientRect{};
        GetClientRect(mainWindow, &clientRect);
        int clientWidth = clientRect.right - clientRect.left;
        int clientHeight = clientRect.bottom - clientRect.top;
        int viewportWidth = static_cast<int>(clientWidth * kViewportFraction);
        int webViewWidth = clientWidth - viewportWidth;

        aige::ViewportWindow viewportWindow(mainWindow, hinstance, webViewWidth, 0, viewportWidth, clientHeight);
        g_viewportWindow = &viewportWindow;

        aige::VulkanRHI rhi(
            viewportWindow.Handle(), viewportWindow.Instance(),
            [&viewportWindow](int& w, int& h) { viewportWindow.GetFramebufferSize(w, h); },
            [&viewportWindow](std::function<void(int, int)> cb) {
                viewportWindow.SetResizeCallback(std::move(cb));
            });
        aige::Renderer2D renderer(rhi, rhi.RenderPass());

        aige::Registry registry;
        std::string sandboxScenePath = std::string(AIGE_EDITOR_SANDBOX_SCENE_PATH) + "/scene_save.aige";
        if (!aige::LoadScene(registry, sandboxScenePath)) {
            BuildFallbackScene(registry);
        }

        ShowWindow(mainWindow, SW_SHOW);
        UpdateWindow(mainWindow);

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
