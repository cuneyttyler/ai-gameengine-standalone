#include <gtest/gtest.h>

#include "assets/ComponentTable.h"
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <cmath>
#include <set>

namespace {

const aige::FieldDesc& FieldOf(const std::string& component, const std::string& field) {
    const aige::ComponentDesc* desc = aige::FindComponent(component);
    EXPECT_NE(desc, nullptr) << component;
    const aige::FieldDesc* f = desc->Field(field);
    EXPECT_NE(f, nullptr) << component << "." << field;
    return *f;
}

bool NoWalls(int, int) {
    return false;
}

} // namespace

TEST(ComponentTableTest, NamesAreUniqueAndFindable) {
    std::set<std::string> names;
    for (const auto& component : aige::Components()) {
        EXPECT_TRUE(names.insert(component.name).second) << "duplicate " << component.name;
        EXPECT_EQ(aige::FindComponent(component.name), &component);
        for (const auto& dependency : component.dependencies) {
            EXPECT_NE(aige::FindComponent(dependency), nullptr) << component.name << " -> " << dependency;
        }
    }
    EXPECT_EQ(aige::FindComponent("NoSuchThing"), nullptr);
}

TEST(ComponentTableTest, EveryComponentAddsAndRemoves) {
    for (const auto& component : aige::Components()) {
        aige::Registry registry;
        auto entity = registry.create();
        ASSERT_TRUE(aige::AddComponent(registry, entity, component.name)) << component.name;
        EXPECT_TRUE(component.has(registry, entity));
        EXPECT_FALSE(aige::AddComponent(registry, entity, component.name)); // already present
        // Every field reads back without throwing.
        for (const auto& field : component.fields) {
            field.get(registry, entity);
        }
        EXPECT_FALSE(aige::RemoveComponent(registry, entity, component.name).has_value()) << component.name;
        EXPECT_FALSE(component.has(registry, entity));
    }
}

TEST(ComponentTableTest, AddingPullsInDependenciesAndRemovalRespectsThem) {
    aige::Registry registry;
    auto entity = registry.create();
    ASSERT_TRUE(aige::AddComponent(registry, entity, "Collider2D"));
    EXPECT_TRUE(registry.all_of<aige::WorldTransform2D>(entity));
    EXPECT_TRUE(registry.all_of<aige::ScreenTransform2D>(entity)); // transitively, via WorldTransform2D

    auto refusal = aige::RemoveComponent(registry, entity, "WorldTransform2D");
    ASSERT_TRUE(refusal.has_value());
    EXPECT_NE(refusal->find("Collider2D"), std::string::npos);
    EXPECT_TRUE(registry.all_of<aige::WorldTransform2D>(entity));

    EXPECT_FALSE(aige::RemoveComponent(registry, entity, "Collider2D").has_value());
    EXPECT_FALSE(aige::RemoveComponent(registry, entity, "WorldTransform2D").has_value());
}

TEST(ComponentTableTest, FieldsOfEveryTypeSetAndGet) {
    aige::Registry registry;
    auto entity = registry.create();
    auto target = registry.create();
    for (const char* name : {"Name", "Faction", "Health", "Follow", "Sprite", "LuaScript"}) {
        aige::AddComponent(registry, entity, name);
    }

    FieldOf("Name", "value").set(registry, entity, std::string("Skeleton"));
    FieldOf("Faction", "team").set(registry, entity, 2);
    FieldOf("Health", "max").set(registry, entity, 25.5f);
    FieldOf("Follow", "target").set(registry, entity, target);
    FieldOf("Sprite", "texture").set(registry, entity, std::string("assets/textures/skeleton.png"));
    FieldOf("LuaScript", "path").set(registry, entity, std::string("scripts/enemy.lua"));

    EXPECT_EQ(registry.get<aige::Name>(entity).value, "Skeleton");
    EXPECT_EQ(registry.get<aige::Faction>(entity).team, aige::Team::Enemy);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(entity).max, 25.5f);
    EXPECT_EQ(registry.get<aige::Follow>(entity).target, target);
    EXPECT_EQ(registry.get<aige::Sprite>(entity).texture, "assets/textures/skeleton.png");
    EXPECT_EQ(registry.get<aige::LuaScript>(entity).path, "scripts/enemy.lua");

    EXPECT_EQ(std::get<int>(FieldOf("Faction", "team").get(registry, entity)), 2);
}

TEST(ComponentTableTest, ChangingScriptPathForcesReload) {
    aige::Registry registry;
    auto entity = registry.create();
    auto& script = registry.emplace<aige::LuaScript>(entity);
    script.path = "scripts/a.lua";
    script.loaded = true;

    FieldOf("LuaScript", "path").set(registry, entity, std::string("scripts/b.lua"));
    EXPECT_EQ(registry.get<aige::LuaScript>(entity).path, "scripts/b.lua");
    EXPECT_FALSE(registry.get<aige::LuaScript>(entity).loaded);
}

TEST(ComponentTableTest, ScreenTransformIsDerivedForPositionedEntities) {
    aige::Registry registry;
    auto character = aige::CreateNpc(registry, 1.5f, 1.5f);
    auto decoration = registry.create();
    registry.emplace<aige::ScreenTransform2D>(decoration);
    const aige::ComponentDesc* screen = aige::FindComponent("ScreenTransform2D");
    EXPECT_TRUE(screen->derived(registry, character));
    EXPECT_FALSE(screen->derived(registry, decoration));
}

TEST(ComponentTableTest, SceneRoundTripPreservesAllFieldTypesAndReferences) {
    aige::Registry registry;
    // The follower is created before its target, so the reference in the
    // file points forward.
    auto enemy = aige::CreateNpc(registry, 2.5f, 2.5f);
    auto player = aige::CreatePlayer(registry, 5.5f, 5.5f);
    registry.get<aige::Name>(enemy).value = "Skeleton \"Bones\" Jr";
    registry.emplace_or_replace<aige::Faction>(enemy, aige::Team::Enemy);
    registry.emplace<aige::Health>(enemy, 7.0f, 12.0f);
    registry.emplace<aige::Follow>(enemy, player, 2.5f, 0.75f, 5.0f);
    auto dangling = registry.create();
    registry.emplace<aige::Follow>(dangling); // no target

    std::string text = aige::SerializeScene(registry);
    EXPECT_NE(text.find("team=Enemy"), std::string::npos); // enums by name, readable
    EXPECT_NE(text.find("target=#1"), std::string::npos);
    EXPECT_NE(text.find("target=none"), std::string::npos);

    aige::Registry loaded;
    aige::DeserializeScene(loaded, text);
    auto loadedEnemy = *aige::EntitiesInOrder(loaded).begin();
    auto loadedPlayer = aige::EntitiesInOrder(loaded)[1];
    EXPECT_EQ(loaded.get<aige::Name>(loadedEnemy).value, "Skeleton \"Bones\" Jr");
    EXPECT_EQ(loaded.get<aige::Faction>(loadedEnemy).team, aige::Team::Enemy);
    EXPECT_FLOAT_EQ(loaded.get<aige::Health>(loadedEnemy).current, 7.0f);
    EXPECT_FLOAT_EQ(loaded.get<aige::Health>(loadedEnemy).max, 12.0f);
    const auto& follow = loaded.get<aige::Follow>(loadedEnemy);
    EXPECT_EQ(follow.target, loadedPlayer);
    EXPECT_FLOAT_EQ(follow.speed, 2.5f);
    EXPECT_FLOAT_EQ(follow.stopDistance, 0.75f);
    EXPECT_FLOAT_EQ(follow.range, 5.0f);
    EXPECT_TRUE(loaded.get<aige::Follow>(aige::EntitiesInOrder(loaded)[2]).target == entt::null);
}

TEST(ComponentTableTest, LoadToleratesUnknownAndMalformedFields) {
    aige::Registry loaded;
    aige::DeserializeScene(loaded,
                           "entity\n"
                           "  Health current=abc max=9 color=red\n"
                           "  Faction team=Pirates\n"
                           "  Follow target=#99\n"
                           "  FutureComponent x=1\n");
    auto entity = *loaded.view<aige::Health>().begin();
    EXPECT_FLOAT_EQ(loaded.get<aige::Health>(entity).current, 10.0f); // malformed keeps default
    EXPECT_FLOAT_EQ(loaded.get<aige::Health>(entity).max, 9.0f);
    EXPECT_EQ(loaded.get<aige::Faction>(entity).team, aige::Team::Neutral);
    EXPECT_TRUE(loaded.get<aige::Follow>(entity).target == entt::null); // out-of-range reference
}

TEST(FollowTest, MovesTowardTargetAndStopsAtDistance) {
    aige::Registry registry;
    auto target = aige::CreatePlayer(registry, 5.0f, 2.0f);
    auto follower = aige::CreateNpc(registry, 1.0f, 2.0f);
    registry.emplace<aige::Follow>(follower, target, 2.0f, 1.0f, 0.0f);

    aige::UpdateFollowers(registry, &NoWalls, 0.5f); // one cell per half second
    EXPECT_NEAR(registry.get<aige::WorldTransform2D>(follower).x, 2.0f, 1e-4f);

    for (int i = 0; i < 20; ++i) {
        aige::UpdateFollowers(registry, &NoWalls, 0.5f);
    }
    EXPECT_NEAR(registry.get<aige::WorldTransform2D>(follower).x, 4.0f, 1e-4f); // stopped 1 cell short
}

TEST(FollowTest, IgnoresTargetsOutOfRangeAndWalls) {
    aige::Registry registry;
    auto target = aige::CreatePlayer(registry, 9.0f, 1.5f);
    auto follower = aige::CreateNpc(registry, 1.5f, 1.5f);
    registry.emplace<aige::Follow>(follower, target, 2.0f, 0.5f, 3.0f);

    aige::UpdateFollowers(registry, &NoWalls, 0.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(follower).x, 1.5f); // 7.5 away, range 3

    registry.get<aige::Follow>(follower).range = 0.0f; // always follow
    auto wallAtX3 = [](int gx, int) { return gx == 3; };
    for (int i = 0; i < 20; ++i) {
        aige::UpdateFollowers(registry, wallAtX3, 0.1f);
    }
    EXPECT_LE(registry.get<aige::WorldTransform2D>(follower).x + 0.3f, 3.0f + 1e-4f); // blocked at the wall
}
