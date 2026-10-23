#pragma once

#include "story/StoryOntology.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace aige {

class Ontology;

// The Planner's schemas (docs/ai/planner-schema.md): the Game Brief with its genre profiles,
// scope table and confirmation window, and the validators of each stage's document. The
// AI service mirrors these (ai_service/app/planner.py), as story.py mirrors Story.cpp.

struct PlanProblem {
    std::string where;   // "scope.places", "cast[2].importance", "s-04.facts[1]"
    std::string message;
    bool warning = false;
};
std::vector<std::string> ProblemTexts(const std::vector<PlanProblem>& problems, bool errorsOnly = true);
bool HasErrors(const std::vector<PlanProblem>& problems);

struct GenreProfile {
    std::string id, label;
    nlohmann::json stages = nlohmann::json::object();   // story/characters/quests/places/systems/visuals: full | light | main-only | side-only | off
    nlohmann::json defaults = nlohmann::json::object();
    std::vector<std::string> asked;
};

// data/planner (AIGE_PLANNER_DATA_DIR): the Brief's schema, the scope table, the profiles.
class PlannerData {
public:
    static const PlannerData& Engine();
    nlohmann::json briefSchema;
    nlohmann::json scope;
    std::map<std::string, GenreProfile> profiles;
    const GenreProfile* Profile(const std::string& id) const;
    std::vector<std::string> Genres() const;
};

// Dotted paths into a Brief ("systems.combat.on").
nlohmann::json BriefGet(const nlohmann::json& brief, const std::string& path);
void BriefSet(nlohmann::json& brief, const std::string& path, const nlohmann::json& value);

// The scope class for a play time ("short" for 3 hours) and its numbers.
std::string ScopeClassFor(double hours);
nlohmann::json ScopeNumbers(const std::string& scopeClass);

// Fills a Brief: the genre profile's defaults under what's set, the scope from the play
// time, autonomy defaults (confirm for concept and outline, review for the rest), the project's
// earlier answers. `assumed` gets every path that was filled in.
nlohmann::json CompleteGameBrief(nlohmann::json brief, const nlohmann::json& projectDefaults = {},
                                 std::vector<std::string>* assumed = nullptr);
std::vector<PlanProblem> ValidateGameBrief(const nlohmann::json& brief, const StoryOntology& gso);
// Values the AI put in a field that aren't among its options ("hub" as the story's pattern) or
// out of its range: taken out, so the genre profile's default fills the field again. Returns
// what was replaced, as "pattern: 'hub' → 'branch-and-bottleneck'".
std::vector<std::string> RepairGameBrief(nlohmann::json& brief, const StoryOntology& gso, const nlohmann::json& projectDefaults = {});
// The confirmation window from the schema: tabs, each field with its value, type, options,
// whether it's asked, and its marks (differ, guessed, assumed). Asked fields first.
nlohmann::json BriefWindow(const nlohmann::json& brief, const nlohmann::json& marks, const StoryOntology& gso);
// What the AI is told about the Brief's shape (for the concept call).
nlohmann::json BriefSchemaForAi(const StoryOntology& gso);

// --- Stage documents ------------------------------------------------------------------------

// "character/tamsin" -> "tamsin"; "q-main" -> "q-main".
std::string PlanSlug(const std::string& id);

std::vector<PlanProblem> ValidateOutline(const nlohmann::json& outline, const nlohmann::json& brief, const StoryOntology& gso,
                                         const std::vector<std::string>& buildTypes);
// The cast's roles put right before the outline is checked: a gameplay role listed as a narrative
// one ("companion") moves to gameplayRoles (and the other way round); one in neither vocabulary is
// dropped. Returns what was changed, as "cast[2]: 'companion' is a gameplay role".
std::vector<std::string> RepairOutline(nlohmann::json& outline, const StoryOntology& gso);
// An act's chapters with their scene cards.
std::vector<PlanProblem> ValidateActPlan(const nlohmann::json& act, const nlohmann::json& outline, const StoryOntology& gso);
std::vector<PlanProblem> ValidateSceneCard(const nlohmann::json& card, const StoryOntology& gso, const std::string& where);
// A written scene: beats, events, facts (checked against the ontology; the graph checks
// the rest when they're applied), new entities, the quest stage it implies.
std::vector<PlanProblem> ValidateSceneDoc(const nlohmann::json& scene, const StoryOntology& gso);
std::vector<PlanProblem> ValidateCharacterDoc(const nlohmann::json& character, const StoryOntology& gso,
                                              const std::vector<std::string>& packageTemplates);
std::vector<PlanProblem> ValidateQuestDesign(const nlohmann::json& quest, const StoryOntology& gso,
                                             const std::vector<std::string>& knownIds);
// A place description and its draft Brief (validated against the place ontology).
std::vector<PlanProblem> ValidatePlaceDesc(const nlohmann::json& place, const Ontology& places);
std::vector<PlanProblem> ValidateUiPlan(const nlohmann::json& ui, const std::vector<std::string>& themes,
                                        const std::vector<std::string>& screens, const std::vector<std::string>& widgets);

// The HUD widgets and the windows a plan may name (game-ui.md §5-6).
const std::vector<std::string>& PlanHudWidgets();
const std::vector<std::string>& PlanWindows();

} // namespace aige
