add_library(aige_gameplay STATIC
    Components.h
    Isometric.h
    Combat.h
    Combat.cpp
    ComponentTable.h
    ComponentTable.cpp
    CameraFollow.h
    Archetypes.h
    Archetypes.cpp
    BootstrapLevel.h
    BootstrapLevel.cpp
    TileGrid.h
    TileGrid.cpp
    GameSession.h
    GameSession.cpp
)
target_include_directories(aige_gameplay PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_gameplay PUBLIC aige_ecs aige_physics2d aige_renderer2d aige_scripting)
target_compile_features(aige_gameplay PUBLIC cxx_std_20)
