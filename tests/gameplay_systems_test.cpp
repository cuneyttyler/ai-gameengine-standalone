#include <gtest/gtest.h>

#include "game/PlayWindows.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Attributes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Encounters.h"
#include "gameplay/GameSession.h"
#include "gameplay/Interact.h"
#include "gameplay/Items.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

// development_plan.md Phase 13: attributes, progression, items, loot, trade,
// interactables, combat v2, factions and spawners.
namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A project with records for every system, opened as the active records.
class SystemsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_systems_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        Write(root_ / "records/item/potion.json", R"({"name": "Potion", "kind": "consumable", "value": 10, "stack": 5,
                                                     "use": {"heal": 5}})");
        Write(root_ / "records/item/sword.json", R"({"name": "Sword", "kind": "weapon", "slot": "weapon", "value": 30,
                                                    "damage": 3, "weight": 4})");
        Write(root_ / "records/item/ring.json", R"({"name": "Ring", "slot": "ring", "value": 50,
                                                   "modifiers": [{"attribute": "maxHealth", "op": "multiply", "value": 1.5}]})");
        Write(root_ / "records/item/bone.json", R"({"name": "Bone", "value": 2})");
        Write(root_ / "records/item/iron-key.json", R"({"name": "Iron Key", "kind": "key"})");
        Write(root_ / "records/loot/skeleton.json", R"({"rolls": 2, "goldMin": 3, "goldMax": 3,
            "entries": [{"item": "bone", "weight": 1, "min": 1, "max": 1}, {"table": "rare", "weight": 1, "min": 1, "max": 1}]})");
        Write(root_ / "records/loot/rare.json", R"({"rolls": 1, "entries": [{"item": "ring", "weight": 1, "levelMin": 5},
                                                  {"item": "potion", "weight": 1, "min": 2, "max": 2}]})");
        Write(root_ / "records/perk/tough.json", R"({"name": "Tough", "level": 2,
                                                    "modifiers": [{"attribute": "defense", "value": 2}]})");
        Write(root_ / "records/progression/default.json", R"({"baseExperience": 100, "growth": 2,
                                                             "perLevel": [{"attribute": "maxHealth", "value": 5}]})");
        Write(root_ / "records/faction/wardens.json", R"({"name": "Wardens", "relations": [{"faction": "bandits", "standing": -80}]})");
        Write(root_ / "records/faction/bandits.json", R"({"name": "Bandits"})");
        Write(root_ / "prefabs/skeleton.prefab", "# a placeholder: the spawner test makes its own\n");
        Write(root_ / "records/encounter/crypt.json", R"({"entries": [{"prefab": "prefabs/skeleton.prefab", "weight": 1, "min": 2, "max": 2}]})");
        aige::ActiveRecords().Open(aige::EngineReflection(), root_);
        ASSERT_TRUE(aige::ActiveRecords().Problems().empty()) << aige::ActiveRecords().Problems()[0].message;
    }
    void TearDown() override {
        aige::ActiveRecords().Close();
        fs::remove_all(root_);
    }
    fs::path root_;
};

} // namespace

TEST_F(SystemsTest, AttributesComeFromBaseEquipmentPerksAndModifiers) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto& attributes = registry.emplace<aige::Attributes>(hero);
    attributes.maxHealth = 20.0f;
    attributes.attack = 1.0f;
    attributes.maxStamina = 50.0f;
    registry.emplace<aige::Equipment>(hero).weapon = "sword";
    registry.get<aige::Equipment>(hero).ring = "ring";
    aige::UpdateAttributes(registry, 0.016f);
    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(hero).attack, 4.0f);   // 1 + the sword's 3
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).max, 30.0f);    // 20 x the ring's 1.5
    EXPECT_FLOAT_EQ(registry.get<aige::Resources>(hero).maxStamina, 50.0f);

    aige::AddModifier(registry, hero, {"attack", aige::ModifierOp::Add, 2.0f, "potion", 1.0f});
    aige::UpdateAttributes(registry, 0.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(hero).attack, 6.0f);
    aige::UpdateAttributes(registry, 0.6f); // the boost has run out
    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(hero).attack, 4.0f);

    // Experience: 100 to level 2 (growth 2: 200 to 3); each level adds 5 max health.
    auto& progression = registry.emplace<aige::Progression>(hero);
    EXPECT_EQ(aige::AwardExperience(registry, hero, 350.0f), 2);
    EXPECT_EQ(progression.level, 3);
    EXPECT_FLOAT_EQ(progression.experience, 50.0f);
    EXPECT_EQ(progression.perkPoints, 2);
    EXPECT_EQ(aige::TakePerk(registry, hero, "tough"), "");
    EXPECT_NE(aige::TakePerk(registry, hero, "tough"), ""); // only once
    aige::UpdateAttributes(registry, 0.016f);
    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(hero).defense, 2.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).max, 45.0f); // (20 + 2 x 5) x 1.5
}

TEST_F(SystemsTest, InventoriesStackWeighEquipAndUse) {
    aige::Inventory bag;
    bag.maxWeight = 10.0f;
    EXPECT_EQ(aige::AddItem(bag, "potion", 3), 3);
    EXPECT_EQ(aige::AddItem(bag, "sword", 3), 2); // 4 each: only two fit under 10
    EXPECT_EQ(bag.items, "potion*3, sword*2");
    EXPECT_EQ(aige::RemoveItem(bag, "potion", 5), 3);
    EXPECT_EQ(bag.items, "sword*2");
    aige::Inventory slots;
    slots.slots = 1;
    EXPECT_EQ(aige::AddItem(slots, "potion", 7), 5); // one slot of five

    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    registry.emplace<aige::Inventory>(hero).items = "potion*2, sword, bone";
    registry.get<aige::Health>(hero).current = 2.0f;
    EXPECT_EQ(aige::UseItem(registry, hero, "potion"), "");
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).current, 7.0f);
    EXPECT_NE(aige::UseItem(registry, hero, "bone"), ""); // nothing to use
    EXPECT_EQ(aige::Equip(registry, hero, "sword"), "");
    EXPECT_EQ(registry.get<aige::Equipment>(hero).weapon, "sword");
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(hero), "sword"), 0);
    EXPECT_EQ(aige::Unequip(registry, hero, "weapon"), "");
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(hero), "sword"), 1);
}

TEST_F(SystemsTest, LootRollsWeightsNestedTablesAndLevels) {
    std::mt19937 rng(7);
    int rings = 0, potions = 0;
    for (int i = 0; i < 200; ++i) {
        auto low = aige::RollLoot("skeleton", 1, rng);
        EXPECT_EQ(low.gold, 3);
        for (const auto& s : low.items) {
            EXPECT_NE(s.item, "ring") << "rings only from level 5";
            if (s.item == "potion") potions += s.count;
        }
        auto high = aige::RollLoot("skeleton", 6, rng);
        for (const auto& s : high.items) rings += s.item == "ring";
    }
    EXPECT_GT(potions, 0);
    EXPECT_GT(rings, 0);
    EXPECT_EQ(aige::ItemPrice("sword", 1.0f), 30);
    EXPECT_EQ(aige::ItemPrice("bone", 0.1f), 1); // at least one
}

TEST_F(SystemsTest, DoorsLocksChestsAndPickups) {
    aige::Registry registry;
    for (int x = 0; x < 6; ++x) {
        for (int y = 0; y < 6; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    registry.emplace<aige::Inventory>(hero);
    auto door = registry.create();
    registry.emplace<aige::WorldTransform2D>(door, 2.5f, 1.5f);
    registry.emplace<aige::Obstacle>(door);
    auto& lock = registry.emplace<aige::Interactable>(door);
    lock.locked = true;
    lock.key = "iron-key";
    lock.consumeKey = true;
    registry.emplace<aige::Door>(door);
    aige::StoryState story;
    std::mt19937 rng(1);

    EXPECT_TRUE(aige::TileGrid::Build(registry).IsSolid(2, 1));
    ASSERT_EQ(aige::FindInteractable(registry, hero), door);
    EXPECT_EQ(aige::InteractPrompt(registry, door), "Open it");
    auto result = aige::Interact(registry, hero, door, story, 1, rng);
    EXPECT_NE(result.message.find("needs Iron Key"), std::string::npos);
    EXPECT_FALSE(registry.get<aige::Door>(door).open);
    aige::AddItem(registry.get<aige::Inventory>(hero), "iron-key");
    result = aige::Interact(registry, hero, door, story, 1, rng);
    EXPECT_TRUE(registry.get<aige::Door>(door).open);
    EXPECT_FALSE(aige::TileGrid::Build(registry).IsSolid(2, 1)); // open: passable
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(hero), "iron-key"), 0); // used up

    // A chest fills from its loot table once.
    registry.get<aige::Door>(door).open = false;
    registry.get<aige::WorldTransform2D>(door).x = 5.5f; // out of the way
    auto chest = registry.create();
    registry.emplace<aige::WorldTransform2D>(chest, 1.5f, 2.5f);
    registry.emplace<aige::Container>(chest, "potion", 0, "skeleton", false);
    ASSERT_EQ(aige::FindInteractable(registry, hero), chest);
    result = aige::Interact(registry, hero, chest, story, 1, rng);
    EXPECT_NE(result.message.find("Took"), std::string::npos);
    EXPECT_GE(registry.get<aige::Inventory>(hero).gold, 3);
    EXPECT_GE(aige::CountItem(registry.get<aige::Inventory>(hero), "potion"), 1);
    EXPECT_TRUE(registry.get<aige::Container>(chest).looted);

    // Walking over an item picks it up.
    auto dropped = aige::DropLoot(registry, 1.5f, 1.5f, {{"bone", 2}}, 4, rng);
    EXPECT_EQ(dropped.size(), 2u);
    auto taken = aige::CollectItemPickups(registry, hero);
    EXPECT_EQ(taken.size(), 2u);
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(hero), "bone"), 2);
}

TEST_F(SystemsTest, ShopsBuyAndSell) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto& bag = registry.emplace<aige::Inventory>(hero);
    bag.gold = 35;
    bag.items = "bone*3";
    auto trader = registry.create();
    registry.emplace<aige::WorldTransform2D>(trader, 2.5f, 1.5f);
    auto& shop = registry.emplace<aige::Merchant>(trader);
    shop.items = "sword, potion*4";
    shop.gold = 10;
    std::mt19937 rng(3);
    aige::PlayWindows windows;
    windows.OpenShop(registry, hero, trader, 1, 1, rng);
    auto view = windows.View(registry);
    ASSERT_TRUE(view.has_value());
    ASSERT_EQ(view->items.size(), 4u); // buy sword, buy potion, sell bone, close
    windows.Choose(registry, 0);       // the sword: 30
    EXPECT_EQ(registry.get<aige::Inventory>(hero).gold, 5);
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(hero), "sword"), 1);
    EXPECT_EQ(windows.TakeMessage(), "Bought Sword");
    windows.Choose(registry, 0); // a potion: 10, too dear now
    EXPECT_EQ(windows.TakeMessage(), "");
    // Sell a bone (value 2 x 0.4 rounds to 1).
    view = windows.View(registry);
    int sellIndex = -1;
    for (size_t i = 0; i < view->items.size(); ++i) {
        if (view->items[i].label.rfind("Sell  Bone", 0) == 0) sellIndex = static_cast<int>(i);
    }
    ASSERT_GE(sellIndex, 0);
    windows.Choose(registry, sellIndex);
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(hero), "bone"), 2);
    EXPECT_EQ(registry.get<aige::Inventory>(hero).gold, 6);
}

TEST_F(SystemsTest, ProjectilesResistancesBlockingAndDodges) {
    aige::Registry registry;
    for (int x = 0; x < 12; ++x) {
        for (int y = 0; y < 4; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    auto archer = aige::CreateEnemy(registry, 1.5f, 1.5f, entt::null, "");
    auto& bow = registry.emplace<aige::RangedAttack>(archer);
    bow.damage = 4.0f;
    bow.damageType = aige::DamageType::Fire;
    auto hero = aige::CreatePlayer(registry, 5.5f, 1.5f);
    registry.get<aige::Health>(hero).current = registry.get<aige::Health>(hero).max = 50.0f;
    registry.emplace<aige::Attributes>(hero).resistFire = 0.5f;
    registry.get<aige::Attributes>(hero).maxHealth = 50.0f;
    auto isSolid = [&](int x, int y) { return aige::TileGrid::Build(registry).IsSolid(x, y); };
    std::vector<aige::GameEvent> events;

    ASSERT_TRUE(aige::TryRangedAttack(registry, archer, 1.0f, 0.0f).has_value());
    EXPECT_FALSE(aige::TryRangedAttack(registry, archer, 1.0f, 0.0f).has_value()); // cooling down
    float dealt = 0.0f;
    for (int i = 0; i < 120 && dealt == 0.0f; ++i) {
        events.clear();
        aige::UpdateCombat(registry, isSolid, 1.0f / 60.0f, events);
        for (const auto& e : events) {
            if (e.type == aige::GameEvent::Type::Hit && e.entity == hero) dealt = e.amount;
        }
    }
    EXPECT_FLOAT_EQ(dealt, 2.0f); // fire 4, half resisted, no defense
    EXPECT_EQ(registry.get<aige::LastHit>(hero).source, archer);

    // Blocking stops most of a hit; dodging, all of it.
    registry.emplace<aige::Blocking>(hero);
    const float blocked = aige::DealDamage(registry, hero, 10.0f, archer, nullptr);
    EXPECT_NEAR(blocked, 3.0f, 0.01f);
    registry.remove<aige::Blocking>(hero);
    ASSERT_TRUE(aige::StartDodge(registry, hero, -1.0f, 0.0f));
    EXPECT_EQ(aige::DealDamage(registry, hero, 10.0f, archer, nullptr), 0.0f);
}

TEST_F(SystemsTest, FactionsDecideWhoFights) {
    aige::Registry registry;
    auto warden = registry.create();
    registry.emplace<aige::FactionMember>(warden, "wardens", 30);
    auto bandit = registry.create();
    registry.emplace<aige::FactionMember>(bandit, "bandits", 0);
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    EXPECT_TRUE(aige::AreHostile(registry, warden, bandit)); // -80 between them
    EXPECT_FALSE(aige::AreHostile(registry, hero, warden));  // likes the player
    registry.emplace<aige::Health>(warden);
    aige::DealDamage(registry, warden, 1.0f, hero, nullptr); // the player hits a warden
    EXPECT_LT(registry.get<aige::FactionMember>(warden).disposition, 0);
    EXPECT_TRUE(aige::AreHostile(registry, warden, hero));
}

TEST_F(SystemsTest, SpawnersFillAndRespawn) {
    aige::Registry registry;
    auto spawner = registry.create();
    registry.emplace<aige::WorldTransform2D>(spawner, 5.5f, 5.5f);
    auto& s = registry.emplace<aige::Spawner>(spawner);
    s.encounter = "crypt";
    s.maxAlive = 2;
    s.respawnSeconds = 1.0f;
    std::mt19937 rng(2);
    int spawned = 0;
    auto spawn = [&](const std::string& prefab, float x, float y) -> std::optional<aige::Entity> {
        EXPECT_EQ(prefab, "prefabs/skeleton.prefab");
        ++spawned;
        return aige::CreateEnemy(registry, x, y, entt::null, "");
    };
    auto made = aige::UpdateSpawners(registry, 0.016f, spawn, {}, rng);
    EXPECT_EQ(made.size(), 2u);
    EXPECT_TRUE(aige::UpdateSpawners(registry, 0.5f, spawn, {}, rng).empty()); // full
    registry.destroy(made[0]);
    EXPECT_TRUE(aige::UpdateSpawners(registry, 0.5f, spawn, {}, rng).empty()); // waiting
    EXPECT_EQ(aige::UpdateSpawners(registry, 0.6f, spawn, {}, rng).size(), 1u);
    EXPECT_EQ(spawned, 3);
}

TEST_F(SystemsTest, KillsGiveExperienceAndDropLootInPlay) {
    aige::LuaVM lua;
    aige::Registry registry;
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    auto hero = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.emplace<aige::Progression>(hero);
    auto enemy = aige::CreateEnemy(registry, 3.2f, 2.5f, hero, "");
    registry.get<aige::Health>(enemy).current = 1.0f;
    registry.emplace<aige::Rewards>(enemy, 150.0f, 5, 5, "");
    registry.emplace_or_replace<aige::Facing>(hero, 1.0f, 0.0f);
    aige::GameSession session(registry, lua, aige::IsoParams{});
    std::set<aige::Key> held = {aige::Key::Space};
    bool levelUp = false;
    session.Advance(aige::GameSession::kFixedDt, [&](aige::Key k) { return held.contains(k); });
    for (const auto& e : session.DeliveredEvents()) levelUp |= e.name == "level_up";
    held.clear();
    for (int i = 0; i < 30; ++i) {
        session.Advance(aige::GameSession::kFixedDt, [&](aige::Key k) { return held.contains(k); });
        for (const auto& e : session.DeliveredEvents()) levelUp |= e.name == "level_up";
    }
    EXPECT_EQ(registry.get<aige::Progression>(hero).level, 2);
    EXPECT_TRUE(levelUp);
    int goldOnGround = 0;
    for (auto [e, pickup] : registry.view<aige::ItemPickup>().each()) goldOnGround += pickup.item == "gold" ? pickup.count : 0;
    // Dropped where it fell: picked up by walking there, or still lying.
    goldOnGround += registry.all_of<aige::Inventory>(hero) ? registry.get<aige::Inventory>(hero).gold : 0;
    EXPECT_EQ(goldOnGround, 5);
}
