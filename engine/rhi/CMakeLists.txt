include(FetchContent)

find_package(Vulkan REQUIRED)

FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.3.295
)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
)
FetchContent_MakeAvailable(volk vma)

add_library(aige_rhi STATIC
    ShaderUtil.h
    ShaderUtil.cpp
    VulkanRHI.h
    VulkanRHI.cpp
    VmaImpl.cpp
)
target_include_directories(aige_rhi PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_rhi PUBLIC aige_platform glfw volk VulkanMemoryAllocator)
target_compile_features(aige_rhi PUBLIC cxx_std_20)
