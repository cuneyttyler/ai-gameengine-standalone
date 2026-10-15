find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found; ensure the Vulkan SDK is installed and VULKAN_SDK is set")
endif()

find_program(NPM_EXECUTABLE npm)
if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm not found; Node.js is required to build the editor's Vue frontend (apps/editor/ui)")
endif()

# WIN32: GUI subsystem (wWinMain entry point, no console) — this app is
# Windows-native via WebView2 throughout, unlike apps/sandbox.
add_executable(editor WIN32 main.cpp)
target_link_libraries(editor PRIVATE aige_rhi aige_ecs aige_physics2d aige_renderer2d aige_scripting aige_assets aige_gameplay aige_editor)
target_compile_definitions(editor PRIVATE
    AIGE_EDITOR_SANDBOX_SCENE_PATH="${CMAKE_SOURCE_DIR}/apps/sandbox"
    AIGE_EDITOR_UI_DIST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui/dist"
)

# Vue/Vite frontend (apps/editor/ui) — rebuilt whenever its source or
# package.json changes. `npm install` is safe to re-run every time (no-op
# once node_modules matches package-lock.json).
set(AIGE_EDITOR_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)
file(GLOB_RECURSE AIGE_EDITOR_UI_SOURCES CONFIGURE_DEPENDS ${AIGE_EDITOR_UI_DIR}/src/*)
# On Windows npm is npm.cmd, a batch file, and Visual Studio runs custom
# commands as one batch script. Invoking a batch file from a batch script
# without `call` never returns, so everything after `npm install` would be
# silently skipped.
if(WIN32)
    set(AIGE_NPM call ${NPM_EXECUTABLE})
else()
    set(AIGE_NPM ${NPM_EXECUTABLE})
endif()
add_custom_command(
    OUTPUT ${AIGE_EDITOR_UI_DIR}/dist/index.html
    COMMAND ${AIGE_NPM} install
    COMMAND ${AIGE_NPM} run build
    WORKING_DIRECTORY ${AIGE_EDITOR_UI_DIR}
    DEPENDS ${AIGE_EDITOR_UI_SOURCES} ${AIGE_EDITOR_UI_DIR}/package.json ${AIGE_EDITOR_UI_DIR}/index.html
    COMMENT "Building editor Vue frontend (apps/editor/ui)"
    VERBATIM
)
add_custom_target(aige_editor_ui DEPENDS ${AIGE_EDITOR_UI_DIR}/dist/index.html)
add_dependencies(editor aige_editor_ui)

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
