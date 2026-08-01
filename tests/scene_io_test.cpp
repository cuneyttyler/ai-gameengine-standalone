#include <gtest/gtest.h>

#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <entt/entt.hpp>
#include <filesystem>

namespace {

// EnTT's all-entities view iterates in reverse creation order, so entities
// created in the same sequence across two independently-populated registries
// don't line up positionally — match by distinguishing component combination
// instead, which is what actually matters here.
void CompareCommon(aige::Registry& original, aige::Entity origE, aige::Registry& loaded, aige::Entity loadE) {
    EXPECT_EQ(original.all_of<aige::WorldTransform2D>(origE), loaded.all_of<aige::WorldTransform2D>(loadE));
    if (original.all_of<aige::WorldTransform2D>(origE)) {
        EXPECT_FLOAT_EQ(original.get<aige::WorldTransform2D>(origE).x, loaded.get<aige::WorldTransform2D>(loadE).x);
        EXPECT_FLOAT_EQ(original.get<aige::WorldTransform2D>(origE).y, loaded.get<aige::WorldTransform2D>(loadE).y);
    }

    ASSERT_TRUE(original.all_of<aige::ScreenTransform2D>(origE));
    ASSERT_TRUE(loaded.all_of<aige::ScreenTransform2D>(loadE));
    EXPECT_FLOAT_EQ(original.get<aige::ScreenTransform2D>(origE).x, loaded.get<aige::ScreenTransform2D>(loadE).x);
    EXPECT_FLOAT_EQ(original.get<aige::ScreenTransform2D>(origE).y, loaded.get<aige::ScreenTransform2D>(loadE).y);
    EXPECT_FLOAT_EQ(original.get<aige::ScreenTransform2D>(origE).depth,
                     loaded.get<aige::ScreenTransform2D>(loadE).depth);

    ASSERT_TRUE(original.all_of<aige::Sprite>(origE));
    ASSERT_TRUE(loaded.all_of<aige::Sprite>(loadE));
    const auto& origSprite = original.get<aige::Sprite>(origE);
    const auto& loadSprite = loaded.get<aige::Sprite>(loadE);
    EXPECT_FLOAT_EQ(origSprite.width, loadSprite.width);
    EXPECT_FLOAT_EQ(origSprite.height, loadSprite.height);
    EXPECT_FLOAT_EQ(origSprite.u0, loadSprite.u0);
    EXPECT_FLOAT_EQ(origSprite.v0, loadSprite.v0);
    EXPECT_FLOAT_EQ(origSprite.u1, loadSprite.u1);
    EXPECT_FLOAT_EQ(origSprite.v1, loadSprite.v1);
    EXPECT_FLOAT_EQ(origSprite.r, loadSprite.r);
    EXPECT_FLOAT_EQ(origSprite.g, loadSprite.g);
    EXPECT_FLOAT_EQ(origSprite.b, loadSprite.b);
    EXPECT_FLOAT_EQ(origSprite.a, loadSprite.a);
}

} // namespace

TEST(SceneIOTest, RoundTripsAllKnownComponents) {
    aige::Registry original;

    auto tile = original.create();
    original.emplace<aige::ScreenTransform2D>(tile, 640.5f, 200.25f, 3.0f);
    original.emplace<aige::Sprite>(tile, 64.0f, 32.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);

    auto player = original.create();
    original.emplace<aige::WorldTransform2D>(player, 3.5f, 3.5f);
    original.emplace<aige::Velocity2D>(player, 1.25f, -2.5f);
    original.emplace<aige::Collider2D>(player, 0.3f, 0.35f);
    original.emplace<aige::ScreenTransform2D>(player, 700.0f, 300.0f, 7.5f);
    original.emplace<aige::Sprite>(player, 32.0f, 48.0f, 0.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    auto scriptedNpc = original.create();
    original.emplace<aige::WorldTransform2D>(scriptedNpc, 5.5f, 5.5f);
    original.emplace<aige::ScreenTransform2D>(scriptedNpc, 800.0f, 400.0f, 11.0f);
    original.emplace<aige::Sprite>(scriptedNpc, 32.0f, 48.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    original.emplace<aige::LuaScript>(scriptedNpc).path = "C:/some path/with spaces/script.lua";

    auto scenePath = std::filesystem::temp_directory_path() / "aige_test_scene_roundtrip.aige";
    aige::SaveScene(original, scenePath.string());

    aige::Registry loaded;
    ASSERT_TRUE(aige::LoadScene(loaded, scenePath.string()));

    // Entity count round-trips even though creation order doesn't.
    size_t originalCount = 0;
    for (auto e : original.view<entt::entity>()) {
        (void)e;
        ++originalCount;
    }
    size_t loadedCount = 0;
    for (auto e : loaded.view<entt::entity>()) {
        (void)e;
        ++loadedCount;
    }
    ASSERT_EQ(originalCount, loadedCount);
    ASSERT_EQ(originalCount, 3u);

    // Tile: the only entity without a WorldTransform2D.
    aige::Entity origTile{};
    for (auto e : original.view<aige::ScreenTransform2D, aige::Sprite>()) {
        if (!original.all_of<aige::WorldTransform2D>(e)) {
            origTile = e;
        }
    }
    aige::Entity loadTile{};
    for (auto e : loaded.view<aige::ScreenTransform2D, aige::Sprite>()) {
        if (!loaded.all_of<aige::WorldTransform2D>(e)) {
            loadTile = e;
        }
    }
    CompareCommon(original, origTile, loaded, loadTile);

    // Player: the only entity with Velocity2D + Collider2D.
    aige::Entity origPlayer = *original.view<aige::Velocity2D, aige::Collider2D>().begin();
    aige::Entity loadPlayer = *loaded.view<aige::Velocity2D, aige::Collider2D>().begin();
    CompareCommon(original, origPlayer, loaded, loadPlayer);
    EXPECT_FLOAT_EQ(original.get<aige::Velocity2D>(origPlayer).x, loaded.get<aige::Velocity2D>(loadPlayer).x);
    EXPECT_FLOAT_EQ(original.get<aige::Velocity2D>(origPlayer).y, loaded.get<aige::Velocity2D>(loadPlayer).y);
    EXPECT_FLOAT_EQ(original.get<aige::Collider2D>(origPlayer).halfWidth,
                     loaded.get<aige::Collider2D>(loadPlayer).halfWidth);
    EXPECT_FLOAT_EQ(original.get<aige::Collider2D>(origPlayer).halfHeight,
                     loaded.get<aige::Collider2D>(loadPlayer).halfHeight);

    // Scripted NPC: the only entity with a LuaScript.
    aige::Entity origNpc = *original.view<aige::LuaScript>().begin();
    aige::Entity loadNpc = *loaded.view<aige::LuaScript>().begin();
    CompareCommon(original, origNpc, loaded, loadNpc);
    EXPECT_EQ(original.get<aige::LuaScript>(origNpc).path, loaded.get<aige::LuaScript>(loadNpc).path);

    std::filesystem::remove(scenePath);
}

TEST(SceneIOTest, LoadReturnsFalseWhenFileMissing) {
    aige::Registry registry;
    auto missingPath = std::filesystem::temp_directory_path() / "aige_does_not_exist.aige";
    EXPECT_FALSE(aige::LoadScene(registry, missingPath.string()));
}
