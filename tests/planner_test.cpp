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
    ASSERT_EQ(failed.size(), 1u) << p.plan->State()["message"];
    EXPECT_EQ(failed[0]["kind"], "character");
    EXPECT_EQ(failed[0]["subject"], "tamsin");
    EXPECT_EQ(failed[0]["doc"], "characters/tamsin");
    EXPECT_FALSE(p.plan->Document("characters/tamsin").is_object());
    for (const auto& s : p.plan->State()["stages"]) {
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
