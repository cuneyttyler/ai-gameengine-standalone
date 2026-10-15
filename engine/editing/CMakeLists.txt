add_library(aige_editing STATIC
    SceneEditing.h
    SceneEditing.cpp
    EditHistory.h
    EditHistory.cpp
    Commands.h
    Commands.cpp
)
target_include_directories(aige_editing PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_editing PUBLIC aige_gameplay aige_assets)
target_compile_features(aige_editing PUBLIC cxx_std_20)
