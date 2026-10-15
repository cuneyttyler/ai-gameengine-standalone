add_library(aige_world STATIC
    WorldBible.h
    WorldBible.cpp
    StoryReferences.h
    StoryReferences.cpp
)
target_include_directories(aige_world PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_world PUBLIC aige_assets)
target_compile_features(aige_world PUBLIC cxx_std_20)
