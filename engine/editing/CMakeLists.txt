add_library(aige_editing STATIC
    SceneEditing.h
    SceneEditing.cpp
    EditHistory.h
    EditHistory.cpp
    Commands.h
    Commands.cpp
    Edits.h
    Edits.cpp
    LevelBuild.h
    LevelBuild.cpp
    SceneTools.h
    SceneTools.cpp
    Autotile.h
    Autotile.cpp
    VillageBuild.h
    VillageBuild.cpp
    BuildWindow.h
    BuildWindow.cpp
    PlaceBuild.h
    PlaceBuild.cpp
    PlaceScene.h
    PlaceScene.cpp
)
target_include_directories(aige_editing PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_editing PUBLIC aige_gameplay aige_assets aige_world)
target_compile_features(aige_editing PUBLIC cxx_std_20)
