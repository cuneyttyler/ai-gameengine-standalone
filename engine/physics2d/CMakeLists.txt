add_library(aige_physics2d STATIC
    Collision.h
    Collision.cpp
    Components.h
)
target_include_directories(aige_physics2d PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_compile_features(aige_physics2d PUBLIC cxx_std_20)
