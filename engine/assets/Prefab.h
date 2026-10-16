#pragma once

#include "assets/Project.h"
#include "ecs/Registry.h"
#include "reflection/Reflection.h"

#include <filesystem>
#include <functional>
#include <map>
#include <variant>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

// Prefabs: reusable entity templates stored as `<project>/prefabs/*.prefab`
// (one entity, in the scene file format). Every placeable thing in a project
// — a grass tile, a wall block, a skeleton, a potion — is a prefab, so the
// editor's palette is data, not code.
//
// A placed copy carries a PrefabInstance component naming its prefab and
// listing its *overrides*: fields changed on that copy. Syncing copies every
// non-overridden field from the prefab to the instance, so editing a prefab
// updates all copies except where a copy was deliberately changed. Fields
// that describe where a copy is (position, grid cell) or point at other
// entities are never synced (IsInstanceLocal in ComponentTable.h).
//
// Override keys: `Component.field`, `LuaScript.prop.<name>` for script
// properties, and `-Component` for a prefab component removed from the copy.

struct PrefabInfo {
    std::string path; // project-relative, e.g. "prefabs/skeleton.prefab"
    std::string name; // display name: the file stem
    std::string error;
    bool isTile = false;   // painted onto grid cells rather than placed
    bool isPlayer = false; // there's only ever one player
    // For palette thumbnails.
    std::string texture;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    float width = 0.0f, height = 0.0f;
};

class PrefabLibrary {
public:
    explicit PrefabLibrary(Project project);
    ~PrefabLibrary();

    // Rescans prefabs/, reloading files changed on disk. Returns the paths
    // whose content changed (so their instances can be re-synced).
    std::vector<std::string> Refresh();

    const std::vector<PrefabInfo>& Prefabs() const { return infos_; }
    const PrefabInfo* Find(const std::string& path) const;

    // Places a copy at a world point: characters and props stand there;
    // tile prefabs snap to the cell under it, replacing that cell's tile.
    std::optional<Entity> Instantiate(Registry& registry, const std::string& path, float worldX, float worldY);

    // Copies the prefab's values into one instance / every instance (of
    // `path`, or of all prefabs if empty), respecting overrides.
    void Sync(Registry& registry, Entity instance);
    void SyncAll(Registry& registry, const std::string& path = {});

    // Writes `entity` as a prefab at `path` ("prefabs/name.prefab"; position
    // and references are stripped) and makes the entity an instance of it
    // with no overrides. Overwrites an existing prefab — that's how "apply
    // instance changes to the prefab" works.
    bool SaveAsPrefab(Registry& registry, Entity entity, const std::string& path, std::string* error = nullptr);

    // Calls `visit` with the prefab's template entity (read-only use), e.g. to
    // describe its values; false if the prefab can't be loaded.
    bool VisitTemplate(const std::string& path, const std::function<void(Registry&, Entity)>& visit);

    // Changes one field of the prefab itself (rewriting its file); copies pick
    // it up on the next Sync. Returns why not if it can't.
    std::optional<std::string> SetTemplateField(const std::string& path, const std::string& component,
                                                const std::string& field, const FieldValue& value);

    // Deletes a prefab (development_plan.md 4.3). Its copies in `registry`
    // stay, as ordinary entities (no longer linked). Returns how many copies
    // were unlinked, or why not.
    std::variant<int, std::string> Remove(Registry& registry, const std::string& path);

    // Drops one override (the field takes the prefab's value again) or all.
    void Revert(Registry& registry, Entity instance, const std::string& overrideKey);
    void RevertAll(Registry& registry, Entity instance);

private:
    struct Template;
    Template* Load(const std::string& path);

    Project project_;
    std::map<std::string, std::unique_ptr<Template>> templates_;
    std::vector<PrefabInfo> infos_;
};

// Override bookkeeping on a PrefabInstance's comma-separated list.
std::set<std::string> ParseOverrides(const std::string& overrides);
bool HasOverride(Registry& registry, Entity entity, const std::string& key);
void AddOverride(Registry& registry, Entity entity, const std::string& key);

// Records that a field was changed on a prefab copy (so prefab updates leave
// it alone); empty `field` records the component as removed. No-op for
// entities that aren't prefab copies and for per-copy fields like position.
void RecordOverride(Registry& registry, Entity entity, const std::string& component, const std::string& field);

} // namespace aige
