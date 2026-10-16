add_library(aige_modules STATIC
    Modules.h
    Modules.cpp
)
target_include_directories(aige_modules PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_modules PUBLIC aige_reflection aige_scripting nlohmann_json::nlohmann_json)
target_compile_features(aige_modules PUBLIC cxx_std_20)
