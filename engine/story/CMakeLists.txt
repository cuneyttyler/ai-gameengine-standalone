# The Game Story Ontology and the story knowledge graph (development_plan.md 21,
# docs/story-ontology.md): ontology loading and publishing, the graph with temporal
# facts, and the story rules.
add_library(aige_story STATIC
    StoryOntology.h
    StoryOntology.cpp
    StoryGraph.h
    StoryGraph.cpp
    StoryRules.h
    StoryRules.cpp
)
target_include_directories(aige_story PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_story PUBLIC nlohmann_json::nlohmann_json aige_world)
target_compile_definitions(aige_story PRIVATE AIGE_STORY_DATA_DIR="${CMAKE_SOURCE_DIR}/data/story")
target_compile_features(aige_story PUBLIC cxx_std_20)
