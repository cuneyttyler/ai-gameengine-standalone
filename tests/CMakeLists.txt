include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(aige_tests
    platform_test.cpp
    rhi_test.cpp
    renderer2d_test.cpp
    collision_test.cpp
    scripting_test.cpp
    scene_io_test.cpp
    gameplay_test.cpp
)
target_link_libraries(aige_tests PRIVATE aige_platform aige_rhi aige_ecs aige_physics2d aige_renderer2d aige_scripting aige_assets aige_gameplay gtest_main)

find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found; ensure the Vulkan SDK is installed and VULKAN_SDK is set")
endif()

set(AIGE_SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/renderer2d/shaders/sprite.vert
    ${CMAKE_SOURCE_DIR}/engine/renderer2d/shaders/sprite.frag
)

add_custom_command(
    TARGET aige_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:aige_tests>/shaders"
    COMMENT "Compiling renderer2d sprite shaders for tests"
)
foreach(SHADER ${AIGE_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(
        TARGET aige_tests POST_BUILD
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o "$<TARGET_FILE_DIR:aige_tests>/shaders/${SHADER_NAME}.spv"
        VERBATIM
    )
endforeach()

include(GoogleTest)
gtest_discover_tests(aige_tests)
