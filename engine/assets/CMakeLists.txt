include(FetchContent)

# stb has no release tags; pinned to a specific commit for reproducible builds.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(stb)

add_library(aige_assets STATIC
    Project.h
    SceneIO.h
    SceneIO.cpp
    TextureCook.h
    TextureCook.cpp
    StbImpl.cpp
    AssetDatabase.h
    AssetDatabase.cpp
)
target_include_directories(aige_assets PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_include_directories(aige_assets PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(aige_assets PUBLIC aige_ecs aige_physics2d aige_renderer2d aige_scripting aige_gameplay aige_audio)
target_compile_features(aige_assets PUBLIC cxx_std_20)
