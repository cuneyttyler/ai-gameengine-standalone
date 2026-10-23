#pragma once

#include "story/StoryOntology.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

class WorldBible;

// The story knowledge graph (docs/systems/story-ontology.md §6, development_plan.md 21.2): the
// instance data of GSO for one game, in planner/kg.json. Nodes have a class, an id and
// fields; edges have a property, a subject and an object, fields, and for temporal
// properties an interval of scenes by discourse order ("from" inclusive, "until"
// exclusive; empty: from the start / to the end) and an optional branch.
// Characters, factions, locations, items, events and lore are World Bible entries: the
// node id is the World Bible key ("character/tamsin").

struct StoryNode {
    std::string id, cls;
    nlohmann::json fields = nlohmann::json::object();

    std::string Text(const char* key) const;
    bool Flag(const char* key) const;
    std::vector<std::string> List(const char* key) const;
};

struct StoryEdge {
    std::string p, s, o;
    std::string from, until, branch, madeBy;
    nlohmann::json fields = nlohmann::json::object();

    std::string Text(const char* key) const;
};

class StoryGraph {
public:
    static StoryGraph FromJson(const nlohmann::json& json);
    nlohmann::json ToJson() const;
    static std::optional<StoryGraph> Load(const std::filesystem::path& path, std::string* error = nullptr);
    bool Save(const std::filesystem::path& path) const;
    static std::filesystem::path PathIn(const std::filesystem::path& projectRoot); // planner/kg.json

    std::vector<StoryNode> nodes;
    std::vector<StoryEdge> edges;
    int revision = 0;

    const StoryNode* Find(const std::string& id) const;
    StoryNode* Find(const std::string& id);
    StoryNode& Upsert(const std::string& id, const std::string& cls);
    void RemoveNode(const std::string& id); // and every edge touching it
    std::vector<const StoryNode*> OfClass(const std::string& cls, const StoryOntology& ontology) const;
    std::vector<const StoryEdge*> Out(const std::string& s, const std::string& p = {}) const;
    std::vector<const StoryEdge*> In(const std::string& o, const std::string& p = {}) const;

    // --- Discourse order and time ---------------------------------------------------------
    // Story scenes by their "order" (then id).
    std::vector<std::string> SceneOrder() const;
    // The scene's place in the discourse order: its "order" (-1: not a scene). Branch scenes
    // have orders too; 2.5 sits between 2 and 3.
    double OrderOf(const std::string& scene) const;
    // The scene's branch ("" = the main line, holds in every branch).
    std::string BranchOf(const std::string& scene) const;
    // Does a temporal fact hold at a scene on a branch? (A branch-less fact holds in all.)
    bool Holds(const StoryEdge& edge, const std::string& scene, const std::string& branch = {}) const;
    // Every temporal fact that holds at a scene: the scene's world state.
    std::vector<const StoryEdge*> FactsAt(const std::string& scene, const std::string& branch = {}) const;

    // --- Questions (§9) ------------------------------------------------------------------------
    // The branch defaults to the scene's own.
    bool AliveAt(const std::string& character, const std::string& scene, const std::string& branch = {}) const;
    std::vector<std::string> LocationsAt(const std::string& entity, const std::string& scene, const std::string& branch = {}) const;
    bool KnowsAt(const std::string& character, const std::string& about, const std::string& scene, const std::string& branch = {}) const;
    std::vector<std::string> PossessorsAt(const std::string& item, const std::string& scene, const std::string& branch = {}) const;
    // Where a scene is set: its "location" field, else its "at" edge.
    std::string LocationOfScene(const std::string& scene) const;
    std::vector<std::string> Participants(const std::string& scene) const;
    // The scenes a quest stage advances; the quest a stage belongs to.
    std::vector<std::string> ScenesAdvancedBy(const std::string& stage) const;
    std::string QuestOfStage(const std::string& stage) const;
    // Characters filling no role; role nodes nobody fills.
    std::vector<std::string> RolelessCharacters(const StoryOntology& ontology) const;
    std::vector<std::string> UnfilledRoles(const StoryOntology& ontology) const;
    // Quests that become impossible if `character` dies at `scene`: they need them later.
    std::vector<std::string> QuestsBrokenByDeath(const std::string& character, const std::string& scene) const;
    // Every location the story's scenes use, with the parts its scenes need there.
    std::map<std::string, std::set<std::string>> PlacesUsed() const;
    // Characters the player meets in a chapter's scenes, in discourse order (first time).
    std::vector<std::string> CharactersMet(const std::string& chapter) const;
    // Why a node exists: its madeBy decision (a node or an edge's madeBy).
    std::string MadeBy(const std::string& id) const;
    // What depends on a node: scenes presenting it, events it causes or enables (and on),
    // quests needing it, dialogue speaking it... transitively, sorted.
    std::vector<std::string> Dependents(const std::string& id) const;
    // A node's facts over time: every edge it's in, ordered by "from".
    nlohmann::json Timeline(const std::string& id) const;
    // What an AI call about one scene gets (§6.4): the scene, its participants with their
    // state, relationships, goals and knowledge there, the location's facts and earlier
    // scenes there, and the open threads (unresolved conflicts, unpaid setups).
    nlohmann::json Subgraph(const std::string& scene, const std::string& branch = {}) const;

    // --- Changes ------------------------------------------------------------------------------
    // {"nodes": [{"id", "class", ...fields}], "assert": [edges], "retract": [{"p", "s", "o",
    // "at": scene}] (ends the fact at that scene), "remove": [ids]} ("facts" and "end" also
    // work: the AI panel's story_graph op). Unknown classes and
    // properties are refused. Returns "" or the problem.
    std::string Apply(const nlohmann::json& change, const StoryOntology& ontology);

    // --- The World Bible (§6.2) ---------------------------------------------------------------
    // The end-of-story relations of a World Bible key as rel: lines (hasRelationship by
    // type, memberOf, locatedAt, possesses).
    std::vector<std::pair<std::string, std::string>> RelLines(const std::string& key) const;
    // Writes every entry's rel: lines from the graph; returns how many entries changed.
    int WriteRelationsTo(WorldBible& bible) const;
    // Hand-written rel: lines the graph doesn't have become current facts; returns how many.
    int ReadRelationsFrom(const WorldBible& bible);
};

} // namespace aige
