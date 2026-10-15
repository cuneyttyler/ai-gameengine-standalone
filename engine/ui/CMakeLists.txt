add_library(aige_ui STATIC
    GameUi.h
    GameUi.cpp
)
target_include_directories(aige_ui PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_ui PUBLIC aige_renderer2d)
target_compile_features(aige_ui PUBLIC cxx_std_20)
