#pragma once

#include "assets/Project.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// The World Bible (product.md §7, development_plan.md 3.1): the project's
// canon — characters, factions, places, items, events, lore and the tone
// guide. Each entry is one human-readable file, world/<kind>/<id>.md:
//
//   ---
//   name: Mara
//   summary: The village blacksmith, gruff but fair.
//   tags: blacksmith, brookvale
//   rel: lives_in location/brookvale
//   rel: member_of faction/iron-guild
//   ---
//   Free Markdown text...
//
// The files are the source of truth (they diff in git and stay editable
// with AI off); the AI service only keeps a rebuildable search index of them.

const std::vector<std::string>& WorldKinds(); // character, faction, location, item, event, lore, style

struct WorldRelation {
    std::string type;   // lives_in, member_of, ... (a word; spaces become _)
    std::string target; // another entry's key: "<kind>/<id>"
};

struct WorldEntry {
    std::string kind;
    std::string id; // file name without .md: lowercase letters, digits, - and _
    std::string name;
    std::string summary;
    std::vector<std::string> tags;
    std::vector<WorldRelation> relations;
    std::string body;
    // Header keys this version doesn't know, kept so hand-written ones survive a save.
    std::vector<std::pair<std::string, std::string>> extra;

    std::string Key() const { return kind + "/" + id; }
};

// The one-line forms used by the world.write command: "a, b" and
// "lives_in location/brookvale; member_of faction/iron-guild".
std::vector<std::string> ParseTagList(const std::string& text);
std::vector<WorldRelation> ParseRelationList(const std::string& text);

// "Mara the Smith" -> "mara-the-smith" (empty if nothing usable is left).
std::string WorldIdFromName(const std::string& name);
bool IsValidWorldId(const std::string& id);

std::string FormatWorldEntry(const WorldEntry& entry);
// `kind` and `id` come from the file's location; nullopt (and `error`) when
// the text isn't an entry at all.
std::optional<WorldEntry> ParseWorldEntry(const std::string& text, const std::string& kind, const std::string& id,
                                          std::string* error = nullptr);

class WorldBible {
public:
    explicit WorldBible(Project project);

    // Rereads world/. Unreadable files are reported by Validate().
    void Refresh();

    const std::vector<WorldEntry>& Entries() const { return entries_; }
    const WorldEntry* Find(const std::string& key) const;

    struct Problem {
        std::string key;
        std::string message;
    };
    // Missing names, relations to entries that don't exist, files that don't parse.
    std::vector<Problem> Validate() const;

    // Writes the entry's file (creating world/<kind>/), then refreshes. Fails
    // on an unknown kind, a missing name or an invalid id.
    std::optional<std::string> Save(const WorldEntry& entry);
    std::optional<std::string> Remove(const std::string& key);
    // Moves an entry to a new id (and/or kind), rewriting every relation that
    // pointed at it.
    std::optional<std::string> Rename(const std::string& key, const std::string& newKey);

    std::filesystem::path PathOf(const std::string& kind, const std::string& id) const;

private:
    Project project_;
    std::vector<WorldEntry> entries_;
    std::vector<Problem> parseProblems_;
};

} // namespace aige
