#include "records/Records.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

// Keys a record file may hold besides its type's fields.
bool IsReservedKey(const std::string& key) { return key == "base" || key == "provenance"; }

bool IsAssetType(FieldType type) {
    return type == FieldType::Texture || type == FieldType::Audio || type == FieldType::Script ||
           type == FieldType::Prefab;
}

std::string Describe(const nlohmann::json& value) {
    std::string text = value.dump();
    return text.size() > 40 ? text.substr(0, 37) + "..." : text;
}

using Exists = std::function<bool(const std::string&, const std::string&)>;

void CheckValue(const RecordField& field, const nlohmann::json& value, const std::string& where, const Exists& exists,
                std::vector<std::string>& out);

void CheckObjectFields(const std::vector<RecordField>& fields, const nlohmann::json& object, const std::string& where,
                       const Exists& exists, std::vector<std::string>& out, bool allowReserved) {
    for (const auto& [key, value] : object.items()) {
        if (allowReserved && IsReservedKey(key)) continue;
        auto it = std::find_if(fields.begin(), fields.end(), [&](const RecordField& f) { return f.name == key; });
        if (it == fields.end()) {
            out.push_back((where.empty() ? "" : where + ".") + key + ": unknown field");
            continue;
        }
        CheckValue(*it, value, where.empty() ? key : where + "." + key, exists, out);
    }
}

void CheckSingle(const RecordField& field, const nlohmann::json& value, const std::string& where, const Exists& exists,
                 std::vector<std::string>& out) {
    switch (field.kind) {
        case RecordFieldKind::Record:
            if (!value.is_string()) {
                out.push_back(where + ": expected the id of a " + field.recordType + ", got " + Describe(value));
            } else if (!value.get<std::string>().empty()) {
                const std::string id = value.get<std::string>();
                if (!IsRecordId(id)) {
                    out.push_back(where + ": '" + id + "' isn't a valid record id");
                } else if (exists && !exists(field.recordType, id)) {
                    out.push_back(where + ": no " + field.recordType + " '" + id + "'");
                }
            }
            return;
        case RecordFieldKind::Object:
            if (!value.is_object()) {
                out.push_back(where + ": expected an object, got " + Describe(value));
                return;
            }
            CheckObjectFields(field.fields, value, where, exists, out, false);
            for (const auto& sub : field.fields) {
                if (sub.required && !value.contains(sub.name)) out.push_back(where + "." + sub.name + ": required");
            }
            return;
        case RecordFieldKind::Value:
            break;
    }
    switch (field.type) {
        case FieldType::Float:
        case FieldType::Int: {
            if (!value.is_number()) {
                out.push_back(where + ": expected a number, got " + Describe(value));
                return;
            }
            const double number = value.get<double>();
            if (field.type == FieldType::Int && std::floor(number) != number) {
                out.push_back(where + ": expected a whole number, got " + Describe(value));
                return;
            }
            std::ostringstream bound;
            if (field.min && number < *field.min) {
                bound << where << ": " << number << " is below the minimum " << *field.min;
                out.push_back(bound.str());
            } else if (field.max && number > *field.max) {
                bound << where << ": " << number << " is above the maximum " << *field.max;
                out.push_back(bound.str());
            }
            return;
        }
        case FieldType::Bool:
            if (!value.is_boolean()) out.push_back(where + ": expected true or false, got " + Describe(value));
            return;
        case FieldType::Enum:
            if (!value.is_string() ||
                std::find(field.options.begin(), field.options.end(), value.get<std::string>()) == field.options.end()) {
                std::string options;
                for (const auto& option : field.options) options += (options.empty() ? "" : ", ") + option;
                out.push_back(where + ": " + Describe(value) + " isn't one of " + options);
            }
            return;
        default:
            if (!value.is_string()) out.push_back(where + ": expected text, got " + Describe(value));
            return;
    }
}

void CheckValue(const RecordField& field, const nlohmann::json& value, const std::string& where, const Exists& exists,
                std::vector<std::string>& out) {
    if (field.list) {
        if (!value.is_array()) {
            out.push_back(where + ": expected a list, got " + Describe(value));
            return;
        }
        for (size_t i = 0; i < value.size(); ++i) {
            CheckSingle(field, value[i], where + "[" + std::to_string(i) + "]", exists, out);
        }
        return;
    }
    CheckSingle(field, value, where, exists, out);
}

void CollectFrom(const RecordField& field, const nlohmann::json& value,
                 std::vector<std::pair<std::string, std::string>>& out) {
    auto one = [&](const nlohmann::json& v) {
        if (field.kind == RecordFieldKind::Record && v.is_string() && !v.get<std::string>().empty()) {
            out.emplace_back(field.recordType, v.get<std::string>());
        } else if (field.kind == RecordFieldKind::Object && v.is_object()) {
            for (const auto& sub : field.fields) {
                if (v.contains(sub.name)) CollectFrom(sub, v[sub.name], out);
            }
        }
    };
    if (field.list && value.is_array()) {
        for (const auto& item : value) one(item);
    } else if (!field.list) {
        one(value);
    }
}

// Replaces references to type/from with `to` in place; true when anything changed.
bool RewriteIn(const RecordField& field, nlohmann::json& value, const std::string& type, const std::string& from,
               const std::string& to) {
    bool changed = false;
    auto one = [&](nlohmann::json& v) {
        if (field.kind == RecordFieldKind::Record && field.recordType == type && v.is_string() &&
            v.get<std::string>() == from) {
            v = to;
            changed = true;
        } else if (field.kind == RecordFieldKind::Object && v.is_object()) {
            for (const auto& sub : field.fields) {
                if (v.contains(sub.name)) changed |= RewriteIn(sub, v[sub.name], type, from, to);
            }
        }
    };
    if (field.list && value.is_array()) {
        for (auto& item : value) one(item);
    } else if (!field.list) {
        one(value);
    }
    return changed;
}

void CollectAssets(const RecordField& field, const nlohmann::json& value, const std::string& where,
                   std::vector<std::pair<std::string, std::string>>& out) {
    auto one = [&](const nlohmann::json& v, const std::string& at) {
        if (field.kind == RecordFieldKind::Value && IsAssetType(field.type) && v.is_string() &&
            !v.get<std::string>().empty()) {
            out.emplace_back(at, v.get<std::string>());
        } else if (field.kind == RecordFieldKind::Object && v.is_object()) {
            for (const auto& sub : field.fields) {
                if (v.contains(sub.name)) CollectAssets(sub, v[sub.name], at + "." + sub.name, out);
            }
        }
    };
    if (field.list && value.is_array()) {
        for (size_t i = 0; i < value.size(); ++i) one(value[i], where + "[" + std::to_string(i) + "]");
    } else if (!field.list) {
        one(value, where);
    }
}

std::optional<nlohmann::json> ReadJson(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "can't be read";
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(in);
    } catch (const std::exception& e) {
        error = std::string("isn't valid JSON: ") + e.what();
        return std::nullopt;
    }
}

} // namespace

bool IsRecordId(const std::string& id) {
    if (id.empty() || id.size() > 96) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

nlohmann::json DefaultValue(const RecordField& field) {
    if (!field.defaultValue.is_null()) return field.defaultValue;
    if (field.list) return nlohmann::json::array();
    switch (field.kind) {
        case RecordFieldKind::Record: return "";
        case RecordFieldKind::Object: {
            nlohmann::json object = nlohmann::json::object();
            for (const auto& sub : field.fields) object[sub.name] = DefaultValue(sub);
            return object;
        }
        case RecordFieldKind::Value: break;
    }
    switch (field.type) {
        case FieldType::Float: return field.min ? *field.min : 0.0;
        case FieldType::Int: return field.min ? static_cast<int>(*field.min) : 0;
        case FieldType::Bool: return false;
        case FieldType::Enum: return field.options.empty() ? "" : field.options.front();
        default: return "";
    }
}

nlohmann::json DefaultRecord(const RecordTypeInfo& type) {
    nlohmann::json record = nlohmann::json::object();
    for (const auto& field : type.fields) record[field.name] = DefaultValue(field);
    return record;
}

std::vector<std::string> ValidateRecord(const RecordTypeInfo& type, const nlohmann::json& data, const Exists& exists,
                                        const nlohmann::json* resolved) {
    std::vector<std::string> problems;
    if (!data.is_object()) {
        problems.push_back("a record is a JSON object");
        return problems;
    }
    if (data.contains("base") && (!data["base"].is_string() || !IsRecordId(data["base"].get<std::string>()))) {
        problems.push_back("base: expected the id of another " + type.name);
    }
    if (data.contains("provenance") && !data["provenance"].is_object()) {
        problems.push_back("provenance: expected an object");
    }
    CheckObjectFields(type.fields, data, "", exists, problems, true);
    const nlohmann::json& complete = resolved ? *resolved : data;
    for (const auto& field : type.fields) {
        if (!field.required) continue;
        if (!complete.contains(field.name) ||
            (complete[field.name].is_string() && complete[field.name].get<std::string>().empty())) {
            problems.push_back(field.name + ": required");
        }
    }
    return problems;
}

void CollectReferences(const RecordTypeInfo& type, const nlohmann::json& data,
                       std::vector<std::pair<std::string, std::string>>& out) {
    if (!data.is_object()) return;
    for (const auto& field : type.fields) {
        if (data.contains(field.name)) CollectFrom(field, data[field.name], out);
    }
}

// --- The database -------------------------------------------------------------------------

void RecordDatabase::Open(const ReflectionRegistry& reflection, const fs::path& projectRoot) {
    reflection_ = &reflection;
    root_ = projectRoot;
    Refresh();
}

void RecordDatabase::Close() {
    reflection_ = nullptr;
    root_.clear();
    records_.clear();
    loadProblems_.clear();
}

void RecordDatabase::Refresh() {
    records_.clear();
    loadProblems_.clear();
    if (!reflection_) return;
    const fs::path folder = root_ / "records";
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return;
    for (const auto& typeDir : fs::directory_iterator(folder, ec)) {
        if (!typeDir.is_directory()) continue;
        const std::string typeName = typeDir.path().filename().string();
        const RecordTypeInfo* type = reflection_->FindRecordType(typeName);
        for (const auto& file : fs::directory_iterator(typeDir.path(), ec)) {
            if (!file.is_regular_file() || file.path().extension() != ".json") continue;
            const std::string id = file.path().stem().string();
            const std::string relative = "records/" + typeName + "/" + file.path().filename().string();
            if (type == nullptr) {
                loadProblems_.push_back({relative, "records/" + typeName + "/ isn't a known record type"});
                break;
            }
            if (!IsRecordId(id)) {
                loadProblems_.push_back({relative, "'" + id + "' isn't a valid record id (a-z, 0-9, - and _)"});
                continue;
            }
            std::string error;
            auto data = ReadJson(file.path(), error);
            if (!data) {
                loadProblems_.push_back({typeName + "/" + id, relative + " " + error});
                continue;
            }
            Record record;
            record.type = typeName;
            record.id = id;
            record.path = relative;
            record.data = std::move(*data);
            records_[typeName][id] = std::move(record);
        }
    }
    Resolve();
}

nlohmann::json RecordDatabase::ResolveOne(const std::string& type, const std::string& id,
                                          std::vector<std::string>& chain) const {
    const RecordTypeInfo* info = reflection_->FindRecordType(type);
    const Record* record = Find(type, id);
    if (!info || !record || !record->data.is_object()) return info ? DefaultRecord(*info) : nlohmann::json::object();
    nlohmann::json resolved;
    const std::string base = record->data.value("base", "");
    if (!base.empty() && Find(type, base) && std::find(chain.begin(), chain.end(), base) == chain.end()) {
        chain.push_back(base);
        resolved = ResolveOne(type, base, chain);
    } else {
        resolved = DefaultRecord(*info);
    }
    for (const auto& [key, value] : record->data.items()) {
        if (!IsReservedKey(key) && info->Field(key)) resolved[key] = value;
    }
    return resolved;
}

void RecordDatabase::Resolve() {
    for (auto& [type, byId] : records_) {
        for (auto& [id, record] : byId) {
            std::vector<std::string> chain = {id};
            record.resolved = ResolveOne(type, id, chain);
        }
    }
}

const Record* RecordDatabase::Find(const std::string& type, const std::string& id) const {
    auto t = records_.find(type);
    if (t == records_.end()) return nullptr;
    auto r = t->second.find(id);
    return r == t->second.end() ? nullptr : &r->second;
}

std::vector<const Record*> RecordDatabase::OfType(const std::string& type) const {
    std::vector<const Record*> out;
    auto t = records_.find(type);
    if (t == records_.end()) return out;
    for (const auto& [id, record] : t->second) out.push_back(&record);
    return out;
}

size_t RecordDatabase::Count() const {
    size_t count = 0;
    for (const auto& [type, byId] : records_) count += byId.size();
    return count;
}

std::vector<RecordProblem> RecordDatabase::Problems() const {
    std::vector<RecordProblem> problems = loadProblems_;
    if (!reflection_) return problems;
    auto exists = [this](const std::string& type, const std::string& id) { return Find(type, id) != nullptr; };
    for (const auto& [type, byId] : records_) {
        const RecordTypeInfo* info = reflection_->FindRecordType(type);
        if (!info) continue;
        for (const auto& [id, record] : byId) {
            for (const auto& message : ValidateRecord(*info, record.data, exists, &record.resolved)) {
                problems.push_back({record.Key(), message});
            }
            // The base chain: it must exist and must not lead back here.
            std::string base = record.data.is_object() ? record.data.value("base", "") : "";
            std::vector<std::string> seen = {id};
            while (!base.empty()) {
                const Record* parent = Find(type, base);
                if (!parent) {
                    problems.push_back({record.Key(), "base: no " + type + " '" + base + "'"});
                    break;
                }
                if (std::find(seen.begin(), seen.end(), base) != seen.end()) {
                    problems.push_back({record.Key(), "base: '" + base + "' leads back to this record"});
                    break;
                }
                seen.push_back(base);
                base = parent->data.is_object() ? parent->data.value("base", "") : "";
            }
            // Asset paths that don't exist.
            std::vector<std::pair<std::string, std::string>> assets;
            if (record.data.is_object()) {
                for (const auto& field : info->fields) {
                    if (record.data.contains(field.name)) CollectAssets(field, record.data[field.name], field.name, assets);
                }
            }
            for (const auto& [where, path] : assets) {
                std::error_code ec;
                if (path.find("..") != std::string::npos || !fs::exists(root_ / path, ec)) {
                    problems.push_back({record.Key(), where + ": " + path + " doesn't exist"});
                }
            }
        }
    }
    return problems;
}

std::optional<std::string> RecordDatabase::Write(const std::string& type, const std::string& id,
                                                 const nlohmann::json& data) {
    if (!reflection_) return "no project is open";
    const RecordTypeInfo* info = reflection_->FindRecordType(type);
    if (!info) return "unknown record type '" + type + "'";
    if (!IsRecordId(id)) return "'" + id + "' isn't a valid record id (a-z, 0-9, - and _)";
    // Required fields may come from the base, so check them on the resolved form.
    nlohmann::json resolved = DefaultRecord(*info);
    if (data.is_object()) {
        const std::string base = data.value("base", "");
        if (const Record* parent = base.empty() ? nullptr : Find(type, base)) resolved = parent->resolved;
        if (base == id) return "base: a record can't be its own base";
        for (const auto& [key, value] : data.items()) {
            if (!IsReservedKey(key)) resolved[key] = value;
        }
    }
    auto problems = ValidateRecord(*info, data, nullptr, &resolved);
    if (!problems.empty()) {
        std::string text;
        for (const auto& p : problems) text += (text.empty() ? "" : "; ") + p;
        return text;
    }
    const fs::path path = root_ / "records" / type / (id + ".json");
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return "can't write " + path.generic_string();
    out << data.dump(2) << "\n";
    out.close();
    Refresh();
    return std::nullopt;
}

std::optional<std::string> RecordDatabase::Remove(const std::string& type, const std::string& id) {
    const Record* record = Find(type, id);
    if (!record) return "no " + type + " '" + id + "'";
    std::error_code ec;
    if (!fs::remove(root_ / record->path, ec) || ec) return "can't delete " + record->path;
    Refresh();
    return std::nullopt;
}

std::optional<std::string> RecordDatabase::Rename(const std::string& type, const std::string& from,
                                                  const std::string& to) {
    if (from == to) return std::nullopt;
    if (!Find(type, from)) return "no " + type + " '" + from + "'";
    if (!IsRecordId(to)) return "'" + to + "' isn't a valid record id (a-z, 0-9, - and _)";
    if (Find(type, to)) return "there is already a " + type + " '" + to + "'";
    // Rewrite the references first, then move the file.
    for (auto& [otherType, byId] : records_) {
        const RecordTypeInfo* info = reflection_->FindRecordType(otherType);
        if (!info) continue;
        for (auto& [otherId, record] : byId) {
            if (!record.data.is_object()) continue;
            bool changed = false;
            for (const auto& field : info->fields) {
                if (record.data.contains(field.name)) {
                    changed |= RewriteIn(field, record.data[field.name], type, from, to);
                }
            }
            if (otherType == type && record.data.value("base", "") == from) {
                record.data["base"] = to;
                changed = true;
            }
            if (changed) {
                std::ofstream out(root_ / record.path, std::ios::binary | std::ios::trunc);
                if (!out) return "can't write " + record.path;
                out << record.data.dump(2) << "\n";
            }
        }
    }
    std::error_code ec;
    const fs::path folder = root_ / "records" / type;
    fs::rename(folder / (from + ".json"), folder / (to + ".json"), ec);
    Refresh();
    if (ec) return "can't rename " + from + ": " + ec.message();
    return std::nullopt;
}

std::vector<std::string> RecordDatabase::ReferencesTo(const std::string& type, const std::string& id) const {
    std::vector<std::string> keys;
    if (!reflection_) return keys;
    for (const auto& [otherType, byId] : records_) {
        const RecordTypeInfo* info = reflection_->FindRecordType(otherType);
        if (!info) continue;
        for (const auto& [otherId, record] : byId) {
            if (otherType == type && otherId == id) continue;
            std::vector<std::pair<std::string, std::string>> refs;
            CollectReferences(*info, record.data, refs);
            const bool byBase = otherType == type && record.data.is_object() && record.data.value("base", "") == id;
            if (byBase || std::find(refs.begin(), refs.end(), std::pair{type, id}) != refs.end()) {
                keys.push_back(record.Key());
            }
        }
    }
    return keys;
}

RecordDatabase& ActiveRecords() {
    static RecordDatabase records;
    return records;
}

// --- The engine's record types ----------------------------------------------------------

void RegisterEngineRecordTypes(ReflectionRegistry& r) {
    // The first record type (12.1); Phase 13 adds equipment, effects and the rest.
    r.RecordType("item", "Something that can be picked up, carried, sold or used. Pickups and inventories refer to "
                         "items by id.")
        .Field("name", FieldType::String, "The name shown to the player.").Required()
        .Field("description", FieldType::String, "A line or two shown in the inventory.")
        .Field("kind", FieldType::Enum, "What sort of item it is.")
        .Options({"misc", "weapon", "armor", "consumable", "key", "quest", "material", "currency", "book"})
        .Field("icon", FieldType::Texture, "The picture shown in the inventory and on the ground.")
        .Field("value", FieldType::Int, "Its price in the game's currency.").Min(0)
        .Field("weight", FieldType::Float, "How heavy it is (for weight-limited inventories).").Min(0)
        .Field("stack", FieldType::Int, "How many fit in one inventory slot.").Min(1).Default(1)
        .Field("unique", FieldType::Bool, "Only one exists in the game (story items).")
        .Field("tags", FieldType::String, "Labels for searching, loot tables and conditions.").List()
        // Phase 13: wearing, wielding and using it.
        .Field("slot", FieldType::Enum, "Where it's worn or wielded (none: carried only).")
        .Options({"none", "weapon", "offhand", "head", "body", "hands", "feet", "ring", "amulet"})
        .Field("damage", FieldType::Float, "Added to attack while wielded.").Min(0)
        // Shown on its wearer (docs/assets/animations.md §3.3) and choosing its attack clips (§2.3).
        .Field("sprite", FieldType::Texture, "Its picture held or worn (the icon is the inventory's).")
        .Field("point", FieldType::String, "The wearer's point it's on (empty: by slot: weapon on hand, head on head...).")
        .Field("grip", FieldType::Float, "Where it's held, 0-1 of its picture across and down ([0.5, 0.9]: the hilt).").List()
        .Field("pointing", FieldType::Float, "Which way its art points (degrees: 0 right, -90 up).")
        .Field("size", FieldType::Float, "Its longest side in the wearer's frame pixels (0: its own size).").Min(0)
        .Field("layer", FieldType::Enum, "In front of or behind the wearer: auto follows the point.").Options({"auto", "front", "behind"})
        .Field("class", FieldType::String, "Its weapon class (sword, axe, spear, dagger, greatsword, bow, staff...): its attack clips.")
        .Field("animation", FieldType::String, "Its attack clip or variant (slash: attack_slash).")
        .Field("armor", FieldType::Float, "Added to defense while worn.").Min(0)
        .Object("modifiers",
                {MakeRecordValue("attribute", FieldType::String, "An attribute: maxHealth, attack, moveSpeed, resistFire..."),
                 [] {
                     RecordField op = MakeRecordValue("op", FieldType::Enum, "add, or multiply (1.1 = +10%)");
                     op.options = {"add", "multiply"};
                     return op;
                 }(),
                 MakeRecordValue("value", FieldType::Float, "How much")},
                "What it does to attributes while worn or wielded.")
        .List()
        .Object("use",
                {MakeRecordValue("heal", FieldType::Float, "Health restored"),
                 MakeRecordValue("mana", FieldType::Float, "Mana restored"),
                 MakeRecordValue("stamina", FieldType::Float, "Stamina restored"),
                 [] {
                     RecordField effect = MakeRecordValue("effect", FieldType::Enum, "A status effect on the user");
                     effect.options = {"none", "burn", "poison", "slow", "regen"};
                     return effect;
                 }(),
                 MakeRecordValue("duration", FieldType::Float, "Seconds the effect or boost lasts"),
                 MakeRecordValue("magnitude", FieldType::Float, "The effect's strength"),
                 MakeRecordValue("attribute", FieldType::String, "An attribute boosted for a while"),
                 MakeRecordValue("value", FieldType::Float, "How much it's boosted"),
                 [] {
                     RecordField consumed = MakeRecordValue("consumed", FieldType::Bool, "Used up when used");
                     consumed.defaultValue = true;
                     return consumed;
                 }(),
                 MakeRecordValue("learn", FieldType::String, "A book: the ability it teaches (19.3)"),
                 MakeRecordValue("ability", FieldType::String, "A scroll: the ability it casts"),
                 [] {
                     RecordField effects = MakeRecordValue("effects", FieldType::String, "Magic effects on the user");
                     effects.list = true;
                     return effects;
                 }()},
                "What using it does (a potion, food, a scroll, a book).")
        .Object("enchantment",
                {[] {
                     RecordField effects = MakeRecordValue("effects", FieldType::String, "Magic effects (ids)");
                     effects.list = true;
                     return effects;
                 }(),
                 MakeRecordValue("on", FieldType::String, "worn (while equipped) or hit (on the target of each hit)")},
                "Magic on equipment (19.3).");

    r.RecordType("loot", "A loot table (13.4): what a chest, an enemy or a merchant's stock gives, rolled from weighted "
                         "entries.")
        .Field("name", FieldType::String, "A label for editors.")
        .Field("rolls", FieldType::Int, "How many picks are made.").Min(0).Default(1)
        .Field("goldMin", FieldType::Int, "Gold given, at least.").Min(0)
        .Field("goldMax", FieldType::Int, "Gold given, at most.").Min(0)
        .Object("entries",
                {MakeRecordRef("item", "item", "An item (or leave empty and name a table)"),
                 MakeRecordRef("table", "loot", "Another loot table rolled instead"),
                 MakeRecordValue("weight", FieldType::Float, "How likely, against the other entries"),
                 MakeRecordValue("min", FieldType::Int, "How many, at least"),
                 MakeRecordValue("max", FieldType::Int, "How many, at most"),
                 MakeRecordValue("chance", FieldType::Float, "The chance it's given when picked (0-1)"),
                 MakeRecordValue("levelMin", FieldType::Int, "Only from this level (0: any)"),
                 MakeRecordValue("levelMax", FieldType::Int, "Only up to this level (0: any)")},
                "What can be given.")
        .List();

    r.RecordType("perk", "A perk (13.2): taken with a perk point at a level, it changes attributes from then on.")
        .Field("name", FieldType::String, "Shown to the player.").Required()
        .Field("description", FieldType::String, "What it does, in a line.")
        .Field("level", FieldType::Int, "The level it can be taken from.").Min(1).Default(1)
        .Ref("requires", "perk", "Perks that must be taken first.").List()
        .Object("modifiers",
                {MakeRecordValue("attribute", FieldType::String, "An attribute"),
                 [] {
                     RecordField op = MakeRecordValue("op", FieldType::Enum, "add or multiply");
                     op.options = {"add", "multiply"};
                     return op;
                 }(),
                 MakeRecordValue("value", FieldType::Float, "How much")},
                "What it changes.")
        .List()
        .Object("abilityModifiers",
                {MakeRecordValue("tag", FieldType::String, "Abilities with this tag (element.fire, school.restoration; empty: all)"),
                 MakeRecordValue("stat", FieldType::String, "magnitude or cost"),
                 MakeRecordValue("value", FieldType::Float, "Times this (1.25 = +25%)")},
                "Scales abilities by tag (19.3).")
        .List();

    r.RecordType("progression", "How characters grow (13.2). The record \"default\" is used: the experience curve, "
                                "what each level gives, perk points.")
        .Field("baseExperience", FieldType::Float, "Experience from level 1 to 2.").Min(1).Default(100)
        .Field("growth", FieldType::Float, "Each next level needs this many times more.").Min(1).Default(1.5)
        .Field("maxLevel", FieldType::Int, "The highest level.").Min(1).Default(50)
        .Field("perkPointsPerLevel", FieldType::Int, "Perk points per level.").Min(0).Default(1)
        .Object("perLevel",
                {MakeRecordValue("attribute", FieldType::String, "An attribute"),
                 MakeRecordValue("value", FieldType::Float, "Added at each level")},
                "What every level adds to the base attributes.")
        .List();

    r.RecordType("faction", "A faction (13.7): who belongs together, and how it stands with the others.")
        .Field("name", FieldType::String, "Shown to the player.").Required()
        .Field("description", FieldType::String, "Who they are.")
        .Field("playerStanding", FieldType::Int, "How members feel about the player at the start (-100..100).")
        .Min(-100)
        .Max(100)
        .Object("relations",
                {MakeRecordRef("faction", "faction", "Another faction"),
                 MakeRecordValue("standing", FieldType::Int, "-100 (war) to 100 (allies); below 0 they fight")},
                "How it stands with other factions.")
        .List();

    // Character AI (14, ai-packages.md).
    r.RecordType("package", "An AI package (14.2): what a character does, where and when. A Behaviour lists "
                            "packages; the first whose schedule and conditions hold runs its template.")
        .Field("template", FieldType::Enum, "What it does.")
        .Options({"stay", "travel", "wander", "sandbox", "sleep", "eat", "work", "vendor", "patrol", "guard", "follow",
                  "accompany", "escort", "flee", "hide", "ambush", "wait", "use-object", "approach-player", "script"})
        .Required()
        .Field("description", FieldType::String, "What it's for, in a line.")
        .Object("location",
                {MakeRecordValue("marker", FieldType::String, "A marker's name"),
                 [] {
                     RecordField alias = MakeRecordValue("alias", FieldType::Enum, "Or the character's own place");
                     alias.options = {"none", "home", "work", "bed"};
                     return alias;
                 }(),
                 MakeRecordValue("x", FieldType::Float, "Or a point: x"),
                 MakeRecordValue("y", FieldType::Float, "and y"),
                 MakeRecordValue("scene", FieldType::String, "The scene it's in (empty: wherever the character is)")},
                "Where.")
        .Field("target", FieldType::String, "Whom: player, a character's name or World Bible key, or a marker.")
        .Object("schedule",
                {MakeRecordValue("from", FieldType::String, "From HH:MM (empty: all day)"),
                 MakeRecordValue("to", FieldType::String, "To HH:MM"),
                 MakeRecordValue("days", FieldType::String, "Days of the week 1-7, e.g. 1,2,3,4,5 (empty: every day)")},
                "When.")
        .Object("conditions",
                {MakeRecordValue("flag", FieldType::String, "A story flag"),
                 [] {
                     RecordField v = MakeRecordValue("flagValue", FieldType::Bool, "that is set (or not)");
                     v.defaultValue = true;
                     return v;
                 }(),
                 MakeRecordValue("quest", FieldType::String, "Or a quest"),
                 [] {
                     RecordField s = MakeRecordValue("questState", FieldType::Enum, "in this state");
                     s.options = {"active", "inactive", "completed"};
                     return s;
                 }()},
                "Only while all of these hold.")
        .List()
        .Object("flags",
                {[] {
                     RecordField v = MakeRecordValue("interruptible", FieldType::Bool, "Combat and reactions may stop it");
                     v.defaultValue = true;
                     return v;
                 }(),
                 MakeRecordValue("mustComplete", FieldType::Bool, "Finishes before another package takes over"),
                 MakeRecordValue("run", FieldType::Bool, "Runs rather than walks")},
                "How it behaves.")
        .Object("data",
                {MakeRecordValue("radius", FieldType::Float, "Cells around the place (wander, sandbox, ambush)"),
                 MakeRecordValue("duration", FieldType::Float, "Seconds (waits, patrol stops)"),
                 MakeRecordValue("object", FieldType::String, "A smart object type (work, use-object)"),
                 MakeRecordValue("dialogue", FieldType::String, "A dialogue file (approach-player)"),
                 [] {
                     RecordField path = MakeRecordValue("path", FieldType::String, "Markers in order (patrol)");
                     path.list = true;
                     return path;
                 }(),
                 MakeRecordValue("distance", FieldType::Float, "Cells (follow, escort, flee)"),
                 MakeRecordValue("script", FieldType::Script, "A Lua file with run() (script)")},
                "What the template needs.");

    r.RecordType("procedure", "A procedure (14.1): a behaviour tree a package template can be, written by hand. "
                              "Nodes: sequence, selector, random, parallel, loop, condition, cooldown, invert; "
                              "move-to, wander, wait, idle, face, bark, use-object, talk-to-player, follow, flee, "
                              "patrol, ambush, escort, set, emit, script.")
        .Field("description", FieldType::String, "What it does.")
        .Field("tree", FieldType::String, "The tree as JSON: {\"type\": \"sequence\", \"children\": [...]}.").Required();

    r.RecordType("combat-style", "How a character fights (14.5).")
        .Field("name", FieldType::String, "A label for editors.")
        .Field("range", FieldType::Enum, "Melee closes in; ranged and kiting keep their distance and shoot.")
        .Options({"melee", "ranged", "kiting"})
        .Field("keepDistance", FieldType::Float, "Cells a ranged fighter keeps from its target (0: most of its range).")
        .Min(0)
        .Field("fleeAt", FieldType::Float, "Runs below this share of its health (confidence changes it).").Min(0).Max(1)
        .Default(0.2)
        .Field("callForHelp", FieldType::Float, "Cells within which allies join the fight.").Min(0).Default(6);

    // Difficulty (16.1, difficulty.md §3.3): levels, and the multipliers their steps choose.
    {
        std::vector<RecordField> steps;
        for (const char* category : {"player", "enemies", "allies", "stealth", "resources"}) {
            RecordField step = MakeRecordValue(category, FieldType::Int, "1-5; 3 is as designed, higher is harder");
            step.min = 1;
            step.max = 5;
            steps.push_back(step);
        }
        RecordField owner = MakeRecordValue("owner", FieldType::Enum, "Whose fields: player, enemy, ally or any");
        owner.options = {"any", "player", "enemy", "ally"};
        RecordField op = MakeRecordValue("op", FieldType::Enum, "mul, add, set, min or max");
        op.options = {"mul", "add", "set", "min", "max"};
        r.RecordType("difficulty", "A difficulty level (16.1): a step per category and modifiers on top.")
            .Field("name", FieldType::String, "Shown to the player.").Required()
            .Field("description", FieldType::String, "A line for the menu.")
            .Field("order", FieldType::Int, "Easiest first.")
            .Object("categories", steps, "A step per category (1-5, 3: as designed).")
            .Object("modifiers",
                    {MakeRecordValue("target", FieldType::String, "A field: Component.field, e.g. Health.max"), owner,
                     MakeRecordValue("tag", FieldType::String, "Only entities whose Name contains this (a boss)"), op,
                     MakeRecordValue("value", FieldType::Float, "The amount")},
                    "Changes on top of the category steps.")
            .List()
            .Field("xp", FieldType::Float, "Experience rewards times this.").Min(0).Default(1)
            .Field("permadeath", FieldType::Bool, "One life: death ends the run.");
        RecordField multipliers = MakeRecordValue("steps", FieldType::Float, "Five multipliers, steps 1-5");
        multipliers.list = true;
        RecordField category = MakeRecordValue("category", FieldType::Enum, "The category");
        category.options = {"player", "enemies", "allies", "stealth", "resources"};
        r.RecordType("difficulty-table", "The game's multipliers per category, kind of value and step (16.1). "
                                         "Only records/difficulty-table/default.json is used.")
            .Object("rows",
                    {category,
                     MakeRecordValue("kind", FieldType::String,
                                     "health, damage, defense, speed, perception, count, gold, loot, price, xp"),
                     multipliers},
                    "One row per category and kind; missing ones use the built-in table.")
            .List();
    }

    // Animation and effects (18): animators, particles, effects. Engine presets in data/fx.
    {
        r.RecordType("animator", "An animator (18.2): states that play clips and transitions between them.")
            .Object("states",
                    {MakeRecordValue("id", FieldType::String, "The state"),
                     MakeRecordValue("clip", FieldType::String, "The clip it plays (in the entity's sheet)"),
                     MakeRecordValue("exit", FieldType::String, "end: back to the default state when the clip ends"),
                     MakeRecordValue("speed", FieldType::Float, "Playback rate")},
                    "The states; the first is the default.")
            .List()
            .Object("transitions",
                    {MakeRecordValue("from", FieldType::String, "A state, or *"),
                     MakeRecordValue("to", FieldType::String, "A state"),
                     MakeRecordValue("if", FieldType::String, "speed > 0.1, attacking, hurt, casting, using, dead, !dead, a && b"),
                     MakeRecordValue("priority", FieldType::Int, "Higher wins")},
                    "When to change state.")
            .List()
            .Field("default", FieldType::String, "The state it starts in (else the first).");
        auto listOf = [](const char* name, FieldType type, const char* description) {
            RecordField f = MakeRecordValue(name, type, description);
            f.list = true;
            return f;
        };
        RecordTypeInfo particles;
        particles.name = "particles";
        particles.description = "Particles (18.4): an emitter's settings. Presets: data/fx/particles.";
        particles.fields = {
            MakeRecordValue("rate", FieldType::Float, "Per second while playing"),
            MakeRecordValue("burst", FieldType::Int, "At once when it starts"),
            MakeRecordValue("duration", FieldType::Float, "Seconds it emits (0: until stopped)"),
            listOf("lifetime", FieldType::Float, "Seconds: [min, max]"),
            MakeRecordValue("shape", FieldType::String, "point, circle or box"),
            MakeRecordValue("radius", FieldType::Float, "Circle radius or box half-size, in cells"),
            listOf("speed", FieldType::Float, "Cells per second: [min, max]"),
            MakeRecordValue("direction", FieldType::Float, "Degrees on screen: 0 right, -90 up"),
            MakeRecordValue("spread", FieldType::Float, "Degrees around the direction"),
            MakeRecordValue("gravity", FieldType::Float, "Pixels per second squared, down"),
            MakeRecordValue("drag", FieldType::Float, "Slowing per second"),
            MakeRecordValue("rise", FieldType::Float, "Pixels per second upward at the start"),
            listOf("size", FieldType::Float, "Pixels: [start, end]"),
            listOf("color", FieldType::String, "[start, end] as #rrggbb"),
            listOf("alpha", FieldType::Float, "[start, end] opacity"),
            MakeRecordValue("texture", FieldType::Texture, "A picture for each particle (empty: a soft dot)"),
            MakeRecordValue("layer", FieldType::String, "world (sorted with everything) or above"),
            MakeRecordValue("max", FieldType::Int, "Most alive at once")};
        r.AddRecordTypeInfo(particles);
        auto objectOf = [](const char* name, const char* description, std::vector<RecordField> fields) {
            RecordField f = MakeRecordValue(name, FieldType::String, description);
            f.kind = RecordFieldKind::Object;
            f.fields = std::move(fields);
            return f;
        };
        RecordField parts = objectOf("parts", "Each a particles, sound, light, flash, shake, hitstop or slowmo part", {
            MakeRecordValue("particles", FieldType::String, "Particles to emit"),
            MakeRecordValue("at", FieldType::String, "target (default) or source"),
            MakeRecordValue("seconds", FieldType::Float, "Particles: keep emitting this long"),
            MakeRecordValue("sound", FieldType::Audio, "A sound to play"),
            MakeRecordValue("shake", FieldType::Float, "Screen shake (trauma 0-1)"),
            MakeRecordValue("hitstop", FieldType::Float, "Seconds the world freezes"),
            objectOf("light", "A short light", {MakeRecordValue("color", FieldType::String, "#rrggbb"),
                                                 MakeRecordValue("radius", FieldType::Float, "Cells"),
                                                 MakeRecordValue("seconds", FieldType::Float, "How long")}),
            objectOf("flash", "A flash on the target", {MakeRecordValue("color", FieldType::String, "#rrggbb"),
                                                         MakeRecordValue("time", FieldType::Float, "Seconds")}),
            objectOf("slowmo", "Slow motion", {MakeRecordValue("scale", FieldType::Float, "Game speed (0.1-1)"),
                                                MakeRecordValue("seconds", FieldType::Float, "How long")})});
        parts.list = true;
        RecordTypeInfo effect;
        effect.name = "effect";
        effect.description = "An effect (18.5): what something looks and sounds like. Presets: data/fx/effects.";
        effect.fields = {parts};
        r.AddRecordTypeInfo(effect);
    }

    // Abilities and magic (19): magic effects, abilities, the balance constants. Presets in data/magic.
    {
        RecordTypeInfo effect;
        effect.name = "magic-effect";
        effect.description = "A magic effect (19.1): what changes. Presets: data/magic/effects.";
        auto value = [](const char* name, FieldType type, const char* description) { return MakeRecordValue(name, type, description); };
        auto listOf = [](const char* name, const char* description) {
            RecordField f = MakeRecordValue(name, FieldType::String, description);
            f.list = true;
            return f;
        };
        RecordField archetype = value("archetype", FieldType::Enum, "What it does");
        archetype.options = {"damage", "heal", "modify", "over-time", "absorb", "state", "summon", "displace", "dispel", "light"};
        effect.fields = {archetype,
                         value("attribute", FieldType::String, "modify: the attribute; over-time: health (damage), heal, mana or stamina"),
                         value("op", FieldType::String, "modify: add or mul"),
                         value("magnitude", FieldType::String, "A number or a formula: 8 + 2 * caster.level"),
                         value("duration", FieldType::Float, "Seconds (0: instant)"),
                         value("period", FieldType::Float, "over-time: seconds between ticks"),
                         value("area", FieldType::Float, "Cells around the target it also reaches"),
                         value("damageType", FieldType::String, "physical, fire, frost, shock, poison"),
                         value("stacking", FieldType::String, "refresh, stack, strongest or separate"),
                         value("maxStacks", FieldType::Int, "stack: at most"),
                         listOf("tags", "Granted while active: state.stunned, element.fire..."),
                         listOf("requires", "Tags the target must have"),
                         listOf("blockedBy", "Tags that stop it (immune.fire)"),
                         value("cue", FieldType::String, "A visual effect (records effect) on apply"),
                         value("prefab", FieldType::Prefab, "summon: what appears"),
                         value("nonLethal", FieldType::Bool, "over-time: can't kill (poison)")};
        effect.fields[14].optional = true;
        r.AddRecordTypeInfo(effect);

        RecordTypeInfo ability;
        ability.name = "ability";
        ability.description = "An ability (19.2): how magic effects are used. Presets: data/magic/abilities.";
        RecordField kind = value("kind", FieldType::Enum, "A label for the UI and the AI");
        kind.options = {"spell", "skill", "power", "passive", "item-use", "shout"};
        RecordField activation = value("activation", FieldType::Enum, "How it's used");
        activation.options = {"press", "hold", "toggle", "passive"};
        RecordField delivery = value("delivery", FieldType::Enum, "How it reaches its target");
        delivery.options = {"self", "touch", "projectile", "area", "cone", "aura"};
        RecordField cost = value("cost", FieldType::String, "");
        cost.kind = RecordFieldKind::Object;
        cost.description = "What it costs";
        cost.fields = {value("attribute", FieldType::String, "mana, stamina or health"), value("amount", FieldType::Float, "How much")};
        RecordField uses = value("effects", FieldType::String, "The magic effects (ids)");
        uses.list = true;
        RecordField cue = value("cue", FieldType::String, "");
        cue.kind = RecordFieldKind::Object;
        cue.description = "Visual effects";
        cue.fields = {value("cast", FieldType::String, "On casting"), value("impact", FieldType::String, "Where it lands")};
        RecordField ai = value("ai", FieldType::String, "");
        ai.kind = RecordFieldKind::Object;
        ai.description = "How characters use it (19.5)";
        ai.fields = {value("use", FieldType::String, "attack, heal-self, buff or debuff"),
                     value("when", FieldType::String, "health < 0.4, distance < 6, mana > 0.2 (&& joins)"),
                     value("priority", FieldType::Int, "Higher first")};
        ability.fields = {value("name", FieldType::String, "Shown to the player"), kind, value("school", FieldType::String, "destruction, restoration..."),
                          activation, delivery, value("range", FieldType::Float, "Cells"), value("radius", FieldType::Float, "area, aura: cells"),
                          value("speed", FieldType::Float, "projectile: cells per second"), cost,
                          value("cooldown", FieldType::Float, "Seconds"), value("castTime", FieldType::Float, "Seconds before it goes off"),
                          value("duration", FieldType::Float, "aura, toggle: seconds"), uses, listOf("tags", "element.fire... (perks scale by them)"),
                          value("animation", FieldType::String, "The caster's animator trigger (casting, attacking)"), cue,
                          value("requiresLevel", FieldType::Int, "At least this level"), ai};
        r.AddRecordTypeInfo(ability);

        r.RecordType("magic-balance", "The constants of the cost formula (19.4). Only records/magic-balance/default is used.")
            .Field("base", FieldType::Float, "Cost per point of magnitude").Default(0.6)
            .Field("magnitudeExponent", FieldType::Float, "").Default(1.1)
            .Field("durationExponent", FieldType::Float, "").Default(0.35)
            .Field("areaExponent", FieldType::Float, "").Default(0.5);
    }

    r.RecordType("achievement", "An achievement (24): what unlocks it — a quest's state, a story flag, a number of game "
                                "events (deaths, pickups, trigger entries), or a script's unlock_achievement.")
        .Field("name", FieldType::String, "As the player sees it.")
        .Field("description", FieldType::String, "How to get it (or a hint).")
        .Field("icon", FieldType::String, "A picture (assets/...).")
        .Field("hidden", FieldType::Bool, "Secret until unlocked.")
        .Field("points", FieldType::Int, "Its worth.").Default(10)
        .Field("kind", FieldType::Enum, "What unlocks it.").Options({"quest", "flag", "event", "script"})
        .Field("quest", FieldType::String, "quest: which quest")
        .Field("state", FieldType::Enum, "quest: in which state").Options({"active", "completed", "failed"})
        .Field("flag", FieldType::String, "flag: which story flag")
        .Field("event", FieldType::String, "event: death, pickup, hit, attack, trigger_enter, talk, quest_completed...")
        .Field("target", FieldType::String, "event: what it's about (a prefab, a name, a trigger, a quest id); empty: any")
        .Field("count", FieldType::Int, "event: how many times").Default(1);

    r.RecordType("encounter", "An encounter (13.7): what a Spawner makes, picked by weight and level.")
        .Field("name", FieldType::String, "A label for editors.")
        .Object("entries",
                {MakeRecordValue("prefab", FieldType::Prefab, "What appears"),
                 MakeRecordValue("weight", FieldType::Float, "How likely"),
                 MakeRecordValue("min", FieldType::Int, "How many, at least"),
                 MakeRecordValue("max", FieldType::Int, "How many, at most"),
                 MakeRecordValue("levelMin", FieldType::Int, "Only from this level (0: any)"),
                 MakeRecordValue("levelMax", FieldType::Int, "Only up to this level (0: any)")},
                "What can appear.")
        .List();
}

} // namespace aige
