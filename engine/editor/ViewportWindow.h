#pragma once

#include "editor/Occlusion.h"
#include "platform/Input.h"

#include <windows.h>

#include <array>
#include <functional>
#include <vector>

namespace aige {

struct ViewportMouseEvent {
    enum class Type { Down, Up, Move, Wheel, Leave };
    enum class Button { None, Left, Right, Middle };

    Type type = Type::Move;
    Button button = Button::None;
    float x = 0.0f; // viewport client pixels
    float y = 0.0f;
    float wheelSteps = 0.0f; // Wheel only: +1 per notch away from the user
    bool ctrl = false;
    bool shift = false;
};

// A raw Win32 WS_CHILD window used as the render target for the editor's
// native Vulkan viewport — a sibling of the WebView2 control that hosts
// everything else (see WebViewHost). Deliberately not aige::Window (GLFW):
// this only makes sense as a child of an existing parent HWND, which GLFW
// doesn't support creating, and this whole app is Windows-specific via
// WebView2 anyway (see docs/project/technical.md's editor architecture notes).
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

    // Cuts these rects (window-local pixels) out of the window, so the web UI
    // under it shows through there: open menus and dialogs (editor/Occlusion).
    // Empty restores the whole window.
    void SetOcclusion(const std::vector<PixelRect>& rects);

    // Keyboard state as seen by this window only: keys pressed while the
    // WebView2 UI has focus never reach it, so typing in an inspector field
    // can't move the player. Clicking the viewport focuses it; losing focus
    // releases every key, so nothing sticks "held" after alt-tabbing away.
    bool IsKeyDown(Key key) const;
    float PadAxisValue(PadAxis axis) const; // the first gamepad (XInput), 12.3
    bool HasFocus() const { return GetFocus() == hwnd_; }
    void Focus() { SetFocus(hwnd_); }

    // Mouse input over the viewport. The window captures the mouse while a
    // button is held, so drags keep reporting even outside its bounds.
    void SetMouseCallback(std::function<void(const ViewportMouseEvent&)> callback);

    // Every WM_KEYDOWN (including auto-repeat) with its Win32 virtual-key
    // code — for editor shortcuts, as opposed to IsKeyDown's held-state
    // polling used by gameplay.
    void SetKeyDownCallback(std::function<void(int virtualKey, bool ctrl, bool shift)> callback);
    // Every typed character (WM_CHAR), whatever keys the layout needs for it:
    // '[' is AltGr+8 on some keyboards.
    void SetCharCallback(std::function<void(wchar_t character)> callback);

    // The mouse pointer over the viewport (development_plan.md 5.6), so it
    // says what a click will do: select, move, paint or place, erase.
    enum class Cursor { Arrow, Move, Brush, Erase, Pan, Select };
    void SetCursor(Cursor cursor);

    // Public so the free function registering the window class (which needs
    // a plain WNDPROC pointer) can reference it.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    HWND hwnd_ = nullptr;
    HINSTANCE hinstance_ = nullptr;
    std::function<void(int, int)> resizeCallback_;
    std::function<void(const ViewportMouseEvent&)> mouseCallback_;
    std::function<void(int, bool, bool)> keyDownCallback_;
    std::function<void(wchar_t)> charCallback_;
    std::array<bool, 256> keyDown_{}; // indexed by Win32 virtual-key code
    int buttonsHeld_ = 0;
    HCURSOR cursor_ = nullptr;
    bool trackingLeave_ = false;
};

} // namespace aige
