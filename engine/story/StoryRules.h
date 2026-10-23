#pragma once

#include "story/StoryGraph.h"
#include "story/StoryOntology.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

// The story rules (docs/systems/story-ontology.md §7, development_plan.md 21.3) as checks over a
// story knowledge graph, plus the shapes (classes, fields, vocabularies, property domains).
// Each issue names its rule, severity and the node it's about; messages come from the
// rule's template in data/story/rules.json. Rules marked critic are left to the AI.

struct StoryIssue {
    std::string rule;     // "R-T1", or "shape"
    std::string severity; // error | warning
    std::string message;
    std::string node;     // the node it's about (for the viewer)
};

std::vector<StoryIssue> CheckStory(const StoryGraph& graph, const StoryOntology& ontology);
// Only the errors a change adds (for applying the AI's facts: new errors refuse it).
std::vector<StoryIssue> NewErrors(const std::vector<StoryIssue>& before, const std::vector<StoryIssue>& after);
nlohmann::json IssuesJson(const std::vector<StoryIssue>& issues);

} // namespace aige
