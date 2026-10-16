#pragma once

#include "ecs/Registry.h"

#include <string>

namespace aige {

// Hand-written entity "prefabs" until a real prefab system exists: one place
// that knows which components and visuals make up a tile, an NPC or the
// player. Used by the bootstrap level and the editor's placement tools, so
// both produce identical entities.

enum class TileKind {
    Grass,
    Dirt,
    Wall,
};

// How a tile looks and behaves: which texture region it shows and whether
// it blocks movement. The built-in kinds are just preset styles.
struct TileStyle {
    std::string texture; // empty = built-in atlas
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float width = 64.0f; // on-screen size; taller than the diamond extends upward
    float height = 32.0f;
    float tint = 1.0f;
    bool solid = false;
    bool flat = false; // top-down art mapped onto the diamond (Sprite::flat)
    int rotation = 0;
};

TileStyle StyleFor(TileKind kind);

// Creates a tile at grid cell (gx, gy). Does not check whether the cell is
// already occupied; callers that care use TileGrid first.
Entity CreateTile(Registry& registry, int gx, int gy, TileKind kind);
Entity CreateTile(Registry& registry, int gx, int gy, const TileStyle& style);

// A textured, non-colliding object standing in the world (tree, crate,
// signpost): positioned and depth-sorted like a character.
Entity CreateProp(Registry& registry, float worldX, float worldY, const std::string& texture, float width,
                  float height);

// Characters are placed in world grid units; cell centers are (n + 0.5).
Entity CreateNpc(Registry& registry, float worldX, float worldY, const std::string& scriptPath = {});

// The player: input-driven movement (Velocity2D + Collider2D mark it), team
// Player, health, and a melee attack (Space / click).
Entity CreatePlayer(Registry& registry, float worldX, float worldY);

// A melee enemy: chases the player (Follow) when within range and swings
// at it via `scriptPath` (see projects/demo/scripts/enemy_melee.lua).
// `target` is what it chases — normally the player.
Entity CreateEnemy(Registry& registry, float worldX, float worldY, Entity target,
                   const std::string& scriptPath = "scripts/enemy_melee.lua");

// A health potion lying on the ground.
Entity CreateHealthPickup(Registry& registry, float worldX, float worldY, float amount = 6.0f);

} // namespace aige
