#include "editor/ViewportWindow.h"

#include <Xinput.h>

#include <cstdint>

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

// The eraser pointer: a tilted eraser block, its tip (the hotspot) at the
// bottom left. Drawn in code, so there's no resource file to ship.
HCURSOR CreateEraserCursor() {
    constexpr int size = 32;
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size; // top-down
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (color == nullptr || bits == nullptr) {
        return LoadCursorW(nullptr, IDC_ARROW);
    }
    auto* pixels = static_cast<uint32_t*>(bits);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // In a frame turned 45°: u along the eraser, v across it.
            const float u = (static_cast<float>(x) - 4.0f + (27.0f - static_cast<float>(y))) * 0.7071f;
            const float v = (static_cast<float>(x) - 4.0f - (27.0f - static_cast<float>(y))) * 0.7071f;
            uint32_t pixel = 0;
            if (u >= 0.0f && u <= 26.0f && v >= -6.0f && v <= 6.0f) {
                const bool edge = u < 1.2f || u > 24.8f || v < -4.8f || v > 4.8f;
                const bool rubber = u < 9.0f; // the pink end that erases
                pixel = edge ? 0xFF1E1E28u : rubber ? 0xFFF08CA0u : 0xFFF0F0F5u;
            }
            pixels[y * size + x] = pixel;
        }
    }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = FALSE;
    info.xHotspot = 4;
    info.yHotspot = 27;
    info.hbmMask = mask;
    info.hbmColor = color;
    HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return cursor != nullptr ? cursor : LoadCursorW(nullptr, IDC_ARROW);
}

int ToVirtualKey(Key key) {
    if (key >= Key::A && key <= Key::Z) return 'A' + (static_cast<int>(key) - static_cast<int>(Key::A));
    if (key >= Key::Num0 && key <= Key::Num9) return '0' + (static_cast<int>(key) - static_cast<int>(Key::Num0));
    if (key >= Key::F1 && key <= Key::F12) return VK_F1 + (static_cast<int>(key) - static_cast<int>(Key::F1));
    switch (key) {
        case Key::Up: return VK_UP;
        case Key::Down: return VK_DOWN;
        case Key::Left: return VK_LEFT;
        case Key::Right: return VK_RIGHT;
        case Key::Space: return VK_SPACE;
        case Key::Enter: return VK_RETURN;
        case Key::Escape: return VK_ESCAPE;
        case Key::Tab: return VK_TAB;
        case Key::Backspace: return VK_BACK;
        // WM_KEYDOWN reports the generic codes for these.
        case Key::LeftShift:
        case Key::RightShift: return VK_SHIFT;
        case Key::LeftCtrl:
        case Key::RightCtrl: return VK_CONTROL;
        case Key::LeftAlt:
        case Key::RightAlt: return VK_MENU;
        default: return 0;
    }
}

// The first XInput gamepad, if one is connected (development_plan.md 12.3).
bool PadState(XINPUT_STATE& state) {
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        if (XInputGetState(i, &state) == ERROR_SUCCESS) return true;
    }
    return false;
}

WORD ToXInputButton(Key key) {
    switch (key) {
        case Key::PadA: return XINPUT_GAMEPAD_A;
        case Key::PadB: return XINPUT_GAMEPAD_B;
        case Key::PadX: return XINPUT_GAMEPAD_X;
        case Key::PadY: return XINPUT_GAMEPAD_Y;
        case Key::PadLB: return XINPUT_GAMEPAD_LEFT_SHOULDER;
        case Key::PadRB: return XINPUT_GAMEPAD_RIGHT_SHOULDER;
        case Key::PadBack: return XINPUT_GAMEPAD_BACK;
        case Key::PadStart: return XINPUT_GAMEPAD_START;
        case Key::PadLStick: return XINPUT_GAMEPAD_LEFT_THUMB;
        case Key::PadRStick: return XINPUT_GAMEPAD_RIGHT_THUMB;
        case Key::PadUp: return XINPUT_GAMEPAD_DPAD_UP;
        case Key::PadDown: return XINPUT_GAMEPAD_DPAD_DOWN;
        case Key::PadLeft: return XINPUT_GAMEPAD_DPAD_LEFT;
        case Key::PadRight: return XINPUT_GAMEPAD_DPAD_RIGHT;
        default: return 0;
    }
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
    if (!HasFocus()) return false; // the UI has the keyboard, mouse and gamepad aren't for the game either
    if (IsMouseKey(key)) {
        const int vk = key == Key::MouseLeft ? VK_LBUTTON : key == Key::MouseRight ? VK_RBUTTON : VK_MBUTTON;
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
    if (IsPadKey(key)) {
        XINPUT_STATE state{};
        if (!PadState(state)) return false;
        if (key == Key::PadLT) return state.Gamepad.bLeftTrigger > 128;
        if (key == Key::PadRT) return state.Gamepad.bRightTrigger > 128;
        return (state.Gamepad.wButtons & ToXInputButton(key)) != 0;
    }
    const int vk = ToVirtualKey(key);
    return vk != 0 && keyDown_[vk];
}

float ViewportWindow::PadAxisValue(PadAxis axis) const {
    XINPUT_STATE state{};
    if (!HasFocus() || !PadState(state)) return 0.0f;
    const auto& pad = state.Gamepad;
    switch (axis) {
        case PadAxis::LeftX: return pad.sThumbLX / 32767.0f;
        case PadAxis::LeftY: return -pad.sThumbLY / 32767.0f; // XInput's up is positive; ours is down
        case PadAxis::RightX: return pad.sThumbRX / 32767.0f;
        case PadAxis::RightY: return -pad.sThumbRY / 32767.0f;
        case PadAxis::LeftTrigger: return pad.bLeftTrigger / 255.0f;
        case PadAxis::RightTrigger: return pad.bRightTrigger / 255.0f;
    }
    return 0.0f;
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

void ViewportWindow::SetCursor(Cursor cursor) {
    static const HCURSOR arrow = LoadCursorW(nullptr, IDC_ARROW);
    static const HCURSOR move = LoadCursorW(nullptr, IDC_SIZEALL);
    static const HCURSOR brush = LoadCursorW(nullptr, IDC_CROSS);
    static const HCURSOR pan = LoadCursorW(nullptr, IDC_SIZEALL);
    static const HCURSOR select = LoadCursorW(nullptr, IDC_HAND);
    static const HCURSOR eraser = CreateEraserCursor();
    HCURSOR next = cursor == Cursor::Move     ? move
                   : cursor == Cursor::Brush  ? brush
                   : cursor == Cursor::Erase  ? eraser
                   : cursor == Cursor::Pan    ? pan
                   : cursor == Cursor::Select ? select
                                              : arrow;
    if (next == cursor_) {
        return;
    }
    cursor_ = next;
    // Apply now if the pointer is over us (WM_SETCURSOR covers later moves).
    POINT point{};
    GetCursorPos(&point);
    if (WindowFromPoint(point) == hwnd_) {
        ::SetCursor(cursor_);
    }
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
        case WM_SYSKEYDOWN:
            if (self != nullptr && wparam < self->keyDown_.size() && wparam != VK_MENU && wparam != VK_F4 &&
                self->keyDownCallback_) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                self->keyDownCallback_(static_cast<int>(wparam), ctrl, shift);
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
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
        case WM_SETCURSOR:
            if (self != nullptr && self->cursor_ != nullptr && LOWORD(lparam) == HTCLIENT) {
                ::SetCursor(self->cursor_);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
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
