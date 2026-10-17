#include <gtest/gtest.h>

#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "editing/EditHistory.h"
#include "editing/SceneEditing.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <iterator>

namespace {

const aige::IsoParams kIso = aige::BootstrapIso();

// Scene-pixel center of a grid cell.
void CellCenter(int gx, int gy, float& x, float& y) {
    aige::ScreenPose pose = aige::ProjectIso(static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, kIso);
    x = pose.x;
    y = pose.y;
}

size_t CountTiles(aige::Registry& registry) {
    return registry.view<aige::Tile>().size();
}

} // namespace

TEST(EditingTest, CellAtFindsTheCellUnderItsCenter) {
    float x = 0.0f;
    float y = 0.0f;
    CellCenter(3, 5, x, y);
    int gx = -1;
    int gy = -1;
    aige::CellAt(x, y, kIso, gx, gy);
    EXPECT_EQ(gx, 3);
    EXPECT_EQ(gy, 5);
}

TEST(EditingTest, PickPrefersCharacterOverTileBeneath) {
    aige::Registry registry;
    aige::CreateTile(registry, 3, 3, aige::TileKind::Grass);
    auto npc = aige::CreateNpc(registry, 3.5f, 3.5f);
    aige::UpdateWorldToScreen(registry, kIso);

    const auto& screen = registry.get<aige::ScreenTransform2D>(npc);
    auto picked = aige::PickEntity(registry, screen.x, screen.y, kIso);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, npc);
}

TEST(EditingTest, PickFallsBackToTileByCell) {
    aige::Registry registry;
    auto tile = aige::CreateTile(registry, 4, 2, aige::TileKind::Dirt);
    aige::UpdateWorldToScreen(registry, kIso);

    float x = 0.0f;
    float y = 0.0f;
    CellCenter(4, 2, x, y);
    auto picked = aige::PickEntity(registry, x, y, kIso);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, tile);

    CellCenter(9, 9, x, y);
    EXPECT_FALSE(aige::PickEntity(registry, x, y, kIso).has_value());
}

TEST(EditingTest, PaintTileReplacesExistingTileInCell) {
    aige::Registry registry;
    aige::PaintTile(registry, 1, 1, aige::TileKind::Grass);
    auto wall = aige::PaintTile(registry, 1, 1, aige::TileKind::Wall);

    EXPECT_EQ(CountTiles(registry), 1u);
    EXPECT_TRUE(registry.get<aige::Tile>(wall).solid);
    EXPECT_EQ(aige::TileGrid::Build(registry).At(1, 1), wall);
}

TEST(EditingTest, PlacingASecondPlayerMovesTheFirst) {
    aige::Registry registry;
    auto first = aige::PlaceCharacter(registry, aige::CharacterKind::Player, 1, 1);
    auto second = aige::PlaceCharacter(registry, aige::CharacterKind::Player, 4, 6);

    EXPECT_EQ(first, second);
    auto players = registry.view<aige::Velocity2D, aige::Collider2D>();
    EXPECT_EQ(std::distance(players.begin(), players.end()), 1);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(first).x, 4.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(first).y, 6.5f);
}

TEST(EditingTest, PlaceNpcAtCellCenter) {
    aige::Registry registry;
    auto npc = aige::PlaceCharacter(registry, aige::CharacterKind::Npc, 2, 5);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(npc).x, 2.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(npc).y, 5.5f);
    EXPECT_FALSE(registry.all_of<aige::Velocity2D>(npc));
}

TEST(EditingTest, DeleteRemovesEntity) {
    aige::Registry registry;
    auto npc = aige::CreateNpc(registry, 1.5f, 1.5f);
    aige::DeleteEntity(registry, npc);
    EXPECT_FALSE(registry.valid(npc));
    aige::DeleteEntity(registry, npc); // deleting twice is harmless
}

TEST(EditingTest, DuplicateCopiesComponentsAndOffsets) {
    aige::Registry registry;
    auto npc = aige::CreateNpc(registry, 2.5f, 2.5f, "scripts/wander.lua");
    registry.get<aige::Sprite>(npc).r = 0.25f;

    auto copy = aige::DuplicateEntity(registry, npc);
    ASSERT_TRUE(copy.has_value());
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*copy).x, 3.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*copy).y, 2.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(*copy).r, 0.25f);
    EXPECT_EQ(registry.get<aige::LuaScript>(*copy).path, "scripts/wander.lua");
    EXPECT_FALSE(registry.get<aige::LuaScript>(*copy).loaded);
    EXPECT_TRUE(registry.all_of<aige::ScreenTransform2D>(*copy));
}

TEST(EditingTest, DuplicateRefusesTilesAndPlayer) {
    aige::Registry registry;
    auto tile = aige::CreateTile(registry, 0, 0, aige::TileKind::Grass);
    auto player = aige::CreatePlayer(registry, 1.5f, 1.5f);
    EXPECT_FALSE(aige::DuplicateEntity(registry, tile).has_value());
    EXPECT_FALSE(aige::DuplicateEntity(registry, player).has_value());
}

TEST(EditingTest, DragTileMovesByWholeCells) {
    aige::Registry registry;
    auto tile = aige::CreateTile(registry, 2, 2, aige::TileKind::Grass);

    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    CellCenter(2, 2, fromX, fromY);
    CellCenter(4, 3, toX, toY);
    EXPECT_TRUE(aige::DragEntity(registry, tile, fromX, fromY, toX, toY, kIso));
    EXPECT_EQ(registry.get<aige::Tile>(tile).gx, 4);
    EXPECT_EQ(registry.get<aige::Tile>(tile).gy, 3);
}

TEST(EditingTest, DragTileRefusesOccupiedCell) {
    aige::Registry registry;
    auto tile = aige::CreateTile(registry, 2, 2, aige::TileKind::Grass);
    aige::CreateTile(registry, 3, 2, aige::TileKind::Grass);

    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    CellCenter(2, 2, fromX, fromY);
    CellCenter(3, 2, toX, toY);
    EXPECT_FALSE(aige::DragEntity(registry, tile, fromX, fromY, toX, toY, kIso));
    EXPECT_EQ(registry.get<aige::Tile>(tile).gx, 2);
}

TEST(EditingTest, DragCharacterMovesByWorldDelta) {
    aige::Registry registry;
    auto npc = aige::CreateNpc(registry, 2.5f, 2.5f);

    aige::ScreenPose from = aige::ProjectIso(1.0f, 1.0f, kIso);
    aige::ScreenPose to = aige::ProjectIso(1.75f, 0.5f, kIso);
    EXPECT_TRUE(aige::DragEntity(registry, npc, from.x, from.y, to.x, to.y, kIso));
    EXPECT_NEAR(registry.get<aige::WorldTransform2D>(npc).x, 3.25f, 1e-4f);
    EXPECT_NEAR(registry.get<aige::WorldTransform2D>(npc).y, 2.0f, 1e-4f);
}

TEST(EditingTest, UndoRedoRestoresScene) {
    aige::Registry registry;
    aige::CreateTile(registry, 0, 0, aige::TileKind::Grass);
    aige::EditHistory history;

    history.Record(registry);
    aige::PaintTile(registry, 1, 0, aige::TileKind::Wall);
    EXPECT_EQ(CountTiles(registry), 2u);

    ASSERT_TRUE(history.Undo(registry));
    EXPECT_EQ(CountTiles(registry), 1u);
    EXPECT_FALSE(history.CanUndo());

    ASSERT_TRUE(history.Redo(registry));
    EXPECT_EQ(CountTiles(registry), 2u);
    EXPECT_FALSE(history.Redo(registry));
}

TEST(EditingTest, NewEditClearsRedo) {
    aige::Registry registry;
    aige::EditHistory history;
    history.Record(registry);
    aige::CreateTile(registry, 0, 0, aige::TileKind::Grass);
    history.Undo(registry);
    EXPECT_TRUE(history.CanRedo());

    history.Record(registry);
    EXPECT_FALSE(history.CanRedo());
}

TEST(EditingTest, HistoryDepthIsCapped) {
    aige::Registry registry;
    aige::EditHistory history(3);
    for (int i = 0; i < 5; ++i) {
        history.Record(registry);
        aige::CreateTile(registry, i, 0, aige::TileKind::Grass);
    }
    int undos = 0;
    while (history.Undo(registry)) {
        ++undos;
    }
    EXPECT_EQ(undos, 3);
    EXPECT_EQ(CountTiles(registry), 2u); // the two oldest edits fell off the stack
}

// The editor relies on this to keep the selection across undo and Play/Stop.
TEST(EditingTest, EntityIndexSurvivesSerializeRoundTrip) {
    aige::Registry registry;
    aige::BuildBootstrapScene(registry, "scripts/npc.lua");
    auto player = *aige::FindPlayer(registry);
    auto index = aige::IndexOfEntity(registry, player);
    ASSERT_TRUE(index.has_value());

    std::string snapshot = aige::SerializeScene(registry);
    aige::DeserializeScene(registry, snapshot);
    aige::DeserializeScene(registry, aige::SerializeScene(registry)); // and again

    auto restored = aige::EntityAtIndex(registry, *index);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(*restored, *aige::FindPlayer(registry));
}

// An applied AI proposal is one undo step; the panel follows it through undo
// and redo by its id (Undo while it's in effect, Apply once it's undone).
TEST(EditingTest, UndoStepsCanBeFollowedByTheirIds) {
    aige::Registry registry;
    aige::EditHistory history;
    const uint64_t first = history.Record(registry);
    aige::PaintTile(registry, 1, 1, aige::TileKind::Grass);
    const uint64_t second = history.Record(registry);
    aige::PaintTile(registry, 2, 2, aige::TileKind::Grass);
    EXPECT_NE(first, 0u);
    EXPECT_NE(first, second);
    EXPECT_EQ(history.NextUndo(), second);
    EXPECT_TRUE(history.IsDone(second));

    ASSERT_TRUE(history.Undo(registry));
    EXPECT_TRUE(history.IsUndone(second));
    EXPECT_FALSE(history.IsDone(second));
    EXPECT_EQ(history.NextRedo(), second);
    EXPECT_EQ(history.NextUndo(), first);

    ASSERT_TRUE(history.Redo(registry));
    EXPECT_TRUE(history.IsDone(second)); // the same step, back in effect
    EXPECT_EQ(history.NextUndo(), second);

    ASSERT_TRUE(history.Undo(registry));
    history.Record(registry); // a new edit replaces the redo
    EXPECT_FALSE(history.IsDone(second));
    EXPECT_FALSE(history.IsUndone(second));
}
