add_library(aige_gameplay STATIC
    Components.h
    Isometric.h
    Combat.h
    Combat.cpp
    Settings.h
    EngineReflection.h
    EngineReflection.cpp
    CameraFollow.h
    Archetypes.h
    Archetypes.cpp
    BootstrapLevel.h
    BootstrapLevel.cpp
    TileGrid.h
    TileGrid.cpp
    GameSession.h
    GameSession.cpp
    Story.h
    Story.cpp
    LevelGen.h
    LevelGen.cpp
    Pathfinding.h
    Pathfinding.cpp
    Schedule.h
    Schedule.cpp
    Benchmark.h
    Benchmark.cpp
)
target_include_directories(aige_gameplay PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_gameplay PUBLIC aige_ecs aige_reflection aige_physics2d aige_renderer2d aige_ui aige_scripting)
target_compile_features(aige_gameplay PUBLIC cxx_std_20)
