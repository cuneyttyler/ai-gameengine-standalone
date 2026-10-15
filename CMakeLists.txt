cmake_minimum_required(VERSION 3.21)
project(ai_gameengine LANGUAGES CXX C) # C: vendored Lua source (engine/scripting) and miniaudio (engine/audio)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AIGE_BUILD_TESTS "Build engine unit tests" ON)

add_subdirectory(engine)
add_subdirectory(apps/sandbox)
add_subdirectory(apps/editor)

if(AIGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
