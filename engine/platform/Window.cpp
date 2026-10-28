#include "platform/Window.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>

namespace aige {

namespace {
int g_windowCount = 0;

int ToGlfwKey(Key key) {
    if (key >= Key::A && key <= Key::Z) return GLFW_KEY_A + (static_cast<int>(key) - static_cast<int>(Key::A));
    if (key >= Key::Num0 && key <= Key::Num9) return GLFW_KEY_0 + (static_cast<int>(key) - static_cast<int>(Key::Num0));
    if (key >= Key::F1 && key <= Key::F12) return GLFW_KEY_F1 + (static_cast<int>(key) - static_cast<int>(Key::F1));
    switch (key) {
        case Key::Up: return GLFW_KEY_UP;
        case Key::Down: return GLFW_KEY_DOWN;
        case Key::Left: return GLFW_KEY_LEFT;
        case Key::Right: return GLFW_KEY_RIGHT;
        case Key::Space: return GLFW_KEY_SPACE;
        case Key::Enter: return GLFW_KEY_ENTER;
        case Key::Escape: return GLFW_KEY_ESCAPE;
        case Key::Tab: return GLFW_KEY_TAB;
        case Key::Backspace: return GLFW_KEY_BACKSPACE;
        case Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case Key::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case Key::LeftCtrl: return GLFW_KEY_LEFT_CONTROL;
        case Key::RightCtrl: return GLFW_KEY_RIGHT_CONTROL;
        case Key::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case Key::RightAlt: return GLFW_KEY_RIGHT_ALT;
        default: return GLFW_KEY_UNKNOWN;
    }
}

int ToGlfwPadButton(Key key) {
    switch (key) {
        case Key::PadA: return GLFW_GAMEPAD_BUTTON_A;
        case Key::PadB: return GLFW_GAMEPAD_BUTTON_B;
        case Key::PadX: return GLFW_GAMEPAD_BUTTON_X;
        case Key::PadY: return GLFW_GAMEPAD_BUTTON_Y;
        case Key::PadLB: return GLFW_GAMEPAD_BUTTON_LEFT_BUMPER;
        case Key::PadRB: return GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER;
        case Key::PadBack: return GLFW_GAMEPAD_BUTTON_BACK;
        case Key::PadStart: return GLFW_GAMEPAD_BUTTON_START;
        case Key::PadLStick: return GLFW_GAMEPAD_BUTTON_LEFT_THUMB;
        case Key::PadRStick: return GLFW_GAMEPAD_BUTTON_RIGHT_THUMB;
        case Key::PadUp: return GLFW_GAMEPAD_BUTTON_DPAD_UP;
        case Key::PadDown: return GLFW_GAMEPAD_BUTTON_DPAD_DOWN;
        case Key::PadLeft: return GLFW_GAMEPAD_BUTTON_DPAD_LEFT;
        case Key::PadRight: return GLFW_GAMEPAD_BUTTON_DPAD_RIGHT;
        default: return -1;
    }
}

// The first connected gamepad's state, if any.
bool PadState(GLFWgamepadstate& state) {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (glfwJoystickIsGamepad(jid) && glfwGetGamepadState(jid, &state)) return true;
    }
    return false;
}
}

Window::Window(const Desc& desc) {
    if (g_windowCount == 0 && !glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    ++g_windowCount;

    // No GL context: rendering will go through our own Vulkan RHI (technical.md §8).
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    handle_ = glfwCreateWindow(static_cast<int>(desc.width), static_cast<int>(desc.height),
                               desc.title.c_str(), nullptr, nullptr);
    if (!handle_) {
        if (--g_windowCount == 0) {
            glfwTerminate();
        }
        throw std::runtime_error("Failed to create window");
    }

    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, &Window::FramebufferResizeTrampoline);
    glfwSetCharCallback(handle_, &Window::CharTrampoline);
}

Window::~Window() {
    glfwDestroyWindow(handle_);
    if (--g_windowCount == 0) {
        glfwTerminate();
    }
}

void Window::SetIcon(uint32_t width, uint32_t height, const uint8_t* rgba) {
    GLFWimage image{static_cast<int>(width), static_cast<int>(height), const_cast<unsigned char*>(rgba)};
    glfwSetWindowIcon(handle_, 1, &image);
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(handle_) != 0;
}

void Window::PollEvents() {
    glfwPollEvents();
}

void Window::WaitEvents() {
    glfwWaitEvents();
}

void Window::GetFramebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(handle_, &width, &height);
}

void Window::SetResizeCallback(std::function<void(int, int)> callback) {
    resizeCallback_ = std::move(callback);
}

void Window::GetCursorPosition(float& x, float& y) const {
    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(handle_, &cx, &cy);
    int ww = 1, wh = 1, fw = 1, fh = 1;
    glfwGetWindowSize(handle_, &ww, &wh);
    glfwGetFramebufferSize(handle_, &fw, &fh);
    x = static_cast<float>(cx * fw / std::max(1, ww));
    y = static_cast<float>(cy * fh / std::max(1, wh));
}

bool Window::IsKeyDown(Key key) const {
    if (IsMouseKey(key)) {
        const int button = key == Key::MouseLeft    ? GLFW_MOUSE_BUTTON_LEFT
                           : key == Key::MouseRight ? GLFW_MOUSE_BUTTON_RIGHT
                                                    : GLFW_MOUSE_BUTTON_MIDDLE;
        return glfwGetMouseButton(handle_, button) == GLFW_PRESS;
    }
    if (IsPadKey(key)) {
        GLFWgamepadstate state;
        if (!PadState(state)) return false;
        if (key == Key::PadLT) return state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.5f;
        if (key == Key::PadRT) return state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.5f;
        const int button = ToGlfwPadButton(key);
        return button >= 0 && state.buttons[button] == GLFW_PRESS;
    }
    const int code = ToGlfwKey(key);
    return code != GLFW_KEY_UNKNOWN && glfwGetKey(handle_, code) == GLFW_PRESS;
}

void Window::SetFullscreen(bool fullscreen) {
    GLFWmonitor* current = glfwGetWindowMonitor(handle_);
    if (fullscreen == (current != nullptr)) return;
    if (fullscreen) {
        glfwGetWindowPos(handle_, &windowedX_, &windowedY_);
        glfwGetWindowSize(handle_, &windowedWidth_, &windowedHeight_);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (mode) glfwSetWindowMonitor(handle_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(handle_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, 0);
    }
}

float Window::PadAxisValue(PadAxis axis) const {
    GLFWgamepadstate state;
    if (!PadState(state)) return 0.0f;
    switch (axis) {
        case PadAxis::LeftX: return state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
        case PadAxis::LeftY: return state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
        case PadAxis::RightX: return state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
        case PadAxis::RightY: return state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
        // GLFW triggers rest at -1; ours at 0.
        case PadAxis::LeftTrigger: return (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
        case PadAxis::RightTrigger: return (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
    }
    return 0.0f;
}

bool Window::IsFocused() const {
    return glfwGetWindowAttrib(handle_, GLFW_FOCUSED) != 0;
}

void Window::FramebufferResizeTrampoline(GLFWwindow* handle, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    if (self && self->resizeCallback_) {
        self->resizeCallback_(width, height);
    }
}

GLFWwindow* Window::NativeHandle() const {
    return handle_;
}

void Window::SetCharCallback(std::function<void(unsigned)> callback) { charCallback_ = std::move(callback); }

void Window::CharTrampoline(GLFWwindow* handle, unsigned codepoint) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
    if (self && self->charCallback_) self->charCallback_(codepoint);
}

} // namespace aige
