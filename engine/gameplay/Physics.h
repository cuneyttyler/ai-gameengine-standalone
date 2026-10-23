#pragma once

#include "ecs/Registry.h"
#include "physics2d/Collision.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Physics v2 (development_plan.md Phase 20, docs/systems/physics.md): collision layers and
// masks, circle and box colliders, triggers (sensors), queries, the grid-step
// character controller, and Box2D v3 for dynamic bodies and joints. Games without
// rigid bodies never step Box2D; top-down movement is the same as before.

// The layer an entity is on: its collider's `layer`, else its team's (player, enemy,
// ally), else "default". A mask is a list of layers ("world, enemy"); empty: all.
std::string LayerOf(Registry& registry, Entity entity);
bool MaskAllows(const std::string& mask, const std::string& layer);

// Where a collider is and how big (cells): its box half-size, or radius for circles.
struct ColliderShape {
    bool circle = false;
    float x = 0, y = 0, halfWidth = 0.3f, halfHeight = 0.3f, radius = 0.3f;
};
std::optional<ColliderShape> ShapeOf(Registry& registry, Entity entity);
bool ShapesOverlap(const ColliderShape& a, const ColliderShape& b);

// --- Queries -----------------------------------------------------------------------------

struct RayHit {
    Entity entity = entt::null; // null: a solid tile
    float x = 0, y = 0;         // where
    float distance = 0;
};
// The first solid tile or collider (on a layer the mask allows) along a segment.
std::optional<RayHit> Raycast(Registry& registry, const SolidQueryFn& isSolid, float x0, float y0, float x1, float y1,
                              const std::string& mask = {}, Entity ignore = entt::null);
// Entities whose colliders overlap a circle.
std::vector<Entity> Overlap(Registry& registry, float x, float y, float radius, const std::string& mask = {},
                            Entity ignore = entt::null);
// Nothing solid between two points (tiles only).
bool LineOfSight(const SolidQueryFn& isSolid, float x0, float y0, float x1, float y1);

// --- Triggers -------------------------------------------------------------------------------

// A sensor area: entities entering and leaving it (on layers the mask allows) raise
// trigger_enter and trigger_exit, with the trigger's name as the event's text.
struct Trigger {
    std::string name;
    std::string mask = "player"; // which layers it notices; empty: all
    bool once = false;
    float radius = 0.0f; // 0: the entity's collider
};
// Runtime: who is inside, and whether it has fired (once).
struct TriggerState {
    std::vector<Entity> inside;
    bool fired = false;
};
struct TriggerEvent {
    Entity trigger, other;
    bool enter;
    std::string name;
};
std::vector<TriggerEvent> UpdateTriggers(Registry& registry);

// --- Character controllers --------------------------------------------------------------------

// How an entity moves (20.2): top-down (the engine's walking, as before) or grid-step
// (cell by cell, for roguelikes and tile-based RPGs).
struct CharacterController2D {
    int mode = 0;            // 0 top-down, 1 grid-step
    float stepTime = 0.18f;  // grid-step: seconds per cell
    bool diagonal = false;   // grid-step: diagonal steps allowed
};
// Runtime: a grid step under way.
struct GridStep {
    float fromX = 0, fromY = 0, toX = 0, toY = 0, t = 1.0f;
};
// Moves a grid-stepping entity by input (-1..1 per axis, screen-free: world axes).
// Starts a step when idle, eases it over stepTime. True while moving.
bool UpdateGridStep(Registry& registry, Entity entity, float inputX, float inputY, const SolidQueryFn& isSolid, float dt);

// --- Rigid bodies (Box2D v3) --------------------------------------------------------------------

struct RigidBody2D {
    int type = 2;            // 0 static, 1 kinematic, 2 dynamic
    float density = 1.0f;
    float friction = 0.6f;
    float restitution = 0.0f;
    float linearDamping = 2.0f; // top-down floors slow things down
    float angularDamping = 2.0f;
    float gravityScale = 1.0f;
    bool fixedRotation = false;
    bool bullet = false;     // continuous collision for fast bodies
};

struct Joint2D {
    int type = 0;            // 0 revolute, 1 distance, 2 weld
    Entity other = entt::null;
    float anchorX = 0, anchorY = 0; // on this body, cells from its centre
    float length = 0;        // distance: 0 = as placed
    bool collide = false;
};

// The Box2D world for a scene: bodies for RigidBody2D entities, kinematic bodies for
// characters (so they push crates), static boxes for solid tiles. Writes dynamic bodies'
// positions and angles back to their WorldTransform2D and Sprite.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // One step: new bodies and joints made, characters' moves handed in, the world stepped,
    // results written back. Does nothing (and costs nothing) without rigid bodies.
    void Step(Registry& registry, const SolidQueryFn& isSolid, float dt);
    void Clear(); // the scene changed
    bool Active() const;
    size_t BodyCount() const;
    void ApplyImpulse(Entity entity, float ix, float iy);
    void SetVelocity(Entity entity, float vx, float vy);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// --- Debug draw (20.4) ----------------------------------------------------------------------

struct DebugShape {
    bool circle = false;
    float x = 0, y = 0, halfWidth = 0, halfHeight = 0, radius = 0, angle = 0;
    int kind = 0; // 0 collider, 1 trigger, 2 dynamic body, 3 static body
};
std::vector<DebugShape> PhysicsDebugShapes(Registry& registry);

} // namespace aige
