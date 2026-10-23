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
    // Typed characters (the debug console, 24), as Unicode code points.
    void SetCharCallback(std::function<void(unsigned)> callback);

    // Keys, mouse buttons and the first gamepad's buttons (development_plan.md 12.3).
    bool IsKeyDown(Key key) const;
    float PadAxisValue(PadAxis axis) const;
    // The whole primary monitor, or back to the window it was (12.7).
    void SetFullscreen(bool fullscreen);
    bool IsFocused() const;

    GLFWwindow* NativeHandle() const;

private:
    static void FramebufferResizeTrampoline(GLFWwindow* handle, int width, int height);
    static void CharTrampoline(GLFWwindow* handle, unsigned codepoint);

    GLFWwindow* handle_ = nullptr;
    int windowedX_ = 100, windowedY_ = 100, windowedWidth_ = 1280, windowedHeight_ = 720;
    std::function<void(int, int)> resizeCallback_;
    std::function<void(unsigned)> charCallback_;
};

} // namespace aige
