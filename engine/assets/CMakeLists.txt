add_library(aige_assets STATIC
    SceneIO.h
    SceneIO.cpp
)
target_include_directories(aige_assets PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_assets PUBLIC aige_ecs aige_physics2d aige_renderer2d aige_scripting)
target_compile_features(aige_assets PUBLIC cxx_std_20)
