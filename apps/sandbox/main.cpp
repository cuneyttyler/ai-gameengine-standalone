#include "platform/Window.h"

#include <iostream>

int main() {
    try {
        aige::Window window({});
        while (!window.ShouldClose()) {
            window.PollEvents();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
