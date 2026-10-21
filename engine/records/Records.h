#pragma once

#include "reflection/Reflection.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Records (development_plan.md 12.1, engine-gaps.md §2.1): typed game data
// kept as one JSON file each, records/<type>/<id>.json. The type's fields
// come from the reflection registry (engine types and module types), so the
// editor's Records tab, Lua, the AI and the validators all read one schema.
//
// A record's file holds its fields, plus optionally
//   "base": "<id>"        another record of the same type whose fields it
//                         inherits (its own fields win, field by field);
//   "provenance": {...}   where it came from (the AI Planner, a build).
// References to other records are their ids; the field says which type.

struct Record {
    std::string type;
    std::string id;
    std::string path;        // project-relative
    nlohmann::json data;     // the file, as written
    nlohmann::json resolved; // defaults, then the base's fields, then its own

    std::string Key() const { return type + "/" + id; }
};

struct RecordProblem {
    std::string key; // "<type>/<id>", or a path for files that aren't records
    std::string message;
};

// Is `id` a valid record id (lower-case letters, digits, '-' and '_')?
bool IsRecordId(const std::string& id);

// Checks `data` against `type`'s fields: unknown fields, types, ranges,
// options, required fields (after `resolved` if given, else `data` itself).
// `exists(type, id)` says whether a referenced record exists; pass nullptr to
// skip reference checks. Messages name the field path ("effects[2].magnitude").
std::vector<std::string> ValidateRecord(const RecordTypeInfo& type, const nlohmann::json& data,
                                        const std::function<bool(const std::string&, const std::string&)>& exists,
                                        const nlohmann::json* resolved = nullptr);

// The empty value of a field (its default, else "", 0, false, the first
// option, [] for lists, an object of sub-defaults).
nlohmann::json DefaultValue(const RecordField& field);

// A record of `type` with every field at its default.
nlohmann::json DefaultRecord(const RecordTypeInfo& type);

// Every record a value refers to: (type, id) pairs, walking lists and objects.
void CollectReferences(const RecordTypeInfo& type, const nlohmann::json& data,
                       std::vector<std::pair<std::string, std::string>>& out);

class RecordDatabase {
public:
    // Reads every record under `projectRoot/records` against `reflection`'s
    // record types. Safe to call again (a project opened, modules reloaded).
    void Open(const ReflectionRegistry& reflection, const std::filesystem::path& projectRoot);
    void Close();
    void Refresh();
    bool IsOpen() const { return reflection_ != nullptr; }

    const ReflectionRegistry* Reflection() const { return reflection_; }
    const std::filesystem::path& Root() const { return root_; }

    const Record* Find(const std::string& type, const std::string& id) const;
    // The records of a type, sorted by id.
    std::vector<const Record*> OfType(const std::string& type) const;
    size_t Count() const;

    // Everything wrong: unreadable files, unknown types, schema violations,
    // bases that are missing or cyclic, references to missing records,
    // asset paths that don't exist.
    std::vector<RecordProblem> Problems() const;

    // Writes a record (creating or replacing it) after checking it against its
    // type; references to records that don't exist yet are allowed (they show
    // in Problems), so several records can be written in any order. Returns
    // the reason it was refused.
    std::optional<std::string> Write(const std::string& type, const std::string& id, const nlohmann::json& data);
    std::optional<std::string> Remove(const std::string& type, const std::string& id);
    // A new id, with every reference to it (and every base naming it) rewritten.
    std::optional<std::string> Rename(const std::string& type, const std::string& from, const std::string& to);

    // Records that refer to type/id (by a field or as their base): their keys.
    std::vector<std::string> ReferencesTo(const std::string& type, const std::string& id) const;

private:
    void Resolve();
    nlohmann::json ResolveOne(const std::string& type, const std::string& id, std::vector<std::string>& chain) const;

    const ReflectionRegistry* reflection_ = nullptr;
    std::filesystem::path root_;
    std::map<std::string, std::map<std::string, Record>> records_; // type -> id -> record
    std::vector<RecordProblem> loadProblems_;
};

// The open project's records, process-wide like ActiveSettings(): the editor
// and the player open it after loading modules; Lua's record() reads it.
RecordDatabase& ActiveRecords();

// Registers the engine's own record types (development_plan.md 12–19 add to
// them). Called by RegisterGameplayTypes.
void RegisterEngineRecordTypes(ReflectionRegistry& registry);

} // namespace aige
