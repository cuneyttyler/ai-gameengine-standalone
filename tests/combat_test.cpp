#include <gtest/gtest.h>

#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

namespace {

constexpr float kDt = 1.0f / 60.0f;

bool NoWalls(int, int) {
    return false;
}

// Runs combat for `seconds` in fixed steps, collecting events.
std::vector<aige::GameEvent> Simulate(aige::Registry& registry, float seconds) {
    std::vector<aige::GameEvent> all;
    for (float t = 0.0f; t < seconds; t += kDt) {
        std::vector<aige::GameEvent> events;
        aige::UpdateCombat(registry, &NoWalls, kDt, events);
        all.insert(all.end(), events.begin(), events.end());
    }
    return all;
}

// A target dummy with health and a hurtbox, on a team.
aige::Entity MakeDummy(aige::Registry& registry, float x, float y, aige::Team team, float hp = 10.0f) {
    auto entity = registry.create();
    registry.emplace<aige::WorldTransform2D>(entity, x, y);
    registry.emplace<aige::Faction>(entity, team);
    registry.emplace<aige::Health>(entity, hp, hp);
    registry.emplace<aige::Hurtbox>(entity);
    return entity;
}

aige::Entity MakeAttacker(aige::Registry& registry, float x, float y, aige::Team team, float damage = 3.0f) {
    auto entity = MakeDummy(registry, x, y, team);
    aige::MeleeAttack attack;
    attack.damage = damage;
    attack.knockback = 0.0f;
    registry.emplace<aige::MeleeAttack>(entity, attack);
    return entity;
}

size_t CountEvents(const std::vector<aige::GameEvent>& events, aige::GameEvent::Type type) {
    return std::count_if(events.begin(), events.end(), [type](const auto& e) { return e.type == type; });
}

} // namespace

TEST(CombatTest, MeleeHitDamagesEnemyOnceAndExpires) {
    aige::Registry registry;
    auto hero = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player, 3.0f);
    auto enemy = MakeDummy(registry, 2.7f, 2.0f, aige::Team::Enemy);

    auto hitbox = aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f);
    ASSERT_TRUE(hitbox.has_value());
    auto events = Simulate(registry, 0.5f); // longer than the hitbox lives

    EXPECT_FLOAT_EQ(registry.get<aige::Health>(enemy).current, 7.0f); // hit exactly once
    EXPECT_EQ(CountEvents(events, aige::GameEvent::Type::Hit), 1u);
    EXPECT_FALSE(registry.valid(*hitbox)); // expired and cleaned up
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(hero).current, 10.0f);
}

TEST(CombatTest, MissesOutOfReachAndSameTeam) {
    aige::Registry registry;
    auto hero = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player);
    auto ally = MakeDummy(registry, 2.7f, 2.0f, aige::Team::Player);
    auto farEnemy = MakeDummy(registry, 6.0f, 2.0f, aige::Team::Enemy);
    auto behindEnemy = MakeDummy(registry, 1.3f, 2.0f, aige::Team::Enemy);

    aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f);
    Simulate(registry, 0.5f);

    EXPECT_FLOAT_EQ(registry.get<aige::Health>(ally).current, 10.0f);        // no friendly fire
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(farEnemy).current, 10.0f);    // out of reach
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(behindEnemy).current, 10.0f); // swing went the other way
}

TEST(CombatTest, CooldownLimitsSwingRate) {
    aige::Registry registry;
    auto hero = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player);
    registry.get<aige::MeleeAttack>(hero).cooldown = 0.5f;

    EXPECT_TRUE(aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f).has_value());
    EXPECT_FALSE(aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f).has_value());
    Simulate(registry, 0.51f);
    EXPECT_TRUE(aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f).has_value());
}

TEST(CombatTest, StatsModifyDamageWithMinimumOne) {
    aige::Registry registry;
    auto hero = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player, 3.0f);
    registry.emplace<aige::Stats>(hero, 2.0f, 0.0f, 3.0f); // +2 attack
    auto armored = MakeDummy(registry, 2.7f, 2.0f, aige::Team::Enemy);
    registry.emplace<aige::Stats>(armored, 0.0f, 4.0f, 3.0f); // 4 defense

    aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f);
    Simulate(registry, 0.3f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(armored).current, 9.0f); // 3 + 2 - 4 = 1

    registry.get<aige::Stats>(armored).defense = 100.0f;
    Simulate(registry, 1.0f); // cooldown + i-frames
    aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f);
    Simulate(registry, 0.3f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(armored).current, 8.0f); // never less than 1
}

TEST(CombatTest, InvulnerabilityFramesBlockRepeatHits) {
    aige::Registry registry;
    auto a = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player, 1.0f);
    auto b = MakeAttacker(registry, 4.0f, 2.0f, aige::Team::Player, 1.0f);
    auto enemy = MakeDummy(registry, 3.0f, 2.0f, aige::Team::Enemy);
    registry.get<aige::Hurtbox>(enemy).invulnerableTime = 0.4f;

    // Two attackers hit in the same instant: only the first lands.
    aige::TryMeleeAttack(registry, a, 1.0f, 0.0f);
    aige::TryMeleeAttack(registry, b, -1.0f, 0.0f);
    Simulate(registry, 0.2f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(enemy).current, 9.0f);
}

TEST(CombatTest, KnockbackPushesAwayButNotThroughWalls) {
    aige::Registry registry;
    auto hero = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player);
    registry.get<aige::MeleeAttack>(hero).knockback = 1.0f;
    auto enemy = MakeDummy(registry, 2.7f, 2.0f, aige::Team::Enemy);

    aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f);
    std::vector<aige::GameEvent> events;
    auto wallAtX3 = [](int gx, int) { return gx >= 3; };
    aige::UpdateCombat(registry, wallAtX3, kDt, events);
    float x = registry.get<aige::WorldTransform2D>(enemy).x;
    EXPECT_FLOAT_EQ(x, 2.7f); // the push would end inside the wall: rejected

    aige::Registry open;
    auto hero2 = MakeAttacker(open, 2.0f, 2.0f, aige::Team::Player);
    open.get<aige::MeleeAttack>(hero2).knockback = 1.0f;
    auto enemy2 = MakeDummy(open, 2.7f, 2.0f, aige::Team::Enemy);
    aige::TryMeleeAttack(open, hero2, 1.0f, 0.0f);
    aige::UpdateCombat(open, &NoWalls, kDt, events);
    EXPECT_NEAR(open.get<aige::WorldTransform2D>(enemy2).x, 3.7f, 1e-4f);
}

TEST(CombatTest, DeathRemovesEnemiesButKeepsPlayer) {
    aige::Registry registry;
    auto player = aige::CreatePlayer(registry, 2.0f, 2.0f);
    auto enemy = MakeDummy(registry, 3.0f, 2.0f, aige::Team::Enemy, 1.0f);

    registry.get<aige::Health>(enemy).current = 0.0f;
    registry.get<aige::Health>(player).current = 0.0f;
    auto events = Simulate(registry, kDt);

    EXPECT_EQ(CountEvents(events, aige::GameEvent::Type::Death), 2u);
    EXPECT_FALSE(registry.valid(enemy));
    ASSERT_TRUE(registry.valid(player));
    EXPECT_TRUE(registry.all_of<aige::Dead>(player));
    EXPECT_FALSE(aige::TryMeleeAttack(registry, player, 1.0f, 0.0f).has_value()); // the dead don't swing
}

TEST(CombatTest, BurnTicksThenExpires) {
    aige::Registry registry;
    auto enemy = MakeDummy(registry, 2.0f, 2.0f, aige::Team::Enemy, 20.0f);
    auto effect = aige::ApplyStatus(registry, enemy, aige::EffectKind::Burn, 2.0f, 1.0f, 0.5f);

    Simulate(registry, 1.01f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(enemy).current, 18.0f); // ticks at 0.5s and 1.0s
    Simulate(registry, 1.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(enemy).current, 16.0f); // 4 ticks over 2s, then gone
    EXPECT_FALSE(registry.valid(effect));
}

TEST(CombatTest, ReapplyingRefreshesInsteadOfStacking) {
    aige::Registry registry;
    auto enemy = MakeDummy(registry, 2.0f, 2.0f, aige::Team::Enemy);
    auto first = aige::ApplyStatus(registry, enemy, aige::EffectKind::Slow, 1.0f, 0.3f);
    Simulate(registry, 0.5f);
    auto second = aige::ApplyStatus(registry, enemy, aige::EffectKind::Slow, 1.0f, 0.5f);

    EXPECT_EQ(first, second);
    EXPECT_EQ(registry.view<aige::StatusEffect>().size(), 1u);
    EXPECT_FLOAT_EQ(registry.get<aige::StatusEffect>(first).remaining, 1.0f);
    EXPECT_FLOAT_EQ(aige::SpeedMultiplier(registry, enemy), 0.5f);
}

TEST(CombatTest, PoisonCantKillAndEffectsDieWithTarget) {
    aige::Registry registry;
    auto enemy = MakeDummy(registry, 2.0f, 2.0f, aige::Team::Enemy, 3.0f);
    aige::ApplyStatus(registry, enemy, aige::EffectKind::Poison, 10.0f, 5.0f, 0.25f);
    Simulate(registry, 2.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(enemy).current, 1.0f);

    auto burn = aige::ApplyStatus(registry, enemy, aige::EffectKind::Burn, 10.0f, 5.0f, 0.25f);
    Simulate(registry, 0.5f); // burn kills; the enemy is removed, and its effects with it
    EXPECT_FALSE(registry.valid(enemy));
    Simulate(registry, kDt);
    EXPECT_FALSE(registry.valid(burn));
    EXPECT_EQ(registry.view<aige::StatusEffect>().size(), 0u);
}

TEST(CombatTest, OnHitEffectFromMeleeAttack) {
    aige::Registry registry;
    auto hero = MakeAttacker(registry, 2.0f, 2.0f, aige::Team::Player, 1.0f);
    auto& attack = registry.get<aige::MeleeAttack>(hero);
    attack.effect = aige::EffectKind::Slow;
    attack.effectDuration = 2.0f;
    attack.effectMagnitude = 0.4f;
    auto enemy = MakeDummy(registry, 2.7f, 2.0f, aige::Team::Enemy);

    aige::TryMeleeAttack(registry, hero, 1.0f, 0.0f);
    Simulate(registry, 0.2f);
    EXPECT_FLOAT_EQ(aige::SpeedMultiplier(registry, enemy), 0.6f);
}

TEST(CombatTest, HitFlashRestoresSpriteTint) {
    aige::Registry registry;
    auto enemy = MakeDummy(registry, 2.0f, 2.0f, aige::Team::Enemy);
    auto& sprite = registry.emplace<aige::Sprite>(enemy);
    sprite.r = 0.2f;
    sprite.g = 0.4f;
    sprite.b = 0.6f;

    aige::DealDamage(registry, enemy, 1.0f, entt::null, nullptr);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(enemy).g, 0.3f); // flashing red
    Simulate(registry, 0.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(enemy).r, 0.2f);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(enemy).g, 0.4f);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(enemy).b, 0.6f);
}

TEST(CombatTest, PickupHealsPlayerOnlyWhenNeeded) {
    aige::Registry registry;
    auto player = aige::CreatePlayer(registry, 2.0f, 2.0f);
    auto potion = aige::CreateHealthPickup(registry, 2.2f, 2.0f, 6.0f);

    auto events = Simulate(registry, kDt);
    EXPECT_TRUE(registry.valid(potion)); // full health: left on the ground
    EXPECT_EQ(CountEvents(events, aige::GameEvent::Type::Pickup), 0u);

    registry.get<aige::Health>(player).current = 17.0f;
    events = Simulate(registry, kDt);
    EXPECT_FALSE(registry.valid(potion));
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(player).current, 20.0f); // clamped to max
    EXPECT_EQ(CountEvents(events, aige::GameEvent::Type::Pickup), 1u);
}

TEST(CombatTest, SlowAffectsPlayerMovement) {
    aige::Registry registry;
    for (int gx = 0; gx < 8; ++gx) {
        for (int gy = 0; gy < 8; ++gy) {
            aige::CreateTile(registry, gx, gy, aige::TileKind::Grass);
        }
    }
    auto player = aige::CreatePlayer(registry, 3.5f, 3.5f);
    aige::ApplyStatus(registry, player, aige::EffectKind::Slow, 10.0f, 0.5f);
    auto holdD = [](aige::Key key) { return key == aige::Key::D; };
    aige::UpdatePlayer(registry, player, holdD, &NoWalls, 1.0f);
    const auto& velocity = registry.get<aige::Velocity2D>(player);
    EXPECT_NEAR(std::hypot(velocity.x, velocity.y), 2.1f, 1e-4f); // 4.2 cells/s halved
}

TEST(CombatTest, CombatComponentsRoundTripThroughScenes) {
    aige::Registry registry;
    auto enemy = aige::CreateEnemy(registry, 4.5f, 4.5f, entt::null);
    registry.get<aige::MeleeAttack>(enemy).effect = aige::EffectKind::Burn;
    aige::CreateHealthPickup(registry, 1.5f, 1.5f, 9.0f);

    std::string text = aige::SerializeScene(registry);
    EXPECT_NE(text.find("effect=Burn"), std::string::npos);
    aige::Registry loaded;
    aige::DeserializeScene(loaded, text);
    auto loadedEnemy = *loaded.view<aige::MeleeAttack>().begin();
    EXPECT_EQ(loaded.get<aige::MeleeAttack>(loadedEnemy).effect, aige::EffectKind::Burn);
    EXPECT_EQ(loaded.get<aige::Faction>(loadedEnemy).team, aige::Team::Enemy);
    EXPECT_FLOAT_EQ(loaded.get<aige::Pickup>(*loaded.view<aige::Pickup>().begin()).amount, 9.0f);
}

// End to end through Lua: an enemy whose script swings at the player when
// close hurts the player via the shared game loop.
TEST(CombatTest, ScriptedEnemyAttacksPlayer) {
    auto root = std::filesystem::temp_directory_path() / "aige_combat_script_test";
    std::filesystem::create_directories(root / "scripts");
    std::ofstream(root / "scripts/brute.lua") << "function update(dt)\n"
                                                 "  local target, distance = find_nearest('Player')\n"
                                                 "  if target and distance < 1.1 then attack_toward(target) end\n"
                                                 "end\n";

    aige::LuaVM lua;
    aige::Registry registry;
    for (int gx = 0; gx < 8; ++gx) {
        for (int gy = 0; gy < 8; ++gy) {
            aige::CreateTile(registry, gx, gy, aige::TileKind::Grass);
        }
    }
    auto player = aige::CreatePlayer(registry, 3.5f, 3.5f);
    aige::CreateEnemy(registry, 5.5f, 3.5f, player, "scripts/brute.lua");

    aige::GameSession session(registry, lua, aige::IsoParams{}, root);
    auto noKeys = [](aige::Key) { return false; };
    bool hit = false;
    for (int i = 0; i < 180 && !hit; ++i) { // up to 3 seconds: chase, then swing
        session.Advance(aige::GameSession::kFixedDt, noKeys);
        for (const auto& event : session.Events()) {
            hit |= event.type == aige::GameEvent::Type::Hit && event.entity == player;
        }
    }
    EXPECT_TRUE(hit);
    EXPECT_LT(registry.get<aige::Health>(player).current, 20.0f);
    std::filesystem::remove_all(root);
}
