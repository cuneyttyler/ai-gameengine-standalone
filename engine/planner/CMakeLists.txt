# The AI Planner (development_plan.md 22, docs/ai/ai-planner.md): the Game Brief, the stage
# documents and their validators, the decision record, and the job engine that runs the
# stages against the AI service.
add_library(aige_planner STATIC
    PlannerStore.h
    PlannerStore.cpp
    PlannerDocs.h
    PlannerDocs.cpp
    GamePlan.h
    GamePlan.cpp
    PlanStages.cpp
)
target_include_directories(aige_planner PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_planner PUBLIC nlohmann_json::nlohmann_json aige_story aige_places aige_world)
target_compile_definitions(aige_planner PRIVATE AIGE_PLANNER_DATA_DIR="${CMAKE_SOURCE_DIR}/data/planner")
target_compile_features(aige_planner PUBLIC cxx_std_20)
