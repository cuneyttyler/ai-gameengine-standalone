add_library(aige_game STATIC
    GameFlow.h
    GameFlow.cpp
    SaveGames.h
    SaveGames.cpp
    PlayerSettings.h
    PlayerSettings.cpp
    GameAudio.h
    GameAudio.cpp
    PlayWindows.h
    PlayWindows.cpp
    DebugConsole.h
    DebugConsole.cpp
    Accessibility.h
    Accessibility.cpp
    Templates.h
    Templates.cpp
    Actors.h
    Actors.cpp
    GameRunner.h
    GameRunner.cpp
)
target_include_directories(aige_game PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_game PUBLIC aige_gameplay aige_assets aige_world aige_ui aige_audio)
target_compile_features(aige_game PUBLIC cxx_std_20)
