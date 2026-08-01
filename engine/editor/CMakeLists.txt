include(FetchContent)

# Dear ImGui has no CMakeLists.txt of its own (it expects the embedding
# application to compile it directly into its build) — same situation as
# Lua, so we glob its sources and the GLFW+Vulkan backends ourselves.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# Vulkan function loading goes through volk (no vulkan-1.lib link, technical.md
# §8) rather than ImGui's own prototypes.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC glfw volk)
target_compile_features(imgui PUBLIC cxx_std_20)

add_library(aige_editor STATIC
    ImGuiLayer.h
    ImGuiLayer.cpp
)
target_include_directories(aige_editor PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_editor PUBLIC aige_platform aige_rhi imgui)
target_compile_features(aige_editor PUBLIC cxx_std_20)
