#include <gtest/gtest.h>

#include "story/StoryGraph.h"
#include "story/StoryOntology.h"
#include "story/StoryRules.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

// development_plan.md Phase 21: the story ontology (GSO), the story knowledge graph with
// temporal facts and branches, the rules, the competency questions (story-ontology.md §9).
using aige::StoryGraph;
using aige::StoryOntology;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// "The Moon Amulet": a short three-act, branch-and-bottleneck story with every rule satisfied.
const char* kStory = R"json({
"nodes": [
  {"id": "story", "class": "Story", "structure": "three-act", "pattern": "branch-and-bottleneck", "tone": "dark",
   "logline": "A disgraced warden must recover a stolen amulet before the dead wake."},
  {"id": "act-1", "class": "Act", "order": 1}, {"id": "act-2", "class": "Act", "order": 2}, {"id": "act-3", "class": "Act", "order": 3},
  {"id": "ch-1", "class": "Chapter", "order": 1}, {"id": "ch-2", "class": "Chapter", "order": 2}, {"id": "ch-3", "class": "Chapter", "order": 3},
  {"id": "character/warden", "class": "PlayerCharacter", "name": "Warden", "importance": "major", "need": "forgiveness",
   "desire": "the amulet back", "motivations": ["protection"], "static": true},
  {"id": "character/tamsin", "class": "Character", "name": "Tamsin", "importance": "supporting", "motivations": ["protection"],
   "traits": [["agreeable", 0.8], ["brave", 0.4]], "package": "temple-healer"},
  {"id": "character/aldric", "class": "Character", "name": "Aldric", "importance": "major", "need": "to be seen",
   "desire": "the amulet's power", "motivations": ["wealth"], "arc": "greed to ruin", "package": "tower-lord", "statClass": "sorcerer"},
  {"id": "character/mara", "class": "Character", "name": "Mara", "importance": "minor", "stock": "potions", "package": "shopkeeper"},
  {"id": "character/brann", "class": "Character", "name": "Brann", "importance": "minor", "idle": true},
  {"id": "role/protagonist", "class": "NarrativeRole", "archetype": "protagonist"},
  {"id": "role/antagonist", "class": "NarrativeRole", "archetype": "antagonist"},
  {"id": "role/mentor", "class": "NarrativeRole", "archetype": "mentor"},
  {"id": "role/vendor", "class": "GameplayRole", "role": "vendor"},
  {"id": "location/riverford", "class": "Location", "name": "Riverford", "buildType": "settlement", "start": true},
  {"id": "location/riverford-temple", "class": "Location", "name": "Riverford Temple", "buildType": "interior", "requiredParts": ["crypt", "altar"]},
  {"id": "location/hideout", "class": "Location", "name": "The Hideout", "buildType": "dungeon"},
  {"id": "location/tower", "class": "Location", "name": "Aldric's Tower", "buildType": "tower"},
  {"id": "item/amulet", "class": "Item", "name": "The Moon Amulet", "unique": true, "setup": true},
  {"id": "goal/aldric-wealth", "class": "Goal", "kind": "wealth"},
  {"id": "goal/warden-recover", "class": "Goal", "kind": "recover"},
  {"id": "conflict/main", "class": "Conflict", "type": "person", "stakes": "the dead wake"},
  {"id": "e-theft", "class": "Action", "agent": "character/aldric", "kernel": true, "inciting": true, "text": "Aldric steals the amulet"},
  {"id": "e-curse", "class": "Happening", "kernel": true, "text": "Tamsin is cursed"},
  {"id": "e-reveal", "class": "Event", "kernel": true, "text": "Tamsin names the thief"},
  {"id": "e-confront", "class": "Action", "agent": "character/warden", "kernel": true, "text": "The warden confronts Aldric"},
  {"id": "e-return", "class": "Event", "text": "The amulet is returned to the temple"},
  {"id": "d-1", "class": "Decision", "stage": "story", "question": "How does the player learn who the thief is?",
   "chosen": "Tamsin tells them", "rationale": "the mentor reveals; it costs her"},
  {"id": "s-01", "class": "StoryScene", "order": 1, "act": "act-1", "chapter": "ch-1", "mode": "cutscene", "kernel": true,
   "inciting": true, "chargeBefore": 0, "chargeAfter": -1, "location": "location/riverford", "value": "life-death"},
  {"id": "s-02", "class": "StoryScene", "order": 2, "act": "act-1", "chapter": "ch-1", "mode": "dialogue", "kernel": true,
   "chargeBefore": -1, "chargeAfter": 1, "location": "location/riverford-temple", "value": "truth-lie", "needsParts": ["altar"], "madeBy": "d-1"},
  {"id": "s-03", "class": "StoryScene", "order": 3, "act": "act-2", "chapter": "ch-2", "mode": "gameplay", "kernel": true,
   "chargeBefore": -1, "chargeAfter": 1, "location": "location/hideout"},
  {"id": "s-04a", "class": "StoryScene", "order": 4, "act": "act-2", "chapter": "ch-2", "mode": "dialogue", "branch": "spare", "location": "location/tower"},
  {"id": "s-04b", "class": "StoryScene", "order": 4.5, "act": "act-2", "chapter": "ch-2", "mode": "gameplay", "branch": "kill", "location": "location/tower"},
  {"id": "s-04c", "class": "StoryScene", "order": 4.6, "act": "act-2", "chapter": "ch-2", "mode": "cutscene", "branch": "kill", "location": "location/tower"},
  {"id": "s-05", "class": "StoryScene", "order": 5, "act": "act-3", "chapter": "ch-3", "mode": "dialogue", "climax": true, "location": "location/riverford"},
  {"id": "s-06", "class": "StoryScene", "order": 6, "act": "act-3", "chapter": "ch-3", "mode": "cutscene", "location": "location/riverford"},
  {"id": "choice/aldric", "class": "ChoicePoint", "options": ["spare", "kill"]},
  {"id": "conv/after", "class": "Convergence", "carries": ["aldric-fate"]},
  {"id": "ending/good", "class": "Ending", "epilogue": "The dead sleep."},
  {"id": "q-main", "class": "Quest", "type": "main", "motivation": "protection", "strategy": "recover-stolen-item"},
  {"id": "q-main/10", "class": "QuestStage", "index": 10, "journal": "Find the hideout."},
  {"id": "q-main/20", "class": "QuestStage", "index": 20, "journal": "Return to Riverford."},
  {"id": "q-main/alias/thief", "class": "Alias", "kind": "character", "fill": "character/aldric"},
  {"id": "q-side", "class": "Quest", "type": "side", "motivation": "wealth"}
],
"edges": [
  {"p": "fills", "s": "character/warden", "o": "role/protagonist"},
  {"p": "fills", "s": "character/aldric", "o": "role/antagonist"},
  {"p": "fills", "s": "character/tamsin", "o": "role/mentor"},
  {"p": "fills", "s": "character/mara", "o": "role/vendor"},
  {"p": "inside", "s": "location/riverford-temple", "o": "location/riverford"},
  {"p": "connects", "s": "location/riverford", "o": "location/hideout", "kind": "road"},
  {"p": "connects", "s": "location/hideout", "o": "location/tower", "kind": "stairs"},
  {"p": "participant", "s": "s-01", "o": "character/warden"}, {"p": "participant", "s": "s-01", "o": "character/tamsin"},
  {"p": "participant", "s": "s-02", "o": "character/warden"}, {"p": "participant", "s": "s-02", "o": "character/tamsin"},
  {"p": "participant", "s": "s-02", "o": "character/mara"},
  {"p": "participant", "s": "s-03", "o": "character/warden"}, {"p": "participant", "s": "s-03", "o": "character/brann"},
  {"p": "participant", "s": "s-04a", "o": "character/warden"}, {"p": "participant", "s": "s-04a", "o": "character/aldric"},
  {"p": "participant", "s": "s-04b", "o": "character/warden"}, {"p": "participant", "s": "s-04b", "o": "character/aldric"},
  {"p": "participant", "s": "s-04c", "o": "character/warden"},
  {"p": "participant", "s": "s-05", "o": "character/warden"}, {"p": "participant", "s": "s-05", "o": "character/tamsin"},
  {"p": "participant", "s": "s-05", "o": "character/aldric"},
  {"p": "participant", "s": "s-06", "o": "character/warden"},
  {"p": "presents", "s": "s-01", "o": "e-theft"}, {"p": "presents", "s": "s-02", "o": "e-reveal"},
  {"p": "presents", "s": "s-04b", "o": "e-confront"}, {"p": "presents", "s": "s-06", "o": "e-return"},
  {"p": "causes", "s": "e-theft", "o": "e-curse"}, {"p": "enables", "s": "e-curse", "o": "e-reveal"},
  {"p": "enables", "s": "e-reveal", "o": "e-confront"}, {"p": "causes", "s": "e-confront", "o": "e-return"},
  {"p": "serves", "s": "e-theft", "o": "goal/aldric-wealth"}, {"p": "serves", "s": "e-confront", "o": "goal/warden-recover"},
  {"p": "resolves", "s": "e-confront", "o": "conflict/main"}, {"p": "paysOff", "s": "e-return", "o": "item/amulet"},
  {"p": "next", "s": "s-01", "o": "s-02"}, {"p": "next", "s": "s-02", "o": "s-03"},
  {"p": "hasChoice", "s": "s-03", "o": "choice/aldric"},
  {"p": "branchesTo", "s": "choice/aldric", "o": "s-04a", "option": "spare", "branch": "spare"},
  {"p": "branchesTo", "s": "choice/aldric", "o": "s-04b", "option": "kill", "branch": "kill"},
  {"p": "next", "s": "s-04a", "o": "conv/after"}, {"p": "next", "s": "s-04b", "o": "s-04c"}, {"p": "next", "s": "s-04c", "o": "conv/after"},
  {"p": "next", "s": "conv/after", "o": "s-05"}, {"p": "next", "s": "s-05", "o": "s-06"}, {"p": "next", "s": "s-06", "o": "ending/good"},
  {"p": "state", "s": "character/aldric", "state": "dead", "from": "s-04c", "branch": "kill"},
  {"p": "possesses", "s": "character/aldric", "o": "item/amulet", "until": "s-04b"},
  {"p": "possesses", "s": "character/warden", "o": "item/amulet", "from": "s-04b", "branch": "kill"},
  {"p": "hasRelationship", "s": "character/tamsin", "o": "character/aldric", "type": "trusts", "valence": 0.7, "until": "s-02"},
  {"p": "hasRelationship", "s": "character/tamsin", "o": "character/aldric", "type": "betrayed-by", "valence": -0.9, "from": "s-02"},
  {"p": "hasRelationship", "s": "character/mara", "o": "character/brann", "type": "sibling"},
  {"p": "hasRelationship", "s": "character/brann", "o": "character/mara", "type": "sibling"},
  {"p": "knows", "s": "character/tamsin", "about": "aldric-stole-amulet", "true": true, "from": "s-01"},
  {"p": "mentions", "s": "character/tamsin", "about": "aldric-stole-amulet", "scene": "s-02"},
  {"p": "locatedAt", "s": "character/tamsin", "o": "location/riverford-temple"},
  {"p": "locatedAt", "s": "character/mara", "o": "location/riverford"},
  {"p": "locatedAt", "s": "character/brann", "o": "location/hideout"},
  {"p": "locatedAt", "s": "character/aldric", "o": "location/tower"},
  {"p": "memberOf", "s": "character/mara", "o": "location/riverford"},
  {"p": "gives", "s": "character/tamsin", "o": "q-main"},
  {"p": "hasStage", "s": "q-main", "o": "q-main/10"}, {"p": "hasStage", "s": "q-main", "o": "q-main/20"},
  {"p": "advancesScene", "s": "q-main/10", "o": "s-03"}, {"p": "advancesScene", "s": "q-main/20", "o": "s-05"},
  {"p": "hasAlias", "s": "q-main", "o": "q-main/alias/thief"},
  {"p": "needsCharacter", "s": "q-side", "o": "character/aldric", "at": "s-05"}
]})json";

StoryGraph Good() {
    StoryGraph g = StoryGraph::FromJson(json::parse(kStory));
    // memberOf needs a faction: that line above is a planted mistake for the shapes test.
    g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(), [](const aige::StoryEdge& e) { return e.p == "memberOf"; }), g.edges.end());
    return g;
}

const StoryOntology& Gso() { return StoryOntology::Engine(); }

std::vector<aige::StoryIssue> Issues(const StoryGraph& g) { return aige::CheckStory(g, Gso()); }

std::string Describe(const std::vector<aige::StoryIssue>& issues) {
    std::ostringstream out;
    for (const auto& i : issues) out << "  " << i.rule << " " << i.severity << ": " << i.message << "\n";
    return out.str();
}

bool Has(const std::vector<aige::StoryIssue>& issues, const std::string& rule) {
    return std::any_of(issues.begin(), issues.end(), [&](const aige::StoryIssue& i) { return i.rule == rule; });
}

void Remove(StoryGraph& g, const std::string& p, const std::string& s, const std::string& o = {}) {
    const size_t before = g.edges.size();
    g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
                                 [&](const aige::StoryEdge& e) { return e.p == p && e.s == s && (o.empty() || e.o == o); }),
                  g.edges.end());
    ASSERT_LT(g.edges.size(), before) << p << " " << s << " " << o;
}

void Add(StoryGraph& g, const json& edge) { g.edges.push_back(StoryGraph::FromJson({{"edges", json::array({edge})}}).edges[0]); }

} // namespace

TEST(StoryOntologyTest, LoadsClassesPropertiesVocabulariesTemplatesAndRules) {
    const auto& o = Gso();
    EXPECT_GE(o.classes.size(), 35u);
    EXPECT_TRUE(o.IsA("Action", "Event"));
    EXPECT_TRUE(o.IsA("PlayerCharacter", "Character"));
    EXPECT_FALSE(o.IsA("Location", "Character"));
    ASSERT_NE(o.Property("hasRelationship"), nullptr);
    EXPECT_TRUE(o.Property("hasRelationship")->temporal);
    EXPECT_FALSE(o.Property("causes")->temporal);
    EXPECT_TRUE(o.InVocabulary("SceneMode", "environmental"));
    EXPECT_TRUE(o.InVocabulary("ObjectiveAction", "escort"));
    EXPECT_EQ(o.inverse.at("parent"), "child");
    ASSERT_TRUE(o.templates.contains("heros-journey"));
    EXPECT_EQ(o.templates.at("heros-journey").beats.size(), 12u);
    EXPECT_GE(o.rules.size(), 30u);
    EXPECT_TRUE(o.Rule("R-P4")->critic);
    EXPECT_EQ(o.FieldType("Action", "kernel"), "boolean"); // inherited from Event
    EXPECT_EQ(o.FieldType("StoryScene", "mode"), "SceneMode");
    const json summary = o.Summary();
    EXPECT_TRUE(summary["classes"].contains("StoryScene"));
}

TEST(StoryOntologyTest, AProjectExtendsIt) {
    const fs::path dir = fs::temp_directory_path() / "aige_story_ontology_ext";
    fs::remove_all(dir);
    fs::create_directories(dir / "story" / "ontology");
    std::ofstream(dir / "story" / "ontology" / "vocabularies.json")
        << R"({"vocabularies": {"Tone": ["noir"], "Sect": ["moon", "sun"]}})";
    const StoryOntology o = StoryOntology::ForProject(dir);
    EXPECT_TRUE(o.InVocabulary("Tone", "noir"));
    EXPECT_TRUE(o.InVocabulary("Tone", "heroic")); // the engine's are still there
    EXPECT_TRUE(o.InVocabulary("Sect", "sun"));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StoryOntologyTest, PublishedTurtleIsCurrent) {
    const fs::path dir = fs::path(AIGE_SOURCE_DIR) / "ontology";
    const std::pair<const char*, std::string> files[] = {{"gso-vocab.ttl", aige::ExportGsoVocabTurtle(Gso())},
                                                         {"gso-shapes.ttl", aige::ExportGsoShapesTurtle(Gso())}};
    for (const auto& [name, text] : files) {
        if (std::getenv("AIGE_WRITE_ONTOLOGY")) std::ofstream(dir / name, std::ios::binary) << text;
        std::ifstream in(dir / name, std::ios::binary);
        const std::string published{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        EXPECT_EQ(published, text) << name << " is stale: run with AIGE_WRITE_ONTOLOGY=1 (ontology/README.md)";
    }
    const std::string vocab = aige::ExportGsoVocabTurtle(Gso());
    EXPECT_NE(vocab.find("gso:Action\n    a owl:Class ;\n    rdfs:subClassOf gso:Event"), std::string::npos);
    EXPECT_NE(vocab.find("gsv:SceneMode_environmental"), std::string::npos);
    EXPECT_NE(aige::ExportGsoShapesTurtle(Gso()).find("sh:in ( \"cutscene\""), std::string::npos);
}

TEST(StoryGraphTest, TheTestStoryPassesEveryRule) {
    const auto issues = Issues(Good());
    EXPECT_TRUE(issues.empty()) << Describe(issues);
}

TEST(StoryGraphTest, SavesLoadsAndApplies) {
    StoryGraph g = Good();
    const fs::path path = StoryGraph::PathIn(fs::temp_directory_path() / "aige_story_graph_save");
    ASSERT_TRUE(g.Save(path));
    auto back = StoryGraph::Load(path);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->nodes.size(), g.nodes.size());
    EXPECT_EQ(back->edges.size(), g.edges.size());
    EXPECT_EQ(back->ToJson(), g.ToJson());
    // The AI's facts: a new character, a fact asserted, a fact ended at a scene.
    const int revision = g.revision;
    EXPECT_EQ(g.Apply({{"nodes", {{{"id", "character/odo"}, {"class", "Character"}, {"name", "Odo"}}}},
                       {"assert", {{{"p", "locatedAt"}, {"s", "character/odo"}, {"o", "location/riverford"}, {"from", "s-02"}}}},
                       {"retract", {{{"p", "locatedAt"}, {"s", "character/mara"}, {"o", "location/riverford"}, {"at", "s-05"}}}}},
                      Gso()),
              "");
    EXPECT_EQ(g.revision, revision + 1);
    EXPECT_EQ(g.LocationsAt("character/odo", "s-03"), std::vector<std::string>{"location/riverford"});
    EXPECT_TRUE(g.LocationsAt("character/odo", "s-01").empty());
    EXPECT_TRUE(g.LocationsAt("character/mara", "s-05").empty());
    EXPECT_FALSE(g.LocationsAt("character/mara", "s-03").empty());
    // Refused, and nothing changed.
    const json before = g.ToJson();
    EXPECT_NE(g.Apply({{"nodes", {{{"id", "x"}, {"class", "Wizard"}}}}}, Gso()), "");
    EXPECT_NE(g.Apply({{"assert", {{{"p", "flies"}, {"s", "character/odo"}, {"o", "s-01"}}}}}, Gso()), "");
    EXPECT_NE(g.Apply({{"assert", {{{"p", "locatedAt"}, {"s", "character/odo"}, {"o", "item/amulet"}}}}}, Gso()), "");
    EXPECT_NE(g.Apply({{"assert", {{{"p", "locatedAt"}, {"s", "character/odo"}, {"o", "location/tower"}, {"from", "s-99"}}}}}, Gso()), "");
    EXPECT_EQ(g.ToJson(), before);
    std::error_code ec;
    fs::remove_all(path.parent_path().parent_path(), ec);
}

// --- The competency questions (story-ontology.md §9) ---------------------------------------------

TEST(StoryGraphTest, Q1WhoIsAliveAndWhereAtASceneOnABranch) {
    const StoryGraph g = Good();
    EXPECT_TRUE(g.AliveAt("character/aldric", "s-04b"));
    EXPECT_FALSE(g.AliveAt("character/aldric", "s-04c"));               // killed on the "kill" branch
    EXPECT_FALSE(g.AliveAt("character/aldric", "s-05", "kill"));
    EXPECT_TRUE(g.AliveAt("character/aldric", "s-05"));                 // the main line: alive
    EXPECT_TRUE(g.AliveAt("character/aldric", "s-05", "spare"));
    EXPECT_EQ(g.LocationsAt("character/tamsin", "s-03"), std::vector<std::string>{"location/riverford-temple"});
    // Possession moves at the confrontation, on that branch only.
    EXPECT_EQ(g.PossessorsAt("item/amulet", "s-03"), std::vector<std::string>{"character/aldric"});
    EXPECT_EQ(g.PossessorsAt("item/amulet", "s-05", "kill"), std::vector<std::string>{"character/warden"});
    EXPECT_TRUE(g.PossessorsAt("item/amulet", "s-05", "spare").empty());
    // The world state of a scene.
    const auto facts = g.FactsAt("s-01");
    EXPECT_TRUE(std::any_of(facts.begin(), facts.end(), [](const aige::StoryEdge* e) { return e->Text("type") == "trusts"; }));
    EXPECT_FALSE(std::any_of(facts.begin(), facts.end(), [](const aige::StoryEdge* e) { return e->Text("type") == "betrayed-by"; }));
}

TEST(StoryGraphTest, Q2WhatACharacterKnows) {
    const StoryGraph g = Good();
    EXPECT_TRUE(g.KnowsAt("character/tamsin", "aldric-stole-amulet", "s-02"));
    EXPECT_FALSE(g.KnowsAt("character/mara", "aldric-stole-amulet", "s-02"));
}

TEST(StoryGraphTest, Q3WhatAStageAdvancesAndWhatItNeedsWhere) {
    const StoryGraph g = Good();
    EXPECT_EQ(g.ScenesAdvancedBy("q-main/10"), std::vector<std::string>{"s-03"});
    EXPECT_EQ(g.QuestOfStage("q-main/10"), "q-main");
    EXPECT_EQ(g.LocationOfScene("s-03"), "location/hideout");
    EXPECT_EQ(g.Find(g.LocationOfScene("s-03"))->Text("buildType"), "dungeon");
}

TEST(StoryGraphTest, Q4RolesAndCharacters) {
    StoryGraph g = Good();
    EXPECT_EQ(g.RolelessCharacters(Gso()), std::vector<std::string>{"character/brann"});
    EXPECT_TRUE(g.UnfilledRoles(Gso()).empty());
    g.Upsert("role/herald", "NarrativeRole").fields["archetype"] = "herald";
    EXPECT_EQ(g.UnfilledRoles(Gso()), std::vector<std::string>{"role/herald"});
}

TEST(StoryGraphTest, Q5OpenThreads) {
    StoryGraph g = Good();
    // Before the end: the conflict is open at s-03; by s-05 (after the confrontation on its branch) ...
    const json early = g.Subgraph("s-03");
    EXPECT_EQ(early["openThreads"]["conflicts"], json::array({"conflict/main"}));
    EXPECT_EQ(early["openThreads"]["setups"], json::array({"item/amulet"}));
    const json late = g.Subgraph("s-06");
    EXPECT_TRUE(late["openThreads"]["setups"].empty());
}

TEST(StoryGraphTest, Q6QuestsBrokenByADeath) {
    const StoryGraph g = Good();
    EXPECT_EQ(g.QuestsBrokenByDeath("character/aldric", "s-04c"), std::vector<std::string>{"q-side"});
    EXPECT_TRUE(g.QuestsBrokenByDeath("character/aldric", "s-06").empty()); // after it's needed
    EXPECT_TRUE(g.QuestsBrokenByDeath("character/tamsin", "s-01").empty());
}

TEST(StoryGraphTest, Q7PlacesAndTheirParts) {
    const auto places = Good().PlacesUsed();
    EXPECT_EQ(places.size(), 4u);
    EXPECT_EQ(places.at("location/riverford-temple"), std::set<std::string>{"altar"});
    EXPECT_TRUE(places.contains("location/tower"));
}

TEST(StoryGraphTest, Q8WhoThePlayerMeetsInAChapter) {
    const StoryGraph g = Good();
    EXPECT_EQ(g.CharactersMet("ch-1"), (std::vector<std::string>{"character/tamsin", "character/mara"}));
    EXPECT_EQ(g.CharactersMet("ch-2"), (std::vector<std::string>{"character/brann", "character/aldric"}));
}

TEST(StoryGraphTest, Q9TheGiversMotivationAndStrategy) {
    const StoryGraph g = Good();
    const auto givers = g.In("q-main", "gives");
    ASSERT_EQ(givers.size(), 1u);
    EXPECT_EQ(givers[0]->s, "character/tamsin");
    EXPECT_EQ(g.Find("q-main")->Text("motivation"), "protection");
    const auto& strategies = Gso().strategies.at("protection");
    EXPECT_NE(std::find(strategies.begin(), strategies.end(), g.Find("q-main")->Text("strategy")), strategies.end());
}

TEST(StoryGraphTest, Q10WhyASceneExists) {
    const StoryGraph g = Good();
    EXPECT_EQ(g.MadeBy("s-02"), "d-1");
    EXPECT_EQ(g.Find(g.MadeBy("s-02"))->Text("rationale"), "the mentor reveals; it costs her");
}

TEST(StoryGraphTest, Q11WhatDependsOnAnEvent) {
    const StoryGraph g = Good();
    const auto deps = g.Dependents("e-theft");
    for (const char* id : {"e-curse", "e-reveal", "e-confront", "e-return", "s-01", "s-02", "s-04b", "s-06"}) {
        EXPECT_NE(std::find(deps.begin(), deps.end(), id), deps.end()) << id;
    }
    const auto onAldric = g.Dependents("character/aldric");
    EXPECT_NE(std::find(onAldric.begin(), onAldric.end(), "q-side"), onAldric.end());
    EXPECT_NE(std::find(onAldric.begin(), onAldric.end(), "q-main/alias/thief"), onAldric.end());
    // A node's facts over time.
    const json timeline = g.Timeline("character/tamsin");
    ASSERT_FALSE(timeline.empty());
}

TEST(StoryGraphTest, TheSubgraphForAScene) {
    const json sub = Good().Subgraph("s-05", "kill");
    EXPECT_EQ(sub["scene"]["id"], "s-05");
    EXPECT_EQ(sub["location"]["id"], "location/riverford");
    EXPECT_EQ(sub["location"]["earlierScenes"], json::array({"s-01"}));
    bool sawAldric = false;
    for (const auto& p : sub["participants"]) {
        if (p["id"] == "character/aldric") {
            sawAldric = true;
            EXPECT_FALSE(p["alive"].get<bool>());
        }
        if (p["id"] == "character/tamsin") EXPECT_EQ(p["relationships"][0]["type"], "betrayed-by");
    }
    EXPECT_TRUE(sawAldric);
}

// --- Each rule catches its planted error ---------------------------------------------------------

TEST(StoryRulesTest, Structure) {
    {
        StoryGraph g = Good();
        g.Upsert("act-4", "Act").fields["order"] = 4;
        EXPECT_TRUE(Has(Issues(g), "R-S1"));
    }
    {
        StoryGraph g = Good();
        auto& n = g.Upsert("s-99", "StoryScene");
        n.fields = {{"order", 7}, {"mode", "dialogue"}};
        EXPECT_TRUE(Has(Issues(g), "R-S2"));
    }
    {
        StoryGraph g = Good();
        auto& n = g.Upsert("s-07", "StoryScene");
        n.fields = {{"order", 4.7}, {"mode", "dialogue"}, {"branch", "flee"}};
        EXPECT_TRUE(Has(Issues(g), "R-S3"));
    }
    {
        StoryGraph g = Good();
        g.Find("choice/aldric")->fields["options"] = {"spare", "kill", "flee"};
        EXPECT_TRUE(Has(Issues(g), "R-S4"));
    }
    {
        StoryGraph g = Good();
        g.Find("s-05")->fields.erase("climax");
        g.Find("s-02")->fields["climax"] = true;
        EXPECT_TRUE(Has(Issues(g), "R-S5"));
    }
    {
        StoryGraph g = Good();
        g.Find("s-02")->fields["chargeAfter"] = -1;
        EXPECT_TRUE(Has(Issues(g), "R-S6"));
    }
}

TEST(StoryRulesTest, Causality) {
    {
        StoryGraph g = Good();
        Remove(g, "causes", "e-theft", "e-curse");
        EXPECT_TRUE(Has(Issues(g), "R-C1"));
    }
    {
        StoryGraph g = Good();
        Remove(g, "serves", "e-theft");
        EXPECT_TRUE(Has(Issues(g), "R-C2"));
    }
    {
        StoryGraph g = Good();
        Remove(g, "paysOff", "e-return");
        EXPECT_TRUE(Has(Issues(g), "R-C3"));
    }
    {
        StoryGraph g = Good();
        Remove(g, "resolves", "e-confront");
        EXPECT_TRUE(Has(Issues(g), "R-C4"));
    }
}

TEST(StoryRulesTest, Continuity) {
    {
        StoryGraph g = Good();
        Add(g, {{"p", "participant"}, {"s", "s-04c"}, {"o", "character/aldric"}});
        EXPECT_TRUE(Has(Issues(g), "R-T1"));
        g.Find("s-04c")->fields["flashback"] = true; // a flashback may show the dead
        EXPECT_FALSE(Has(Issues(g), "R-T1"));
    }
    {
        StoryGraph g = Good();
        Add(g, {{"p", "locatedAt"}, {"s", "character/tamsin"}, {"o", "location/riverford"}});
        EXPECT_TRUE(Has(Issues(g), "R-T2"));
    }
    {
        StoryGraph g = Good();
        Add(g, {{"p", "mentions"}, {"s", "character/mara"}, {"about", "aldric-stole-amulet"}, {"scene", "s-02"}});
        EXPECT_TRUE(Has(Issues(g), "R-T3"));
    }
    {
        StoryGraph g = Good();
        Add(g, {{"p", "possesses"}, {"s", "character/mara"}, {"o", "item/amulet"}});
        EXPECT_TRUE(Has(Issues(g), "R-T4"));
    }
    {
        StoryGraph g = Good();
        Add(g, {{"p", "hasRelationship"}, {"s", "character/mara"}, {"o", "character/aldric"}, {"type", "fears"}, {"from", "s-03"}});
        EXPECT_TRUE(Has(Issues(g), "R-T5"));
    }
    {
        StoryGraph g = Good();
        auto& island = g.Upsert("location/island", "Location");
        island.fields = {{"name", "The Island"}, {"buildType", "wilderness"}};
        g.Find("s-03")->fields["location"] = "location/island";
        EXPECT_TRUE(Has(Issues(g), "R-T6"));
    }
}

TEST(StoryRulesTest, Characters) {
    {
        StoryGraph g = Good();
        Remove(g, "fills", "character/aldric", "role/antagonist");
        EXPECT_TRUE(Has(Issues(g), "R-K1"));
    }
    {
        StoryGraph g = Good();
        g.Find("character/aldric")->fields.erase("need");
        EXPECT_TRUE(Has(Issues(g), "R-K2"));
    }
    {
        StoryGraph g = Good();
        Remove(g, "hasRelationship", "character/brann", "character/mara");
        EXPECT_TRUE(Has(Issues(g), "R-K3"));
    }
    {
        StoryGraph g = Good();
        auto& n = g.Upsert("character/aldwin", "Character");
        n.fields = {{"name", "Aldwin"}, {"importance", "major"}};
        EXPECT_TRUE(Has(Issues(g), "R-K4"));
    }
    {
        StoryGraph g = Good();
        g.Find("character/mara")->fields.erase("stock");
        EXPECT_TRUE(Has(Issues(g), "R-K5"));
    }
    {
        StoryGraph g = Good();
        g.Find("character/tamsin")->fields.erase("package");
        EXPECT_TRUE(Has(Issues(g), "R-K6"));
    }
}

TEST(StoryRulesTest, Quests) {
    {
        StoryGraph g = Good();
        Remove(g, "advancesScene", "q-main/10");
        EXPECT_TRUE(Has(Issues(g), "R-Q1"));
    }
    {
        StoryGraph g = Good();
        g.Find("q-main/alias/thief")->fields["fill"] = "character/nobody";
        EXPECT_TRUE(Has(Issues(g), "R-Q2"));
    }
    {
        StoryGraph g = Good();
        Add(g, {{"p", "state"}, {"s", "character/aldric"}, {"state", "dead"}, {"from", "s-03"}});
        const auto issues = Issues(g);
        EXPECT_TRUE(Has(issues, "R-Q3"));
        EXPECT_FALSE(Has(issues, "R-Q7"));
    }
    {
        StoryGraph g = Good();
        Add(g, {{"p", "state"}, {"s", "character/aldric"}, {"state", "dead"}, {"from", "s-03"}, {"byQuest", "q-main"}});
        const auto issues = Issues(g);
        EXPECT_TRUE(Has(issues, "R-Q7"));
        Add(g, {{"p", "excludes"}, {"s", "q-main"}, {"o", "q-side"}});
        EXPECT_FALSE(Has(Issues(g), "R-Q7"));
    }
    {
        StoryGraph g = Good();
        g.Find("character/tamsin")->fields["motivations"] = {"knowledge"};
        EXPECT_TRUE(Has(Issues(g), "R-Q4"));
    }
}

TEST(StoryRulesTest, PlacesAndShapes) {
    {
        StoryGraph g = Good();
        g.Find("s-06")->fields["location"] = "location/nowhere";
        EXPECT_TRUE(Has(Issues(g), "R-P1"));
    }
    {
        StoryGraph g = Good();
        Remove(g, "connects", "location/hideout", "location/tower");
        EXPECT_TRUE(Has(Issues(g), "R-P2"));
    }
    {
        StoryGraph g = Good();
        g.Find("s-02")->fields["needsParts"] = {"altar", "font"};
        EXPECT_TRUE(Has(Issues(g), "R-P3"));
    }
    {
        // The test story's memberOf a location (not a faction), a made-up class, a mode that isn't one.
        StoryGraph g = StoryGraph::FromJson(json::parse(kStory));
        g.Upsert("x", "Wizard");
        g.Find("s-06")->fields["mode"] = "musical";
        const auto issues = Issues(g);
        int shapes = 0;
        for (const auto& i : issues) shapes += i.rule == "shape";
        EXPECT_EQ(shapes, 3) << Describe(issues);
        // New errors only: what a change adds.
        EXPECT_EQ(aige::NewErrors(Issues(Good()), issues).size(), 3u);
    }
}

TEST(StoryGraphTest, WorldBibleRelationsComeFromTheGraph) {
    const fs::path root = fs::temp_directory_path() / "aige_story_world_bible";
    fs::remove_all(root);
    fs::create_directories(root / "world" / "character");
    fs::create_directories(root / "world" / "location");
    aige::WorldBible bible{aige::Project{root}};
    aige::WorldEntry tamsin;
    tamsin.kind = "character";
    tamsin.id = "tamsin";
    tamsin.name = "Tamsin";
    tamsin.relations = {{"friend", "character/odo"}}; // hand-written: not in the graph yet
    aige::WorldEntry odo{"character", "odo", "Odo"};
    aige::WorldEntry aldric{"character", "aldric", "Aldric"};
    aige::WorldEntry temple{"location", "riverford-temple", "Riverford Temple"};
    for (const auto* e : {&tamsin, &odo, &aldric, &temple}) ASSERT_FALSE(bible.Save(*e));
    StoryGraph g = Good();
    // A hand edit comes into the graph...
    EXPECT_EQ(g.ReadRelationsFrom(bible), 1);
    ASSERT_NE(g.Find("character/odo"), nullptr); // made from its World Bible entry
    // ... and the end-of-story relations go out as rel: lines.
    EXPECT_GE(g.WriteRelationsTo(bible), 1);
    bible.Refresh();
    const auto* written = bible.Find("character/tamsin");
    ASSERT_NE(written, nullptr);
    std::vector<std::string> lines;
    for (const auto& r : written->relations) lines.push_back(r.type + " " + r.target);
    EXPECT_EQ(lines, (std::vector<std::string>{"betrayed_by character/aldric", "friend character/odo",
                                                "located_at location/riverford-temple"}));
    EXPECT_EQ(g.ReadRelationsFrom(bible), 0); // nothing new: one fact, one place
    std::error_code ec;
    fs::remove_all(root, ec);
}
