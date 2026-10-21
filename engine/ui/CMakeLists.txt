include(FetchContent)

# RmlUi (MIT): HTML/CSS-like documents for the game's UI (game-ui.md §3). Built
# static, without its FreeType font engine: ui/Documents gives it our own fonts.
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG 6.0
)
set(RMLUI_FONT_ENGINE "none" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(_aige_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rmlui)
set(BUILD_SHARED_LIBS ${_aige_shared} CACHE BOOL "" FORCE)

add_library(aige_ui STATIC
    GameUi.h
    GameUi.cpp
    Documents.h
    Documents.cpp
)
target_include_directories(aige_ui PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_ui PUBLIC aige_renderer2d nlohmann_json::nlohmann_json PRIVATE RmlUi::Core)
target_compile_definitions(aige_ui PRIVATE AIGE_UI_DATA_DIR="${CMAKE_SOURCE_DIR}/data/ui")
target_compile_features(aige_ui PUBLIC cxx_std_20)
