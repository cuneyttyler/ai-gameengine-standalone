#pragma once

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace aige {

// Thin owned wrapper around the windowing backend (GLFW today) so engine/editor
// code never depends on GLFW directly — swapping the backend later stays local
// to Window.cpp.
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
    void PollEvents();

    GLFWwindow* NativeHandle() const;

private:
    GLFWwindow* handle_ = nullptr;
};

} // namespace aige
