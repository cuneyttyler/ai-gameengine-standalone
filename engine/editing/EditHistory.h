#pragma once

#include "ecs/Registry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace aige {

// Snapshot-based undo/redo: call Record() *before* each user edit, and the
// whole scene (in SerializeScene's text format) is pushed onto the undo
// stack. Whole-scene snapshots are wasteful in principle but trivially
// correct for every kind of edit, and scenes at this stage are a few KB.
// Replaced by command-based undo once the Reflection & Command Registry
// exists (development_plan.md Phase 2).
class EditHistory {
public:
    explicit EditHistory(size_t maxDepth = 100) : maxDepth_(maxDepth) {}

    void Record(Registry& registry);

    // Same as Record, for a snapshot taken earlier (SerializeScene) — lets a
    // caller snapshot when an interaction starts but only commit the undo
    // step once something actually changed.
    void Push(std::string snapshot);

    // Each returns false (registry untouched) when there's nothing to undo/redo.
    bool Undo(Registry& registry);
    bool Redo(Registry& registry);

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void Clear();

private:
    size_t maxDepth_;
    std::vector<std::string> undo_;
    std::vector<std::string> redo_;
};

} // namespace aige
