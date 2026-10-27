#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace aige {

// New and copied projects (development_plan.md 4.2).
//
// A project is a folder: scenes/, prefabs/, scripts/, assets/, world/,
// dialogue/, quests/, schedules/ and settings.aige. Editor-only and per-user
// state (.cache/, ai_log.jsonl) never travels with a copy.

// Creates a new project at `root` (which must not exist, or be empty): the
// folders, a settings file and a README. With `contentFrom`, that project's
// art, prefabs and scripts are copied in as a starting palette (no scene,
// story or canon). The editor builds the starter scene on first open.
// `topDown`: square cells for art drawn from above (Grid ▸ TopDown, 6.1). `ai`: the project's own AI
// choices (Project Settings ▸ AI: provider, model, art, background), each missing or empty using the app's.
// `buildMethod`: Places ▸ method (Assembled or Paint; empty: the default).
std::optional<std::string> CreateProject(const std::filesystem::path& root,
                                         const std::optional<std::filesystem::path>& contentFrom = std::nullopt,
                                         bool topDown = false, const std::map<std::string, std::string>& ai = {},
                                         const std::string& buildMethod = {});

// Copies a whole project to `to` (which must not exist, or be empty), leaving
// out .cache/ and ai_log.jsonl. For Save Project As.
std::optional<std::string> CopyProject(const std::filesystem::path& from, const std::filesystem::path& to);

// Whether a folder looks like a project (has a scene, settings or prefabs).
bool LooksLikeProject(const std::filesystem::path& root);

} // namespace aige
