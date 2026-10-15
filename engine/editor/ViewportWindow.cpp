#include "editor/ViewportWindow.h"

#include <stdexcept>

namespace aige {

namespace {

const wchar_t* kClassName = L"AIGEViewportWindow";

void EnsureClassRegistered(HINSTANCE hinstance) {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC; // conventional for a window a GPU API renders into directly
    wc.lpfnWndProc = &ViewportWindow::WndProc;
    wc.hInstance = hinstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we render every pixel; avoid GDI erase-flicker
    wc.lpszClassName = kClassName;

    if (!RegisterClassExW(&wc)) {
        throw std::runtime_error("Failed to register ViewportWindow class");
    }
}

int ToVirtualKey(Key key) {
    switch (key) {
        case Key::W: return 'W';
        case Key::A: return 'A';
        case Key::S: return 'S';
        case Key::D: return 'D';
        case Key::Up: return VK_UP;
        case Key::Down: return VK_DOWN;
        case Key::Left: return VK_LEFT;
        case Key::Right: return VK_RIGHT;
        case Key::Escape: return VK_ESCAPE;
        case Key::F5: return VK_F5;
    }
    return 0;
}

} // namespace

ViewportWindow::ViewportWindow(HWND parent, HINSTANCE hinstance, int x, int y, int width, int height)
    : hinstance_(hinstance) {
    EnsureClassRegistered(hinstance);

    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, x, y, width, height, parent, nullptr,
                             hinstance, this);
    if (hwnd_ == nullptr) {
        throw std::runtime_error("Failed to create ViewportWindow");
    }
}

ViewportWindow::~ViewportWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void ViewportWindow::GetFramebufferSize(int& width, int& height) const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

void ViewportWindow::SetResizeCallback(std::function<void(int, int)> callback) {
    resizeCallback_ = std::move(callback);
}

void ViewportWindow::SetBounds(int x, int y, int width, int height) {
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool ViewportWindow::IsKeyDown(Key key) const {
    return keyDown_[ToVirtualKey(key)];
}

LRESULT CALLBACK ViewportWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* self = reinterpret_cast<ViewportWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE: {
            if (self != nullptr && self->resizeCallback_) {
                int width = LOWORD(lparam);
                int height = HIWORD(lparam);
                self->resizeCallback_(width, height);
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1; // we render every pixel ourselves; avoid GDI erase-flicker
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            SetFocus(hwnd); // plain child windows don't take focus on click by themselves
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (self != nullptr && wparam < self->keyDown_.size()) {
                self->keyDown_[wparam] = (msg == WM_KEYDOWN);
            }
            return 0;
        case WM_KILLFOCUS:
            if (self != nullptr) {
                self->keyDown_.fill(false);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace aige
