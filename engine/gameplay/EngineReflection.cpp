#include "gameplay/EngineReflection.h"

#include "gameplay/Components.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <sstream>

namespace aige {

namespace {

// Script property values in scene text: `n:<number>`, `b:0|1`, `s:<text>`,
// so a scene loads without running the script to learn the types.
std::string EncodeScriptValue(const ScriptValue& value) {
    if (const auto* number = std::get_if<double>(&value)) {
        std::ostringstream out;
        out.precision(15);
        out << "n:" << *number;
        return out.str();
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "b:1" : "b:0";
    }
    return "s:" + std::get<std::string>(value);
}

std::optional<ScriptValue> DecodeScriptValue(const std::string& text) {
    if (text.size() < 2 || text[1] != ':') {
        return std::nullopt;
    }
    std::string payload = text.substr(2);
    try {
        switch (text[0]) {
            case 'n': return ScriptValue(std::stod(payload));
            case 'b': return ScriptValue(payload == "1");
            case 's': return ScriptValue(payload);
            default: return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

const std::vector<std::string> kEffects = {"None", "Burn", "Poison", "Slow", "Regen"};

} // namespace

ProjectSettings& ActiveSettings() {
    static ProjectSettings settings;
    return settings;
}

void RegisterGameplayTypes(ReflectionRegistry& r) {
    // --- Identity & placement -------------------------------------------------

    r.Component<Name>("Name", "A designer-facing name, shown in the Scene Tree and usable from scripts.")
        .Field("value", &Name::value, "The entity's name.");

    r.Component<Tile>("Tile",
                      "A ground cell. Collision comes from tiles: a cell is walkable only if it holds a "
                      "non-solid tile, so empty space is a wall.")
        .Field("gx", &Tile::gx, "Grid column.")
        .Field("gy", &Tile::gy, "Grid row.")
        .Field("solid", &Tile::solid, "Blocks movement (walls, water, hedges).")
        .DependsOn({"ScreenTransform2D"})
        .Structural();

    r.Component<WorldTransform2D>("WorldTransform2D",
                                  "Position in world grid units (1.0 = one tile; cell centers are n + 0.5).")
        .Field("x", &WorldTransform2D::x, "Grid x; increases down-right on screen.")
        .Field("y", &WorldTransform2D::y, "Grid y; increases down-left on screen.")
        .DependsOn({"ScreenTransform2D"});

    r.Component<Velocity2D>("Velocity2D",
                            "Movement this frame, in cells per second. Velocity2D + Collider2D marks the player.")
        .Field("x", &Velocity2D::x, "Cells per second along grid x.")
        .Field("y", &Velocity2D::y, "Cells per second along grid y.")
        .DependsOn({"WorldTransform2D"});

    r.Component<Collider2D>("Collider2D", "Box used for wall collision, centered on the position, in cells.")
        .Field("halfWidth", &Collider2D::halfWidth, "Half the box width, in cells.").Range(0.05, 2.0)
        .Field("halfHeight", &Collider2D::halfHeight, "Half the box height, in cells.").Range(0.05, 2.0)
        .DependsOn({"WorldTransform2D"});

    // --- Combat ------------------------------------------------------------------

    r.Component<Faction>("Faction", "Which side an entity is on. Attacks never hurt members of the same "
                                    "non-neutral team.")
        .Field("team", &Faction::team, "Neutral, Player or Enemy.").Options({"Neutral", "Player", "Enemy"});

    r.Component<Health>("Health", "Hit points. At 0 the entity dies (enemies are removed; the player stays, dead).")
        .Field("current", &Health::current, "Current hit points.").Range(0, 100000)
        .Field("max", &Health::max, "Maximum hit points; healing stops here.").Range(1, 100000);

    r.Component<Stats>("Stats", "Combat numbers. Damage dealt = max(minimum, attack's damage + attack - "
                                "target's defense).")
        .Field("attack", &Stats::attack, "Added to every attack's damage.").Range(-1000, 1000)
        .Field("defense", &Stats::defense, "Subtracted from damage taken.").Range(-1000, 1000)
        .Field("moveSpeed", &Stats::moveSpeed, "Walking speed in cells per second (the player's).").Range(0, 50);

    r.Component<Hurtbox>("Hurtbox", "The area attacks can hit, centered on the position; brief invulnerability "
                                    "after each hit.")
        .Field("halfWidth", &Hurtbox::halfWidth, "Half the area's width, in cells.").Range(0.05, 3.0)
        .Field("halfHeight", &Hurtbox::halfHeight, "Half the area's height, in cells.").Range(0.05, 3.0)
        .Field("invulnerableTime", &Hurtbox::invulnerableTime, "Seconds of invulnerability after being hit.")
        .Range(0, 10)
        .DependsOn({"WorldTransform2D", "Health"});

    r.Component<MeleeAttack>("MeleeAttack",
                             "A close-range swing: a short-lived hit area `reach` cells in front of the attacker, "
                             "optionally applying a status effect on hit.")
        .Field("damage", &MeleeAttack::damage, "Damage per hit (before stats).").Range(0, 100000)
        .Field("reach", &MeleeAttack::reach, "How far in front the hit area appears, in cells.").Range(0, 10)
        .Field("radius", &MeleeAttack::radius, "Half-size of the hit area, in cells.").Range(0.05, 5)
        .Field("cooldown", &MeleeAttack::cooldown, "Seconds between swings.").Range(0, 60)
        .Field("knockback", &MeleeAttack::knockback, "How far a hit pushes the target, in cells.").Range(0, 10)
        .Field("effect", &MeleeAttack::effect, "Status effect applied on hit.").Options(kEffects)
        .Field("effectDuration", &MeleeAttack::effectDuration, "How long the effect lasts, in seconds.")
        .Range(0, 600)
        .Field("effectMagnitude", &MeleeAttack::effectMagnitude,
               "Burn/Poison: damage per tick. Regen: healing per tick. Slow: fraction of speed removed (0-1).")
        .Range(0, 100000)
        .DependsOn({"WorldTransform2D"});

    r.Component<Pickup>("Pickup", "Collected when the player walks over it (left alone at full health).")
        .Field("kind", &Pickup::kind, "What it does.").Options({"Heal"})
        .Field("amount", &Pickup::amount, "Hit points restored.").Range(0, 100000)
        .Field("radius", &Pickup::radius, "Pickup distance, in cells.").Range(0.05, 5)
        .DependsOn({"WorldTransform2D"});

    // --- World & AI ----------------------------------------------------------------

    r.Component<Obstacle>("Obstacle", "Blocks movement through the cell it stands in (trees, rocks, crates).")
        .DependsOn({"WorldTransform2D"});

    r.Component<Sounds>("Sounds", "Sound effects this entity plays.")
        .Field("attack", &Sounds::attack, "When it swings.").As(FieldType::Audio)
        .Field("hurt", &Sounds::hurt, "When it takes damage.").As(FieldType::Audio)
        .Field("death", &Sounds::death, "When it dies.").As(FieldType::Audio)
        .Field("collect", &Sounds::collect, "When it's picked up (pickups).").As(FieldType::Audio);

    r.Component<Follow>("Follow", "Walks toward a target (the player if none is set) while within range, "
                                  "stopping at stopDistance; collides with walls.")
        .Field("target", &Follow::target, "Who to follow; none = the player.")
        .Field("speed", &Follow::speed, "Cells per second.").Range(0, 50)
        .Field("stopDistance", &Follow::stopDistance, "Stops this close to the target, in cells.").Range(0, 50)
        .Field("range", &Follow::range, "Only follows within this distance; 0 = always.").Range(0, 1000)
        .DependsOn({"WorldTransform2D"});

    // --- Rendering -------------------------------------------------------------------

    r.Component<ScreenTransform2D>("ScreenTransform2D",
                                   "Screen position in scene pixels and draw order. Computed from the world "
                                   "position for characters and tiles.")
        .Field("x", &ScreenTransform2D::x, "Pixels from the left.")
        .Field("y", &ScreenTransform2D::y, "Pixels from the top.")
        .Field("depth", &ScreenTransform2D::depth, "Draw order: higher draws in front.")
        .DerivedWhen([](Registry& registry, Entity entity) {
            return registry.any_of<WorldTransform2D, Tile>(entity);
        });

    r.Component<Sprite>("Sprite", "The image drawn for this entity: a texture (or a region of one, like a "
                                  "tileset tile), its size on screen and a color tint.")
        .Field("texture", &Sprite::texture, "Texture asset; empty = built-in placeholder.").As(FieldType::Texture)
        .Field("width", &Sprite::width, "On-screen width in pixels.").Range(1, 4096)
        .Field("height", &Sprite::height, "On-screen height in pixels.").Range(1, 4096)
        .Field("u0", &Sprite::u0, "Texture region left (0-1); greater than u1 mirrors it.").Range(0, 1)
        .Field("v0", &Sprite::v0, "Texture region top (0-1).").Range(0, 1)
        .Field("u1", &Sprite::u1, "Texture region right (0-1).").Range(0, 1)
        .Field("v1", &Sprite::v1, "Texture region bottom (0-1).").Range(0, 1)
        .Field("r", &Sprite::r, "Red tint (0-1).").Range(0, 1)
        .Field("g", &Sprite::g, "Green tint (0-1).").Range(0, 1)
        .Field("b", &Sprite::b, "Blue tint (0-1).").Range(0, 1)
        .Field("a", &Sprite::a, "Opacity (0-1).").Range(0, 1)
        .DependsOn({"ScreenTransform2D"});

    // --- Scripting -------------------------------------------------------------------

    r.Component<LuaScript>("LuaScript", "Lua behavior (see docs/scripting.md). The script's `properties` "
                                        "table becomes per-entity settings.")
        .Field("path", &LuaScript::path, "The .lua file, relative to the project.").As(FieldType::Script)
        .OnSet([](Registry& registry, Entity entity, const FieldValue& value) {
            auto& script = registry.get<LuaScript>(entity);
            script.path = ToString(value);
            script.Unload(); // the new file loads on the next update
        });
    ComponentInfo& script = r.MutableComponent("LuaScript");
    script.writeExtra = [](Registry& registry, Entity entity) {
        std::vector<std::pair<std::string, std::string>> extra;
        for (const auto& [name, value] : registry.get<LuaScript>(entity).properties) {
            extra.emplace_back("prop." + name, QuoteValue(EncodeScriptValue(value)));
        }
        return extra;
    };
    script.readExtra = [](Registry& registry, Entity entity, const std::string& key, const std::string& text) {
        if (key.rfind("prop.", 0) != 0) {
            return false;
        }
        if (auto value = DecodeScriptValue(text)) {
            registry.get<LuaScript>(entity).properties[key.substr(5)] = *value;
        }
        return true;
    };

    // Which prefab an entity was placed from and which fields this copy
    // overrides (comma-separated `Component.field`, `-Component` for removed
    // components). Shown by the Inspector's prefab header, not as a section.
    r.Component<PrefabInstance>("PrefabInstance", "Placed from a prefab.")
        .Field("path", &PrefabInstance::path, "The prefab file.")
        .Field("overrides", &PrefabInstance::overrides, "Fields changed on this copy.")
        .Hidden();

    // --- Project settings -------------------------------------------------------------

    ProjectSettings& settings = ActiveSettings();
    r.Settings("Renderer", "How the game is drawn.", &settings.renderer)
        .Field("clearR", &RendererSettings::clearR, "Background red (0-1).").Range(0, 1)
        .Field("clearG", &RendererSettings::clearG, "Background green (0-1).").Range(0, 1)
        .Field("clearB", &RendererSettings::clearB, "Background blue (0-1).").Range(0, 1);
    r.Settings("Camera", "The play camera.", &settings.camera)
        .Field("followStiffness", &CameraSettings::followStiffness,
               "How quickly the camera catches up with the player; higher is snappier.")
        .Range(0.5, 50)
        .Field("playZoom", &CameraSettings::playZoom, "Zoom while playing.").Range(0.25, 4);
    r.Settings("Combat", "Rules shared by every attack.", &settings.combat)
        .Field("minimumDamage", &CombatSettings::minimumDamage, "A hit never deals less than this.").Range(0, 1000)
        .Field("hitFlashSeconds", &CombatSettings::hitFlashSeconds, "How long a hit sprite flashes red.")
        .Range(0, 2)
        .Field("knockbackScale", &CombatSettings::knockbackScale, "Multiplies every attack's knockback.")
        .Range(0, 10);
    r.Settings("Physics", "Movement and collision.", &settings.physics)
        .Field("playerCollides", &PhysicsSettings::playerCollides, "Off lets the player walk through walls.");
}

ReflectionRegistry& EngineReflection() {
    static ReflectionRegistry registry = [] {
        ReflectionRegistry built;
        RegisterGameplayTypes(built);
        return built;
    }();
    return registry;
}

} // namespace aige
