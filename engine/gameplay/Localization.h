#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace aige {

// Localization (development_plan.md 12.7): every string a player reads can be
// translated. A project's string tables are loc/<language>.json, flat maps of
// key -> text:
//
//   { "menu.new_game": "Nouvelle partie",
//     "dialogue/tamsin/greet": "Encore un Gardien ?",
//     "quest/quiet-the-courtyard/name": "Calmer la cour" }
//
// The game's text is written in the project's source language (game.json's
// language, English by default) where it's authored (dialogue files, quests,
// records); a table for another language translates it by key, and anything
// it doesn't translate falls back to the source. Keys for authored content:
//   dialogue/<file id>/<node id>          a line
//   dialogue/<file id>/<node id>/<n>      its n-th choice (from 1)
//   quest/<id>/name, quest/<id>/description, quest/<id>/<objective id>
//   record/<type>/<id>/<field>            a record's text field (an item's name)
// `{name}` in a text is replaced by the value given for it.
class Localization {
public:
    // Reads loc/*.json; `language` is the one to show (empty or the source: none).
    void Load(const std::filesystem::path& projectRoot, const std::string& language);
    const std::string& Language() const { return language_; }
    // The languages the project has tables for (the file names).
    const std::vector<std::string>& Available() const { return available_; }

    // The translation of `key`, else `fallback` (the source text).
    std::string Text(const std::string& key, const std::string& fallback) const;
    // The same, with {placeholders} filled.
    std::string Format(const std::string& key, const std::string& fallback,
                       const std::map<std::string, std::string>& values) const;

    // Every string a project's content holds, by key (source text), for writing a
    // table to translate: dialogue, quests, records' string fields.
    static std::map<std::string, std::string> CollectSource(const std::filesystem::path& projectRoot);

private:
    std::string language_;
    std::vector<std::string> available_;
    std::map<std::string, std::string> table_;
};

// Fills {name} placeholders.
std::string FillPlaceholders(const std::string& text, const std::map<std::string, std::string>& values);

// The game's current language, process-wide like ActiveSettings().
Localization& ActiveLocalization();

// Shorthand: ActiveLocalization().Text(key, fallback).
inline std::string Tr(const std::string& key, const std::string& fallback) {
    return ActiveLocalization().Text(key, fallback);
}

} // namespace aige
