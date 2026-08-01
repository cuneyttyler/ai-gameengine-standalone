#pragma once

#include <windows.h>

#include <functional>

namespace aige {

// A raw Win32 WS_CHILD window used as the render target for the editor's
// native Vulkan viewport — a sibling of the WebView2 control that hosts
// everything else (see WebViewHost). Deliberately not aige::Window (GLFW):
// this only makes sense as a child of an existing parent HWND, which GLFW
// doesn't support creating, and this whole app is Windows-specific via
// WebView2 anyway (see docs/technical.md's editor architecture notes).
class ViewportWindow {
public:
    ViewportWindow(HWND parent, HINSTANCE hinstance, int x, int y, int width, int height);
    ~ViewportWindow();

    ViewportWindow(const ViewportWindow&) = delete;
    ViewportWindow& operator=(const ViewportWindow&) = delete;

    HWND Handle() const { return hwnd_; }
    HINSTANCE Instance() const { return hinstance_; }

    // Matches the shape VulkanRHI's raw-HWND constructor expects.
    void GetFramebufferSize(int& width, int& height) const;
    void SetResizeCallback(std::function<void(int, int)> callback);

    // Repositions/resizes this child window within its parent — called
    // whenever the host's layout (driven by the Vue viewport placeholder's
    // reported bounds) changes.
    void SetBounds(int x, int y, int width, int height);

    // Public so the free function registering the window class (which needs
    // a plain WNDPROC pointer) can reference it.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    std::function<void(int, int)> resizeCallback_;
};

} // namespace aige
