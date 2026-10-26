#include <fstream>
#include <gtest/gtest.h>

#include "PlannerFixture.h"

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

using namespace planner_fixture;

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
    EXPECT_EQ(aige::BriefGet(brief, "plan.autonomy.outline"), "confirm");
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

TEST(PlannerDocsTest, CastRolesFromTheWrongVocabularyArePutRight) {
    const auto& gso = aige::StoryOntology::Engine();
    json outline = {{"cast", json::array({{{"id", "character/mira"},
                                           {"narrativeRoles", {"ally", "companion", "sidekick-ish"}},
                                           {"gameplayRoles", {"vendor", "mentor"}}}})}};
    const auto notes = aige::RepairOutline(outline, gso);
    EXPECT_EQ(outline["cast"][0]["narrativeRoles"], json::array({"ally", "mentor"}));
    EXPECT_EQ(outline["cast"][0]["gameplayRoles"], json::array({"companion", "vendor"}));
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0], "cast[0]: 'companion' is a gameplay role");
    EXPECT_EQ(notes[1], "cast[0]: dropped 'sidekick-ish' (not a role)");
    EXPECT_TRUE(aige::RepairOutline(outline, gso).empty()); // right the second time
}

TEST(PlannerDocsTest, ValuesThatArentOptionsAreReplacedByTheProfiles) {
    const auto& gso = aige::StoryOntology::Engine();
    json brief = aige::CompleteGameBrief({{"title", "Moon"}, {"logline", "A warden."}, {"genre", "arpg"}, {"pattern", "hub"},
                                          {"tone", {"dark", "spooky"}}, {"endings", 40}});
    EXPECT_TRUE(aige::HasErrors(aige::ValidateGameBrief(brief, gso)));
    const auto notes = aige::RepairGameBrief(brief, gso);
    EXPECT_EQ(brief["pattern"], "branch-and-bottleneck"); // the arpg profile's
    EXPECT_EQ(brief["tone"], json::array({"dark"}));
    EXPECT_EQ(brief["endings"], 6);
    EXPECT_EQ(notes.size(), 3u);
    EXPECT_FALSE(aige::HasErrors(aige::ValidateGameBrief(brief, gso))) << aige::ProblemTexts(aige::ValidateGameBrief(brief, gso))[0];
}

// A step that keeps failing (the model busy three times) stays failed, not done: the plan goes
// on without it, lists it, and Retry makes it, after which the plan is back where it was.
TEST(PlannerTest, AFailedStepCanBeRetried) {
    Planner p("retry");
    p.ThroughOutline();
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        const bool tamsin = r.path == "/planner/character" && r.body.value("entry", json::object()).value("id", "") == "character/tamsin";
        p.plan->OnResponse(r.id, tamsin ? json{{"ok", false}, {"errors", {"the model is busy"}}} : Answer(r.path, r.body));
    }
    const std::string before = p.plan->Status();
    json failed = p.plan->State()["failedJobs"];
    ASSERT_EQ(failed.size(), 1u) << p.plan->State()["message"] << " " << failed.dump();
    EXPECT_EQ(failed[0]["kind"], "character");
    EXPECT_EQ(failed[0]["subject"], "tamsin");
    EXPECT_EQ(failed[0]["doc"], "characters/tamsin");
    EXPECT_FALSE(p.plan->Document("characters/tamsin").is_object());
    const json withFailure = p.plan->State();
    for (const auto& s : withFailure["stages"]) {
        if (s["id"] == "characters") EXPECT_EQ(s["failed"], 1);
    }
    EXPECT_NE(p.plan->RetryJob("j-nope"), "");
    ASSERT_EQ(p.plan->RetryJob(failed[0]["id"]), "");
    EXPECT_EQ(p.plan->Status(), "working");
    p.service.Drive(*p.plan);
    EXPECT_TRUE(p.plan->State()["failedJobs"].empty());
    EXPECT_TRUE(p.plan->Document("characters/tamsin").is_object());
    EXPECT_EQ(p.plan->Status(), before); // back to what it was doing
    EXPECT_EQ(p.plan->RetryFailed(), "nothing failed");
}

// What a scene names that the story doesn't have is put right, not a failed scene: an item
// it names becomes a stub, an id gets its kind, made-up links are left out (PlannerFixture's s1-02).
TEST(PlannerTest, WhatASceneNamesThatIsntThereIsPutRight) {
    Planner p("names");
    p.ThroughOutline();
    p.service.Drive(*p.plan);
    EXPECT_TRUE(p.plan->State()["failedJobs"].empty()) << p.plan->State()["failedJobs"].dump();
    const auto g = p.plan->Graph();
    ASSERT_NE(g.Find("item/ledger"), nullptr);
    EXPECT_EQ(g.Find("item/ledger")->cls, "Item");
    EXPECT_NE(g.Find("item/old-map"), nullptr);
    ASSERT_NE(g.Find("goal/seek-amulet"), nullptr);
    EXPECT_EQ(g.Find("goal/seek-amulet")->cls, "Goal");
    EXPECT_TRUE(std::none_of(g.edges.begin(), g.edges.end(), [](const aige::StoryEdge& e) { return e.o == "conflict/guard-demands-proof"; }));
    EXPECT_TRUE(std::none_of(g.edges.begin(), g.edges.end(), [](const aige::StoryEdge& e) { return e.p == "paysOff" && e.o == "s1-01"; }));
    EXPECT_NE(g.Find("s1-02/e2"), nullptr); // the event without an id got one, as an Event
    // s2-01's: the relationship has a real type, Tamsin learns what she mentions, the warden
    // is in one place at a time (the move starts at s2-01, and riverford ends there).
    bool rival = false, learned = false;
    for (const auto& e : g.edges) {
        rival |= e.p == "hasRelationship" && e.s == "character/warden" && e.Text("type") == "rival";
        learned |= e.p == "knows" && e.s == "character/tamsin" && e.Text("about") == "the-old-seal";
        if (e.p == "locatedAt" && e.o == "location/crypt" && e.s == "character/warden") EXPECT_EQ(e.from, "s2-01");
        if (e.p == "knows" && e.Text("about") == "aldric-is-dead") EXPECT_EQ(e.from, "s2-01");
    }
    EXPECT_TRUE(rival);
    EXPECT_TRUE(learned);
    EXPECT_EQ(g.LocationsAt("character/warden", "s2-01").size(), 1u);
    // The scene board says where each scene is.
    const json state = p.plan->State();
    EXPECT_EQ(state["storyProgress"]["written"], state["storyProgress"]["total"]);
    EXPECT_EQ(state["acts"][0]["chapters"][0]["scenes"][0]["progress"], "written");
}

// A scene whose answer contradicts the story: the retry is a quick fix (the answer goes back
// with its problems), and if the fix still contradicts, the scene is written with what fits
// and the rest left out, quietly: completed, not failed.
TEST(PlannerTest, AContradictingSceneIsFixedQuietly) {
    Planner p("quiet");
    p.ThroughOutline();
    json fixRequest;
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        json a = Answer(r.path, r.body);
        if (r.path == "/planner/scene" && r.body.value("id", "") == "s1-02") {
            // A parent with no child the other way (R-K3), every time.
            a["scene"]["facts"].push_back({{"p", "hasRelationship"}, {"s", "character/warden"}, {"o", "character/tamsin"}, {"type", "parent"}});
            if (r.body.contains("answer")) fixRequest = r.body;
        }
        p.plan->OnResponse(r.id, a);
    }
    ASSERT_TRUE(fixRequest.is_object()) << "the retry should carry the answer to fix";
    EXPECT_TRUE(fixRequest["answer"].contains("scene"));
    EXPECT_NE(fixRequest["problems"].dump().find("R-K3"), std::string::npos);
    const json state = p.plan->State();
    EXPECT_TRUE(state["failedJobs"].empty()) << state["failedJobs"].dump();
    const auto g = p.plan->Graph();
    EXPECT_TRUE(std::none_of(g.edges.begin(), g.edges.end(), [](const aige::StoryEdge& e) { return e.Text("type") == "parent"; }));
    EXPECT_EQ(p.plan->Document("story/scenes/s1-02").value("status", ""), "written");
}

// A small model's character (Test2, 2026-10-23): templates the engine hasn't, a relationship
// type and a motivation outside the vocabulary, a relationship with nobody. Put right, not failed.
TEST(PlannerTest, ACharacterWithMadeUpValuesIsPutRight) {
    Planner p("character");
    p.ThroughOutline();
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        json a = Answer(r.path, r.body);
        if (r.path == "/planner/character" && r.body.value("entry", json::object()).value("id", "") == "character/tamsin") {
            json& c = a["character"];
            c["behaviour"] = {{"packages", {{{"template", "patrol"}}, {{"template", "accompany"}}}}};
            c["relationships"] = {{{"with", "character/warden"}, {"type", "skeptical-of"}}, {{"type", "friend"}}};
            c["biography"]["motivations"] = {"glory-seeking"};
        }
        p.plan->OnResponse(r.id, a);
    }
    EXPECT_TRUE(p.plan->State()["failedJobs"].empty()) << p.plan->State()["failedJobs"].dump();
    const json c = p.plan->Document("characters/tamsin");
    ASSERT_TRUE(c.is_object());
    EXPECT_EQ(c["behaviour"]["packages"][0]["template"], "guard");
    EXPECT_EQ(c["behaviour"]["packages"][1]["template"], "wander");
    ASSERT_EQ(c["relationships"].size(), 1u);
    EXPECT_EQ(c["relationships"][0]["type"], "rival");
}

// A step that failed but whose answer is in the call log is re-applied when the plan opens:
// no model call, even with the plan paused, and the board shows it made.
TEST(PlannerTest, AFailedStepIsReappliedFromTheLogOnOpen) {
    Planner p("replay-log");
    p.ThroughOutline();
    p.service.Drive(*p.plan);
    ASSERT_TRUE(p.plan->Document("characters/tamsin").is_object());
    const int calls = p.plan->State()["usage"]["calls"];
    p.plan.reset();
    // As an older engine left it: tamsin's job failed, her document never written, the plan paused.
    const fs::path statePath = p.root / "planner" / "state.json";
    json state = json::parse(std::ifstream(statePath));
    for (auto& j : state["jobs"]) {
        if (j["kind"] == "character" && j["subject"] == "character/tamsin") j["status"] = "failed";
    }
    state["paused"] = true;
    state["status"] = "paused";
    std::ofstream(statePath) << state.dump();
    fs::remove(p.root / "planner" / "characters" / "tamsin.json");
    p.plan = std::make_unique<GamePlan>(p.service.Host(p.root));
    ASSERT_EQ(p.plan->Open(), "");
    ASSERT_EQ(p.service.outstanding.size(), 1u);
    EXPECT_EQ(p.service.outstanding.front().path, "/planner/character");
    EXPECT_TRUE(p.service.outstanding.front().body.contains("replay"));
    p.service.Drive(*p.plan);
    EXPECT_TRUE(p.plan->Document("characters/tamsin").is_object());
    EXPECT_TRUE(p.plan->State()["failedJobs"].empty());
    EXPECT_EQ(p.plan->State()["usage"]["calls"], calls); // no model call
    EXPECT_EQ(p.plan->Status(), "paused");               // and the plan stays as it was
}

// ⑨ finds an error with a known remedy (the journal window missing while the game has
// quests): the plan fixes it on its own, validates again, and the design is done.
TEST(PlannerTest, ValidationFixesWhatHasARemedyOnItsOwn) {
    Planner p("autofix");
    p.ThroughOutline();
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        json a = Answer(r.path, r.body);
        if (r.path == "/planner/visuals") {
            json kept = json::array();
            for (const auto& s : a["ui"]["screens"]) {
                if (s != "journal") kept.push_back(s);
            }
            a["ui"]["screens"] = kept;
        }
        p.plan->OnResponse(r.id, a);
    }
    EXPECT_EQ(p.plan->Status(), "done") << p.plan->State()["message"] << "\n" << Findings(p.plan->Document("report"));
    const auto screens = p.plan->Document("ui")["screens"];
    EXPECT_NE(std::find(screens.begin(), screens.end(), "journal"), screens.end());
    EXPECT_EQ(p.plan->Document("report")["summary"]["errors"], 0);
}

// ⑦'s records as a small model wrote them (Test2): "stack": false on the moon amulet, a record
// of a type the engine hasn't. The field is left at its default, the record left out; the
// systems are made, not failed.
TEST(PlannerTest, RecordsTheEngineRefusesArePutRight) {
    Planner p("records");
    GamePlan::Host host = p.service.Host(p.root);
    host.validateRecord = [](const std::string& type, const json& record) -> std::vector<std::string> {
        if (type != "item") return {"there's no record type '" + type + "'"};
        if (record.contains("stack") && !record["stack"].is_number()) return {"stack: expected a number, got " + record["stack"].dump()};
        return {};
    };
    p.plan = std::make_unique<GamePlan>(host);
    p.ThroughOutline();
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        json a = Answer(r.path, r.body);
        if (r.path == "/planner/systems") {
            a["systems"]["records"].push_back({{"type", "item"}, {"id", "moon-amulet"}, {"record", {{"name", "Moon Amulet"}, {"stack", false}}}});
            a["systems"]["records"].push_back({{"type", "spell"}, {"id", "moonfire"}, {"record", {{"name", "Moonfire"}}}});
        }
        p.plan->OnResponse(r.id, a);
    }
    EXPECT_TRUE(p.plan->State()["failedJobs"].empty()) << p.plan->State()["failedJobs"].dump();
    const json records = p.plan->Document("systems")["records"];
    bool amulet = false, spell = false;
    for (const auto& r : records) {
        if (r["id"] == "moon-amulet") {
            amulet = true;
            EXPECT_FALSE(r["record"].contains("stack"));
        }
        if (r["id"] == "moonfire") spell = true;
    }
    EXPECT_TRUE(amulet);
    EXPECT_FALSE(spell);
}

// A companion with no AI package (R-K5, an error): ⑨ gives it one directly, no AI call, and the
// design is done.
TEST(PlannerTest, ACompanionWithoutAPackageIsGivenOneWithoutTheAi) {
    Planner p("companion");
    ASSERT_EQ(p.plan->Start("a short dark-fantasy ARPG about a disgraced warden recovering a stolen amulet"), "");
    int tamsinCalls = 0;
    auto drive = [&] {
        while (!p.service.outstanding.empty()) {
            const auto r = p.service.outstanding.front();
            p.service.outstanding.pop_front();
            json a = Answer(r.path, r.body);
            if (r.path == "/planner/outline") {
                for (auto& c : a["outline"]["cast"]) {
                    if (c["id"] == "character/tamsin") c["gameplayRoles"] = {"companion"};
                }
            }
            if (r.path == "/planner/character" && r.body.value("entry", json::object()).value("id", "") == "character/tamsin") {
                ++tamsinCalls;
                a["character"]["behaviour"] = {{"packages", json::array()}};
            }
            p.plan->OnResponse(r.id, a);
        }
    };
    drive();
    ASSERT_EQ(p.plan->Confirm(), "");
    drive();
    ASSERT_EQ(p.plan->Approve(), "");
    drive();
    EXPECT_EQ(p.plan->Status(), "done") << p.plan->State()["message"] << "\n" << Findings(p.plan->Document("report"));
    EXPECT_EQ(tamsinCalls, 1); // written once; the package came without the AI
    const json c = p.plan->Document("characters/tamsin");
    ASSERT_TRUE(c["behaviour"]["packages"].is_array() && !c["behaviour"]["packages"].empty());
    EXPECT_EQ(c["behaviour"]["packages"][0]["template"], "wander");
}

// Closing the editor with steps to do pauses the plan (nothing left running) and opening it
// again carries on from where it was, to the end.
TEST(PlannerTest, ClosingPausesAndOpeningCarriesOn) {
    Planner p("close");
    p.ThroughOutline();
    for (int i = 0; i < 3 && !p.service.outstanding.empty(); ++i) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        p.plan->OnResponse(r.id, Answer(r.path, r.body));
    }
    ASSERT_GT(p.plan->Busy(), 0);
    p.plan->PauseForClose();
    EXPECT_EQ(p.plan->Status(), "paused");
    EXPECT_EQ(p.plan->Busy(), 0);
    p.service.outstanding.clear(); // the session ends
    p.plan = std::make_unique<GamePlan>(p.service.Host(p.root));
    ASSERT_EQ(p.plan->Open(), "");
    EXPECT_EQ(p.plan->Status(), "working");
    EXPECT_FALSE(p.service.outstanding.empty()); // it carries on by itself
    p.service.Drive(*p.plan);
    EXPECT_EQ(p.plan->Status(), "done") << Findings(p.plan->Document("report"));
}

// Stop fix: what's fixing is open again, the fix jobs are cancelled, and an answer that comes
// after for one of them is ignored.
TEST(PlannerTest, StopFixCancelsEveryFix) {
    Planner p("stopfix");
    p.ThroughOutline();
    // Tamsin's character keeps failing: ⑨ reports it, and its own fix (a retry) is queued.
    for (int guard = 0; guard < 500 && !p.service.outstanding.empty(); ++guard) {
        const auto r = p.service.outstanding.front();
        const bool tamsin = r.path == "/planner/character" && r.body.value("entry", json::object()).value("id", "") == "character/tamsin";
        if (tamsin && p.plan->Stage() == "validate") break; // the fix's retry: left unanswered
        p.service.outstanding.pop_front();
        p.plan->OnResponse(r.id, tamsin ? json{{"ok", false}, {"errors", {"the model is busy"}}} : Answer(r.path, r.body));
    }
    ASSERT_EQ(p.plan->Stage(), "validate");
    ASSERT_GT(p.plan->Busy(), 0);
    EXPECT_EQ(p.plan->StopFixes(), "");
    EXPECT_EQ(p.plan->Busy(), 0);
    EXPECT_EQ(p.plan->Status(), "waiting");
    for (const auto& f : p.plan->Document("report")["findings"]) EXPECT_NE(f["status"], "fixing");
    // The cancelled retry's answer arrives anyway: nothing happens.
    const auto late = p.service.outstanding.front();
    p.plan->OnResponse(late.id, Answer(late.path, late.body));
    EXPECT_FALSE(p.plan->Document("characters/tamsin").is_object());
}

// A quest as a small model wrote it (Test2): an alias filled by an item the story lacks, an
// alias of no kind and nothing to fill it, stage numbers that fall, a giver that isn't an alias,
// an action outside the vocabulary. Put right, one call for the quest, nothing failed.
TEST(PlannerTest, AQuestWithASmallModelsMistakesIsPutRight) {
    Planner p("quest-repair");
    p.ThroughOutline();
    int mainCalls = 0;
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        json a = Answer(r.path, r.body);
        if (r.path == "/planner/quests" && r.body.value("only", "") == "q-main") {
            ++mainCalls;
            json& q = a["quests"][0];
            q["giver"] = {{"alias", "nobody"}};
            q["aliases"].push_back({{"id", "relic"}, {"kind", "item"}, {"fill", "item/amulet-hollow-king"}});
            q["aliases"].push_back({{"id", "helper"}, {"kind", "npc"}, {"fill", "character/tamsin"}});
            q["aliases"].push_back({{"id", "mystery"}, {"kind", "thing"}});
            q["stages"].insert(q["stages"].begin() + 1, json{{"index", 5}, {"journal", "Go back."}, {"objectives", {{{"action", "retrieve"}, {"target", "relic"}}}}});
        }
        p.plan->OnResponse(r.id, a);
    }
    EXPECT_TRUE(p.plan->State()["failedJobs"].empty()) << p.plan->State()["failedJobs"].dump();
    EXPECT_EQ(mainCalls, 1);
    const json q = p.plan->Document("quests/q-main");
    ASSERT_TRUE(q.is_object());
    EXPECT_EQ(q["giver"]["alias"], "giver");                 // the first character alias
    EXPECT_NE(p.plan->Graph().Find("item/amulet-hollow-king"), nullptr);
    int last = 0;
    for (const auto& st : q["stages"]) {
        EXPECT_GT(st["index"].get<int>(), last);
        last = st["index"].get<int>();
    }
    for (const auto& al : q["aliases"]) {
        if (al["id"] == "helper") EXPECT_EQ(al["kind"], "character");
        if (al["id"] == "mystery") EXPECT_TRUE(al.contains("query"));
    }
}

// A quest's giver without its motivation (R-Q4, a warning): Fix now settles it without the AI.
TEST(PlannerTest, AQuestMotivationIsMatchedToItsGiverWithoutTheAi) {
    Planner p("motivation");
    p.ThroughOutline();
    p.service.Drive(*p.plan);
    auto open = [&](const char* rule) {
        int n = 0;
        const json report = p.plan->Document("report");
        for (const auto& f : report["findings"]) n += f["rule"] == rule && f["status"] == "open";
        return n;
    };
    ASSERT_GT(open("R-Q4"), 0);
    ASSERT_EQ(p.plan->FixNow({"warnings"}), "");
    int questCalls = 0;
    while (!p.service.outstanding.empty()) {
        const auto r = p.service.outstanding.front();
        p.service.outstanding.pop_front();
        questCalls += r.path == "/planner/quests";
        p.plan->OnResponse(r.id, Answer(r.path, r.body));
    }
    EXPECT_EQ(questCalls, 0);
    EXPECT_EQ(open("R-Q4"), 0) << Findings(p.plan->Document("report"));
}
