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
        case Key::F3: return VK_F3;
        case Key::F5: return VK_F5;
        case Key::Space: return VK_SPACE;
        case Key::E: return 'E';
        case Key::Enter: return VK_RETURN;
        case Key::Num1: return '1';
        case Key::Num2: return '2';
        case Key::Num3: return '3';
        case Key::Num4: return '4';
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

void ViewportWindow::SetOcclusion(const std::vector<PixelRect>& rects) {
    if (rects.empty()) {
        SetWindowRgn(hwnd_, nullptr, TRUE);
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    HRGN region = CreateRectRgn(0, 0, client.right, client.bottom);
    for (const auto& r : rects) {
        HRGN hole = CreateRectRgn(r.x, r.y, r.x + r.w, r.y + r.h);
        CombineRgn(region, region, hole, RGN_DIFF);
        DeleteObject(hole);
    }
    SetWindowRgn(hwnd_, region, TRUE); // the system owns `region` now
}

bool ViewportWindow::IsKeyDown(Key key) const {
    return keyDown_[ToVirtualKey(key)];
}

void ViewportWindow::SetMouseCallback(std::function<void(const ViewportMouseEvent&)> callback) {
    mouseCallback_ = std::move(callback);
}

void ViewportWindow::SetKeyDownCallback(std::function<void(int, bool, bool)> callback) {
    keyDownCallback_ = std::move(callback);
}

void ViewportWindow::SetCharCallback(std::function<void(wchar_t)> callback) {
    charCallback_ = std::move(callback);
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
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE: {
            if (self == nullptr) {
                return 0;
            }
            ViewportMouseEvent event;
            event.x = static_cast<float>(static_cast<short>(LOWORD(lparam)));
            event.y = static_cast<float>(static_cast<short>(HIWORD(lparam)));
            event.ctrl = (wparam & MK_CONTROL) != 0;
            event.shift = (wparam & MK_SHIFT) != 0;

            if (msg == WM_MOUSEMOVE) {
                event.type = ViewportMouseEvent::Type::Move;
                if (!self->trackingLeave_) {
                    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                    self->trackingLeave_ = TrackMouseEvent(&track) != FALSE;
                }
            } else {
                bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
                event.type = down ? ViewportMouseEvent::Type::Down : ViewportMouseEvent::Type::Up;
                if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
                    event.button = ViewportMouseEvent::Button::Left;
                } else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
                    event.button = ViewportMouseEvent::Button::Right;
                } else {
                    event.button = ViewportMouseEvent::Button::Middle;
                }
                if (down) {
                    SetFocus(hwnd); // plain child windows don't take focus on click by themselves
                    if (self->buttonsHeld_++ == 0) {
                        SetCapture(hwnd);
                    }
                } else if (self->buttonsHeld_ > 0 && --self->buttonsHeld_ == 0) {
                    ReleaseCapture();
                }
            }
            if (self->mouseCallback_) {
                self->mouseCallback_(event);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            // Capture lost mid-drag (alt-tab, a dialog): the matching button-up
            // will never arrive, so report a button-less Up that ends any drag.
            if (self != nullptr && self->buttonsHeld_ > 0) {
                self->buttonsHeld_ = 0;
                if (self->mouseCallback_) {
                    ViewportMouseEvent event;
                    event.type = ViewportMouseEvent::Type::Up;
                    self->mouseCallback_(event);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (self != nullptr) {
                self->trackingLeave_ = false;
                if (self->mouseCallback_) {
                    ViewportMouseEvent event;
                    event.type = ViewportMouseEvent::Type::Leave;
                    self->mouseCallback_(event);
                }
            }
            return 0;
        case WM_MOUSEWHEEL: {
            if (self != nullptr && self->mouseCallback_) {
                POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
                ScreenToClient(hwnd, &point); // wheel coordinates arrive in screen space
                ViewportMouseEvent event;
                event.type = ViewportMouseEvent::Type::Wheel;
                event.x = static_cast<float>(point.x);
                event.y = static_cast<float>(point.y);
                event.wheelSteps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
                event.ctrl = (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0;
                event.shift = (GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT) != 0;
                self->mouseCallback_(event);
            }
            return 0;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (self != nullptr && wparam < self->keyDown_.size()) {
                self->keyDown_[wparam] = (msg == WM_KEYDOWN);
                if (msg == WM_KEYDOWN && self->keyDownCallback_) {
                    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    self->keyDownCallback_(static_cast<int>(wparam), ctrl, shift);
                }
            }
            return 0;
        case WM_CHAR:
            if (self != nullptr && self->charCallback_) {
                self->charCallback_(static_cast<wchar_t>(wparam));
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
