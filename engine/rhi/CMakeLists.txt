include(FetchContent)

find_package(Vulkan REQUIRED)

FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.3.295
)
FetchContent_MakeAvailable(volk)

add_library(aige_rhi STATIC
    VulkanRHI.h
    VulkanRHI.cpp
)
target_include_directories(aige_rhi PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_rhi PUBLIC aige_platform glfw volk)
target_compile_features(aige_rhi PUBLIC cxx_std_20)
