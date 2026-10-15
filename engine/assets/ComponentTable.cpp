#include "assets/ComponentTable.h"

#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
#include <sstream>
#include <type_traits>

namespace aige {

namespace {

float ToFloat(const FieldValue& value) {
    if (auto* f = std::get_if<float>(&value)) return *f;
    if (auto* i = std::get_if<int>(&value)) return static_cast<float>(*i);
    if (auto* b = std::get_if<bool>(&value)) return *b ? 1.0f : 0.0f;
    return 0.0f;
}

int ToInt(const FieldValue& value) {
    if (auto* i = std::get_if<int>(&value)) return *i;
    if (auto* f = std::get_if<float>(&value)) return static_cast<int>(*f);
    if (auto* b = std::get_if<bool>(&value)) return *b ? 1 : 0;
    return 0;
}

bool ToBool(const FieldValue& value) {
    if (auto* b = std::get_if<bool>(&value)) return *b;
    if (auto* i = std::get_if<int>(&value)) return *i != 0;
    if (auto* f = std::get_if<float>(&value)) return *f != 0.0f;
    return false;
}

std::string ToString(const FieldValue& value) {
    if (auto* s = std::get_if<std::string>(&value)) return *s;
    return {};
}

Entity ToEntity(const FieldValue& value) {
    if (auto* e = std::get_if<Entity>(&value)) return *e;
    return entt::null;
}

// A field bound to a data member. The member's C++ type picks the value
// conversion; `type` says how the editor presents it.
template <typename C, typename M>
FieldDesc Member(const char* name, FieldType type, M C::*member, std::vector<std::string> options = {}) {
    FieldDesc field;
    field.name = name;
    field.type = type;
    field.options = std::move(options);
    field.get = [member](Registry& registry, Entity entity) -> FieldValue {
        const M& value = registry.get<C>(entity).*member;
        // Entity first: entt::entity is itself an enum type.
        if constexpr (std::is_same_v<M, Entity>) {
            return value;
        } else if constexpr (std::is_enum_v<M>) {
            return static_cast<int>(value);
        } else {
            return FieldValue(value);
        }
    };
    field.set = [member](Registry& registry, Entity entity, const FieldValue& value) {
        M& target = registry.get<C>(entity).*member;
        if constexpr (std::is_same_v<M, float>) {
            target = ToFloat(value);
        } else if constexpr (std::is_same_v<M, int>) {
            target = ToInt(value);
        } else if constexpr (std::is_same_v<M, bool>) {
            target = ToBool(value);
        } else if constexpr (std::is_same_v<M, std::string>) {
            target = ToString(value);
        } else if constexpr (std::is_same_v<M, Entity>) {
            target = ToEntity(value);
        } else if constexpr (std::is_enum_v<M>) {
            target = static_cast<M>(ToInt(value));
        }
    };
    return field;
}

template <typename C>
ComponentDesc Component(const char* name, std::vector<FieldDesc> fields, std::vector<std::string> dependencies = {},
                        bool userEditable = true) {
    ComponentDesc desc;
    desc.name = name;
    desc.fields = std::move(fields);
    desc.dependencies = std::move(dependencies);
    desc.userEditable = userEditable;
    desc.has = [](Registry& registry, Entity entity) { return registry.all_of<C>(entity); };
    desc.add = [](Registry& registry, Entity entity) { registry.emplace_or_replace<C>(entity); };
    desc.remove = [](Registry& registry, Entity entity) { registry.remove<C>(entity); };
    desc.derived = [](Registry&, Entity) { return false; };
    return desc;
}

std::vector<ComponentDesc> BuildTable() {
    std::vector<ComponentDesc> table;

    table.push_back(Component<Name>("Name", {Member("value", FieldType::String, &Name::value)}));

    // Structural: a tile is defined by being one, so it isn't added/removed
    // by hand (paint and erase instead), but its fields are editable.
    table.push_back(Component<Tile>("Tile",
                                    {Member("gx", FieldType::Int, &Tile::gx), Member("gy", FieldType::Int, &Tile::gy),
                                     Member("solid", FieldType::Bool, &Tile::solid)},
                                    {"ScreenTransform2D"}, false));

    table.push_back(Component<WorldTransform2D>(
        "WorldTransform2D",
        {Member("x", FieldType::Float, &WorldTransform2D::x), Member("y", FieldType::Float, &WorldTransform2D::y)},
        {"ScreenTransform2D"}));
    table.push_back(Component<Velocity2D>(
        "Velocity2D", {Member("x", FieldType::Float, &Velocity2D::x), Member("y", FieldType::Float, &Velocity2D::y)},
        {"WorldTransform2D"}));
    table.push_back(Component<Collider2D>("Collider2D",
                                          {Member("halfWidth", FieldType::Float, &Collider2D::halfWidth),
                                           Member("halfHeight", FieldType::Float, &Collider2D::halfHeight)},
                                          {"WorldTransform2D"}));

    table.push_back(Component<Faction>(
        "Faction", {Member("team", FieldType::Enum, &Faction::team, {"Neutral", "Player", "Enemy"})}));
    table.push_back(Component<Health>("Health", {Member("current", FieldType::Float, &Health::current),
                                                 Member("max", FieldType::Float, &Health::max)}));
    table.push_back(Component<Stats>("Stats", {Member("attack", FieldType::Float, &Stats::attack),
                                               Member("defense", FieldType::Float, &Stats::defense),
                                               Member("moveSpeed", FieldType::Float, &Stats::moveSpeed)}));
    table.push_back(Component<Hurtbox>("Hurtbox",
                                       {Member("halfWidth", FieldType::Float, &Hurtbox::halfWidth),
                                        Member("halfHeight", FieldType::Float, &Hurtbox::halfHeight),
                                        Member("invulnerableTime", FieldType::Float, &Hurtbox::invulnerableTime)},
                                       {"WorldTransform2D", "Health"}));
    static const std::vector<std::string> kEffects = {"None", "Burn", "Poison", "Slow", "Regen"};
    table.push_back(Component<MeleeAttack>(
        "MeleeAttack",
        {Member("damage", FieldType::Float, &MeleeAttack::damage), Member("reach", FieldType::Float, &MeleeAttack::reach),
         Member("radius", FieldType::Float, &MeleeAttack::radius),
         Member("cooldown", FieldType::Float, &MeleeAttack::cooldown),
         Member("knockback", FieldType::Float, &MeleeAttack::knockback),
         Member("effect", FieldType::Enum, &MeleeAttack::effect, kEffects),
         Member("effectDuration", FieldType::Float, &MeleeAttack::effectDuration),
         Member("effectMagnitude", FieldType::Float, &MeleeAttack::effectMagnitude)},
        {"WorldTransform2D"}));
    table.push_back(Component<Pickup>("Pickup",
                                      {Member("kind", FieldType::Enum, &Pickup::kind, {"Heal"}),
                                       Member("amount", FieldType::Float, &Pickup::amount),
                                       Member("radius", FieldType::Float, &Pickup::radius)},
                                      {"WorldTransform2D"}));
    table.push_back(Component<Follow>("Follow",
                                      {Member("target", FieldType::EntityRef, &Follow::target),
                                       Member("speed", FieldType::Float, &Follow::speed),
                                       Member("stopDistance", FieldType::Float, &Follow::stopDistance),
                                       Member("range", FieldType::Float, &Follow::range)},
                                      {"WorldTransform2D"}));

    ComponentDesc screen = Component<ScreenTransform2D>("ScreenTransform2D",
                                                        {Member("x", FieldType::Float, &ScreenTransform2D::x),
                                                         Member("y", FieldType::Float, &ScreenTransform2D::y),
                                                         Member("depth", FieldType::Float, &ScreenTransform2D::depth)});
    screen.derived = [](Registry& registry, Entity entity) {
        return registry.any_of<WorldTransform2D, Tile>(entity);
    };
    table.push_back(std::move(screen));

    table.push_back(Component<Sprite>(
        "Sprite",
        {Member("texture", FieldType::Texture, &Sprite::texture), Member("width", FieldType::Float, &Sprite::width),
         Member("height", FieldType::Float, &Sprite::height), Member("u0", FieldType::Float, &Sprite::u0),
         Member("v0", FieldType::Float, &Sprite::v0), Member("u1", FieldType::Float, &Sprite::u1),
         Member("v1", FieldType::Float, &Sprite::v1), Member("r", FieldType::Float, &Sprite::r),
         Member("g", FieldType::Float, &Sprite::g), Member("b", FieldType::Float, &Sprite::b),
         Member("a", FieldType::Float, &Sprite::a)},
        {"ScreenTransform2D"}));

    ComponentDesc script = Component<LuaScript>("LuaScript", {Member("path", FieldType::Script, &LuaScript::path)});
    // Changing the path must drop the loaded environment so the new script
    // loads on the next update.
    script.fields[0].set = [](Registry& registry, Entity entity, const FieldValue& value) {
        registry.get<LuaScript>(entity) = LuaScript{ToString(value)};
    };
    table.push_back(std::move(script));

    return table;
}

// Quotes a string value when it would otherwise break the scene format's
// whitespace splitting (see SceneIO's ParseFields).
std::string Quote(const std::string& value) {
    bool needsQuotes = value.empty() || value.find_first_of(" \t\"\\") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + "\"";
}

} // namespace

const FieldDesc* ComponentDesc::Field(const std::string& fieldName) const {
    for (const auto& field : fields) {
        if (field.name == fieldName) {
            return &field;
        }
    }
    return nullptr;
}

const std::vector<ComponentDesc>& Components() {
    static const std::vector<ComponentDesc> table = BuildTable();
    return table;
}

const ComponentDesc* FindComponent(const std::string& name) {
    for (const auto& desc : Components()) {
        if (desc.name == name) {
            return &desc;
        }
    }
    return nullptr;
}

bool AddComponent(Registry& registry, Entity entity, const std::string& name) {
    const ComponentDesc* desc = FindComponent(name);
    if (desc == nullptr || !registry.valid(entity) || desc->has(registry, entity)) {
        return false;
    }
    for (const auto& required : desc->dependencies) {
        const ComponentDesc* dependency = FindComponent(required);
        if (dependency != nullptr && !dependency->has(registry, entity)) {
            AddComponent(registry, entity, required);
        }
    }
    desc->add(registry, entity);
    return true;
}

std::optional<std::string> RemoveComponent(Registry& registry, Entity entity, const std::string& name) {
    const ComponentDesc* desc = FindComponent(name);
    if (desc == nullptr || !registry.valid(entity) || !desc->has(registry, entity)) {
        return name + " isn't on this entity";
    }
    for (const auto& other : Components()) {
        if (other.has(registry, entity) &&
            std::find(other.dependencies.begin(), other.dependencies.end(), name) != other.dependencies.end()) {
            return name + " is required by " + other.name;
        }
    }
    desc->remove(registry, entity);
    return std::nullopt;
}

std::string FormatField(const FieldDesc& field, const FieldValue& value,
                        const std::function<std::optional<size_t>(Entity)>& entityIndex) {
    switch (field.type) {
        case FieldType::Float: {
            std::ostringstream out;
            out.precision(9); // enough significant digits to round-trip an IEEE 754 float exactly
            out << ToFloat(value);
            return out.str();
        }
        case FieldType::Int:
            return std::to_string(ToInt(value));
        case FieldType::Bool:
            return ToBool(value) ? "1" : "0";
        case FieldType::Enum: {
            int index = ToInt(value);
            return index >= 0 && index < static_cast<int>(field.options.size()) ? field.options[index]
                                                                                 : std::to_string(index);
        }
        case FieldType::EntityRef: {
            Entity target = ToEntity(value);
            auto index = target == entt::null || !entityIndex ? std::nullopt : entityIndex(target);
            return index ? "#" + std::to_string(*index) : "none";
        }
        case FieldType::String:
        case FieldType::Texture:
        case FieldType::Script:
            return Quote(ToString(value));
    }
    return {};
}

std::optional<FieldValue> ParseField(const FieldDesc& field, const std::string& text,
                                     std::optional<size_t>* refIndex) {
    try {
        switch (field.type) {
            case FieldType::Float:
                return FieldValue(std::stof(text));
            case FieldType::Int:
                return FieldValue(std::stoi(text));
            case FieldType::Bool:
                return FieldValue(text == "1" || text == "true");
            case FieldType::Enum: {
                for (size_t i = 0; i < field.options.size(); ++i) {
                    if (field.options[i] == text) {
                        return FieldValue(static_cast<int>(i));
                    }
                }
                return FieldValue(std::stoi(text)); // numeric fallback
            }
            case FieldType::EntityRef:
                if (text.size() > 1 && text[0] == '#') {
                    if (refIndex) {
                        *refIndex = static_cast<size_t>(std::stoul(text.substr(1)));
                    }
                }
                return FieldValue(Entity{entt::null});
            case FieldType::String:
            case FieldType::Texture:
            case FieldType::Script:
                return FieldValue(text);
        }
    } catch (const std::exception&) {
        // Malformed number: the caller keeps the default.
    }
    return std::nullopt;
}

} // namespace aige
