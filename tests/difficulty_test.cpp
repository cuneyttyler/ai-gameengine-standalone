#include <gtest/gtest.h>

#include "game/GameFlow.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/Difficulty.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Encounters.h"
#include "gameplay/GameSession.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md Phase 16: scalable fields and categories, level records and
// effective values, per-entity overrides, level scaling, adaptive difficulty,
// assists, the combat preview, and the game's menus (difficulty.md).
namespace {

const aige::FieldInfo& FieldOf(const char* component, const char* field) {
    return *aige::EngineReflection().FindComponent(component)->Field(field);
}

class DifficultyTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_ = aige::ActiveSettings();
        root_ = fs::temp_directory_path() /
                ("aige_difficulty_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "records");
    }
    void TearDown() override {
        aige::ActiveRecords().Close();
        aige::ActiveSettings() = saved_;
        aige::ActiveAssists() = {};
        fs::remove_all(root_);
    }
    void Write(const std::string& relative, const std::string& text) {
        fs::create_directories((root_ / relative).parent_path());
        std::ofstream(root_ / relative, std::ios::binary) << text;
    }
    void OpenRecords() {
        aige::ActiveRecords().Open(aige::EngineReflection(), root_);
        ASSERT_TRUE(aige::ActiveRecords().Problems().empty()) << aige::ActiveRecords().Problems()[0].message;
    }
    aige::ProjectSettings saved_;
    fs::path root_;
};

} // namespace

TEST_F(DifficultyTest, FieldsAreScalableByKind) {
    EXPECT_EQ(FieldOf("Health", "max").scalable, "health");
    EXPECT_EQ(FieldOf("MeleeAttack", "damage").scalable, "damage");
    EXPECT_EQ(FieldOf("Perception", "sight").scalable, "perception");
    EXPECT_TRUE(FieldOf("Health", "current").scalable.empty());
    // ...and the AI's schema says so.
    const auto schema = aige::EngineReflection().ExportSchema();
    bool found = false;
    for (const auto& c : schema["components"]) {
        if (c["name"] != "Health") continue;
        for (const auto& f : c["fields"]) found |= f["name"] == "max" && f.value("scalable", "") == "health";
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(aige::DifficultyCategory("health", aige::DifficultyOwner::Enemy), "enemies");
    EXPECT_EQ(aige::DifficultyCategory("health", aige::DifficultyOwner::Player), "player");
    EXPECT_EQ(aige::DifficultyCategory("perception", aige::DifficultyOwner::Enemy), "stealth");
    EXPECT_EQ(aige::DifficultyCategory("gold", aige::DifficultyOwner::Player), "resources");
}

TEST_F(DifficultyTest, BuiltInLevelsScaleByCategory) {
    const auto model = aige::DifficultyModel::BuiltIn();
    ASSERT_EQ(model.Levels().size(), 5u);
    EXPECT_EQ(model.DefaultLevel(), "normal");
    const auto& health = FieldOf("Health", "max");
    const nlohmann::json none = nlohmann::json::object();
    auto value = [&](aige::DifficultyOwner owner, const char* level) {
        return model.Effective(10.0f, health, "Health.max", owner, "", level, none).value;
    };
    EXPECT_FLOAT_EQ(value(aige::DifficultyOwner::Enemy, "normal"), 10.0f);
    EXPECT_FLOAT_EQ(value(aige::DifficultyOwner::Enemy, "hard"), 15.0f);
    EXPECT_FLOAT_EQ(value(aige::DifficultyOwner::Enemy, "story"), 5.0f);
    EXPECT_FLOAT_EQ(value(aige::DifficultyOwner::Player, "story"), 20.0f);
    EXPECT_FLOAT_EQ(value(aige::DifficultyOwner::Player, "hard"), 10.0f); // the player's step stays 3
    // The why.
    const auto hard = model.Effective(10.0f, health, "Health.max", aige::DifficultyOwner::Enemy, "", "hard", none);
    ASSERT_EQ(hard.why.size(), 1u);
    EXPECT_NE(hard.why[0].find("enemies health x1.5 (step 4)"), std::string::npos);
    // A hand-set value wins for its level only.
    const auto overrides = nlohmann::json::parse(R"({"Health.max": {"hard": 40}})");
    EXPECT_FLOAT_EQ(model.Effective(10.0f, health, "Health.max", aige::DifficultyOwner::Enemy, "", "hard", overrides).value, 40.0f);
    EXPECT_TRUE(model.Effective(10.0f, health, "Health.max", aige::DifficultyOwner::Enemy, "", "hard", overrides).overridden);
    EXPECT_FLOAT_EQ(model.Effective(10.0f, health, "Health.max", aige::DifficultyOwner::Enemy, "", "nightmare", overrides).value,
                    20.0f);
    // Whole numbers stay whole.
    const auto& alive = FieldOf("Spawner", "maxAlive");
    EXPECT_FLOAT_EQ(model.Effective(3.0f, alive, "Spawner.maxAlive", aige::DifficultyOwner::Ally, "", "hard", none).value, 3.0f);
    EXPECT_FLOAT_EQ(model.Effective(3.0f, alive, "Spawner.maxAlive", aige::DifficultyOwner::Enemy, "", "nightmare", none).value,
                    5.0f); // 4.5 rounds
}

TEST_F(DifficultyTest, ProjectLevelsAndModifiers) {
    Write("records/difficulty/casual.json", R"({"name": "Casual", "order": 1, "categories": {"enemies": 2}})");
    Write("records/difficulty/brutal.json", R"({"name": "Brutal", "order": 2, "categories": {"enemies": 5},
        "modifiers": [{"target": "Health.max", "owner": "enemy", "tag": "boss", "op": "mul", "value": 2},
                      {"target": "MeleeAttack.damage", "op": "add", "value": 1}], "xp": 1.5})");
    Write("records/difficulty-table/default.json",
          R"({"rows": [{"category": "enemies", "kind": "health", "steps": [0.5, 0.8, 1, 1.4, 3]}]})");
    OpenRecords();
    const auto model = aige::DifficultyModel::FromRecords(aige::ActiveRecords());
    ASSERT_EQ(model.Levels().size(), 2u);
    EXPECT_EQ(model.Levels()[0].id, "casual");
    EXPECT_FALSE(model.BuiltInLevels());
    EXPECT_EQ(model.DefaultLevel(), "brutal"); // no "normal": the middle one
    const nlohmann::json none = nlohmann::json::object();
    const auto& health = FieldOf("Health", "max");
    EXPECT_FLOAT_EQ(model.Effective(10, health, "Health.max", aige::DifficultyOwner::Enemy, "Skeleton", "brutal", none).value, 30.0f);
    EXPECT_FLOAT_EQ(model.Effective(10, health, "Health.max", aige::DifficultyOwner::Enemy, "Skeleton Boss", "brutal", none).value,
                    60.0f);
    EXPECT_FLOAT_EQ(model.Effective(10, health, "Health.max", aige::DifficultyOwner::Enemy, "Rat", "casual", none).value, 8.0f);
    EXPECT_FLOAT_EQ(model.Find("brutal")->xp, 1.5f);
}

TEST_F(DifficultyTest, ApplyingScalesEntitiesFromTheirBase) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto skeleton = aige::CreateEnemy(registry, 5.5f, 5.5f, hero, "");
    registry.get<aige::Health>(skeleton) = {10.0f, 10.0f};
    registry.get<aige::Health>(hero).current = registry.get<aige::Health>(hero).max * 0.5f; // hurt
    const float heroMax = registry.get<aige::Health>(hero).max;
    const auto model = aige::DifficultyModel::BuiltIn();
    auto& reflection = aige::EngineReflection();

    EXPECT_GT(aige::ApplyDifficulty(registry, reflection, model, "hard"), 0);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 15.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).current, 15.0f); // the same share
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).max, heroMax);
    // Changing level recomputes from the base, not from the scaled value.
    aige::ApplyDifficulty(registry, reflection, model, "nightmare");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 20.0f);
    aige::ApplyDifficulty(registry, reflection, model, "story");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 5.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).max, heroMax * 2.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).current, heroMax); // still half
    // A boss's hand-set Hard health is kept.
    registry.emplace<aige::DifficultyOverrides>(skeleton, R"({"Health.max": {"hard": 99}})");
    aige::ApplyDifficulty(registry, reflection, model, "hard");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 99.0f);
    // The preview shows every level.
    const auto preview = aige::DifficultyPreview(registry, reflection, skeleton, model);
    bool sawHealth = false;
    for (const auto& field : preview) {
        if (field["field"] != "Health.max") continue;
        sawHealth = true;
        EXPECT_FLOAT_EQ(field["base"].get<float>(), 10.0f);
        EXPECT_EQ(field["levels"].size(), 5u);
        EXPECT_TRUE(field["levels"][3]["overridden"].get<bool>()); // hard
    }
    EXPECT_TRUE(sawHealth);
    // Back to the files' values.
    aige::RestoreDifficultyBase(registry, reflection);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 10.0f);
    EXPECT_FALSE(registry.all_of<aige::DifficultyBase>(skeleton));
}

TEST_F(DifficultyTest, TheCombatPreview) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    registry.emplace<aige::MeleeAttack>(hero, 3.0f, 0.7f, 0.5f, 0.35f, 0.5f); // characters come without an attack
    auto skeleton = aige::CreateEnemy(registry, 5.5f, 5.5f, hero, "");
    registry.get<aige::Health>(hero) = {20.0f, 20.0f};
    registry.get<aige::Health>(skeleton) = {12.0f, 12.0f};
    registry.get<aige::MeleeAttack>(hero).damage = 3.0f;
    registry.get<aige::MeleeAttack>(skeleton).damage = 2.0f;
    registry.emplace_or_replace<aige::Stats>(hero, 0.0f, 0.0f, 4.0f);
    registry.emplace_or_replace<aige::Stats>(skeleton, 0.0f, 0.0f, 2.0f);
    const auto rows = aige::CombatPreview(registry, aige::EngineReflection(), skeleton, hero, aige::DifficultyModel::BuiltIn());
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(rows[2].level, "normal");
    EXPECT_EQ(rows[2].hitsToKillEnemy, 4);  // 12 / 3
    EXPECT_EQ(rows[2].hitsToKillPlayer, 10); // 20 / 2
    EXPECT_EQ(rows[3].hitsToKillEnemy, 6);  // 18 / 3
    EXPECT_EQ(rows[3].hitsToKillPlayer, 7); // 20 / 3
    EXPECT_GT(rows[0].hitsToKillPlayer, rows[4].hitsToKillPlayer);
}

TEST_F(DifficultyTest, EncounterLevels) {
    aige::Registry registry;
    auto zone = registry.create();
    registry.emplace<aige::Zone>(zone, 5, 8);
    auto& settings = aige::ActiveSettings().difficulty;
    settings.scaling = aige::LevelScaling::None;
    EXPECT_EQ(aige::EncounterLevel(registry, 2, 10), 2);
    settings.scaling = aige::LevelScaling::FixedZones;
    EXPECT_EQ(aige::EncounterLevel(registry, 2, 10), 5);
    settings.scaling = aige::LevelScaling::Scaled;
    settings.levelOffset = 1;
    EXPECT_EQ(aige::EncounterLevel(registry, 2, 10), 11);
    settings.scaling = aige::LevelScaling::Hybrid;
    EXPECT_EQ(aige::EncounterLevel(registry, 2, 10), 8);
    EXPECT_EQ(aige::EncounterLevel(registry, 2, 5), 6);
}

TEST_F(DifficultyTest, AdaptiveDifficultyStaysWithinOneStep) {
    aige::AdaptiveDifficulty a;
    a.OnPlayerDeath();
    a.OnPlayerDeath();
    EXPECT_EQ(a.shift, 0);
    a.OnPlayerDeath();
    EXPECT_EQ(a.shift, -1);
    for (int i = 0; i < 9; ++i) a.OnPlayerDeath();
    EXPECT_EQ(a.shift, -1); // never beyond one
    a.OnFightWon(true);     // hurt: doesn't count
    a.OnFightWon(false);
    EXPECT_EQ(a.shift, -1);
    a.OnFightWon(false);
    EXPECT_EQ(a.shift, 0);
    EXPECT_EQ(aige::AdaptiveDifficulty::FromJson(a.ToJson()).shift, 0);
}

TEST_F(DifficultyTest, AssistsProtectThePlayer) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    registry.get<aige::Health>(hero) = {100.0f, 100.0f};
    aige::ActiveAssists().damageReduction = 0.5f;
    aige::DealDamage(registry, hero, 10.0f, entt::null, nullptr, false);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).current, 95.0f);
    aige::ActiveAssists().invincible = true;
    aige::DealDamage(registry, hero, 10.0f, entt::null, nullptr, false);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).current, 95.0f);
    aige::ActiveAssists() = {};
    aige::ActiveAssists().growingReduction = true;
    aige::ActiveAssists().deaths = 100;
    EXPECT_FLOAT_EQ(aige::ActiveAssists().Reduction(), 0.8f);
}

TEST_F(DifficultyTest, TheSessionPlaysAtALevel) {
    Write("records/item/x.json", R"({"name": "X"})"); // a records folder the session reads
    OpenRecords();
    aige::Registry registry;
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto skeleton = aige::CreateEnemy(registry, 8.5f, 8.5f, hero, "");
    registry.get<aige::Health>(skeleton) = {10.0f, 10.0f};
    aige::LuaVM lua;
    aige::GameSession session(registry, lua, aige::IsoParams{}, root_);
    auto step = [&] { session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; }); };
    step(); // the default level, applied before play
    EXPECT_EQ(session.DifficultyLevelId(), "normal");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 10.0f);
    session.SetDifficulty("hard"); // during play
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 15.0f);
    const auto save = session.MakeSave();
    session.SetDifficulty("story");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 5.0f);
    session.ApplySave(save);
    step();
    EXPECT_EQ(session.DifficultyLevelId(), "hard");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(skeleton).max, 15.0f);
    session.SetDifficulty("no-such-level"); // ignored
    EXPECT_EQ(session.DifficultyLevelId(), "hard");
    // Stories can ask about it.
    EXPECT_TRUE(session.Story().Holds(aige::StoryConditionsFromJson(nlohmann::json::parse(R"([{"difficulty": "hard"}])"))));
    EXPECT_TRUE(session.Story().Holds(
        aige::StoryConditionsFromJson(nlohmann::json::parse(R"([{"difficulty": {"enemies": {"atLeast": 4}}}])"))));
    EXPECT_FALSE(session.Story().Holds(aige::StoryConditionsFromJson(nlohmann::json::parse(R"([{"difficulty": "story"}])"))));
}

TEST_F(DifficultyTest, TheMenusOfferLevelsAndAssists) {
    aige::GameFlow flow;
    aige::PlayerSettings settings;
    flow.SetPlayerSettings(&settings, {});
    flow.SetDifficulties({{"easy", "Easy", "Gentle."}, {"normal", "Normal", ""}, {"hard", "Hard", ""}}, "normal", false, true);
    flow.Begin();
    ASSERT_EQ(flow.Current(), aige::GameFlow::State::Title);
    // New game asks for the level first.
    auto items = flow.View()->items;
    int newGame = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].label == "New game") newGame = static_cast<int>(i);
    }
    ASSERT_GE(newGame, 0);
    EXPECT_EQ(flow.Choose(newGame).kind, aige::GameFlow::Command::Kind::None);
    ASSERT_EQ(flow.Current(), aige::GameFlow::State::Difficulty);
    EXPECT_EQ(flow.View()->body, "Gentle.");
    const auto command = flow.Choose(2);
    EXPECT_EQ(command.kind, aige::GameFlow::Command::Kind::NewGame);
    EXPECT_EQ(command.level, "hard");
    // In play, with changeAnytime off, Settings has assists but no difficulty.
    flow.Pause();
    items = flow.View()->items;
    int settingsItem = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].label == "Settings") settingsItem = static_cast<int>(i);
    }
    ASSERT_GE(settingsItem, 0);
    flow.Choose(settingsItem);
    std::string labels;
    const auto settingsView = flow.View();
    for (const auto& item : settingsView->items) labels += item.label + "|";
    EXPECT_EQ(labels.find("Difficulty  "), std::string::npos) << labels;
    EXPECT_NE(labels.find("Adjust difficulty to how I play"), std::string::npos);
    EXPECT_NE(labels.find("Damage reduction  0%"), std::string::npos) << labels;
    // Picking an assist steps it.
    const auto settingsItems = flow.View()->items;
    for (size_t i = 0; i < settingsItems.size(); ++i) {
        if (settingsItems[i].label.rfind("Damage reduction", 0) == 0) {
            EXPECT_EQ(flow.Choose(static_cast<int>(i)).kind, aige::GameFlow::Command::Kind::SettingsChanged);
        }
    }
    EXPECT_FLOAT_EQ(settings.damageReduction, 0.25f);
    EXPECT_FLOAT_EQ(aige::PlayerSettings::FromJson(settings.ToJson()).damageReduction, 0.25f);
}
