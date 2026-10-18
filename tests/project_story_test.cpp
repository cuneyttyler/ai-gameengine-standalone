#include <gtest/gtest.h>

#include "assets/ProjectMove.h"
#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"
#include "world/StoryReferences.h"
#include "world/WorldBible.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md 6.9: quests, dialogue, schedules and the World Bible
// belong to the project; objectives, schedule entries and places can name the
// scene they're about, and a run's progress can be saved.
namespace {

struct Project {
    Project() {
        root = fs::temp_directory_path() /
               ("aige_project_story_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        for (const char* dir : {"quests", "dialogue", "schedules", "scenes", "world/location"}) fs::create_directories(root / dir);
    }
    ~Project() { fs::remove_all(root); }
    void Write(const std::string& relative, const std::string& text) { std::ofstream(root / relative, std::ios::binary) << text; }
    std::string Read(const std::string& relative) {
        std::ifstream in(root / relative, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    aige::StoryLibrary Story() {
        aige::StoryLibrary library(root);
        library.Refresh();
        return library;
    }
    fs::path root;
};

const char* kCellarQuest = R"({
  "name": "The cellar",
  "giver": "character/innkeeper",
  "objectives": [
    {"id": "down", "text": "Go down to the cellar", "type": "reach", "scene": "scenes/cellar.aige"},
    {"id": "rats", "text": "Clear the rats", "type": "defeat", "target": "Enemy", "count": 2, "scene": "scenes/cellar.aige"},
    {"id": "door", "text": "Find the old door", "type": "reach", "target": "old_door"},
    {"id": "back", "text": "Tell the innkeeper", "type": "talk", "target": "character/innkeeper"}
  ]
})";

} // namespace

TEST(ProjectStoryTest, ObjectivesCountOnlyInTheirSceneAndReachCompletes) {
    Project p;
    p.Write("quests/cellar.json", kCellarQuest);
    aige::StoryLibrary library = p.Story();
    ASSERT_NE(library.FindQuest("cellar"), nullptr);
    EXPECT_TRUE(library.Validate().empty());
    EXPECT_EQ(library.FindQuest("cellar")->objectives[1].scene, "scenes/cellar.aige");
    // Written back with the scene, read again the same.
    EXPECT_EQ(aige::QuestToJson(*aige::QuestFromJson(aige::QuestToJson(*library.FindQuest("cellar")), "cellar", nullptr)),
              aige::QuestToJson(*library.FindQuest("cellar")));

    aige::StoryState state;
    state.SetScene("scenes/main.aige", library);
    state.StartQuest("cellar", library);
    EXPECT_EQ(state.Quest("cellar").objective, 0u); // not in the cellar yet
    state.OnDefeated("prefabs/rat.prefab", "Enemy", library);
    EXPECT_EQ(state.Quest("cellar").count, 0); // "go down" comes first

    state.SetScene("scenes/cellar.aige", library); // arriving completes "go down"
    EXPECT_EQ(state.Quest("cellar").objective, 1u);
    state.OnDefeated("prefabs/rat.prefab", "Enemy", library);
    state.SetScene("scenes/main.aige", library); // back up: rats up here don't count for the cellar
    state.OnDefeated("prefabs/rat.prefab", "Enemy", library);
    EXPECT_EQ(state.Quest("cellar").count, 1);
    state.SetScene("scenes/cellar.aige", library);
    state.OnDefeated("prefabs/rat.prefab", "Enemy", library);
    EXPECT_EQ(state.Quest("cellar").objective, 2u);

    state.OnReached("somewhere_else", library);
    EXPECT_EQ(state.Quest("cellar").objective, 2u);
    state.OnReached("old_door", library); // any scene: no scene on this one
    EXPECT_EQ(state.Quest("cellar").objective, 3u);
    state.OnTalkedTo("character/innkeeper", library);
    EXPECT_EQ(state.Quest("cellar").state, aige::StoryState::QuestState::Completed);
}

TEST(ProjectStoryTest, ArrivingFirstStillCountsWhenTheObjectiveComesUp) {
    Project p;
    p.Write("quests/cellar.json", kCellarQuest);
    aige::StoryLibrary library = p.Story();
    aige::StoryState state;
    state.SetScene("scenes/cellar.aige", library); // already in the cellar
    state.StartQuest("cellar", library);
    EXPECT_EQ(state.Quest("cellar").objective, 1u); // "go down to the cellar": done on the spot
}

TEST(ProjectStoryTest, ProgressIsSavedAndLoaded) {
    Project p;
    p.Write("quests/cellar.json", kCellarQuest);
    aige::StoryLibrary library = p.Story();
    aige::StoryState state;
    state.SetScene("scenes/cellar.aige", library);
    state.StartQuest("cellar", library);
    state.OnDefeated("prefabs/rat.prefab", "Enemy", library);
    state.SetFlag("met_innkeeper", true);

    aige::StoryState loaded = aige::StoryState::FromJson(state.ToJson());
    EXPECT_EQ(loaded.ToJson(), state.ToJson());
    EXPECT_TRUE(loaded.Flag("met_innkeeper"));
    EXPECT_EQ(loaded.Quest("cellar").objective, 1u);
    EXPECT_EQ(loaded.Quest("cellar").count, 1);
    EXPECT_EQ(loaded.Scene(), "scenes/cellar.aige");
    loaded.OnDefeated("prefabs/rat.prefab", "Enemy", library); // carries on from where it was
    EXPECT_EQ(loaded.Quest("cellar").objective, 2u);

    // A save pointing past a quest that was edited since doesn't crash.
    nlohmann::json stale = state.ToJson();
    stale["quests"]["cellar"]["objective"] = 99;
    aige::StoryState odd = aige::StoryState::FromJson(stale);
    odd.OnDefeated("prefabs/rat.prefab", "Enemy", library);
    odd.OnReached("old_door", library);
    EXPECT_FALSE(odd.CurrentObjective(library).has_value());
    EXPECT_TRUE(aige::StoryState::FromJson("junk").ToJson()["quests"].empty());
}

TEST(ProjectStoryTest, ASessionSavesWhereThePlayerIsAndTheClock) {
    Project p;
    p.Write("quests/cellar.json", kCellarQuest);
    aige::StoryLibrary library = p.Story();
    aige::LuaVM lua;
    aige::Registry registry;
    aige::Entity player = aige::CreatePlayer(registry, 7.5f, 3.5f);
    registry.get<aige::Health>(player).current = 4.0f;
    aige::GameSession session(registry, lua, aige::BootstrapIso(), p.root);
    session.SetStory(&library);
    session.SetScene("scenes/cellar.aige");
    session.Story().StartQuest("cellar", library);
    session.SetClockMinutes(9 * 60.0);
    const nlohmann::json save = session.MakeSave();
    EXPECT_EQ(save["scene"], "scenes/cellar.aige");
    EXPECT_FLOAT_EQ(save["player"]["x"].get<float>(), 7.5f);

    // Somewhere else, later, with no progress: loading puts it all back.
    registry.get<aige::WorldTransform2D>(player) = {1.0f, 1.0f};
    registry.get<aige::Health>(player).current = 10.0f;
    session.SetStory(&library);
    session.SetClockMinutes(20 * 60.0);
    session.ApplySave(save);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(player).x, 7.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(player).current, 4.0f);
    EXPECT_DOUBLE_EQ(session.ClockMinutes(), 9 * 60.0);
    EXPECT_EQ(session.Story().Quest("cellar").objective, 1u);
    EXPECT_EQ(session.Scene(), "scenes/cellar.aige");
}

TEST(ProjectStoryTest, StandingAtAMarkerReachesIt) {
    Project p;
    p.Write("quests/door.json", R"({"name": "Door", "objectives": [
        {"id": "door", "text": "Find the old door", "type": "reach", "target": "old_door"}]})");
    aige::StoryLibrary library = p.Story();
    aige::LuaVM lua;
    aige::Registry registry;
    aige::Entity player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    aige::Entity marker = registry.create();
    registry.emplace<aige::WorldTransform2D>(marker, 6.5f, 2.5f);
    registry.emplace<aige::Marker>(marker, "old_door");
    aige::GameSession session(registry, lua, aige::BootstrapIso(), p.root);
    session.SetStory(&library);
    session.Story().StartQuest("door", library);
    auto none = [](aige::Key) { return false; };
    session.Advance(1.0 / 60.0, none);
    EXPECT_EQ(session.Story().Quest("door").state, aige::StoryState::QuestState::Active);
    registry.get<aige::WorldTransform2D>(player) = {6.3f, 2.6f};
    session.Advance(1.0 / 60.0, none);
    EXPECT_EQ(session.Story().Quest("door").state, aige::StoryState::QuestState::Completed);
}

TEST(ProjectStoryTest, AScheduleCanSendSomeoneToAnotherScene) {
    Project p;
    p.Write("schedules/innkeeper.json", R"({"name": "Innkeeper", "entries": [
        {"from": "06:00", "activity": "stay", "place": "bar"},
        {"from": "20:00", "activity": "stay", "place": "cellar_door", "scene": "scenes/cellar.aige"}]})");
    aige::ScheduleLibrary schedules(p.root);
    schedules.Refresh();
    ASSERT_EQ(schedules.Schedules().size(), 1u);
    EXPECT_EQ(schedules.Schedules()[0].entries[1].scene, "scenes/cellar.aige");
    EXPECT_EQ(aige::ScheduleToJson(*aige::ScheduleFromJson(aige::ScheduleToJson(schedules.Schedules()[0]), "innkeeper", nullptr)),
              aige::ScheduleToJson(schedules.Schedules()[0]));

    // Places are checked in the entry's own scene.
    auto problems = aige::ValidateSchedule(schedules.Schedules()[0], [](const std::string& scene, const std::string& name) {
        return scene.empty() ? name == "bar" : (scene == "scenes/cellar.aige" && name == "bar");
    });
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("no marker called 'cellar_door' in scenes/cellar.aige"), std::string::npos);

    aige::Registry registry;
    aige::Entity bar = registry.create();
    registry.emplace<aige::WorldTransform2D>(bar, 3.5f, 3.5f);
    registry.emplace<aige::Marker>(bar, "bar");
    aige::Entity npc = registry.create();
    registry.emplace<aige::WorldTransform2D>(npc, 3.5f, 3.5f);
    registry.emplace<aige::Routine>(npc, "schedules/innkeeper.json");
    registry.emplace<aige::Sprite>(npc);
    auto open = [](int, int) { return false; };
    uint64_t rng = 1;

    // Daytime in the inn: here.
    aige::UpdateRoutines(registry, schedules, 9 * 60.0f, open, 0.1f, rng, "scenes/inn.aige");
    EXPECT_FALSE(registry.all_of<aige::Sleeping>(npc));
    // Evening: down in the cellar, so away from the inn.
    aige::UpdateRoutines(registry, schedules, 21 * 60.0f, open, 0.1f, rng, "scenes/inn.aige");
    EXPECT_TRUE(registry.all_of<aige::Sleeping>(npc));
    EXPECT_EQ(registry.get<aige::Sprite>(npc).a, 0.0f);
    // The next morning she's back.
    aige::UpdateRoutines(registry, schedules, 7 * 60.0f, open, 0.1f, rng, "scenes/inn.aige");
    EXPECT_FALSE(registry.all_of<aige::Sleeping>(npc));
    EXPECT_EQ(registry.get<aige::Sprite>(npc).a, 1.0f);
    // Played in the cellar itself, the evening entry has her there.
    aige::UpdateRoutines(registry, schedules, 21 * 60.0f, open, 0.1f, rng, "scenes/cellar.aige");
    EXPECT_FALSE(registry.all_of<aige::Sleeping>(npc));
}

TEST(ProjectStoryTest, LocationsNameTheirSceneAndFollowARename) {
    Project p;
    p.Write("scenes/cellar.aige", "# AIGE Scene v1\n");
    aige::WorldBible world(aige::Project{p.root});
    aige::WorldEntry cellar;
    cellar.kind = "location";
    cellar.id = "inn-cellar";
    cellar.name = "The inn's cellar";
    cellar.summary = "Damp, and full of rats.";
    cellar.scene = "scenes/cellar.aige";
    ASSERT_FALSE(world.Save(cellar).has_value());
    EXPECT_NE(p.Read("world/location/inn-cellar.md").find("scene: scenes/cellar.aige"), std::string::npos);
    world.Refresh();
    ASSERT_NE(world.LocationOf("scenes/cellar.aige"), nullptr);
    EXPECT_EQ(world.LocationOf("scenes/cellar.aige")->name, "The inn's cellar");
    EXPECT_TRUE(world.Validate().empty());

    // Checked: a scene that doesn't exist, a non-place naming a scene, two places for one scene.
    aige::WorldEntry lost = cellar;
    lost.id = "lost";
    lost.scene = "scenes/attic.aige";
    world.Save(lost);
    aige::WorldEntry person;
    person.kind = "character";
    person.id = "rat";
    person.name = "A rat";
    person.scene = "scenes/cellar.aige";
    world.Save(person);
    aige::WorldEntry twin = cellar;
    twin.id = "twin";
    world.Save(twin);
    world.Refresh();
    std::string problems;
    for (const auto& problem : world.Validate()) problems += problem.key + ": " + problem.message + "\n";
    EXPECT_NE(problems.find("scene scenes/attic.aige doesn't exist"), std::string::npos) << problems;
    EXPECT_NE(problems.find("only locations name a scene"), std::string::npos) << problems;
    EXPECT_NE(problems.find("is already location/inn-cellar"), std::string::npos) << problems;

    // Renaming the scene: the place, a quest and a schedule follow.
    p.Write("quests/cellar.json", kCellarQuest);
    p.Write("schedules/innkeeper.json", R"({"name": "Innkeeper", "entries": [
        {"from": "20:00", "activity": "stay", "place": "cellar_door", "scene": "scenes/cellar.aige"}]})");
    auto moved = aige::MoveProjectFile(aige::Project{p.root}, "scenes/cellar.aige", "scenes/undercroft.aige");
    ASSERT_TRUE(moved.error.empty()) << moved.error;
    EXPECT_NE(p.Read("world/location/inn-cellar.md").find("scene: scenes/undercroft.aige"), std::string::npos);
    EXPECT_EQ(p.Read("quests/cellar.json").find("scenes/cellar.aige"), std::string::npos);
    EXPECT_NE(p.Read("schedules/innkeeper.json").find("scenes/undercroft.aige"), std::string::npos);
}

TEST(ProjectStoryTest, QuestReferencesToScenesAndMarkersAreChecked) {
    Project p;
    p.Write("quests/cellar.json", kCellarQuest);
    aige::StoryLibrary library = p.Story();
    aige::WorldBible world(aige::Project{p.root});
    aige::WorldEntry innkeeper;
    innkeeper.kind = "character";
    innkeeper.id = "innkeeper";
    innkeeper.name = "Innkeeper";
    fs::create_directories(p.root / "world/character");
    world.Save(innkeeper);
    world.Refresh();

    aige::SceneMarkerMap scenes = {{"scenes/main.aige", {"well"}}, {"scenes/cellar.aige", {"old_door"}}};
    auto problems = aige::ValidateStoryReferences(library, world, {}, &scenes);
    EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].message);

    scenes.erase("scenes/cellar.aige");
    problems = aige::ValidateStoryReferences(library, world, {}, &scenes);
    std::string text;
    for (const auto& problem : problems) text += problem.message + "\n";
    EXPECT_NE(text.find("scene scenes/cellar.aige doesn't exist"), std::string::npos) << text;
    EXPECT_NE(text.find("no marker called 'old_door' in any scene"), std::string::npos) << text;
}
