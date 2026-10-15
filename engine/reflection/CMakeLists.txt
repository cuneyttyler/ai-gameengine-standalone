add_library(aige_reflection STATIC
    Reflection.h
    Reflection.cpp
)
target_include_directories(aige_reflection PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_reflection PUBLIC aige_ecs nlohmann_json::nlohmann_json)
target_compile_features(aige_reflection PUBLIC cxx_std_20)
