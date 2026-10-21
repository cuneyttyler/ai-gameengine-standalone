#pragma once

#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

// The ontology as users see and extend it (development_plan.md 9): everything that can be
// built, readable; and the user's own modules and extensions, kept in their folder (used by
// every project), checked as the engine's are, exported and imported as one file.
//
// The user's folder has the engine library's layout: modules/<id>.module.json (a module of
// theirs; "extends": {"parents": {module: rule}, "components": [build type]} puts it in
// places) and extensions.json ({"fields": {module: {quantity, quality}}, "children":
// {parent: {child: rule}}, "components": {build type: [module]}}).

// For the ontology panel: build types (their components), modules (fields, slots,
// children, classes, where they came from: "engine", "user", a project), classes, roles,
// vocabularies (their ids), relation types, and the field types a module may use.
nlohmann::json OntologyJson(const Ontology& ontology);

// The field types a module's fields may have (FieldDef::type).
const std::vector<std::string>& FieldTypes();

// Saves a module of the user's (an aige.module/1 object) into `userDir`, after checking it
// with the rest: the engine's library and the user's other additions. Its id can't be one of
// the engine's (they aren't editable). "" when saved, else what's wrong.
std::string SaveUserModule(const std::filesystem::path& userDir, const std::filesystem::path& engineDir, const nlohmann::json& module);
std::string DeleteUserModule(const std::filesystem::path& userDir, const std::string& id);

// The user's extensions.json, replaced (checked the same way).
std::string SaveUserExtensions(const std::filesystem::path& userDir, const std::filesystem::path& engineDir, const nlohmann::json& extensions);
nlohmann::json UserExtensions(const std::filesystem::path& userDir);

// Everything the user added, as one file ({"schema": "aige.ontology-extension/1", modules,
// extensions}), and back: imported modules replace the user's of the same id; extensions merge.
nlohmann::json ExportUserOntology(const std::filesystem::path& userDir);
std::string ImportUserOntology(const std::filesystem::path& userDir, const std::filesystem::path& engineDir, const nlohmann::json& bundle);

} // namespace aige
