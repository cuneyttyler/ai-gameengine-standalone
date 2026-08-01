#include <gtest/gtest.h>

#include "platform/Window.h"
#include "rhi/VulkanRHI.h"

TEST(VulkanRHITest, InitializesAndRunsFrameLifecycleWithoutThrowing) {
    aige::Window window({});
    EXPECT_NO_THROW({
        aige::VulkanRHI rhi(window);
        for (int i = 0; i < 5; ++i) {
            window.PollEvents();
            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                rhi.EndFrame();
            }
        }
    });
}
