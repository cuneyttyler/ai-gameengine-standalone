include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# JSON for the reflection schema, the editor's IPC and the AI service bridge.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(aige_platform STATIC
    platform/Window.h
    platform/Window.cpp
)
target_include_directories(aige_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aige_platform PUBLIC glfw)
target_compile_features(aige_platform PUBLIC cxx_std_20)

add_subdirectory(rhi)
add_subdirectory(ecs)
add_subdirectory(reflection)
add_subdirectory(physics2d)
add_subdirectory(renderer2d)
add_subdirectory(scripting)
add_subdirectory(audio)
add_subdirectory(assets)
add_subdirectory(gameplay)
add_subdirectory(editing)
add_subdirectory(ai_bridge)
add_subdirectory(editor)
