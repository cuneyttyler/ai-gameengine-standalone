#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "editing/SceneEditing.h"
#include "editing/SceneTools.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <filesystem>
#include <variant>

namespace fs = std::filesystem;

// development_plan.md 4.3.
namespace {

struct Fixture {
    Fixture() {
        root = fs::temp_directory_path() /
               ("aige_tools_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "prefabs");
        project = aige::Project{root};
        prefabs = std::make_unique<aige::PrefabLibrary>(project);
        aige::Registry scratch;
        prefabs->SaveAsPrefab(scratch, aige::CreateEnemy(scratch, 0, 0, entt::null, ""), "prefabs/skeleton.prefab");
        prefabs->Refresh();
    }
    ~Fixture() { fs::remove_all(root); }
    fs::path root;
    aige::Project project;
    std::unique_ptr<aige::PrefabLibrary> prefabs;
    aige::Registry registry;
};

} // namespace

TEST(SceneToolsTest, ScalingResizesSpriteAndCollisionTogetherAsAnOverride) {
    Fixture f;
    aige::Entity skeleton = *f.prefabs->Instantiate(f.registry, "prefabs/skeleton.prefab", 3.5f, 3.5f);
    const auto sprite = f.registry.get<aige::Sprite>(skeleton);
    const auto collider = f.registry.get<aige::Collider2D>(skeleton);
    const auto hurtbox = f.registry.get<aige::Hurtbox>(skeleton);

    ASSERT_FALSE(aige::ScaleEntity(aige::EngineReflection(), f.registry, skeleton, 2.0f));
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(skeleton).width, sprite.width * 2);
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(skeleton).height, sprite.height * 2);
    EXPECT_FLOAT_EQ(f.registry.get<aige::Collider2D>(skeleton).halfWidth, collider.halfWidth * 2);
    EXPECT_FLOAT_EQ(f.registry.get<aige::Hurtbox>(skeleton).halfHeight, hurtbox.halfHeight * 2);
    // A prefab copy remembers it was resized: a prefab update won't undo it.
    EXPECT_TRUE(aige::HasOverride(f.registry, skeleton, "Sprite.width"));
    f.prefabs->Sync(f.registry, skeleton);
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(skeleton).width, sprite.width * 2);

    // A tile's image resizes; the tile stays in its one cell (5.2). Absurd sizes are clamped.
    aige::Entity tile = aige::PaintTile(f.registry, 3, 4, aige::TileKind::Grass);
    const float tileWidth = f.registry.get<aige::Sprite>(tile).width;
    ASSERT_FALSE(aige::ScaleEntity(aige::EngineReflection(), f.registry, tile, 0.5f));
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(tile).width, tileWidth * 0.5f);
    EXPECT_EQ(f.registry.get<aige::Tile>(tile).gx, 3);
    EXPECT_EQ(f.registry.get<aige::Tile>(tile).gy, 4);
    ASSERT_FALSE(aige::ScaleEntity(aige::EngineReflection(), f.registry, skeleton, 1000.0f));
    EXPECT_LE(f.registry.get<aige::Sprite>(skeleton).height, 2048.0f);
}

TEST(SceneToolsTest, NudgingMovesByCellsForTilesAndExactlyForEverythingElse) {
    Fixture f;
    const auto iso = aige::BootstrapIso();
    aige::Entity skeleton = *f.prefabs->Instantiate(f.registry, "prefabs/skeleton.prefab", 3.5f, 3.5f);
    EXPECT_TRUE(aige::NudgeEntity(f.registry, skeleton, 1.0f, 0.0f, iso));
    EXPECT_NEAR(f.registry.get<aige::WorldTransform2D>(skeleton).x, 4.5f, 1e-3f);
    EXPECT_TRUE(aige::NudgeEntity(f.registry, skeleton, 0.0f, -0.1f, iso));
    EXPECT_NEAR(f.registry.get<aige::WorldTransform2D>(skeleton).y, 3.4f, 1e-3f);

    aige::Entity a = aige::PaintTile(f.registry, 0, 0, aige::TileKind::Grass);
    aige::PaintTile(f.registry, 1, 0, aige::TileKind::Dirt);
    EXPECT_FALSE(aige::NudgeEntity(f.registry, a, 0.1f, 0.0f, iso)); // less than a cell: nothing
    EXPECT_FALSE(aige::NudgeEntity(f.registry, a, 1.0f, 0.0f, iso)); // onto another tile: refused
    EXPECT_TRUE(aige::NudgeEntity(f.registry, a, 0.0f, 1.0f, iso));
    EXPECT_EQ(f.registry.get<aige::Tile>(a).gy, 1);
}

TEST(SceneToolsTest, CopyAndPasteAtACell) {
    Fixture f;
    aige::Entity skeleton = *f.prefabs->Instantiate(f.registry, "prefabs/skeleton.prefab", 3.5f, 3.5f);
    aige::ScaleEntity(aige::EngineReflection(), f.registry, skeleton, 1.5f);
    std::string text = aige::CopyEntities(f.registry, {skeleton});

    auto pasted = aige::PasteEntities(f.registry, text, 10, 7);
    ASSERT_EQ(pasted.size(), 1u);
    const auto& world = f.registry.get<aige::WorldTransform2D>(pasted[0]);
    EXPECT_NEAR(world.x, 10.5f, 1e-3f);
    EXPECT_NEAR(world.y, 7.5f, 1e-3f);
    // A real copy: same prefab link, same size override.
    EXPECT_EQ(f.registry.get<aige::PrefabInstance>(pasted[0]).path, "prefabs/skeleton.prefab");
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(pasted[0]).width, f.registry.get<aige::Sprite>(skeleton).width);

    // A pasted tile replaces the tile in its cell.
    aige::Entity grass = aige::PaintTile(f.registry, 0, 0, aige::TileKind::Grass);
    aige::Entity dirt = aige::PaintTile(f.registry, 5, 5, aige::TileKind::Dirt);
    auto tiles = aige::PasteEntities(f.registry, aige::CopyEntities(f.registry, {grass}), 5, 5);
    ASSERT_EQ(tiles.size(), 1u);
    EXPECT_FALSE(f.registry.valid(dirt));
    EXPECT_EQ(f.registry.get<aige::Tile>(tiles[0]).gx, 5);
}

TEST(SceneToolsTest, RemovingAPrefabKeepsItsCopiesAsOrdinaryEntities) {
    Fixture f;
    aige::Registry scratch;
    f.prefabs->SaveAsPrefab(scratch, aige::CreateEnemy(scratch, 0, 0, entt::null, ""), "prefabs/other.prefab");
    f.prefabs->Refresh();
    aige::Entity a = *f.prefabs->Instantiate(f.registry, "prefabs/skeleton.prefab", 1.5f, 1.5f);
    aige::Entity b = *f.prefabs->Instantiate(f.registry, "prefabs/skeleton.prefab", 2.5f, 1.5f);
    aige::Entity other = *f.prefabs->Instantiate(f.registry, "prefabs/other.prefab", 3.5f, 1.5f);

    auto result = f.prefabs->Remove(f.registry, "prefabs/skeleton.prefab");
    ASSERT_TRUE(std::holds_alternative<int>(result));
    EXPECT_EQ(std::get<int>(result), 2);
    EXPECT_FALSE(fs::exists(f.root / "prefabs" / "skeleton.prefab"));
    EXPECT_EQ(f.prefabs->Find("prefabs/skeleton.prefab"), nullptr);
    for (aige::Entity e : {a, b}) {
        EXPECT_TRUE(f.registry.valid(e));
        EXPECT_FALSE(f.registry.all_of<aige::PrefabInstance>(e));
        EXPECT_TRUE(f.registry.all_of<aige::Health>(e)); // still a skeleton, just unlinked
    }
    EXPECT_TRUE(f.registry.all_of<aige::PrefabInstance>(other));
    EXPECT_TRUE(std::holds_alternative<std::string>(f.prefabs->Remove(f.registry, "prefabs/skeleton.prefab")));
}

// development_plan.md 6.6: flat images turn a quarter, upright ones mirror.
TEST(SceneToolsTest, TurningFlatImagesAndMirroringUprightOnes) {
    Fixture f;
    aige::Entity skeleton = *f.prefabs->Instantiate(f.registry, "prefabs/skeleton.prefab", 3.5f, 3.5f);
    const auto drawn = f.registry.get<aige::Sprite>(skeleton);
    EXPECT_FALSE(aige::IsMirrored(f.registry, skeleton));

    // Upright: an odd turn mirrors, an even one leaves it; as an override.
    ASSERT_FALSE(aige::TurnEntity(aige::EngineReflection(), f.registry, skeleton, 1));
    EXPECT_TRUE(aige::IsMirrored(f.registry, skeleton));
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(skeleton).u0, drawn.u1);
    EXPECT_FLOAT_EQ(f.registry.get<aige::Sprite>(skeleton).u1, drawn.u0);
    EXPECT_EQ(f.registry.get<aige::Sprite>(skeleton).rotation, 0);
    EXPECT_TRUE(aige::HasOverride(f.registry, skeleton, "Sprite.u0"));
    f.prefabs->Sync(f.registry, skeleton);
    EXPECT_TRUE(aige::IsMirrored(f.registry, skeleton)); // a prefab update keeps it
    ASSERT_FALSE(aige::TurnEntity(aige::EngineReflection(), f.registry, skeleton, 2));
    EXPECT_TRUE(aige::IsMirrored(f.registry, skeleton));
    ASSERT_FALSE(aige::MirrorEntity(aige::EngineReflection(), f.registry, skeleton));
    EXPECT_FALSE(aige::IsMirrored(f.registry, skeleton)); // twice: as drawn

    // Flat: quarter turns, either way, wrapping.
    aige::TileStyle style;
    style.flat = true;
    aige::Entity tile = aige::CreateTile(f.registry, 2, 2, style);
    ASSERT_FALSE(aige::TurnEntity(aige::EngineReflection(), f.registry, tile, 1));
    EXPECT_EQ(f.registry.get<aige::Sprite>(tile).rotation, 1);
    ASSERT_FALSE(aige::TurnEntity(aige::EngineReflection(), f.registry, tile, -2));
    EXPECT_EQ(f.registry.get<aige::Sprite>(tile).rotation, 3);
    EXPECT_FALSE(aige::IsMirrored(f.registry, tile));

    // The AI has them as commands.
    aige::RegisterEditingCommands(aige::EngineReflection());
    const aige::CommandInfo* turn = aige::EngineReflection().FindCommand("entity.turn");
    const aige::CommandInfo* mirror = aige::EngineReflection().FindCommand("entity.mirror");
    ASSERT_NE(turn, nullptr);
    ASSERT_NE(mirror, nullptr);
    EXPECT_TRUE(aige::TurnEntity(aige::EngineReflection(), f.registry, entt::null, 1).has_value());
}
