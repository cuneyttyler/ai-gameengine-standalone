#include "game/SaveGames.h"

#include "assets/SceneIO.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>

namespace aige {

namespace fs = std::filesystem;

namespace {

std::map<int, Entity> ByOrigin(Registry& registry, Entity skip) {
    std::map<int, Entity> out;
    for (auto [entity, origin] : registry.view<SceneOrigin>().each()) {
        if (entity != skip && origin.index >= 0) out[origin.index] = entity;
    }
    return out;
}

bool SameValue(const nlohmann::json& a, const nlohmann::json& b) {
    if (a.is_number() && b.is_number()) return std::abs(a.get<double>() - b.get<double>()) < 1e-4;
    return a == b;
}

// Components whose values are recomputed every frame or are bookkeeping.
bool Persisted(const ComponentInfo& component) {
    return component.name != "ScreenTransform2D" && component.name != "PrefabInstance";
}

} // namespace

nlohmann::json CaptureSceneState(Registry& live, Registry& pristine, const ReflectionRegistry& reflection) {
    const auto livePlayer = FindPlayer(live);
    const auto pristinePlayer = FindPlayer(pristine);
    const auto now = ByOrigin(live, livePlayer.value_or(Entity{entt::null}));
    const auto before = ByOrigin(pristine, pristinePlayer.value_or(Entity{entt::null}));
    // The scene's own player isn't part of the world (the player is saved apart).
    nlohmann::json removed = nlohmann::json::array();
    nlohmann::json changed = nlohmann::json::object();
    nlohmann::json removedComponents = nlohmann::json::object();
    for (const auto& [index, old] : before) {
        auto it = now.find(index);
        if (it == now.end()) {
            removed.push_back(index);
            continue;
        }
        const Entity entity = it->second;
        nlohmann::json diff = nlohmann::json::object();
        nlohmann::json gone = nlohmann::json::array();
        for (const auto& component : reflection.Components()) {
            if (!Persisted(component)) continue;
            const bool had = component.has(pristine, old);
            const bool has = component.has(live, entity);
            if (had && !has) {
                gone.push_back(component.name);
                continue;
            }
            if (!has) continue;
            nlohmann::json fields = nlohmann::json::object();
            for (const auto& field : component.fields) {
                if (field.type == FieldType::EntityRef) continue;
                const nlohmann::json value = ValueToJson(field, field.get(live, entity));
                if (!had || !SameValue(value, ValueToJson(field, field.get(pristine, old)))) fields[field.name] = value;
            }
            if (!had) fields["$added"] = true;
            if (!fields.empty()) diff[component.name] = fields;
        }
        if (!diff.empty()) changed[std::to_string(index)] = diff;
        if (!gone.empty()) removedComponents[std::to_string(index)] = gone;
    }

    // Things made in play (spawned): kept whole, as scene text.
    std::vector<Entity> spawned;
    for (auto entity : live.view<WorldTransform2D>()) {
        if (livePlayer && entity == *livePlayer) continue;
        if (live.all_of<SceneOrigin>(entity) || live.all_of<Dead>(entity)) continue;
        spawned.push_back(entity);
    }
    nlohmann::json state = {{"removed", removed}, {"changed", changed}, {"removedComponents", removedComponents}};
    if (!spawned.empty()) state["spawned"] = SerializeEntities(live, spawned);
    return state;
}

void ApplySceneState(Registry& registry, const nlohmann::json& state, const ReflectionRegistry& reflection) {
    if (!state.is_object()) return;
    const auto player = FindPlayer(registry);
    const auto entities = ByOrigin(registry, player.value_or(Entity{entt::null}));
    for (const auto& index : state.value("removed", nlohmann::json::array())) {
        if (!index.is_number_integer()) continue;
        if (auto it = entities.find(index.get<int>()); it != entities.end() && registry.valid(it->second)) {
            registry.destroy(it->second);
        }
    }
    const nlohmann::json changed = state.value("changed", nlohmann::json::object());
    for (const auto& [key, components] : changed.items()) {
        auto it = entities.find(std::atoi(key.c_str()));
        if (it == entities.end() || !registry.valid(it->second) || !components.is_object()) continue;
        for (const auto& [name, fields] : components.items()) {
            const ComponentInfo* component = reflection.FindComponent(name);
            if (!component || !fields.is_object()) continue;
            if (!component->has(registry, it->second)) AddComponent(reflection, registry, it->second, name);
            if (!component->has(registry, it->second)) continue;
            for (const auto& [fieldName, value] : fields.items()) {
                const FieldInfo* field = component->Field(fieldName);
                if (!field || field->type == FieldType::EntityRef) continue;
                if (auto converted = ValueFromJson(*field, value); converted && !Validate(*field, *converted)) {
                    field->set(registry, it->second, *converted);
                }
            }
        }
    }
    const nlohmann::json removedComponents = state.value("removedComponents", nlohmann::json::object());
    for (const auto& [key, names] : removedComponents.items()) {
        auto it = entities.find(std::atoi(key.c_str()));
        if (it == entities.end() || !registry.valid(it->second) || !names.is_array()) continue;
        for (const auto& name : names) {
            if (name.is_string()) RemoveComponent(reflection, registry, it->second, name.get<std::string>());
        }
    }
    if (state.contains("spawned") && state["spawned"].is_string()) AppendEntities(registry, state["spawned"].get<std::string>());
}

// --- Slots ------------------------------------------------------------------------------

const std::vector<std::string>& SaveGames::SlotNames() {
    static const std::vector<std::string> names = {"quick", "auto", "slot1", "slot2", "slot3", "slot4", "slot5", "slot6"};
    return names;
}

fs::path SaveGames::PathOf(const std::string& slot) const { return dir_ / (slot + ".json"); }

std::optional<nlohmann::json> SaveGames::Read(const std::string& slot) const {
    std::ifstream in(PathOf(slot), std::ios::binary);
    if (!in && slot == "quick") in = std::ifstream(dir_ / "quicksave.json", std::ios::binary); // version 1 (6.9)
    if (!in) return std::nullopt;
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (!json.is_object()) return std::nullopt;
    if (!json.contains("schema")) {
        // A version-1 save: the session's own fields at the top level.
        json = {{"schema", "aige.save/2"}, {"meta", {{"slot", slot}}}, {"session", json}};
    }
    return json;
}

bool SaveGames::Write(const std::string& slot, nlohmann::json save) const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    save["schema"] = "aige.save/2";
    save["meta"]["slot"] = slot;
    save["meta"]["time"] = stamp;
    const fs::path path = PathOf(slot);
    const fs::path temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << save.dump(2) << "\n";
        if (!out) return false;
    }
    fs::rename(temporary, path, ec); // whole or not at all: a crash mid-save keeps the old one
    return !ec;
}

bool SaveGames::Remove(const std::string& slot) const {
    std::error_code ec;
    return fs::remove(PathOf(slot), ec);
}

std::vector<SaveSlotInfo> SaveGames::List() const {
    std::vector<SaveSlotInfo> slots;
    for (const auto& name : SlotNames()) {
        SaveSlotInfo info;
        info.slot = name;
        if (auto save = Read(name)) {
            info.used = true;
            const nlohmann::json meta = save->value("meta", nlohmann::json::object());
            std::string time = meta.value("time", "");
            if (time.empty()) {
                // A version-1 save has no time: the file's.
                std::error_code ec;
                auto path = fs::exists(PathOf(name), ec) ? PathOf(name) : dir_ / "quicksave.json";
                auto written = fs::last_write_time(path, ec);
                if (!ec) {
                    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(written);
                    const auto t = std::chrono::system_clock::to_time_t(sys);
                    std::tm local{};
#ifdef _WIN32
                    localtime_s(&local, &t);
#else
                    localtime_r(&t, &local);
#endif
                    char stamp[32];
                    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
                    time = stamp;
                }
            }
            info.time = time.substr(0, 16);
            info.place = meta.value("place", save->value("session", nlohmann::json::object()).value("scene", ""));
            info.playSeconds = meta.value("playSeconds", 0.0);
        }
        slots.push_back(info);
    }
    return slots;
}

fs::path PlayerSaveDir(const std::string& gameName) {
    const char* base = std::getenv("LOCALAPPDATA");
    const fs::path root = base ? fs::path(base) / "AIGE" / "saves" : fs::temp_directory_path() / "AIGE" / "saves";
    return root / gameName;
}

} // namespace aige
