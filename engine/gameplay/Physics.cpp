#include "gameplay/Physics.h"

#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace aige {

namespace {

std::vector<std::string> Split(const std::string& list) {
    std::vector<std::string> out;
    std::stringstream in(list);
    std::string part;
    while (std::getline(in, part, ',')) {
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

} // namespace

std::string LayerOf(Registry& registry, Entity entity) {
    if (const auto* c = registry.try_get<Collider2D>(entity); c && !c->layer.empty()) return c->layer;
    if (registry.all_of<Projectile>(entity)) return "projectile";
    if (registry.all_of<Trigger>(entity)) return "trigger";
    switch (TeamOf(registry, entity)) {
        case Team::Player: return "player";
        case Team::Enemy: return "enemy";
        default: break;
    }
    if (registry.all_of<Faction>(entity)) return "ally";
    return "default";
}

bool MaskAllows(const std::string& mask, const std::string& layer) {
    if (mask.empty()) return true;
    const auto layers = Split(mask);
    return std::find(layers.begin(), layers.end(), layer) != layers.end() ||
           std::find(layers.begin(), layers.end(), "all") != layers.end();
}

std::optional<ColliderShape> ShapeOf(Registry& registry, Entity entity) {
    const auto* w = registry.try_get<WorldTransform2D>(entity);
    if (!w) return std::nullopt;
    ColliderShape s;
    s.x = w->x;
    s.y = w->y;
    if (const auto* c = registry.try_get<Collider2D>(entity)) {
        s.circle = c->shape == 1;
        s.halfWidth = c->halfWidth;
        s.halfHeight = c->halfHeight;
        s.radius = c->shape == 1 ? c->radius : std::max(c->halfWidth, c->halfHeight);
        return s;
    }
    if (const auto* h = registry.try_get<Hurtbox>(entity)) {
        s.halfWidth = h->halfWidth;
        s.halfHeight = h->halfHeight;
        s.radius = std::max(h->halfWidth, h->halfHeight);
        return s;
    }
    return std::nullopt;
}

bool ShapesOverlap(const ColliderShape& a, const ColliderShape& b) {
    if (a.circle && b.circle) return std::hypot(a.x - b.x, a.y - b.y) <= a.radius + b.radius;
    if (!a.circle && !b.circle) {
        return std::abs(a.x - b.x) <= a.halfWidth + b.halfWidth && std::abs(a.y - b.y) <= a.halfHeight + b.halfHeight;
    }
    const ColliderShape& c = a.circle ? a : b;
    const ColliderShape& r = a.circle ? b : a;
    const float nx = std::clamp(c.x, r.x - r.halfWidth, r.x + r.halfWidth);
    const float ny = std::clamp(c.y, r.y - r.halfHeight, r.y + r.halfHeight);
    return std::hypot(c.x - nx, c.y - ny) <= c.radius;
}

// --- Queries ---------------------------------------------------------------------------------

bool LineOfSight(const SolidQueryFn& isSolid, float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
    for (float t = 0.25f; t < length; t += 0.25f) {
        if (isSolid(static_cast<int>(std::floor(x0 + dx / length * t)), static_cast<int>(std::floor(y0 + dy / length * t)))) {
            return false;
        }
    }
    return true;
}

std::optional<RayHit> Raycast(Registry& registry, const SolidQueryFn& isSolid, float x0, float y0, float x1, float y1,
                              const std::string& mask, Entity ignore) {
    const float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-5f) return std::nullopt;
    // Colliders: the nearest a sampled point along the ray falls in.
    std::vector<std::pair<Entity, ColliderShape>> shapes;
    for (auto [e, w] : registry.view<WorldTransform2D>(entt::exclude<Dead>).each()) {
        if (e == ignore || (!registry.all_of<Collider2D>(e) && !registry.all_of<Hurtbox>(e))) continue;
        if (!MaskAllows(mask, LayerOf(registry, e))) continue;
        if (auto s = ShapeOf(registry, e)) shapes.push_back({e, *s});
    }
    for (float t = 0.0f; t <= length; t += 0.05f) {
        const float px = x0 + dx / length * t, py = y0 + dy / length * t;
        if (isSolid && isSolid(static_cast<int>(std::floor(px)), static_cast<int>(std::floor(py)))) {
            return RayHit{entt::null, px, py, t};
        }
        ColliderShape point;
        point.circle = true;
        point.x = px;
        point.y = py;
        point.radius = 0.0f;
        for (const auto& [e, s] : shapes) {
            if (ShapesOverlap(point, s)) return RayHit{e, px, py, t};
        }
    }
    return std::nullopt;
}

std::vector<Entity> Overlap(Registry& registry, float x, float y, float radius, const std::string& mask, Entity ignore) {
    std::vector<Entity> out;
    ColliderShape area;
    area.circle = true;
    area.x = x;
    area.y = y;
    area.radius = radius;
    for (auto [e, w] : registry.view<WorldTransform2D>(entt::exclude<Dead>).each()) {
        if (e == ignore || !MaskAllows(mask, LayerOf(registry, e))) continue;
        auto s = ShapeOf(registry, e);
        if (!s) {
            ColliderShape point;
            point.circle = true;
            point.x = w.x;
            point.y = w.y;
            point.radius = 0.05f;
            s = point;
        }
        if (ShapesOverlap(area, *s)) out.push_back(e);
    }
    return out;
}

// --- Triggers -----------------------------------------------------------------------------------

std::vector<TriggerEvent> UpdateTriggers(Registry& registry) {
    std::vector<TriggerEvent> events;
    std::vector<Entity> triggers;
    for (auto [e, trigger, w] : registry.view<Trigger, WorldTransform2D>().each()) triggers.push_back(e);
    for (Entity t : triggers) {
        const auto& trigger = registry.get<Trigger>(t);
        auto& state = registry.get_or_emplace<TriggerState>(t);
        if (trigger.once && state.fired) continue;
        ColliderShape area;
        if (trigger.radius > 0.0f || !registry.all_of<Collider2D>(t)) {
            const auto& w = registry.get<WorldTransform2D>(t);
            area.circle = true;
            area.x = w.x;
            area.y = w.y;
            area.radius = trigger.radius > 0.0f ? trigger.radius : 0.5f;
        } else {
            area = *ShapeOf(registry, t);
        }
        std::vector<Entity> now;
        for (auto [e, w] : registry.view<WorldTransform2D>(entt::exclude<Dead>).each()) {
            if (e == t || registry.all_of<Trigger>(e) || !MaskAllows(trigger.mask, LayerOf(registry, e))) continue;
            if (!registry.all_of<Collider2D>(e) && !registry.all_of<Hurtbox>(e)) continue;
            if (auto s = ShapeOf(registry, e); s && ShapesOverlap(area, *s)) now.push_back(e);
        }
        for (Entity e : now) {
            if (std::find(state.inside.begin(), state.inside.end(), e) == state.inside.end()) {
                events.push_back({t, e, true, trigger.name});
                if (trigger.once) state.fired = true;
            }
        }
        for (Entity e : state.inside) {
            if (std::find(now.begin(), now.end(), e) == now.end()) events.push_back({t, e, false, trigger.name});
        }
        registry.get<TriggerState>(t).inside = std::move(now);
    }
    return events;
}

// --- Grid step ------------------------------------------------------------------------------------

bool UpdateGridStep(Registry& registry, Entity entity, float inputX, float inputY, const SolidQueryFn& isSolid, float dt) {
    auto* world = registry.try_get<WorldTransform2D>(entity);
    const auto* controller = registry.try_get<CharacterController2D>(entity);
    if (!world || !controller) return false;
    auto& step = registry.get_or_emplace<GridStep>(entity);
    if (step.t < 1.0f) {
        step.t = std::min(1.0f, step.t + dt / std::max(0.01f, controller->stepTime));
        world->x = step.fromX + (step.toX - step.fromX) * step.t;
        world->y = step.fromY + (step.toY - step.fromY) * step.t;
        if (auto* v = registry.try_get<Velocity2D>(entity)) {
            v->x = (step.toX - step.fromX) / controller->stepTime;
            v->y = (step.toY - step.fromY) / controller->stepTime;
        }
        return true;
    }
    if (auto* v = registry.try_get<Velocity2D>(entity)) *v = {0.0f, 0.0f};
    int sx = inputX > 0.3f ? 1 : inputX < -0.3f ? -1 : 0;
    int sy = inputY > 0.3f ? 1 : inputY < -0.3f ? -1 : 0;
    if (!controller->diagonal && sx != 0 && sy != 0) {
        if (std::abs(inputX) >= std::abs(inputY)) sy = 0;
        else sx = 0;
    }
    if (sx == 0 && sy == 0) {
        // Settle on the cell's centre.
        world->x = std::floor(world->x) + 0.5f;
        world->y = std::floor(world->y) + 0.5f;
        return false;
    }
    const int cx = static_cast<int>(std::floor(world->x)), cy = static_cast<int>(std::floor(world->y));
    registry.emplace_or_replace<Facing>(entity, static_cast<float>(sx), static_cast<float>(sy));
    if (isSolid && isSolid(cx + sx, cy + sy)) return false; // a wall: turn to face it only
    step = {cx + 0.5f, cy + 0.5f, cx + sx + 0.5f, cy + sy + 0.5f, 0.0f};
    return true;
}

// --- Box2D ------------------------------------------------------------------------------------------

struct PhysicsWorld::Impl {
    b2WorldId world = b2_nullWorldId;
    std::map<Entity, b2BodyId> bodies;     // RigidBody2D entities
    std::map<Entity, b2BodyId> characters; // kinematic bodies following our movement
    std::set<Entity> joined;
    std::vector<b2BodyId> walls;
    size_t wallHash = 0;

    void EnsureWorld() {
        if (b2World_IsValid(world)) return;
        b2WorldDef def = b2DefaultWorldDef();
        const auto& s = ActiveSettings().physics;
        def.gravity = {s.gravityX, s.gravityY};
        world = b2CreateWorld(&def);
    }
    void Destroy() {
        if (b2World_IsValid(world)) b2DestroyWorld(world);
        world = b2_nullWorldId;
        bodies.clear();
        characters.clear();
        joined.clear();
        walls.clear();
        wallHash = 0;
    }
    static b2ShapeDef ShapeDefFor(const RigidBody2D* body) {
        b2ShapeDef def = b2DefaultShapeDef();
        if (body) {
            def.density = body->density;
            def.friction = body->friction;
            def.restitution = body->restitution;
        }
        return def;
    }
    static void AddShape(b2BodyId id, Registry& registry, Entity e, const RigidBody2D* body) {
        b2ShapeDef def = ShapeDefFor(body);
        const auto* c = registry.try_get<Collider2D>(e);
        if (c && c->shape == 1) {
            b2Circle circle{{0.0f, 0.0f}, std::max(0.05f, c->radius)};
            b2CreateCircleShape(id, &def, &circle);
        } else {
            const float hw = c ? c->halfWidth : 0.4f, hh = c ? c->halfHeight : 0.4f;
            b2Polygon box = b2MakeBox(std::max(0.05f, hw), std::max(0.05f, hh));
            b2CreatePolygonShape(id, &def, &box);
        }
    }
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() { impl_->Destroy(); }
void PhysicsWorld::Clear() { impl_->Destroy(); }
bool PhysicsWorld::Active() const { return b2World_IsValid(impl_->world); }
size_t PhysicsWorld::BodyCount() const { return impl_->bodies.size(); }

void PhysicsWorld::ApplyImpulse(Entity entity, float ix, float iy) {
    auto it = impl_->bodies.find(entity);
    if (it != impl_->bodies.end() && b2Body_IsValid(it->second)) b2Body_ApplyLinearImpulseToCenter(it->second, {ix, iy}, true);
}

void PhysicsWorld::SetVelocity(Entity entity, float vx, float vy) {
    auto it = impl_->bodies.find(entity);
    if (it != impl_->bodies.end() && b2Body_IsValid(it->second)) b2Body_SetLinearVelocity(it->second, {vx, vy});
}

void PhysicsWorld::Step(Registry& registry, const SolidQueryFn& isSolid, float dt) {
    auto view = registry.view<RigidBody2D, WorldTransform2D>();
    if (view.begin() == view.end()) {
        if (Active()) impl_->Destroy(); // no rigid bodies: nothing to pay for
        return;
    }
    impl_->EnsureWorld();
    // Bodies for new rigid bodies; gone ones destroyed.
    for (auto it = impl_->bodies.begin(); it != impl_->bodies.end();) {
        if (!registry.valid(it->first) || !registry.all_of<RigidBody2D>(it->first)) {
            if (b2Body_IsValid(it->second)) b2DestroyBody(it->second);
            it = impl_->bodies.erase(it);
        } else {
            ++it;
        }
    }
    for (auto [e, body, w] : view.each()) {
        if (impl_->bodies.contains(e)) continue;
        b2BodyDef def = b2DefaultBodyDef();
        def.type = body.type == 0 ? b2_staticBody : body.type == 1 ? b2_kinematicBody : b2_dynamicBody;
        def.position = {w.x, w.y};
        def.linearDamping = body.linearDamping;
        def.angularDamping = body.angularDamping;
        def.gravityScale = body.gravityScale;
        def.fixedRotation = body.fixedRotation;
        def.isBullet = body.bullet;
        if (const auto* sprite = registry.try_get<Sprite>(e); sprite && sprite->angle != 0.0f) {
            def.rotation = b2MakeRot(sprite->angle * 3.14159265f / 180.0f);
        }
        b2BodyId id = b2CreateBody(impl_->world, &def);
        Impl::AddShape(id, registry, e, &body);
        impl_->bodies[e] = id;
    }
    // Joints, once both ends have bodies.
    for (auto [e, joint] : registry.view<Joint2D>().each()) {
        if (impl_->joined.contains(e)) continue;
        auto a = impl_->bodies.find(e), b = impl_->bodies.find(joint.other);
        if (a == impl_->bodies.end() || b == impl_->bodies.end()) continue;
        const b2Vec2 anchor{joint.anchorX, joint.anchorY};
        if (joint.type == 1) {
            b2DistanceJointDef def = b2DefaultDistanceJointDef();
            def.bodyIdA = a->second;
            def.bodyIdB = b->second;
            def.localAnchorA = anchor;
            const b2Vec2 pa = b2Body_GetPosition(a->second), pb = b2Body_GetPosition(b->second);
            def.length = joint.length > 0 ? joint.length : std::hypot(pb.x - pa.x - anchor.x, pb.y - pa.y - anchor.y);
            def.collideConnected = joint.collide;
            b2CreateDistanceJoint(impl_->world, &def);
        } else if (joint.type == 2) {
            b2WeldJointDef def = b2DefaultWeldJointDef();
            def.bodyIdA = a->second;
            def.bodyIdB = b->second;
            const b2Vec2 pa = b2Body_GetPosition(a->second), pb = b2Body_GetPosition(b->second);
            def.localAnchorA = anchor;
            def.localAnchorB = {pa.x + anchor.x - pb.x, pa.y + anchor.y - pb.y};
            def.collideConnected = joint.collide;
            b2CreateWeldJoint(impl_->world, &def);
        } else {
            b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
            def.bodyIdA = a->second;
            def.bodyIdB = b->second;
            const b2Vec2 pa = b2Body_GetPosition(a->second), pb = b2Body_GetPosition(b->second);
            def.localAnchorA = anchor;
            def.localAnchorB = {pa.x + anchor.x - pb.x, pa.y + anchor.y - pb.y};
            def.collideConnected = joint.collide;
            b2CreateRevoluteJoint(impl_->world, &def);
        }
        impl_->joined.insert(e);
    }
    // Solid tiles near the bodies as static boxes (rebuilt when the set changes).
    if (isSolid) {
        std::set<std::pair<int, int>> cells;
        for (auto [e, body, w] : view.each()) {
            const int cx = static_cast<int>(std::floor(w.x)), cy = static_cast<int>(std::floor(w.y));
            for (int y = cy - 6; y <= cy + 6; ++y) {
                for (int x = cx - 6; x <= cx + 6; ++x) {
                    if (isSolid(x, y)) cells.insert({x, y});
                }
            }
        }
        size_t hash = cells.size();
        for (const auto& [x, y] : cells) hash = hash * 1000003u ^ (static_cast<size_t>(x) * 73856093u ^ static_cast<size_t>(y) * 19349663u);
        if (hash != impl_->wallHash) {
            for (auto id : impl_->walls) {
                if (b2Body_IsValid(id)) b2DestroyBody(id);
            }
            impl_->walls.clear();
            for (const auto& [x, y] : cells) {
                b2BodyDef def = b2DefaultBodyDef();
                def.position = {x + 0.5f, y + 0.5f};
                b2BodyId id = b2CreateBody(impl_->world, &def);
                b2ShapeDef shape = b2DefaultShapeDef();
                b2Polygon box = b2MakeBox(0.5f, 0.5f);
                b2CreatePolygonShape(id, &shape, &box);
                impl_->walls.push_back(id);
            }
            impl_->wallHash = hash;
        }
    }
    // Characters (moved by the engine) as kinematic bodies that push what they walk into.
    for (auto it = impl_->characters.begin(); it != impl_->characters.end();) {
        if (!registry.valid(it->first) || registry.all_of<Dead>(it->first)) {
            if (b2Body_IsValid(it->second)) b2DestroyBody(it->second);
            it = impl_->characters.erase(it);
        } else {
            ++it;
        }
    }
    for (auto [e, collider, w] : registry.view<Collider2D, WorldTransform2D>(entt::exclude<RigidBody2D, Dead>).each()) {
        if (!registry.all_of<Velocity2D>(e)) continue;
        auto it = impl_->characters.find(e);
        if (it == impl_->characters.end()) {
            b2BodyDef def = b2DefaultBodyDef();
            def.type = b2_kinematicBody;
            def.position = {w.x, w.y};
            b2BodyId id = b2CreateBody(impl_->world, &def);
            Impl::AddShape(id, registry, e, nullptr);
            it = impl_->characters.emplace(e, id).first;
        }
        const b2Vec2 at = b2Body_GetPosition(it->second);
        b2Body_SetLinearVelocity(it->second, {(w.x - at.x) / std::max(dt, 1e-4f), (w.y - at.y) / std::max(dt, 1e-4f)});
    }
    b2World_Step(impl_->world, dt, 4);
    // Results back: dynamic and kinematic bodies' places and angles.
    for (auto& [e, id] : impl_->bodies) {
        if (!registry.valid(e) || !b2Body_IsValid(id)) continue;
        const auto& body = registry.get<RigidBody2D>(e);
        if (body.type == 0) continue;
        const b2Vec2 p = b2Body_GetPosition(id);
        auto& w = registry.get<WorldTransform2D>(e);
        w.x = p.x;
        w.y = p.y;
        if (auto* sprite = registry.try_get<Sprite>(e)) sprite->angle = b2Rot_GetAngle(b2Body_GetRotation(id)) * 180.0f / 3.14159265f;
    }
}

// --- Debug draw ---------------------------------------------------------------------------------------

std::vector<DebugShape> PhysicsDebugShapes(Registry& registry) {
    std::vector<DebugShape> out;
    for (auto [e, w] : registry.view<WorldTransform2D>().each()) {
        auto shape = ShapeOf(registry, e);
        const bool trigger = registry.all_of<Trigger>(e);
        const auto* body = registry.try_get<RigidBody2D>(e);
        if (!shape && !trigger) continue;
        DebugShape d;
        if (shape) {
            d.circle = shape->circle;
            d.halfWidth = shape->halfWidth;
            d.halfHeight = shape->halfHeight;
            d.radius = shape->radius;
        }
        d.x = w.x;
        d.y = w.y;
        if (trigger && (!shape || registry.get<Trigger>(e).radius > 0.0f)) {
            d.circle = true;
            d.radius = registry.get<Trigger>(e).radius > 0.0f ? registry.get<Trigger>(e).radius : 0.5f;
        }
        if (const auto* sprite = registry.try_get<Sprite>(e)) d.angle = sprite->angle;
        d.kind = trigger ? 1 : body ? (body->type == 0 ? 3 : 2) : 0;
        out.push_back(d);
    }
    return out;
}

} // namespace aige
