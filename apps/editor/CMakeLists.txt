find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found; ensure the Vulkan SDK is installed and VULKAN_SDK is set")
endif()

add_executable(editor main.cpp)
target_link_libraries(editor PRIVATE
    aige_platform aige_rhi aige_ecs aige_physics2d aige_renderer2d
    aige_scripting aige_assets aige_editor
)
target_compile_definitions(editor PRIVATE AIGE_EDITOR_SANDBOX_SCENE_PATH="${CMAKE_SOURCE_DIR}/apps/sandbox")

set(AIGE_SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/renderer2d/shaders/sprite.vert
    ${CMAKE_SOURCE_DIR}/engine/renderer2d/shaders/sprite.frag
)

add_custom_command(
    TARGET editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:editor>/shaders"
    COMMENT "Compiling renderer2d sprite shaders"
)
foreach(SHADER ${AIGE_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(
        TARGET editor POST_BUILD
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o "$<TARGET_FILE_DIR:editor>/shaders/${SHADER_NAME}.spv"
        VERBATIM
    )
endforeach()
