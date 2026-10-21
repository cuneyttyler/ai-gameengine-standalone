#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "editing/Commands.h"
#include "gameplay/EngineReflection.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Isometric.h"
#include "gameplay/Story.h"
#include "physics2d/Components.h"
#include "scripting/LuaVM.h"
#include "world/StoryReferences.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kDemo = fs::path(AIGE_SOURCE_DIR) / "projects" / "demo";

// A small library in memory: one quest, one dialogue.
struct Fixture {
    Fixture() {
        root = fs::temp_directory_path() /
               ("aige_story_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "dialogue");
        fs::create_directories(root / "quests");
    }
    ~Fixture() { fs::remove_all(root); }
    void Write(const std::string& relative, const nlohmann::json& json) {
        std::ofstream(root / relative) << json.dump(2);
    }
    aige::StoryLibrary Load() {
        aige::StoryLibrary library(root);
        library.Refresh();
        return library;
    }
    fs::path root;
};

nlohmann::json HuntQuest() {
    return nlohmann::json::parse(R"({
        "name": "The hunt",
        "giver": "character/villager",
        "objectives": [
            {"id": "wolves", "text": "Defeat wolves", "type": "defeat", "target": "prefabs/wolf.prefab", "count": 2},
            {"id": "pelt", "text": "Find the pelt", "type": "collect", "target": "prefabs/pelt.prefab"},
            {"id": "report", "text": "Report back", "type": "talk", "target": "character/villager"}
        ],
        "onComplete": [{"op": "set_flag", "flag": "hunt_done"}]
    })");
}

nlohmann::json HuntDialogue() {
    return nlohmann::json::parse(R"({
        "speaker": "character/villager",
        "start": "hello",
        "starts": [{"if": [{"quest": "hunt", "is": "completed"}], "node": "thanks"}],
        "nodes": [
            {"id": "hello", "text": "Hello.", "choices": [
                {"text": "Any work?", "next": "work"},
                {"text": "Secret option", "next": "work", "if": [{"flag": "knows_secret"}]},
                {"text": "Bye.", "next": ""}
            ]},
            {"id": "work", "text": "Wolves.", "choices": [
                {"text": "On it.", "next": "", "do": [{"op": "start_quest", "quest": "hunt"}]}
            ]},
            {"id": "thanks", "text": "Thanks!", "next": ""}
        ]
    })");
}

} // namespace

TEST(StoryTest, DialogueAndQuestFilesRoundTrip) {
    std::string error;
    auto dialogue = aige::DialogueFromJson(HuntDialogue(), "dialogue/hunt.json", &error);
    ASSERT_TRUE(dialogue) << error;
    EXPECT_EQ(dialogue->nodes.size(), 3u);
    EXPECT_EQ(dialogue->starts.size(), 1u);
    EXPECT_EQ(dialogue->Node("hello")->choices[1].conditions[0].flag, "knows_secret");
    auto again = aige::DialogueFromJson(aige::DialogueToJson(*dialogue), "dialogue/hunt.json", &error);
    ASSERT_TRUE(again);
    EXPECT_EQ(aige::DialogueToJson(*again), aige::DialogueToJson(*dialogue));

    auto quest = aige::QuestFromJson(HuntQuest(), "hunt", &error);
    ASSERT_TRUE(quest) << error;
    EXPECT_EQ(quest->objectives[0].count, 2);
    EXPECT_EQ(aige::QuestToJson(*aige::QuestFromJson(aige::QuestToJson(*quest), "hunt", &error)), aige::QuestToJson(*quest));

    EXPECT_FALSE(aige::DialogueFromJson(nlohmann::json::object(), "x", &error));
    EXPECT_FALSE(aige::QuestFromJson(nlohmann::json::array(), "x", &error));
}

TEST(StoryTest, ConversationsFollowConditionsAndEffects) {
    Fixture f;
    f.Write("quests/hunt.json", HuntQuest());
    f.Write("dialogue/hunt.json", HuntDialogue());
    auto library = f.Load();
    ASSERT_TRUE(library.Validate().empty()) << library.Validate()[0].message;

    aige::StoryState state;
    aige::DialogueRunner talk;
    ASSERT_TRUE(talk.Start(*library.FindDialogue("dialogue/hunt.json"), state, library));
    EXPECT_EQ(talk.Node()->id, "hello");
    EXPECT_EQ(talk.VisibleChoices(), (std::vector<size_t>{0, 2})); // the secret one is hidden

    state.SetFlag("knows_secret", true);
    talk.Start(*library.FindDialogue("dialogue/hunt.json"), state, library);
    EXPECT_EQ(talk.VisibleChoices().size(), 3u);
    talk.MoveHighlight(-1);
    EXPECT_EQ(talk.Highlighted(), 2); // wraps around
    talk.Choose(0, state, library);
    EXPECT_EQ(talk.Node()->id, "work");
    talk.Continue(state, library); // the only choice: start the quest, end
    EXPECT_FALSE(talk.Active());
    EXPECT_EQ(state.Quest("hunt").state, aige::StoryState::QuestState::Active);
}

TEST(StoryTest, ObjectivesAreDoneInOrderAndCompleteTheQuest) {
    Fixture f;
    f.Write("quests/hunt.json", HuntQuest());
    auto library = f.Load();
    aige::StoryState state;

    state.OnDefeated("prefabs/wolf.prefab", "Enemy", library); // before the quest: doesn't count
    state.StartQuest("hunt", library);
    EXPECT_EQ(state.CurrentObjective(library)->text, "Defeat wolves (0/2)");
    state.OnCollected("prefabs/pelt.prefab", library); // not the current objective yet
    state.OnDefeated("prefabs/rabbit.prefab", "Neutral", library);
    state.OnDefeated("prefabs/wolf.prefab", "Enemy", library);
    EXPECT_EQ(state.CurrentObjective(library)->text, "Defeat wolves (1/2)");
    state.OnDefeated("prefabs/wolf.prefab", "Enemy", library);
    EXPECT_EQ(state.CurrentObjective(library)->text, "Find the pelt");
    state.OnCollected("prefabs/pelt.prefab", library);
    EXPECT_EQ(state.CurrentObjective(library)->quest, "The hunt");
    EXPECT_EQ(state.CurrentObjective(library)->text, "Report back");
    state.OnTalkedTo("character/villager", library);

    EXPECT_EQ(state.Quest("hunt").state, aige::StoryState::QuestState::Completed);
    EXPECT_FALSE(state.CurrentObjective(library));
    EXPECT_TRUE(state.Flag("hunt_done")); // onComplete
    auto notices = state.TakeNotices();
    ASSERT_EQ(notices.size(), 5u); // started, 3 objectives, completed
    EXPECT_EQ(notices.front().kind, aige::StoryState::Notice::Kind::QuestStarted);
    EXPECT_EQ(notices.back().kind, aige::StoryState::Notice::Kind::QuestCompleted);
    EXPECT_TRUE(state.TakeNotices().empty());

    // A team target counts any member of that team.
    nlohmann::json anyEnemy = HuntQuest();
    anyEnemy["objectives"][0]["target"] = "Enemy";
    f.Write("quests/hunt.json", anyEnemy);
    library = f.Load();
    aige::StoryState fresh;
    fresh.StartQuest("hunt", library);
    fresh.OnDefeated("prefabs/skeleton.prefab", "Enemy", library);
    fresh.OnDefeated("prefabs/villager.prefab", "Neutral", library);
    EXPECT_EQ(fresh.CurrentObjective(library)->text, "Defeat wolves (1/2)");
}

TEST(StoryTest, ValidationFindsBrokenStories) {
    Fixture f;
    f.Write("quests/hunt.json", HuntQuest());
    nlohmann::json broken = HuntDialogue();
    broken["nodes"][0]["choices"][0]["next"] = "nowhere";
    broken["nodes"].push_back(nlohmann::json::parse(R"({"id": "orphan", "text": "Nobody gets here.", "next": ""})"));
    broken["nodes"][1]["choices"][0]["do"] = nlohmann::json::parse(R"([{"op": "start_quest", "quest": "no-such-quest"}])");
    broken["nodes"][2]["choices"] = nlohmann::json::parse(
        R"([{"text": "a", "next": ""}, {"text": "b", "next": ""}, {"text": "c", "next": ""},
            {"text": "d", "next": ""}, {"text": "e", "next": ""}, {"text": "f", "next": ""},
            {"text": "g", "next": ""}, {"text": "h", "next": ""}, {"text": "i", "next": ""},
            {"text": "j", "next": ""}])");
    f.Write("dialogue/broken.json", broken);
    nlohmann::json badQuest = HuntQuest();
    badQuest["objectives"][1]["type"] = "dance";
    f.Write("quests/bad.json", badQuest);
    std::ofstream(f.root / "dialogue" / "garbage.json") << "{ not json";

    auto library = f.Load();
    std::vector<std::string> messages;
    for (const auto& p : library.Validate()) {
        messages.push_back(p.file + ": " + p.message);
    }
    auto has = [&](const std::string& text) {
        return std::any_of(messages.begin(), messages.end(),
                           [&](const std::string& m) { return m.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("dialogue/garbage.json: isn't a JSON object"));
    EXPECT_TRUE(has("goes to 'nowhere', which isn't a node"));
    EXPECT_TRUE(has("node 'orphan' can't be reached"));
    EXPECT_TRUE(has("no quest 'no-such-quest'"));
    EXPECT_TRUE(has("more than 9 choices"));
    EXPECT_TRUE(has("quests/bad.json: objective 'pelt': type should be defeat, talk, collect, reach"));

    // References to the World Bible and prefabs are checked with the rest of the project.
    aige::WorldBible world(aige::Project{f.root});
    auto references = aige::ValidateStoryReferences(library, world, {"prefabs/wolf.prefab"});
    std::vector<std::string> refs;
    for (const auto& p : references) refs.push_back(p.message);
    auto refHas = [&](const std::string& text) {
        return std::any_of(refs.begin(), refs.end(), [&](const std::string& m) { return m.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(refHas("speaker: character/villager isn't in the World Bible"));
    EXPECT_TRUE(refHas("objective 'pelt': prefabs/pelt.prefab isn't a prefab"));
    EXPECT_TRUE(refHas("objective 'report': character/villager isn't in the World Bible"));
    EXPECT_FALSE(refHas("prefabs/wolf.prefab"));
}

