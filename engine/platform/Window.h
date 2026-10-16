#pragma once

#include "platform/Input.h"

#include <cstdint>
#include <functional>
#include <string>

struct GLFWwindow;

namespace aige {

// Thin owned wrapper around the windowing backend (GLFW today) so engine/editor
// code never depends on GLFW directly — swapping the backend later stays local
// to Window.cpp. The RHI still talks to GLFW's Vulkan-bridge calls
// (glfwCreateWindowSurface, glfwGetRequiredInstanceExtensions) directly via
// NativeHandle(), since those are Vulkan/platform interop, not windowing.
class Window {
public:
    struct Desc {
        std::string title = "AI-Native Game Engine";
        uint32_t width = 1280;
        uint32_t height = 720;
    };

    explicit Window(const Desc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ShouldClose() const;
    // The title-bar and taskbar icon, from RGBA pixels (an exported game's icon).
    void SetIcon(uint32_t width, uint32_t height, const uint8_t* rgba);
    void PollEvents();
    void WaitEvents();

    void GetFramebufferSize(int& width, int& height) const;
    void SetResizeCallback(std::function<void(int, int)> callback);

    bool IsKeyDown(Key key) const;
    bool IsFocused() const;

    GLFWwindow* NativeHandle() const;

private:
    static void FramebufferResizeTrampoline(GLFWwindow* handle, int width, int height);

    GLFWwindow* handle_ = nullptr;
    std::function<void(int, int)> resizeCallback_;
};

} // namespace aige
