cmake_minimum_required(VERSION 3.21)
project(ai_gameengine LANGUAGES CXX C) # C: vendored Lua source (engine/scripting) and miniaudio (engine/audio)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AIGE_BUILD_TESTS "Build engine unit tests" ON)

# Faster builds with MSVC: a library's files compiled side by side (Visual Studio otherwise compiles them one at a
# time). Capped: a laptop under sustained full load can overheat; raise it on a machine that keeps cool.
set(AIGE_COMPILE_JOBS 4 CACHE STRING "How many files MSVC compiles at once in each library (/MP)")
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/MP${AIGE_COMPILE_JOBS}>)
endif()

add_subdirectory(engine)
add_subdirectory(apps/sandbox)
add_subdirectory(apps/editor)
add_subdirectory(tools)

# The heavy headers (sol2's Lua binding, JSON, EnTT) parsed once per library instead of in every file that reaches
# them (GameSession.h brings sol2 into most of the engine and the editor).
option(AIGE_PRECOMPILED_HEADERS "Precompile the heavy third-party headers" ON)
if(AIGE_PRECOMPILED_HEADERS)
    # The libraries built on gameplay (they all see sol2, JSON and EnTT).
    foreach(target IN ITEMS aige_gameplay aige_editing aige_ai_bridge aige_game aige_gamebuild editor)
        if(TARGET ${target})
            target_precompile_headers(${target} PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:<sol/sol.hpp$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<nlohmann/json.hpp$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<entt/entt.hpp$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<map$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>"
                "$<$<COMPILE_LANGUAGE:CXX>:<algorithm$<ANGLE-R>>")
        endif()
    endforeach()
endif()

if(AIGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
