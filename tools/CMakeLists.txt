# Development tools (not shipped). See each tool's header comment.
add_executable(make_demo_level make_demo_level/main.cpp)
target_link_libraries(make_demo_level PRIVATE aige_assets aige_gameplay)
target_compile_definitions(make_demo_level PRIVATE AIGE_DEFAULT_PROJECT="${CMAKE_SOURCE_DIR}/projects/demo")

add_executable(plan_blockout plan_blockout/main.cpp)
# aige_ai_bridge: httplib (the AI service's planning steps, `"ai": "sections"`).
target_link_libraries(plan_blockout PRIVATE aige_editing aige_places aige_assets aige_ai_bridge)

# Regenerate a place's Paint block-out through the editor's own code, without decorations.
add_executable(regen_blockout regen_blockout/main.cpp)
target_link_libraries(regen_blockout PRIVATE aige_editing aige_places aige_assets aige_ai_bridge)

# A request read into a Brief as the Builder reads it, and its Confirm window (the AI service's /places/interpret).
add_executable(brief_test brief_test/main.cpp)
target_link_libraries(brief_test PRIVATE aige_editing aige_places aige_ai_bridge)

# A place planned from a request with the AI service, as the Planner plans it, without the editor (its Brief, plan and images).
add_executable(plan_place plan_place/main.cpp)
target_link_libraries(plan_place PRIVATE aige_editing aige_places aige_assets aige_ai_bridge)

# A project's planned places built into their scenes again, without the editor (their marks' occluders and walkable ground).
add_executable(build_scenes build_scenes/main.cpp)
target_link_libraries(build_scenes PRIVATE aige_gamebuild aige_ai_bridge)
target_compile_features(build_scenes PRIVATE cxx_std_20)
