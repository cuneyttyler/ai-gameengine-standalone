#include "assets/Prefab.h"

#include "assets/SceneIO.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Components.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

struct PrefabLibrary::Template {
    Registry registry;
    Entity entity = entt::null;
    fs::file_time_type mtime{};
    std::string error;
};

namespace {

std::string JoinOverrides(const std::set<std::string>& overrides) {
    std::string joined;
    for (const auto& key : overrides) {
        joined += (joined.empty() ? "" : ",") + key;
    }
    return joined;
}

// Copies one component's values from `src` to `dst` (adding it if needed),
// skipping instance-local fields and `skip` (override keys).
void CopyComponent(const ComponentInfo& component, Registry& src, Entity srcEntity, Registry& dst, Entity dstEntity,
                   const std::set<std::string>& skip, bool copyInstanceLocal) {
    if (!component.has(dst, dstEntity)) {
        component.add(dst, dstEntity);
    }
    for (const auto& field : component.fields) {
        if (field.type == FieldType::EntityRef) {
            continue; // references can't cross registries
        }
        if ((!copyInstanceLocal && IsInstanceLocal(component.name, field)) ||
            skip.count(component.name + "." + field.name)) {
            continue;
        }
        field.set(dst, dstEntity, field.get(src, srcEntity));
    }
    // Script properties: the prefab's set, plus this copy's overridden ones.
    if (component.name == "LuaScript") {
        auto& to = dst.get<LuaScript>(dstEntity).properties;
        const auto& from = src.get<LuaScript>(srcEntity).properties;
        std::map<std::string, ScriptValue> merged = from;
        for (const auto& [name, value] : to) {
            if (skip.count("LuaScript.prop." + name)) {
                merged[name] = value;
            }
        }
        to = std::move(merged);
    }
}

} // namespace

std::set<std::string> ParseOverrides(const std::string& overrides) {
    std::set<std::string> keys;
    std::stringstream stream(overrides);
    std::string key;
    while (std::getline(stream, key, ',')) {
        if (!key.empty()) {
            keys.insert(key);
        }
    }
    return keys;
}

bool HasOverride(Registry& registry, Entity entity, const std::string& key) {
    const auto* instance = registry.try_get<PrefabInstance>(entity);
    return instance != nullptr && ParseOverrides(instance->overrides).count(key) > 0;
}

void AddOverride(Registry& registry, Entity entity, const std::string& key) {
    if (auto* instance = registry.try_get<PrefabInstance>(entity)) {
        auto keys = ParseOverrides(instance->overrides);
        keys.insert(key);
        instance->overrides = JoinOverrides(keys);
    }
}

void RecordOverride(Registry& registry, Entity entity, const std::string& component, const std::string& field) {
    if (!registry.all_of<PrefabInstance>(entity)) {
        return;
    }
    if (field.empty()) {
        AddOverride(registry, entity, "-" + component);
        return;
    }
    const ComponentInfo* info = EngineReflection().FindComponent(component);
    const FieldInfo* fieldInfo = info ? info->Field(field) : nullptr;
    if (fieldInfo != nullptr && !IsInstanceLocal(component, *fieldInfo)) {
        AddOverride(registry, entity, component + "." + field);
    }
}

PrefabLibrary::PrefabLibrary(Project project) : project_(std::move(project)) {}
PrefabLibrary::~PrefabLibrary() = default;

PrefabLibrary::Template* PrefabLibrary::Load(const std::string& path) {
    fs::path file = project_.Resolve(path);
    std::error_code ec;
    auto mtime = fs::last_write_time(file, ec);
    if (ec) {
        return nullptr;
    }
    auto& slot = templates_[path];
    if (slot && slot->mtime == mtime) {
        return slot.get();
    }
    auto loaded = std::make_unique<Template>();
    loaded->mtime = mtime;
    std::ifstream in(file);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto entities = AppendEntities(loaded->registry, text);
    if (entities.size() != 1) {
        loaded->error = "a prefab must contain exactly one entity (found " + std::to_string(entities.size()) + ")";
    } else {
        loaded->entity = entities.front();
    }
    slot = std::move(loaded);
    return slot.get();
}

std::vector<std::string> PrefabLibrary::Refresh() {
    std::vector<std::string> changed;
    std::vector<PrefabInfo> infos;
    std::error_code ec;
    fs::path dir = project_.root / "prefabs";
    if (fs::exists(dir, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".prefab") {
                continue;
            }
            std::string path = project_.Relative(entry.path());
            auto previous = templates_.count(path) ? templates_[path]->mtime : fs::file_time_type{};
            Template* prefab = Load(path);
            if (prefab == nullptr) {
                continue;
            }
            if (prefab->mtime != previous) {
                changed.push_back(path);
            }

            PrefabInfo info;
            info.path = path;
            info.name = entry.path().stem().string();
            info.error = prefab->error;
            if (prefab->error.empty()) {
                Registry& r = prefab->registry;
                info.isTile = r.all_of<Tile>(prefab->entity);
                info.isPlayer = r.all_of<Velocity2D, Collider2D>(prefab->entity);
                info.kind = info.isTile                          ? "tile"
                            : r.all_of<Health>(prefab->entity)   ? "character"
                            : r.all_of<Pickup>(prefab->entity)   ? "item"
                                                                 : "other";
                if (const auto* sprite = r.try_get<Sprite>(prefab->entity)) {
                    info.flat = sprite->flat;
                    info.texture = sprite->texture;
                    info.u0 = sprite->u0;
                    info.v0 = sprite->v0;
                    info.u1 = sprite->u1;
                    info.v1 = sprite->v1;
                    info.width = sprite->width;
                    info.height = sprite->height;
                }
            }
            infos.push_back(std::move(info));
        }
    }
    // Palette order: tiles first (the ground you paint), then everything you
    // place; alphabetical within each. Number keys 1-9 follow this order.
    std::sort(infos.begin(), infos.end(), [](const PrefabInfo& a, const PrefabInfo& b) {
        return a.isTile != b.isTile ? a.isTile : a.path < b.path;
    });
    infos_ = std::move(infos);
    return changed;
}

const PrefabInfo* PrefabLibrary::Find(const std::string& path) const {
    for (const auto& info : infos_) {
        if (info.path == path) {
            return &info;
        }
    }
    return nullptr;
}

std::optional<Entity> PrefabLibrary::Instantiate(Registry& registry, const std::string& path, float worldX,
                                                 float worldY, bool replaceTile) {
    Template* prefab = Load(path);
    if (prefab == nullptr || !prefab->error.empty()) {
        return std::nullopt;
    }
    Registry& source = prefab->registry;
    const bool isTile = source.all_of<Tile>(prefab->entity);
    const int gx = static_cast<int>(std::floor(worldX));
    const int gy = static_cast<int>(std::floor(worldY));
    if (isTile && replaceTile) {
        Entity existing = TileGrid::Build(registry).At(gx, gy);
        if (existing != entt::null) {
            registry.destroy(existing); // a cell holds one tile
        }
    }

    Entity entity = registry.create();
    for (const auto& component : Components()) {
        if (component.has(source, prefab->entity)) {
            CopyComponent(component, source, prefab->entity, registry, entity, {}, false);
        }
    }
    registry.emplace_or_replace<PrefabInstance>(entity, path, "");

    if (isTile) {
        auto& tile = registry.get<Tile>(entity);
        tile.gx = gx;
        tile.gy = gy;
    } else if (auto* world = registry.try_get<WorldTransform2D>(entity)) {
        world->x = worldX;
        world->y = worldY;
    }
    return entity;
}

void PrefabLibrary::Sync(Registry& registry, Entity instance) {
    const auto* link = registry.try_get<PrefabInstance>(instance);
    if (link == nullptr) {
        return;
    }
    Template* prefab = Load(link->path);
    if (prefab == nullptr || !prefab->error.empty()) {
        return; // missing or broken prefab: keep the copy as it is
    }
    auto overrides = ParseOverrides(link->overrides);
    for (const auto& component : Components()) {
        if (component.name == "PrefabInstance" || !component.has(prefab->registry, prefab->entity) ||
            overrides.count("-" + component.name)) {
            continue;
        }
        CopyComponent(component, prefab->registry, prefab->entity, registry, instance, overrides, false);
    }
}

void PrefabLibrary::SyncAll(Registry& registry, const std::string& path) {
    std::vector<Entity> instances;
    for (auto [entity, link] : registry.view<PrefabInstance>().each()) {
        if (path.empty() || link.path == path) {
            instances.push_back(entity);
        }
    }
    for (auto entity : instances) {
        Sync(registry, entity);
    }
}

bool PrefabLibrary::SaveAsPrefab(Registry& registry, Entity entity, const std::string& path, std::string* error) {
    if (!registry.valid(entity)) {
        if (error) *error = "no such entity";
        return false;
    }
    // Copy into a scratch registry, then strip what belongs to this copy.
    Registry scratch;
    Entity copy = scratch.create();
    for (const auto& component : Components()) {
        if (component.name != "PrefabInstance" && component.has(registry, entity)) {
            CopyComponent(component, registry, entity, scratch, copy, {}, true);
        }
    }
    if (auto* world = scratch.try_get<WorldTransform2D>(copy)) {
        *world = {};
    }
    if (auto* tile = scratch.try_get<Tile>(copy)) {
        tile->gx = 0;
        tile->gy = 0;
    }
    if (auto* screen = scratch.try_get<ScreenTransform2D>(copy)) {
        *screen = {};
    }

    fs::path file = project_.Resolve(path);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    if (!out) {
        if (error) *error = "can't write " + file.string();
        return false;
    }
    out << SerializeEntities(scratch, {copy});
    out.close();

    registry.emplace_or_replace<PrefabInstance>(entity, path, "");
    // Drop the cached copy explicitly: two saves within one file-clock tick
    // can leave the modification time unchanged.
    templates_.erase(path);
    Refresh();
    return true;
}

std::optional<std::string> PrefabLibrary::SetTemplateField(const std::string& path, const std::string& component,
                                                           const std::string& field, const FieldValue& value) {
    Template* prefab = Load(path);
    if (prefab == nullptr || !prefab->error.empty()) {
        return "no prefab " + path;
    }
    const ComponentInfo* info = EngineReflection().FindComponent(component);
    const FieldInfo* fieldInfo = info ? info->Field(field) : nullptr;
    if (fieldInfo == nullptr) {
        return "unknown field " + component + "." + field;
    }
    if (!info->has(prefab->registry, prefab->entity)) {
        return path + " has no " + component;
    }
    if (auto error = Validate(*fieldInfo, value)) {
        return *error;
    }
    fieldInfo->set(prefab->registry, prefab->entity, value);
    std::ofstream out(project_.Resolve(path), std::ios::trunc);
    if (!out) {
        return "can't write " + path;
    }
    out << SerializeEntities(prefab->registry, {prefab->entity});
    out.close();
    templates_.erase(path); // reload from disk next time (and report it changed)
    Refresh();
    return std::nullopt;
}

bool PrefabLibrary::VisitTemplate(const std::string& path, const std::function<void(Registry&, Entity)>& visit) {
    Template* prefab = Load(path);
    if (prefab == nullptr || !prefab->error.empty()) {
        return false;
    }
    visit(prefab->registry, prefab->entity);
    return true;
}

void PrefabLibrary::Revert(Registry& registry, Entity instance, const std::string& overrideKey) {
    if (auto* link = registry.try_get<PrefabInstance>(instance)) {
        auto keys = ParseOverrides(link->overrides);
        keys.erase(overrideKey);
        link->overrides = JoinOverrides(keys);
        Sync(registry, instance);
    }
}

std::variant<int, std::string> PrefabLibrary::Remove(Registry& registry, const std::string& path) {
    if (Find(path) == nullptr) {
        return "no prefab " + path;
    }
    std::error_code ec;
    std::filesystem::remove(project_.Resolve(path), ec);
    if (ec) {
        return "couldn't delete " + path + ": " + ec.message();
    }
    std::vector<Entity> copies;
    for (auto [entity, instance] : registry.view<PrefabInstance>().each()) {
        if (instance.path == path) copies.push_back(entity);
    }
    for (Entity entity : copies) registry.remove<PrefabInstance>(entity);
    templates_.erase(path);
    Refresh();
    return static_cast<int>(copies.size());
}

void PrefabLibrary::RevertAll(Registry& registry, Entity instance) {
    if (auto* link = registry.try_get<PrefabInstance>(instance)) {
        link->overrides.clear();
        Sync(registry, instance);
    }
}

} // namespace aige
