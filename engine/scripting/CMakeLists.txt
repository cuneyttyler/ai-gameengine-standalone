include(FetchContent)

# Plain lua.org source — no CMakeLists.txt included (their build is a plain
# Makefile), so we glob and build the library ourselves rather than pull in
# a third-party CMake wrapper for it.
FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
)
FetchContent_MakeAvailable(lua)

file(GLOB AIGE_LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM AIGE_LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)

add_library(lua_lib STATIC ${AIGE_LUA_SOURCES})
target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR}/src)
if(MSVC)
    target_compile_definitions(lua_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(sol2)

add_library(aige_scripting STATIC
    LuaVM.h
    LuaVM.cpp
    Components.h
    ScriptSystem.h
    ScriptSystem.cpp
)
target_include_directories(aige_scripting PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_scripting PUBLIC aige_ecs sol2 lua_lib)
target_compile_features(aige_scripting PUBLIC cxx_std_20)
