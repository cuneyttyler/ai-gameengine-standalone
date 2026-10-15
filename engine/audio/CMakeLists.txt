include(FetchContent)

# Single-header library; SOURCE_SUBDIR points at a directory with no
# CMakeLists.txt so only the sources are fetched, not miniaudio's own
# example/test targets.
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(miniaudio)

add_library(aige_audio STATIC
    Audio.h
    Audio.cpp
    MiniaudioImpl.c
)
target_include_directories(aige_audio PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_include_directories(aige_audio PRIVATE ${miniaudio_SOURCE_DIR})
target_compile_features(aige_audio PUBLIC cxx_std_20)
if(MSVC)
    set_source_files_properties(MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4267")
endif()
