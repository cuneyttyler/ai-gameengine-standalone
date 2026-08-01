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

} // namespace aige
