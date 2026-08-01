#include <gtest/gtest.h>

#include "platform/Window.h"
#include "rhi/VulkanRHI.h"

TEST(VulkanRHITest, InitializesAndDrawsFramesWithoutThrowing) {
    aige::Window window({});
    EXPECT_NO_THROW({
        aige::VulkanRHI rhi(window);
        for (int i = 0; i < 5; ++i) {
            window.PollEvents();
            rhi.DrawFrame();
        }
    });
}
