#include <gtest/gtest.h>

#include "gameplay/Archetypes.h"
#include "gameplay/Attributes.h"
#include "gameplay/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/GameSession.h"
#include "gameplay/Items.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md Phase 15: quests v2 (stages, branches, failure, aliases,
// rewards, the new objectives), story variables and conditions, dialogue v2,
// the journal and cutscenes.
namespace {

struct Project {
    Project() {
        root = fs::temp_directory_path() /
               ("aige_narrative_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "quests");
        fs::create_directories(root / "dialogue");
        fs::create_directories(root / "cutscenes");
    }
    ~Project() { fs::remove_all(root); }
    void Write(const std::string& relative, const std::string& text) {
        std::ofstream(root / relative, std::ios::binary) << text;
    }
    aige::StoryLibrary Load() {
        aige::StoryLibrary library(root);
        library.Refresh();
        return library;
    }
    fs::path root;
};

// A thief to find: talk to the guard; then find the thief or (optionally) the stolen
// ring; a branch on whether the ring was found; a fail stage when the thief escapes.
const char* kThief = R"({
    "name": "The thief",
    "giver": "character/guard",
    "aliases": [{"id": "thief", "kind": "character", "fill": "character/mira"},
                {"id": "loot", "kind": "item", "fill": "ring"}],
    "rewards": {"experience": 50, "gold": 20, "items": "potion*2", "faction": "watch", "reputation": 10},
    "stages": [
        {"index": 10, "journal": "The guard told me about a thief.",
         "objectives": [{"id": "guard", "text": "Talk to the guard", "type": "talk", "target": "character/guard"}]},
        {"index": 20, "journal": "Mira is the thief.",
         "objectives": [{"id": "find", "text": "Find Mira", "type": "talk", "target": "@thief"},
                        {"id": "ring", "text": "Find the ring", "type": "have", "target": "@loot", "optional": true}],
         "branches": [{"if": [{"var": "mercy", "op": ">=", "value": 1}], "stage": 40}]},
        {"index": 30, "journal": "Mira is in the cells.", "ends": "complete"},
        {"index": 40, "journal": "I let Mira go.", "onEnter": [{"op": "set_flag", "flag": "spared"}], "ends": "complete"},
        {"index": 90, "journal": "Mira got away.", "ends": "fail"}
    ],
    "onFail": [{"op": "set_flag", "flag": "thief_escaped"}]
})";

} // namespace

TEST(NarrativeTest, OldQuestFilesLoadAsStages) {
    std::string error;
    auto quest = aige::QuestFromJson(nlohmann::json::parse(R"({"name": "Old", "objectives": [
        {"id": "a", "text": "A", "type": "talk", "target": "x"},
        {"id": "b", "text": "B", "type": "talk", "target": "y"}]})"), "old", &error);
    ASSERT_TRUE(quest) << error;
    ASSERT_EQ(quest->stages.size(), 2u);
    EXPECT_EQ(quest->stages[0].index, 10);
    EXPECT_EQ(quest->stages[1].index, 20);
    EXPECT_FALSE(quest->staged);
    // It writes back in its own shape.
    EXPECT_TRUE(aige::QuestToJson(*quest).contains("objectives"));
    EXPECT_FALSE(aige::QuestToJson(*quest).contains("stages"));

    auto v2 = aige::QuestFromJson(nlohmann::json::parse(kThief), "thief", &error);
    ASSERT_TRUE(v2) << error;
    EXPECT_TRUE(v2->staged);
    EXPECT_EQ(v2->Resolve("@thief"), "character/mira");
    EXPECT_EQ(v2->Resolve("plain"), "plain");
    EXPECT_EQ(aige::QuestToJson(*aige::QuestFromJson(aige::QuestToJson(*v2), "thief", &error)), aige::QuestToJson(*v2));
}

TEST(NarrativeTest, ABranchingQuestWithAliasesAndRewards) {
    Project p;
    p.Write("quests/thief.json", kThief);
    auto library = p.Load();
    ASSERT_TRUE(library.Validate().empty()) << library.Validate()[0].message;

    aige::StoryState state;
    state.StartQuest("thief", library);
    EXPECT_EQ(state.StageOf("thief", library), 10);
    state.OnTalkedTo("character/guard", library);
    EXPECT_EQ(state.StageOf("thief", library), 20);
    EXPECT_EQ(state.OpenObjectives("thief", library).size(), 2u);
    EXPECT_EQ(state.CurrentObjective(library)->text, "Find Mira"); // the required one first

    // The optional objective doesn't hold the stage back; the alias names Mira.
    state.OnHave([](const std::string& item) { return item == "ring" ? 1 : 0; }, library);
    EXPECT_EQ(state.StageOf("thief", library), 20);
    state.OnTalkedTo("character/mira", library);
    EXPECT_EQ(state.Quest("thief").state, aige::StoryState::QuestState::Completed);
    EXPECT_FALSE(state.Flag("spared")); // went on to 30, not 40

    // The rewards are the game's to hand out.
    auto effects = state.TakeWorldEffects();
    ASSERT_EQ(effects.size(), 4u);
    EXPECT_EQ(effects[0].op, "give_xp");
    EXPECT_EQ(effects[1].op, "give_gold");
    EXPECT_EQ(effects[2].op, "give_item");
    EXPECT_EQ(effects[2].name, "potion");
    EXPECT_EQ(effects[2].value, 2);
    EXPECT_EQ(effects[3].op, "reputation");

    // The journal kept every stage's line.
    ASSERT_EQ(state.Journal().size(), 3u);
    EXPECT_EQ(state.Journal()[1].text, "Mira is the thief.");

    // With mercy the branch goes to 40.
    aige::StoryState kind;
    kind.SetVar("mercy", 1);
    kind.StartQuest("thief", library);
    kind.OnTalkedTo("character/guard", library);
    kind.OnTalkedTo("character/mira", library);
    EXPECT_TRUE(kind.Flag("spared"));
    EXPECT_EQ(kind.Quest("thief").state, aige::StoryState::QuestState::Completed);
}

TEST(NarrativeTest, QuestsFail) {
    Project p;
    p.Write("quests/thief.json", kThief);
    p.Write("quests/escort.json", R"({"name": "Escort", "stages": [
        {"index": 10, "objectives": [{"id": "bring", "text": "Bring Tam home", "type": "escort",
                                      "target": "character/tam", "to": "home"}]}]})");
    auto library = p.Load();
    ASSERT_TRUE(library.Validate().empty()) << library.Validate()[0].message;
    aige::StoryState state;
    state.StartQuest("thief", library);
    state.Apply(aige::StoryEffectsFromJson(nlohmann::json::parse(R"([{"op": "set_stage", "quest": "thief", "stage": 90}])")),
                library);
    EXPECT_EQ(state.Quest("thief").state, aige::StoryState::QuestState::Failed);
    EXPECT_TRUE(state.Flag("thief_escaped")); // onFail
    EXPECT_TRUE(state.Holds(aige::StoryConditionsFromJson(nlohmann::json::parse(R"([{"quest": "thief", "is": "failed"}])"))));
    state.StartQuest("thief", library); // a failed quest doesn't start again
    EXPECT_EQ(state.Quest("thief").state, aige::StoryState::QuestState::Failed);

    // The one you escort dies: failed.
    state.StartQuest("escort", library);
    state.OnLost("character/someone-else", library);
    EXPECT_EQ(state.Quest("escort").state, aige::StoryState::QuestState::Active);
    state.OnLost("character/tam", library);
    EXPECT_EQ(state.Quest("escort").state, aige::StoryState::QuestState::Failed);
    bool failedNotice = false;
    for (const auto& n : state.TakeNotices()) failedNotice |= n.kind == aige::StoryState::Notice::Kind::QuestFailed;
    EXPECT_TRUE(failedNotice);
}

TEST(NarrativeTest, TheNewObjectives) {
    Project p;
    p.Write("quests/errands.json", R"({"name": "Errands", "stages": [
        {"index": 10, "objectives": [{"id": "give", "text": "Give Tam the letter", "type": "deliver",
                                      "target": "character/tam", "item": "letter"}]},
        {"index": 20, "objectives": [{"id": "pull", "text": "Pull the lever", "type": "use", "target": "lever"},
                                     {"id": "read", "text": "Read the sign", "type": "examine", "target": "sign"}]},
        {"index": 30, "objectives": [{"id": "hold", "text": "Hold out", "type": "survive", "seconds": 3}]},
        {"index": 40, "objectives": [{"id": "walk", "text": "Bring Tam to the gate", "type": "escort",
                                      "target": "character/tam", "to": "gate"}]}]})");
    auto library = p.Load();
    ASSERT_TRUE(library.Validate().empty()) << library.Validate()[0].message;
    aige::StoryState state;
    state.StartQuest("errands", library);

    int letters = 0;
    auto take = [&](const std::string& item, int count) {
        if (item != "letter" || letters < count) return false;
        letters -= count;
        return true;
    };
    state.OnDelivered("character/tam", take, library); // nothing to give yet
    EXPECT_EQ(state.StageOf("errands", library), 10);
    letters = 1;
    state.OnDelivered("character/tam", take, library);
    EXPECT_EQ(letters, 0);
    EXPECT_EQ(state.StageOf("errands", library), 20);

    state.OnUsed("lever", library);
    EXPECT_EQ(state.StageOf("errands", library), 20); // both are needed
    state.OnUsed("sign", library);
    EXPECT_EQ(state.StageOf("errands", library), 30);

    state.OnTime(2.0f, library);
    EXPECT_EQ(state.CurrentObjective(library)->text, "Hold out (1s)");
    state.OnTime(1.5f, library);
    EXPECT_EQ(state.StageOf("errands", library), 40);

    bool there = false;
    auto arrived = [&](const std::string& who, const std::string& marker) {
        return there && who == "character/tam" && marker == "gate";
    };
    state.OnEscorted(arrived, library);
    EXPECT_EQ(state.Quest("errands").state, aige::StoryState::QuestState::Active);
    there = true;
    state.OnEscorted(arrived, library);
    EXPECT_EQ(state.Quest("errands").state, aige::StoryState::QuestState::Completed);
}

TEST(NarrativeTest, VariablesAndWorldConditions) {
    Project p;
    p.Write("quests/thief.json", kThief);
    auto library = p.Load();
    aige::StoryState state;
    auto holds = [&](const char* json) { return state.Holds(aige::StoryConditionsFromJson(nlohmann::json::parse(json))); };

    state.Apply(aige::StoryEffectsFromJson(nlohmann::json::parse(
                    R"([{"op": "set_var", "var": "paid", "value": 5}, {"op": "add_var", "var": "paid", "value": 3},
                        {"op": "add_var", "var": "visits"}, {"op": "set_var", "var": "name", "value": "Ada"}])")),
                library);
    EXPECT_EQ(state.Var("paid"), 8);
    EXPECT_EQ(state.Var("visits"), 1);
    EXPECT_TRUE(holds(R"([{"var": "paid", "op": ">=", "value": 8}])"));
    EXPECT_FALSE(holds(R"([{"var": "paid", "op": ">", "value": 8}])"));
    EXPECT_TRUE(holds(R"([{"var": "name", "value": "Ada"}])"));
    EXPECT_TRUE(holds(R"([{"var": "visits"}])"));
    EXPECT_FALSE(holds(R"([{"var": "never"}])"));

    // Questions about the world go to the game; with nobody to ask, they don't hold.
    EXPECT_FALSE(holds(R"([{"item": "potion", "count": 2}])"));
    state.SetWorldConditions([](const nlohmann::json& c) { return c.value("item", "") == "potion" && c.value("count", 1) <= 3; });
    EXPECT_TRUE(holds(R"([{"item": "potion", "count": 2}])"));
    EXPECT_FALSE(holds(R"([{"item": "potion", "count": 4}])"));

    // A quest's stage.
    state.StartQuest("thief", library);
    EXPECT_TRUE(holds(R"([{"quest": "thief", "is": "active", "stage": 10}])"));
    EXPECT_FALSE(holds(R"([{"quest": "thief", "is": "active", "stage": 20}])"));

    // World effects are handed over, not applied.
    state.Apply(aige::StoryEffectsFromJson(nlohmann::json::parse(
                    R"([{"op": "give_item", "item": "key", "count": 1}, {"op": "play_cutscene", "cutscene": "intro"}])")),
                library);
    auto effects = state.TakeWorldEffects();
    ASSERT_EQ(effects.size(), 2u);
    EXPECT_EQ(effects[1].op, "play_cutscene");
    EXPECT_EQ(effects[1].name, "intro");
    EXPECT_TRUE(state.TakeWorldEffects().empty());
}

TEST(NarrativeTest, SavesKeepStagesVariablesAndTheJournal) {
    Project p;
    p.Write("quests/thief.json", kThief);
    auto library = p.Load();
    aige::StoryState state;
    state.SetVar("mercy", 0);
    state.StartQuest("thief", library);
    state.OnTalkedTo("character/guard", library);
    state.OnHave([](const std::string&) { return 1; }, library); // the optional ring
    auto loaded = aige::StoryState::FromJson(state.ToJson());
    EXPECT_EQ(loaded.StageOf("thief", library), 20);
    EXPECT_EQ(loaded.OpenObjectives("thief", library).size(), 1u); // the ring stays found
    EXPECT_EQ(loaded.Journal().size(), 2u);
    EXPECT_EQ(loaded.Var("mercy"), 0);
    loaded.OnTalkedTo("character/mira", library);
    EXPECT_EQ(loaded.Quest("thief").state, aige::StoryState::QuestState::Completed);
}

TEST(NarrativeTest, DialogueSkillChecksAndMoreChoices) {
    Project p;
    p.Write("dialogue/gate.json", R"({"speaker": "character/guard", "start": "ask", "nodes": [
        {"id": "ask", "text": "Halt.", "portrait": "assets/guard.png", "voice": "assets/halt.wav", "choices": [
            {"text": "Let me through.", "check": {"attribute": "speech", "difficulty": 5}, "next": "yes", "fail": "no",
             "do": [{"op": "set_flag", "flag": "talked_through"}]},
            {"text": "2", "next": ""}, {"text": "3", "next": ""}, {"text": "4", "next": ""},
            {"text": "5", "next": ""}, {"text": "6", "next": ""}]},
        {"id": "yes", "text": "Go on.", "next": ""},
        {"id": "no", "text": "No.", "next": ""}]})");
    auto library = p.Load();
    ASSERT_TRUE(library.Validate().empty()) << library.Validate()[0].message;
    const aige::Dialogue* gate = library.FindDialogue("dialogue/gate.json");
    EXPECT_EQ(gate->Node("ask")->portrait, "assets/guard.png");
    EXPECT_EQ(gate->Node("ask")->voice, "assets/halt.wav");
    std::string error;
    EXPECT_EQ(aige::DialogueToJson(*aige::DialogueFromJson(aige::DialogueToJson(*gate), gate->path, &error)),
              aige::DialogueToJson(*gate));

    float speech = 3.0f;
    aige::StoryState state;
    state.SetWorldConditions([&](const nlohmann::json& c) {
        return c.value("attribute", "") == "speech" && speech >= c.value("atLeast", 0.0f);
    });
    aige::DialogueRunner talk;
    talk.Start(*gate, state, library);
    EXPECT_EQ(talk.VisibleChoices().size(), 6u);
    talk.Choose(0, state, library);
    EXPECT_EQ(talk.Node()->id, "no");
    EXPECT_EQ(talk.LastCheck(), false);
    EXPECT_FALSE(state.Flag("talked_through")); // failing skips the effects

    speech = 6.0f;
    talk.Start(*gate, state, library);
    talk.Choose(0, state, library);
    EXPECT_EQ(talk.Node()->id, "yes");
    EXPECT_EQ(talk.LastCheck(), true);
    EXPECT_TRUE(state.Flag("talked_through"));
}

TEST(NarrativeTest, ValidationKnowsTheNewShapes) {
    Project p;
    p.Write("quests/bad.json", R"({"name": "Bad", "aliases": [{"id": "who", "fill": ""}], "stages": [
        {"index": 10, "objectives": [{"id": "a", "text": "A", "type": "deliver", "target": "@nobody"}],
         "branches": [{"if": [], "stage": 70}], "next": 80},
        {"index": 10, "objectives": [{"id": "b", "text": "B", "type": "survive"}], "ends": "maybe"}]})");
    auto library = p.Load();
    std::vector<std::string> messages;
    for (const auto& problem : library.Validate()) messages.push_back(problem.message);
    auto has = [&](const std::string& text) {
        return std::any_of(messages.begin(), messages.end(), [&](const std::string& m) { return m.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("needs an item to deliver"));
    EXPECT_TRUE(has("no alias 'nobody'"));
    EXPECT_TRUE(has("alias 'who' isn't filled"));
    EXPECT_TRUE(has("two stages are numbered 10"));
    EXPECT_TRUE(has("branches to stage 70"));
    EXPECT_TRUE(has("goes on to stage 80"));
    EXPECT_TRUE(has("needs a number of seconds"));
    EXPECT_TRUE(has("ends should be complete or fail"));
}

TEST(NarrativeTest, CutscenesPlayInOrderWaitAndSkip) {
    std::string error;
    auto cutscene = aige::CutsceneFromJson(nlohmann::json::parse(R"({"name": "Intro", "tracks": [
        {"type": "dialogue", "keys": [{"at": 1, "speaker": "character/guard", "text": "Halt.", "seconds": 2}]},
        {"type": "logic", "keys": [{"at": 0.5, "do": [{"op": "set_flag", "flag": "seen"}]},
                                   {"at": 2, "op": "wait_input"},
                                   {"at": 4, "op": "emit", "event": "done"}]}]})"), "intro", &error);
    ASSERT_TRUE(cutscene) << error;
    EXPECT_TRUE(aige::ValidateCutscene(*cutscene).empty());
    EXPECT_FLOAT_EQ(cutscene->Length(), 4.0f);
    EXPECT_EQ(aige::CutsceneToJson(*aige::CutsceneFromJson(aige::CutsceneToJson(*cutscene), "intro", &error)),
              aige::CutsceneToJson(*cutscene));

    aige::CutscenePlayer player;
    std::vector<std::string> fired;
    auto fire = [&](const aige::CutscenePlayer::Due& due) { fired.push_back(due.key->op + (due.skipping ? "!" : "")); };
    player.Start(*cutscene);
    player.Update(1.2f, fire);
    EXPECT_EQ(fired, (std::vector<std::string>{"do", "line"}));
    player.Update(5.0f, fire); // stops at the wait
    EXPECT_TRUE(player.WaitingForInput());
    EXPECT_TRUE(player.Active());
    player.Update(5.0f, fire);
    EXPECT_EQ(fired.size(), 3u);
    player.Continue();
    player.Update(2.5f, fire);
    EXPECT_EQ(fired.back(), "emit");
    EXPECT_FALSE(player.Active());

    // Skipping fires what's left, marked.
    fired.clear();
    player.Start(*cutscene);
    player.Update(0.6f, fire);
    player.Skip(fire);
    EXPECT_EQ(fired, (std::vector<std::string>{"do", "line!", "wait_input!", "emit!"}));
    EXPECT_FALSE(player.Active());

    // Problems.
    auto bad = aige::CutsceneFromJson(nlohmann::json::parse(R"({"tracks": [
        {"type": "actor", "keys": [{"at": 0, "op": "move"}]}, {"type": "screen", "keys": [{"op": "explode"}]},
        {"type": "nonsense"}]})"), "bad", &error);
    const auto problems = aige::ValidateCutscene(*bad);
    auto has = [&](const std::string& text) {
        return std::any_of(problems.begin(), problems.end(), [&](const std::string& m) { return m.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("needs an actor"));
    EXPECT_TRUE(has("move needs a marker"));
    EXPECT_TRUE(has("unknown op 'explode'"));
    EXPECT_TRUE(has("type should be"));
}

// The session end to end: a quest's reward lands in the inventory, a trigger
// plays a cutscene that walks an actor to a marker and sets a flag, skipping
// puts everyone at their marks, and the objective marker points the way.
TEST(NarrativeTest, TheGamePlaysItAll) {
    Project p;
    p.Write("quests/thief.json", kThief);
    p.Write("cutscenes/arrest.json", R"({"name": "Arrest", "length": 3, "tracks": [
        {"type": "actor", "actor": "@thief", "keys": [{"at": 0, "op": "move", "marker": "cells", "speed": 2}]},
        {"type": "camera", "keys": [{"at": 0, "op": "move", "marker": "cells"}]},
        {"type": "dialogue", "keys": [{"at": 0.2, "speaker": "@thief", "text": "Fine, fine.", "seconds": 1}]},
        {"type": "logic", "keys": [{"at": 1, "do": [{"op": "set_flag", "flag": "arrested"}]}]}]})");
    auto library = p.Load();
    ASSERT_TRUE(library.Validate().empty()) << library.Validate()[0].message;

    aige::Registry registry;
    for (int x = 0; x < 20; ++x) {
        for (int y = 0; y < 20; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    auto hero = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.emplace<aige::Inventory>(hero);
    auto mira = registry.create();
    registry.emplace<aige::WorldTransform2D>(mira, 10.5f, 10.5f);
    registry.emplace<aige::Name>(mira, "Mira");
    registry.emplace<aige::Talk>(mira, "", "character/mira", 1.5f);
    auto cells = registry.create();
    registry.emplace<aige::Marker>(cells, "cells");
    registry.emplace<aige::WorldTransform2D>(cells, 14.5f, 10.5f);
    auto trigger = registry.create();
    registry.emplace<aige::WorldTransform2D>(trigger, 2.5f, 2.5f);

    aige::LuaVM lua;
    aige::GameSession session(registry, lua, aige::IsoParams{}, p.root);
    session.SetStory(&library);
    auto step = [&](double seconds = aige::GameSession::kFixedDt) { session.Advance(seconds, [](aige::Key) { return false; }); };

    session.Story().StartQuest("thief", library);
    session.Story().OnTalkedTo("character/guard", library);
    // The marker points at Mira, the alias's fill, 8 cells off.
    auto at = session.ObjectiveLocation();
    ASSERT_TRUE(at);
    EXPECT_FLOAT_EQ(at->first, 10.5f);

    // The trigger plays the cutscene; the world waits while it runs.
    registry.emplace<aige::CutsceneTrigger>(trigger, "arrest", 1.0f, true);
    step();
    ASSERT_TRUE(session.InCutscene());
    for (int i = 0; i < 40; ++i) step(0.05); // 2 s
    EXPECT_NE(session.CameraTarget().value_or(aige::Entity{entt::null}), hero); // the camera's focus moved to the cells
    EXPECT_GT(registry.get<aige::WorldTransform2D>(mira).x, 12.0f); // walking
    EXPECT_TRUE(session.Story().Flag("arrested"));
    session.SkipCutscene();
    EXPECT_FALSE(session.InCutscene());
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(mira).x, 14.5f); // at the mark
    EXPECT_EQ(session.CameraTarget().value_or(aige::Entity{entt::null}), hero);
    step();
    EXPECT_FALSE(session.InCutscene()); // once only
    EXPECT_TRUE(session.Story().Flag("cutscene:arrest"));

    // Finishing the quest pays out.
    session.Story().OnTalkedTo("character/mira", library);
    step();
    const auto& inventory = registry.get<aige::Inventory>(hero);
    EXPECT_EQ(aige::CountItem(inventory, "potion"), 2);
    EXPECT_EQ(inventory.gold, 20);
    EXPECT_EQ(session.Story().Var("reputation.watch"), 10);

    // A failed play_cutscene (no file) is reported, not fatal.
    EXPECT_FALSE(session.PlayCutscene("missing"));
    EXPECT_FALSE(session.ScriptErrors().empty());
}
