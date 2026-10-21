add_library(aige_records STATIC
    Records.h
    Records.cpp
)
target_include_directories(aige_records PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_records PUBLIC aige_reflection nlohmann_json::nlohmann_json)
target_compile_features(aige_records PUBLIC cxx_std_20)
