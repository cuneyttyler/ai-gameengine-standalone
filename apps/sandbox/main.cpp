#include "platform/Window.h"
#include "rhi/VulkanRHI.h"

#include <iostream>

int main() {
    try {
        aige::Window window({});
        aige::VulkanRHI rhi(window);
        while (!window.ShouldClose()) {
            window.PollEvents();
            rhi.DrawFrame();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
