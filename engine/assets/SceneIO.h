#pragma once

#include "ecs/Registry.h"

#include <string>

namespace aige {

// Serializes every entity's known components to a plain-text, git-diffable
// scene file (technical.md §13): a blank-line-separated block per entity,
// one line per component, `Key=Value` fields. Deliberately hand-written
// per-component-type for now — becomes reflection-driven once the
// Reflection & Command Registry exists (development_plan.md Phase 2).
void SaveScene(Registry& registry, const std::string& path);

// Clears `registry` and repopulates it from a scene file written by
// SaveScene. Returns false (leaving registry untouched) if the file doesn't
// exist. Unrecognized component lines are skipped rather than failing the
// whole load, so scene files stay loadable across small engine changes.
bool LoadScene(Registry& registry, const std::string& path);

// In-memory equivalents of SaveScene/LoadScene, same text format. Used for
// the editor's Play-mode snapshot: serialize on Play, deserialize on Stop, so
// nothing that happens during a playtest leaks into the edited scene.
// DeserializeScene clears `registry` first, like LoadScene.
std::string SerializeScene(Registry& registry);
void DeserializeScene(Registry& registry, const std::string& text);

} // namespace aige
