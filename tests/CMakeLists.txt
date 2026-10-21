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
    editing_test.cpp
    assets_test.cpp
    component_table_test.cpp
    combat_test.cpp
    slice_test.cpp
    prefab_test.cpp
    reflection_test.cpp
    commands_test.cpp
    schema_contract_test.cpp
    ai_bridge_test.cpp
    proposal_test.cpp
    ai_connection_test.cpp
    review_policy_test.cpp
    task_runner_test.cpp
    world_bible_test.cpp
    text_test.cpp
    story_test.cpp
    level_gen_test.cpp
    routine_test.cpp
    occlusion_test.cpp
    project_template_test.cpp
    scene_tools_test.cpp
    project_move_test.cpp
    ai_transcript_test.cpp
    perf_test.cpp
    modules_test.cpp
    records_test.cpp
    events_test.cpp
    input_test.cpp
    game_flow_test.cpp
    runtime_settings_test.cpp
    gameplay_systems_test.cpp
    character_ai_test.cpp
    narrative_test.cpp
    difficulty_test.cpp
    game_ui_test.cpp
    fx_test.cpp
    export_test.cpp
    flat_sprites_test.cpp
    scenes_test.cpp
    recent_projects_test.cpp
    grid_style_test.cpp
    tile_edges_test.cpp
    pack_import_test.cpp
    autotile_test.cpp
    village_gen_test.cpp
    project_story_test.cpp
    places_test.cpp
    ontology_edit_test.cpp
    user_library_test.cpp
    live_build_test.cpp
    place_flow_test.cpp
    big_scene_test.cpp
    catalog_test.cpp
    brief_test.cpp
    plan_test.cpp
    build_robust_test.cpp
    buildtypes_test.cpp
    place_scene_test.cpp
    live_plan_test.cpp
    source_rules_test.cpp
    where_test.cpp
    sourcing_test.cpp
    solve_test.cpp
)
target_link_libraries(aige_tests PRIVATE aige_platform aige_rhi aige_ecs aige_physics2d aige_renderer2d aige_scripting aige_assets aige_gameplay aige_editing aige_world aige_ai_bridge aige_ui aige_editor aige_modules aige_places aige_game gtest_main)
target_compile_definitions(aige_tests PRIVATE AIGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}" AIGE_PLAYER_DIR="$<TARGET_FILE_DIR:sandbox>")
add_dependencies(aige_tests sandbox) # the export smoke test runs the player

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
