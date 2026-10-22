include(FetchContent)

# Box2D v3 (MIT, C): dynamic bodies and joints (development_plan.md 20.3, physics.md).
FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG v3.0.0
)
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(box2d)

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
    TileLayer.h
    TileLayer.cpp
    TileLayerView.h
    TileLayerView.cpp
    GameSession.h
    GameSession.cpp
    InputMap.h
    InputMap.cpp
    Localization.h
    Localization.cpp
    Attributes.h
    Attributes.cpp
    Items.h
    Items.cpp
    Interact.h
    Interact.cpp
    Encounters.h
    Encounters.cpp
    Behaviour.h
    Behaviour.cpp
    Story.h
    Story.cpp
    Cutscene.h
    Cutscene.cpp
    Difficulty.h
    Difficulty.cpp
    Animation.h
    Animation.cpp
    Effects.h
    Effects.cpp
    Magic.h
    Magic.cpp
    Physics.h
    Physics.cpp
    LevelGen.h
    LevelGen.cpp
    VillageGen.h
    VillageGen.cpp
    Pathfinding.h
    Pathfinding.cpp
    Schedule.h
    Schedule.cpp
    Benchmark.h
    Benchmark.cpp
)
target_include_directories(aige_gameplay PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_gameplay PUBLIC aige_ecs aige_reflection aige_records aige_modules aige_physics2d aige_renderer2d aige_ui aige_scripting
                      PRIVATE box2d)
target_compile_features(aige_gameplay PUBLIC cxx_std_20)
target_compile_definitions(aige_gameplay PRIVATE AIGE_FX_DATA_DIR="${CMAKE_SOURCE_DIR}/data/fx"
                                                  AIGE_MAGIC_DATA_DIR="${CMAKE_SOURCE_DIR}/data/magic")
