add_library(aige_renderer2d STATIC
    Components.h
    Renderer2D.h
    Renderer2D.cpp
)
target_include_directories(aige_renderer2d PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_renderer2d PUBLIC aige_rhi aige_ecs)
target_compile_features(aige_renderer2d PUBLIC cxx_std_20)
