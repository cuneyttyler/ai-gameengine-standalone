# The AI Planner, building the game it designed (development_plan.md 23, docs/ai-planner.md
# §8-§11): the build plan and its automatic driver, binding quests to the built world,
# writing the game's files, the playtest and the result.
add_library(aige_gamebuild STATIC
    GameBuild.h
    GameBuild.cpp
)
target_include_directories(aige_gamebuild PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_gamebuild PUBLIC aige_planner aige_editing aige_gameplay aige_assets aige_records aige_places aige_story)
target_compile_features(aige_gamebuild PUBLIC cxx_std_20)
