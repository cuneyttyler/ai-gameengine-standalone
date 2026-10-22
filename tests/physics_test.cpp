#include <gtest/gtest.h>

#include "gameplay/Archetypes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Physics.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"

#include <algorithm>
#include <cmath>

// development_plan.md Phase 20: layers and masks, shapes, triggers, queries, swept
// projectiles, the grid-step controller, Box2D bodies and joints (physics.md).
namespace {

aige::Entity Box(aige::Registry& registry, float x, float y, float half = 0.4f) {
    auto e = registry.create();
    registry.emplace<aige::WorldTransform2D>(e, x, y);
    auto& c = registry.emplace<aige::Collider2D>(e);
    c.halfWidth = c.halfHeight = half;
    return e;
}

} // namespace

TEST(PhysicsTest, LayersShapesAndQueries) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto foe = aige::CreateEnemy(registry, 5.5f, 1.5f, hero, "");
    auto crate = Box(registry, 3.5f, 1.5f);
    registry.get<aige::Collider2D>(crate).layer = "world";
    EXPECT_EQ(aige::LayerOf(registry, hero), "player");
    EXPECT_EQ(aige::LayerOf(registry, foe), "enemy");
    EXPECT_EQ(aige::LayerOf(registry, crate), "world");
    EXPECT_TRUE(aige::MaskAllows("", "anything"));
    EXPECT_TRUE(aige::MaskAllows("enemy, world", "world"));
    EXPECT_FALSE(aige::MaskAllows("enemy", "player"));
    // Circles and boxes.
    aige::ColliderShape a{true, 0, 0, 0, 0, 1.0f}, b{false, 1.5f, 0, 0.6f, 0.6f, 0};
    EXPECT_TRUE(aige::ShapesOverlap(a, b));
    b.x = 2.0f;
    EXPECT_FALSE(aige::ShapesOverlap(a, b));
    // A ray hits the crate first; masked to enemies it passes the crate.
    auto solid = [](int x, int) { return x >= 9; };
    auto hit = aige::Raycast(registry, solid, 1.5f, 1.5f, 8.0f, 1.5f, {}, hero);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, crate);
    hit = aige::Raycast(registry, solid, 1.5f, 1.5f, 8.0f, 1.5f, "enemy", hero);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, foe);
    hit = aige::Raycast(registry, solid, 6.5f, 1.5f, 12.0f, 1.5f, "nothing");
    ASSERT_TRUE(hit);
    EXPECT_TRUE(hit->entity == entt::null); // the wall
    EXPECT_NEAR(hit->x, 9.0f, 0.06f);
    // Overlap and line of sight.
    const auto around = aige::Overlap(registry, 3.0f, 1.5f, 1.0f);
    EXPECT_NE(std::find(around.begin(), around.end(), crate), around.end());
    EXPECT_TRUE(aige::LineOfSight(solid, 1.5f, 1.5f, 8.5f, 1.5f));
    EXPECT_FALSE(aige::LineOfSight(solid, 1.5f, 1.5f, 10.5f, 1.5f));
}

TEST(PhysicsTest, TriggersSeeWhoComesAndGoes) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto foe = aige::CreateEnemy(registry, 8.5f, 8.5f, hero, "");
    auto gate = registry.create();
    registry.emplace<aige::WorldTransform2D>(gate, 4.5f, 1.5f);
    registry.emplace<aige::Trigger>(gate, "gate", "player", false, 1.0f);
    EXPECT_TRUE(aige::UpdateTriggers(registry).empty());
    registry.get<aige::WorldTransform2D>(hero) = {4.2f, 1.5f};
    auto events = aige::UpdateTriggers(registry);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].enter);
    EXPECT_EQ(events[0].name, "gate");
    EXPECT_TRUE(aige::UpdateTriggers(registry).empty()); // still inside: nothing new
    registry.get<aige::WorldTransform2D>(foe) = {4.5f, 1.8f}; // an enemy isn't on its mask
    EXPECT_TRUE(aige::UpdateTriggers(registry).empty());
    registry.get<aige::WorldTransform2D>(hero) = {1.5f, 1.5f};
    events = aige::UpdateTriggers(registry);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].enter);
}

TEST(PhysicsTest, ProjectilesDontTunnelAndRespectMasks) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto foe = aige::CreateEnemy(registry, 4.5f, 1.5f, hero, "");
    registry.get<aige::Health>(foe) = {10.0f, 10.0f};
    // A very fast shot that would jump past the enemy in one step.
    auto shot = registry.create();
    registry.emplace<aige::WorldTransform2D>(shot, 2.0f, 1.5f);
    auto& p = registry.emplace<aige::Projectile>(shot);
    p.owner = hero;
    p.team = aige::Team::Player;
    p.damage = 3.0f;
    p.vx = 120.0f;
    p.remaining = 1.0f;
    std::vector<aige::GameEvent> events;
    auto solid = [](int, int) { return false; };
    aige::UpdateCombat(registry, solid, 1.0f / 60.0f, events);
    EXPECT_LT(registry.get<aige::Health>(foe).current, 10.0f);
    // A shot masked to "world" passes through characters.
    registry.get<aige::Health>(foe) = {10.0f, 10.0f};
    registry.get<aige::Hurtbox>(foe).invulnerableRemaining = 0.0f;
    auto ghost = registry.create();
    registry.emplace<aige::WorldTransform2D>(ghost, 4.0f, 1.5f);
    auto& g = registry.emplace<aige::Projectile>(ghost);
    g.owner = hero;
    g.team = aige::Team::Player;
    g.damage = 3.0f;
    g.vx = 6.0f;
    g.remaining = 1.0f;
    g.mask = "world";
    for (int i = 0; i < 10; ++i) aige::UpdateCombat(registry, solid, 1.0f / 60.0f, events);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(foe).current, 10.0f);
}

TEST(PhysicsTest, GridStepMovesCellByCell) {
    aige::Registry registry;
    auto hero = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.emplace<aige::CharacterController2D>(hero, 1, 0.2f, false);
    auto solid = [](int x, int) { return x >= 5; };
    EXPECT_TRUE(aige::UpdateGridStep(registry, hero, 1.0f, 0.0f, solid, 0.05f)); // starts a step
    for (int i = 0; i < 10; ++i) aige::UpdateGridStep(registry, hero, 0.0f, 0.0f, solid, 0.05f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(hero).x, 3.5f); // exactly one cell
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(hero).y, 2.5f);
    // Diagonal input steps along one axis when diagonals are off.
    aige::UpdateGridStep(registry, hero, 1.0f, 0.4f, solid, 0.05f);
    for (int i = 0; i < 10; ++i) aige::UpdateGridStep(registry, hero, 0.0f, 0.0f, solid, 0.05f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(hero).x, 4.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(hero).y, 2.5f);
    // A wall: it turns but doesn't move.
    EXPECT_FALSE(aige::UpdateGridStep(registry, hero, 1.0f, 0.0f, solid, 0.05f));
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(hero).x, 4.5f);
}

TEST(PhysicsTest, AStackOfCratesTopples) {
    auto saved = aige::ActiveSettings();
    aige::ActiveSettings().physics.gravityY = 10.0f; // a side-on test: +y is down
    aige::Registry registry;
    auto solid = [](int, int y) { return y >= 10; }; // the floor
    std::vector<aige::Entity> crates;
    for (int i = 0; i < 4; ++i) {
        auto c = Box(registry, 5.0f + i * 0.3f, 9.5f - i * 0.82f); // each further right: the top two overhang
        auto& body = registry.emplace<aige::RigidBody2D>(c);
        body.linearDamping = 0.0f;
        body.angularDamping = 0.0f;
        registry.emplace<aige::Sprite>(c);
        crates.push_back(c);
    }
    aige::PhysicsWorld physics;
    for (int i = 0; i < 240; ++i) physics.Step(registry, solid, 1.0f / 60.0f);
    EXPECT_TRUE(physics.Active());
    EXPECT_EQ(physics.BodyCount(), 4u);
    const auto& top = registry.get<aige::WorldTransform2D>(crates.back());
    EXPECT_GT(top.y, 7.5f);                               // it came down
    EXPECT_GT(std::abs(registry.get<aige::Sprite>(crates.back()).angle), 5.0f); // and turned over
    // Nothing bodies: no world kept.
    aige::Registry empty;
    aige::PhysicsWorld idle;
    idle.Step(empty, solid, 1.0f / 60.0f);
    EXPECT_FALSE(idle.Active());
    aige::ActiveSettings() = saved;
}

TEST(PhysicsTest, CharactersPushCratesAndJointsHold) {
    aige::Registry registry;
    auto solid = [](int, int) { return false; };
    auto hero = aige::CreatePlayer(registry, 1.0f, 1.0f);
    auto crate = Box(registry, 2.0f, 1.0f);
    registry.emplace<aige::RigidBody2D>(crate);
    aige::PhysicsWorld physics;
    physics.Step(registry, solid, 1.0f / 60.0f);
    // The player walks into the crate: it moves.
    for (int i = 0; i < 60; ++i) {
        registry.get<aige::WorldTransform2D>(hero).x += 2.0f / 60.0f;
        physics.Step(registry, solid, 1.0f / 60.0f);
    }
    EXPECT_GT(registry.get<aige::WorldTransform2D>(crate).x, 2.4f);
    // A distance joint keeps two bodies apart by its length.
    auto a = Box(registry, 8.0f, 8.0f, 0.2f);
    auto b = Box(registry, 10.0f, 8.0f, 0.2f);
    registry.emplace<aige::RigidBody2D>(a).type = 0; // static anchor
    registry.emplace<aige::RigidBody2D>(b);
    registry.emplace<aige::Joint2D>(b, 1, a, 0.0f, 0.0f, 2.0f, false);
    physics.ApplyImpulse(b, 0.0f, 0.0f);
    for (int i = 0; i < 30; ++i) physics.Step(registry, solid, 1.0f / 60.0f);
    physics.ApplyImpulse(b, 0.0f, 5.0f);
    for (int i = 0; i < 60; ++i) physics.Step(registry, solid, 1.0f / 60.0f);
    const auto& pb = registry.get<aige::WorldTransform2D>(b);
    EXPECT_NEAR(std::hypot(pb.x - 8.0f, pb.y - 8.0f), 2.0f, 0.15f);
}

TEST(PhysicsTest, DebugShapes) {
    aige::Registry registry;
    Box(registry, 1, 1);
    auto t = registry.create();
    registry.emplace<aige::WorldTransform2D>(t, 2.0f, 2.0f);
    registry.emplace<aige::Trigger>(t, "area", "", false, 1.5f);
    const auto shapes = aige::PhysicsDebugShapes(registry);
    ASSERT_EQ(shapes.size(), 2u);
    const auto trigger = std::find_if(shapes.begin(), shapes.end(), [](const aige::DebugShape& s) { return s.kind == 1; });
    ASSERT_NE(trigger, shapes.end());
    EXPECT_TRUE(trigger->circle);
    EXPECT_FLOAT_EQ(trigger->radius, 1.5f);
}
