# Development tools (not shipped). See each tool's header comment.
add_executable(make_demo_level make_demo_level/main.cpp)
target_link_libraries(make_demo_level PRIVATE aige_assets aige_gameplay)
target_compile_definitions(make_demo_level PRIVATE AIGE_DEFAULT_PROJECT="${CMAKE_SOURCE_DIR}/projects/demo")
