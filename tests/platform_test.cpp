#include <gtest/gtest.h>

#include "platform/Window.h"

TEST(WindowTest, ConstructAndDestroyDoesNotThrow) {
    aige::Window window({});
    EXPECT_FALSE(window.ShouldClose());
    EXPECT_NE(window.NativeHandle(), nullptr);
}

TEST(WindowTest, SupportsMultipleSequentialWindows) {
    {
        aige::Window first({});
        EXPECT_FALSE(first.ShouldClose());
    }
    aige::Window second({});
    EXPECT_FALSE(second.ShouldClose());
}
