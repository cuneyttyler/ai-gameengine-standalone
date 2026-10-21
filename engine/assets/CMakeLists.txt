include(FetchContent)

add_library(aige_assets STATIC
    Project.h
    SceneIO.h
    SceneIO.cpp
    TextureCook.h
    TextureCook.cpp
    StbImpl.cpp
    AssetDatabase.h
    AssetDatabase.cpp
    Prefab.h
    Prefab.cpp
    ProjectTemplate.h
    ProjectTemplate.cpp
    ProjectMove.h
    ProjectMove.cpp
    Export.h
    Export.cpp
    SceneFiles.h
    SceneFiles.cpp
    TileEdges.h
    TileEdges.cpp
    TileLayerIO.h
    TileLayerIO.cpp
    ArtAnalysis.h
    ArtAnalysis.cpp
    Catalog.h
    Catalog.cpp
    Sourcing.h
    Sourcing.cpp
    UserLibrary.h
    UserLibrary.cpp
    LayoutImage.h
    LayoutImage.cpp
    PackImport.h
    PackImport.cpp
)
target_include_directories(aige_assets PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_include_directories(aige_assets PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(aige_assets PUBLIC nlohmann_json::nlohmann_json aige_ecs aige_physics2d aige_renderer2d aige_scripting aige_gameplay aige_audio aige_places)
target_compile_features(aige_assets PUBLIC cxx_std_20)
