#pragma once

#include "ecs/Registry.h"

#include <string>

namespace aige {

// A ground tile occupying one isometric grid cell. Its ScreenTransform2D is
// derived from (gx, gy) every frame (UpdateWorldToScreen), so moving a tile
// means changing its cell. Collision comes from tiles too (TileGrid): a cell
// is walkable only if it holds a non-solid tile, so empty space is a wall.
struct Tile {
    int gx = 0;
    int gy = 0;
    bool solid = false;
};

// A designer-facing name, shown in the Scene Tree and usable from scripts.
struct Name {
    std::string value;
};

// Which side an entity is on; combat (milestone 1.5) only lets attacks hit
// entities of an opposing faction.
enum class Team {
    Neutral,
    Player,
    Enemy,
};

struct Faction {
    Team team = Team::Neutral;
};

struct Health {
    float current = 10.0f;
    float max = 10.0f;
};

// Walks toward another entity (with collision) until within stopDistance.
// The simplest useful "AI": an enemy that chases the player needs nothing
// but this and a target. Distances in world grid units, speed in cells/s.
struct Follow {
    Entity target = entt::null;
    float speed = 2.0f;
    float stopDistance = 0.8f;
    float range = 6.0f; // only follows while the target is this close; 0 = always
};

} // namespace aige
