#include <gtest/gtest.h>

#include "places/Ontology.h"
#include "planner/GamePlan.h"
#include "planner/PlannerDocs.h"
#include "story/StoryRules.h"
#include "world/WorldBible.h"

#include <deque>
#include <filesystem>
#include <memory>
#include <set>

// development_plan.md Phase 22: the AI Planner designs a game — the Game Brief, outline, story
// scene by scene, characters, quests, places with Briefs, systems, visuals and validation —
// with every item answering Why?, surviving a closed editor, regenerating only what's stale.
using aige::GamePlan;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const aige::Ontology& Places() {
    static aige::Ontology ontology = [] {
        aige::Ontology o;
        o.Load({aige::EnginePlacesDir()});
        return o;
    }();
    return ontology;
}

// The AI service, answering from a script: a short ARPG, "The Moon Amulet".
json Answer(const std::string& path, const json& body) {
    json a = {{"ok", true}, {"rationale", "Because the story needs it."}};
    if (path == "/planner/concept") {
        a["brief"] = {{"title", "The Moon Amulet"}, {"logline", "A disgraced warden must recover a stolen amulet before the dead wake."},
                      {"genre", "arpg"}, {"playTime", body.value("reading", "") == "generous" ? 4 : 2}, {"tone", {"dark"}},
                      {"setting", {{"world", "A river valley under a pale moon."}}}};
        a["confidence"] = {{"genre", 0.9}};
        a["decisions"] = {{{"question", "A fixed hero or one you make?"}, {"assumed", "fixed"}, {"path", "playerCharacter.kind"}}};
    } else if (path == "/planner/outline") {
        a["outline"] = json::parse(R"({
          "story": {"logline": "A disgraced warden must recover a stolen amulet.", "synopsis": "Aldric steals the amulet; Tamsin names him; the warden takes it back.",
            "structure": "three-act",
            "acts": [{"id": "act-1", "purpose": "setup", "summary": "The theft and the accusation."},
                     {"id": "act-2", "purpose": "confrontation", "summary": "Into the crypt after Aldric."},
                     {"id": "act-3", "purpose": "resolution", "summary": "The amulet comes home."}],
            "storylines": [{"id": "main", "kind": "main", "summary": "The amulet."}],
            "endings": [{"id": "end-a", "summary": "The dead sleep."}]},
          "cast": [{"id": "character/warden", "name": "The Warden", "importance": "major", "player": true, "narrativeRoles": ["protagonist"], "line": "A disgraced guard."},
                   {"id": "character/aldric", "name": "Aldric", "importance": "major", "narrativeRoles": ["antagonist"], "gameplayRoles": ["boss"], "line": "The thief."},
                   {"id": "character/tamsin", "name": "Tamsin", "importance": "supporting", "narrativeRoles": ["mentor"], "gameplayRoles": ["quest-giver"], "line": "The temple healer."},
                   {"id": "character/mara", "name": "Mara", "importance": "minor", "gameplayRoles": ["vendor"], "line": "Sells potions."}],
          "places": [{"id": "location/riverford", "name": "Riverford", "kind": "settlement", "line": "A river town."},
                     {"id": "location/temple", "name": "The Temple", "kind": "interior", "parent": "location/riverford", "line": "Tamsin's temple."},
                     {"id": "location/crypt", "name": "The Crypt", "kind": "dungeon", "line": "Where Aldric hides."}],
          "connections": [{"from": "location/riverford", "to": "location/crypt", "kind": "road"}]})");
    } else if (path == "/planner/act") {
        const std::string act = body["act"].value("id", "");
        json scenes;
        if (act == "act-1") {
            scenes = json::parse(R"([
              {"id": "a", "mode": "cutscene", "kernel": true, "beat": "inciting-incident", "goal": "Show the theft", "value": "life-death",
               "chargeBefore": 0, "chargeAfter": -1, "location": "location/riverford", "participants": ["character/warden", "character/tamsin"]},
              {"id": "b", "mode": "dialogue", "kernel": true, "goal": "Learn who stole it", "value": "truth-lie", "chargeBefore": -1, "chargeAfter": 1,
               "location": "location/temple", "participants": ["character/warden", "character/tamsin"], "needsParts": ["altar"]}])");
        } else if (act == "act-2") {
            scenes = json::parse(R"([{"id": "c", "mode": "gameplay", "kernel": true, "goal": "Defeat Aldric", "value": "life-death",
               "chargeBefore": -1, "chargeAfter": 1, "location": "location/crypt", "participants": ["character/warden", "character/aldric"]}])");
        } else {
            scenes = json::parse(R"([{"id": "d", "mode": "dialogue", "beat": "climax", "goal": "Return the amulet", "location": "location/riverford",
               "participants": ["character/warden", "character/tamsin"]}])");
        }
        a["act"] = {{"chapters", {{{"id", "ch"}, {"question", "Will the amulet come home?"}, {"summary", "..."}, {"scenes", scenes}}}}};
    } else if (path == "/planner/scene") {
        const std::string id = body.value("id", "");
        json scene = {{"summary", "Scene " + id + " happens."}, {"beats", {{{"function", "turn"}, {"text", "Something turns."}}}}};
        if (id == "s1-01") {
            scene["events"] = json::parse(R"([{"id": "theft", "class": "Action", "agent": "character/aldric", "serves": "aldric-power",
                                               "text": "Aldric steals the amulet", "kernel": true, "inciting": true}])");
            scene["newEntities"] = json::parse(R"([{"id": "item/amulet", "class": "Item", "name": "The Moon Amulet", "line": "It keeps the dead asleep."}])");
            scene["facts"] = json::parse(R"([{"p": "possesses", "s": "character/aldric", "o": "item/amulet"}])");
        } else if (id == "s1-02") {
            scene["facts"] = json::parse(R"([{"p": "knows", "s": "character/warden", "about": "aldric-stole-the-amulet"}])");
        } else if (id == "s2-01") {
            scene["events"] = json::parse(R"([{"id": "fight", "class": "Action", "agent": "character/warden", "serves": "recover-amulet",
                                               "text": "The warden defeats Aldric", "kernel": true, "causes": []}])");
            scene["facts"] = json::parse(R"([{"p": "state", "s": "character/aldric", "state": "dead"},
                                              {"p": "possesses", "s": "character/warden", "o": "item/amulet"}])");
            scene["end"] = json::parse(R"([{"p": "possesses", "s": "character/aldric", "o": "item/amulet"}])");
            scene["questStage"] = {{"index", 10}, {"journal", "Take the amulet back from Aldric."}};
        }
        a["scene"] = scene;
    } else if (path == "/planner/critic" || path == "/planner/review") {
        a["findings"] = json::array();
    } else if (path == "/planner/character") {
        const std::string id = body["entry"].value("id", "");
        const std::string slug = aige::PlanSlug(id);
        json c = {{"name", body["entry"].value("name", slug)},
                  {"line", body["entry"].value("line", "")},
                  {"biography", {{"backstory", slug + " grew up by the river."}, {"personality", {{"brave", 0.6}}}, {"need", "peace"},
                                 {"desire", "the amulet"}, {"motivations", {"protection"}}, {"arc", {{"from", "doubt"}, {"to", "resolve"}}}}},
                  {"gameplay", {{"roles", json::array()}, {"class", "c-" + slug}}},
                  {"behaviour", {{"packages", {{{"template", "work"}, {"place", "location/riverford"}}}}}},
                  {"home", id == "character/tamsin" ? "location/temple" : "location/riverford"}};
        if (id == "character/mara") c["gameplay"]["stock"] = "potions";
        if (id == "character/warden") c["behaviour"] = {{"packages", json::array()}};
        if (id == "character/tamsin") c["relationships"] = {{{"with", "character/aldric"}, {"type", "trusts"}, {"valence", 0.4}}};
        a["character"] = c;
    } else if (path == "/planner/quests") {
        a["quests"] = json::parse(R"([
          {"id": "q-main", "name": "The Moon Amulet", "type": "main", "motivation": "protection", "strategy": "recover-stolen-item",
           "giver": {"alias": "giver"}, "level": [1, 3],
           "aliases": [{"id": "giver", "kind": "character", "fill": "character/tamsin"},
                       {"id": "hiding-spot", "kind": "spot", "in": "location/crypt", "where": {"anchor": "altar"}}],
           "stages": [{"index": 10, "journal": "Take the amulet back.", "advancesScene": "s2-01",
                       "objectives": [{"action": "reach", "target": "hiding-spot"}]},
                      {"index": 100, "terminal": "completed"}],
           "rewards": [{"xp": 300}, {"item": "warden-blade"}]},
          {"id": "q-herbs", "name": "Herbs for Mara", "type": "side", "motivation": "wealth", "strategy": "gather-raw-materials",
           "giver": {"alias": "giver"}, "level": [1, 2],
           "aliases": [{"id": "giver", "kind": "character", "fill": "character/mara"}],
           "stages": [{"index": 10, "journal": "Gather herbs.", "objectives": [{"action": "gather", "target": "giver", "count": 5}]},
                      {"index": 100, "terminal": "completed"}]}])");
    } else if (path == "/planner/place") {
        const std::string kind = body.value("kind", "settlement");
        json place = {{"kind", kind},
                      {"description", {{"purpose", "Where the story needs it."}, {"mood", "grim"}}},
                      {"brief", {{"schema", "aige.brief/1"}, {"buildType", kind}}}};
        if (body["entry"].value("id", "") == "location/crypt") place["storyAnchors"] = {{{"id", "altar"}, {"module", "altar"}, {"for", {{"quest", "q-main"}}}}};
        if (body["entry"].value("id", "") == "location/temple") place["storyAnchors"] = {{{"id", "altar"}, {"module", "altar"}}};
        a["place"] = place;
    } else if (path == "/planner/systems") {
        a["systems"] = {{"summary", {{"economy", "gold"}}}, {"records", {{{"type", "item"}, {"id", "warden-blade"}, {"record", {{"name", "Warden's Blade"}}}}}}};
    } else if (path == "/planner/revise") {
        a["edits"] = {{{"doc", "characters/tamsin"}, {"pointer", "/biography/need"}, {"value", "forgiveness"}, {"reason", "asked"}}};
    } else if (path == "/planner/visuals") {
        a["ui"] = {{"theme", "stone"},
                   {"screens", {"title", "pause", "settings", "save-load", "game-over", "inventory", "journal", "map", "spellbook", "dialogue"}},
                   {"hud", {"health", "mana", "quest-tracker"}}};
    }
    return a;
}

struct FakeService {
    struct Request {
        int id;
        std::string path;
        json body;
    };
    std::deque<Request> outstanding;
    std::vector<std::string> paths; // every request, in order
    int next = 1;
    bool on = true;

    GamePlan::Host Host(const fs::path& root) {
        GamePlan::Host host;
        host.projectRoot = root;
        host.places = [] () -> const aige::Ontology& { return Places(); };
        host.catalog = {{"packageTemplates", {"work", "wander", "guard", "sleep"}},
                        {"themes", {"stone", "parchment"}},
                        {"buildTypes", {"settlement", "interior", "dungeon", "wilderness"}},
                        {"recordTypes", {{"item", {"name"}}}}};
        host.request = [this](const std::string& path, const json& body, int) {
            if (!on) return 0;
            outstanding.push_back({next, path, body});
            paths.push_back(path);
            return next++;
        };
        return host;
    }
    // Answers what's asked (and what that asks in turn) until nothing is outstanding.
    void Drive(GamePlan& plan, int limit = 1000) {
        while (!outstanding.empty() && limit-- > 0) {
            Request r = outstanding.front();
            outstanding.pop_front();
            plan.OnResponse(r.id, Answer(r.path, r.body));
        }
    }
};

struct Planner {
    fs::path root;
    FakeService service;
    std::unique_ptr<GamePlan> plan;
    explicit Planner(const std::string& name) : root(fs::temp_directory_path() / ("aige_planner_" + name)) {
        fs::remove_all(root);
        fs::create_directories(root / "world");
        plan = std::make_unique<GamePlan>(service.Host(root));
    }
    ~Planner() {
        plan.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    // ① to the end of ②, approving the outline.
    void ThroughOutline() {
        ASSERT_EQ(plan->Start("a short dark-fantasy ARPG about a disgraced warden recovering a stolen amulet"), "");
        service.Drive(*plan);
        ASSERT_EQ(plan->Status(), "waiting");
        ASSERT_EQ(plan->Confirm(), "");
        service.Drive(*plan);
        ASSERT_EQ(plan->Stage(), "outline");
        ASSERT_EQ(plan->Status(), "waiting"); // autonomy "ask" for the outline
        ASSERT_EQ(plan->Approve(), "");
    }
};

std::string Findings(const json& report) {
    std::string out;
    for (const auto& f : report.value("findings", json::array())) {
        out += "  " + f.value("rule", "") + " " + f.value("severity", "") + " " + f.value("subject", "") + ": " + f.value("message", "") + "\n";
    }
    return out;
}

} // namespace

TEST(PlannerDocsTest, BriefsProfilesScopeAndTheWindow) {
    EXPECT_EQ(aige::ScopeClassFor(0.5), "micro");
    EXPECT_EQ(aige::ScopeClassFor(3), "short");
    EXPECT_EQ(aige::ScopeClassFor(60), "epic");
    const auto genres = aige::PlannerData::Engine().Genres();
    for (const char* g : {"arpg", "rpg", "action-adventure", "life-sim"}) EXPECT_NE(std::find(genres.begin(), genres.end(), g), genres.end()) << g;
    std::vector<std::string> assumed;
    const json brief = aige::CompleteGameBrief({{"title", "Farm Days"}, {"logline", "Grow a farm."}, {"genre", "life-sim"}, {"playTime", 8}}, {}, &assumed);
    EXPECT_EQ(brief["scopeClass"], "medium");
    EXPECT_EQ(aige::BriefGet(brief, "systems.farming.on"), true);
    EXPECT_EQ(aige::BriefGet(brief, "plan.autonomy.outline"), "ask");
    EXPECT_NE(std::find(assumed.begin(), assumed.end(), "perspective"), assumed.end());
    const auto& gso = aige::StoryOntology::Engine();
    EXPECT_FALSE(aige::HasErrors(aige::ValidateGameBrief(brief, gso)));
    json bad = brief;
    bad["genre"] = "shmup";
    bad["perspective"] = "side";
    bad["playTime"] = 500;
    const auto problems = aige::ValidateGameBrief(bad, gso);
    EXPECT_GE(problems.size(), 3u);
    // The window: tabs from the schema, asked fields first, values in.
    const json window = aige::BriefWindow(brief, {{"genre", {{"confidence", 0.5}}}}, gso);
    ASSERT_GE(window.size(), 8u);
    EXPECT_EQ(window[0]["id"], "game");
    EXPECT_TRUE(window[0]["fields"][0]["asked"].get<bool>());
    bool sawTone = false;
    for (const auto& tab : window) {
        for (const auto& f : tab["fields"]) {
            if (f["path"] == "tone") sawTone = f.contains("options") && !f["options"].empty();
        }
    }
    EXPECT_TRUE(sawTone); // a GSO vocabulary
}

TEST(PlannerDocsTest, MergingReadingsMarksWhatDiffers) {
    json marks;
    const json merged = aige::MergeGameBriefs({{{"reading", "literal"}, {"brief", {{"genre", "arpg"}, {"playTime", 2}}}},
                                               {{"reading", "typical"}, {"brief", {{"genre", "arpg"}, {"playTime", 3}}}},
                                               {{"reading", "generous"}, {"brief", {{"genre", "arpg"}, {"playTime", 5}}}}},
                                              &marks);
    EXPECT_EQ(merged["genre"], "arpg");
    EXPECT_EQ(merged["playTime"], 3); // a three-way tie goes to the typical reading
    EXPECT_FALSE(marks["genre"].contains("differ"));
    EXPECT_EQ(marks["playTime"]["differ"].size(), 2u);
    EXPECT_TRUE(marks["playTime"]["guessed"].get<bool>());
    const json fallback = aige::FallbackGameBrief("a cozy farm life game, 10 hours");
    EXPECT_EQ(fallback["genre"], "life-sim");
    EXPECT_EQ(fallback["playTime"], 10.0);
}

TEST(PlannerTest, AShortArpgIsDesignedAndValidated) {
    Planner p("design");
    p.ThroughOutline();
    p.service.Drive(*p.plan);
    const json state = p.plan->State();
    const json report = p.plan->Document("report");
    EXPECT_EQ(p.plan->Status(), "done") << p.plan->State()["message"] << "\n" << Findings(report);
    EXPECT_EQ(report["summary"]["errors"], 0) << Findings(report);
    // The design: story, cast, quests, places with Briefs, records, UI.
    EXPECT_EQ(state["acts"].size(), 3u);
    EXPECT_EQ(p.plan->Graph().SceneOrder().size(), 4u);
    EXPECT_EQ(p.plan->Store().List("characters").size(), 4u);
    EXPECT_EQ(p.plan->Store().List("quests").size(), 2u);
    ASSERT_EQ(p.plan->Store().List("places").size(), 3u);
    const json crypt = p.plan->Document("places/crypt");
    EXPECT_EQ(crypt["brief"]["buildType"], "dungeon");
    EXPECT_FALSE(aige::HasErrors(aige::ValidatePlaceDesc(crypt, Places()))) << aige::ProblemTexts(aige::ValidatePlaceDesc(crypt, Places()))[0];
    EXPECT_EQ(p.plan->Document("systems")["records"].size(), 1u);
    EXPECT_EQ(p.plan->Document("ui")["theme"], "stone");
    EXPECT_FALSE(state["baseline"].is_null());
    // The graph: Aldric dies in the crypt and the warden ends up with the amulet.
    const auto g = p.plan->Graph();
    EXPECT_FALSE(g.AliveAt("character/aldric", "s3-01"));
    EXPECT_EQ(g.PossessorsAt("item/amulet", "s3-01"), std::vector<std::string>{"character/warden"});
    EXPECT_EQ(g.ScenesAdvancedBy("q-main/10"), std::vector<std::string>{"s2-01"});
    // The World Bible has the cast, with the biographies.
    aige::WorldBible bible{aige::Project{p.root}};
    bible.Refresh();
    ASSERT_NE(bible.Find("character/tamsin"), nullptr);
    EXPECT_NE(bible.Find("character/tamsin")->body.find("grew up by the river"), std::string::npos);
    ASSERT_NE(bible.Find("location/crypt"), nullptr);
    // Every item answers Why?
    for (const char* doc : {"outline", "story/scenes/s2-01", "characters/tamsin", "quests/q-main", "places/crypt", "systems", "ui"}) {
        const json why = p.plan->Why(doc);
        EXPECT_TRUE(why.contains("decision")) << doc;
        EXPECT_FALSE(why.value("rationale", "").empty()) << doc;
    }
    EXPECT_TRUE(p.plan->Why("character/aldric").contains("decision"));
    // The AI panel's card.
    EXPECT_NE(p.plan->Card()["headline"].get<std::string>().find("4 scenes"), std::string::npos);
}

TEST(PlannerTest, ItSurvivesClosingTheEditorMidPlan) {
    Planner p("resume");
    p.ThroughOutline();
    // A few answers, then the editor closes with requests outstanding.
    for (int i = 0; i < 3 && !p.service.outstanding.empty(); ++i) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        p.plan->OnResponse(r.id, Answer(r.path, r.body));
    }
    ASSERT_FALSE(p.service.outstanding.empty());
    p.service.outstanding.clear(); // their answers are lost with the session
    p.plan = std::make_unique<GamePlan>(p.service.Host(p.root));
    ASSERT_EQ(p.plan->Open(), "");
    EXPECT_FALSE(p.service.outstanding.empty()); // what was running is asked again
    p.service.Drive(*p.plan);
    EXPECT_EQ(p.plan->Status(), "done") << Findings(p.plan->Document("report"));
    EXPECT_EQ(p.plan->Graph().SceneOrder().size(), 4u);
}

TEST(PlannerTest, AnEditRegeneratesOnlyWhatDependsOnIt) {
    Planner p("stale");
    p.ThroughOutline();
    p.service.Drive(*p.plan);
    ASSERT_EQ(p.plan->Status(), "done");
    EXPECT_TRUE(p.plan->Stale().empty());
    // Tamsin's need changes: the quests she's in and the systems were made from her.
    ASSERT_EQ(p.plan->Edit("characters/tamsin", "/biography/need", "forgiveness", "a better arc"), "");
    const auto stale = p.plan->Stale();
    EXPECT_NE(std::find(stale.begin(), stale.end(), "quests/q-main"), stale.end());
    EXPECT_NE(std::find(stale.begin(), stale.end(), "systems"), stale.end());
    EXPECT_EQ(std::find(stale.begin(), stale.end(), "story/scenes/s1-01"), stale.end());
    EXPECT_EQ(std::find(stale.begin(), stale.end(), "characters/aldric"), stale.end());
    EXPECT_EQ(p.plan->Graph().Find("character/tamsin")->Text("need"), "forgiveness");
    const size_t before = p.service.paths.size();
    ASSERT_EQ(p.plan->RegenerateStale(), "");
    std::set<std::string> asked(p.service.paths.begin() + static_cast<long>(before), p.service.paths.end());
    EXPECT_EQ(asked, (std::set<std::string>{"/planner/quests", "/planner/systems"}));
    p.service.Drive(*p.plan);
    // A locked document is never regenerated.
    ASSERT_EQ(p.plan->Lock("places/crypt", true), "");
    EXPECT_NE(p.plan->Regenerate("places/crypt"), "");
    // The user's edit is a decision too.
    const json why = p.plan->Why("characters/tamsin");
    bool sawEdit = false;
    for (const auto& d : why["history"]) sawEdit = sawEdit || d.value("kind", "") == "edit";
    EXPECT_TRUE(sawEdit);
}

TEST(PlannerTest, WithoutTheAiTheBriefComesFromTheWords) {
    Planner p("offline");
    p.service.on = false;
    ASSERT_EQ(p.plan->Start("a cozy farm life game"), "");
    EXPECT_EQ(p.plan->Status(), "waiting");
    EXPECT_EQ(p.plan->Document("brief")["genre"], "life-sim");
    EXPECT_FALSE(p.plan->State()["window"].empty());
    // Confirming waits for the AI rather than guessing a story.
    ASSERT_EQ(p.plan->Confirm(), "");
    EXPECT_EQ(p.plan->Status(), "failed");
}

TEST(PlannerTest, TheAiPanelRevisesThePlanInWords) {
    Planner p("revise");
    p.ThroughOutline();
    p.service.Drive(*p.plan);
    ASSERT_EQ(p.plan->Status(), "done");
    EXPECT_TRUE(p.plan->AboutThePlan("make Tamsin want forgiveness"));
    EXPECT_FALSE(p.plan->AboutThePlan("make the grass greener"));
    ASSERT_EQ(p.plan->Revise("make Tamsin want forgiveness"), "");
    p.service.Drive(*p.plan);
    EXPECT_EQ(p.plan->Document("characters/tamsin")["biography"]["need"], "forgiveness");
    const auto stale = p.plan->Stale();
    EXPECT_NE(std::find(stale.begin(), stale.end(), "quests/q-main"), stale.end());
}
