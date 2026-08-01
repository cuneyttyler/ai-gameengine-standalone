find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found; ensure the Vulkan SDK is installed and VULKAN_SDK is set")
endif()

add_executable(sandbox main.cpp)
target_link_libraries(sandbox PRIVATE aige_platform aige_rhi)

set(AIGE_SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/engine/rhi/shaders/triangle.vert
    ${CMAKE_SOURCE_DIR}/engine/rhi/shaders/triangle.frag
)

add_custom_command(
    TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sandbox>/shaders"
    COMMENT "Compiling RHI bootstrap shaders"
)

foreach(SHADER ${AIGE_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(
        TARGET sandbox POST_BUILD
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o "$<TARGET_FILE_DIR:sandbox>/shaders/${SHADER_NAME}.spv"
        VERBATIM
    )
endforeach()
