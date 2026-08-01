#include "platform/Window.h"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace aige {

namespace {
int g_windowCount = 0;
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
}

Window::~Window() {
    glfwDestroyWindow(handle_);
    if (--g_windowCount == 0) {
        glfwTerminate();
    }
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(handle_) != 0;
}

void Window::PollEvents() {
    glfwPollEvents();
}

GLFWwindow* Window::NativeHandle() const {
    return handle_;
}

} // namespace aige
