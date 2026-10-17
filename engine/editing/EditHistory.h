#pragma once

#include "ecs/Registry.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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

    // Each returns the new step's id (never 0), so a caller can follow one
    // step — an applied AI proposal, say — through later undos and redos.
    uint64_t Record(Registry& registry);

    // Same as Record, for a snapshot taken earlier (SerializeScene) — lets a
    // caller snapshot when an interaction starts but only commit the undo
    // step once something actually changed.
    uint64_t Push(std::string snapshot);

    // Each returns false (registry untouched) when there's nothing to undo/redo.
    bool Undo(Registry& registry);
    bool Redo(Registry& registry);

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }

    // The step Undo / Redo would take back or bring back next (0: none).
    uint64_t NextUndo() const { return undo_.empty() ? 0 : undo_.back().id; }
    uint64_t NextRedo() const { return redo_.empty() ? 0 : redo_.back().id; }
    // Whether a step is in effect (undoable) or undone (redoable). Neither:
    // it fell off the end of the history, or a new edit replaced its redo.
    bool IsDone(uint64_t id) const;
    bool IsUndone(uint64_t id) const;
    void Clear();

    // Rewrites every stored snapshot (a moved file's references, 4.4), so
    // undo doesn't bring back a path that no longer exists.
    void RewriteSnapshots(const std::function<std::string(const std::string&)>& rewrite);

private:
    struct Step {
        std::string snapshot; // the scene before (undo) / after (redo) the step
        uint64_t id;
    };
    size_t maxDepth_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    uint64_t nextId_ = 1;
};

} // namespace aige
