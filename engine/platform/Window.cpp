#include "platform/Window.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace aige {

namespace {
int g_windowCount = 0;

int ToGlfwKey(Key key) {
    switch (key) {
        case Key::W:
            return GLFW_KEY_W;
        case Key::A:
            return GLFW_KEY_A;
        case Key::S:
            return GLFW_KEY_S;
        case Key::D:
            return GLFW_KEY_D;
        case Key::Up:
            return GLFW_KEY_UP;
        case Key::Down:
            return GLFW_KEY_DOWN;
        case Key::Left:
            return GLFW_KEY_LEFT;
        case Key::Right:
            return GLFW_KEY_RIGHT;
        case Key::Escape:
            return GLFW_KEY_ESCAPE;
        case Key::F3:
            return GLFW_KEY_F3;
        case Key::F5:
            return GLFW_KEY_F5;
        case Key::Space:
            return GLFW_KEY_SPACE;
        case Key::E:
            return GLFW_KEY_E;
        case Key::Enter:
            return GLFW_KEY_ENTER;
        case Key::Num1:
            return GLFW_KEY_1;
        case Key::Num2:
            return GLFW_KEY_2;
        case Key::Num3:
            return GLFW_KEY_3;
        case Key::Num4:
            return GLFW_KEY_4;
    }
    return GLFW_KEY_UNKNOWN;
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

bool Window::IsKeyDown(Key key) const {
    return glfwGetKey(handle_, ToGlfwKey(key)) == GLFW_PRESS;
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

} // namespace aige
