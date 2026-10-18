#pragma once

#include "ecs/Registry.h"
#include "gameplay/TileLayer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aige {

// An edit that knows how to take itself back (development_plan.md 7.2).
// Bulk operations — a place build, a layer fill — would make whole-scene
// snapshots huge and slow, so they record what they changed instead. A
// command must not hold entity ids across steps (a snapshot step in
// between recreates every entity under new ids): find entities by what
// they carry (PlaceNode), and tile layers by name.
class EditCommand {
public:
    virtual ~EditCommand() = default;
    // The command was applied when pushed; Undo takes it back, Redo applies it again.
    virtual void Undo(Registry& registry) = 0;
    virtual void Redo(Registry& registry) = 0;
    virtual std::string Label() const { return {}; }
};

// Undo/redo of two kinds of step:
// - snapshots: call Record() *before* a user edit, and the whole scene (in
//   SerializeScene's text format) is kept. Trivially correct for every kind
//   of edit; fine while scenes are small. Tile layers aren't in snapshots.
// - commands: PushCommand() *after* a bulk edit, with what it changed.
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

    // A command already applied to the scene.
    uint64_t PushCommand(std::unique_ptr<EditCommand> command);

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
        std::shared_ptr<EditCommand> command; // set: a command step (no snapshot)
    };
    void Trim();

    size_t maxDepth_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    uint64_t nextId_ = 1;
};

// Commands for bulk edits ------------------------------------------------------------

// Several commands as one step: undone last first.
class CompoundCommand : public EditCommand {
public:
    explicit CompoundCommand(std::string label = {}) : label_(std::move(label)) {}
    void Add(std::unique_ptr<EditCommand> command) { commands_.push_back(std::move(command)); }
    bool Empty() const { return commands_.empty(); }
    void Undo(Registry& registry) override;
    void Redo(Registry& registry) override;
    std::string Label() const override { return label_; }

private:
    std::string label_;
    std::vector<std::unique_ptr<EditCommand>> commands_;
};

// Entities a build created, found by their PlaceNode's place: Undo removes
// them (keeping their text), Redo brings them back.
class PlaceEntitiesCommand : public EditCommand {
public:
    explicit PlaceEntitiesCommand(std::string place) : place_(std::move(place)) {}
    void Undo(Registry& registry) override;
    void Redo(Registry& registry) override;
    std::string Label() const override { return "Build " + place_; }

private:
    std::string place_;
    std::string text_; // the entities, while undone
};

// Cells changed in tile layers (gameplay/TileLayer.h).
class TileLayerCommand : public EditCommand {
public:
    explicit TileLayerCommand(std::vector<TileLayerEdit> edits, std::string label = {})
        : edits_(std::move(edits)), label_(std::move(label)) {}
    void Undo(Registry& registry) override;
    void Redo(Registry& registry) override;
    std::string Label() const override { return label_; }
    size_t Cells() const;

private:
    std::vector<TileLayerEdit> edits_;
    std::string label_;
};

} // namespace aige
