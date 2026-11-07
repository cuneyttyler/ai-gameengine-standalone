# Development tools (not shipped). See each tool's header comment.
add_executable(make_demo_level make_demo_level/main.cpp)
target_link_libraries(make_demo_level PRIVATE aige_assets aige_gameplay)
target_compile_definitions(make_demo_level PRIVATE AIGE_DEFAULT_PROJECT="${CMAKE_SOURCE_DIR}/projects/demo")

add_executable(plan_blockout plan_blockout/main.cpp)
# aige_ai_bridge: httplib (the AI service's planning steps, `"ai": "sections"`).
target_link_libraries(plan_blockout PRIVATE aige_editing aige_places aige_assets aige_ai_bridge)

# A request read into a Brief as the Builder reads it, and its Confirm window (the AI service's /places/interpret).
add_executable(brief_test brief_test/main.cpp)
target_link_libraries(brief_test PRIVATE aige_editing aige_places aige_ai_bridge)
