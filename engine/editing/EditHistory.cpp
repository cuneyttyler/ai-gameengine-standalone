#include "editing/EditHistory.h"

#include "assets/SceneIO.h"
#include "gameplay/Components.h"

namespace aige {

uint64_t EditHistory::Record(Registry& registry) {
    return Push(SerializeScene(registry));
}

uint64_t EditHistory::Push(std::string snapshot) {
    const uint64_t id = nextId_++;
    undo_.push_back({std::move(snapshot), id, nullptr});
    Trim();
    redo_.clear(); // a new edit invalidates the redo branch
    return id;
}

uint64_t EditHistory::PushCommand(std::unique_ptr<EditCommand> command) {
    const uint64_t id = nextId_++;
    undo_.push_back({{}, id, std::shared_ptr<EditCommand>(std::move(command))});
    Trim();
    redo_.clear();
    return id;
}

void EditHistory::Trim() {
    if (undo_.size() > maxDepth_) {
        undo_.erase(undo_.begin());
    }
}

bool EditHistory::IsDone(uint64_t id) const {
    for (const Step& step : undo_) {
        if (step.id == id) return true;
    }
    return false;
}

bool EditHistory::IsUndone(uint64_t id) const {
    for (const Step& step : redo_) {
        if (step.id == id) return true;
    }
    return false;
}

bool EditHistory::Undo(Registry& registry) {
    if (undo_.empty()) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    if (step.command) {
        step.command->Undo(registry);
        redo_.push_back(std::move(step));
        return true;
    }
    redo_.push_back({SerializeScene(registry), step.id, nullptr});
    DeserializeScene(registry, step.snapshot);
    return true;
}

bool EditHistory::Redo(Registry& registry) {
    if (redo_.empty()) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    if (step.command) {
        step.command->Redo(registry);
        undo_.push_back(std::move(step));
        return true;
    }
    undo_.push_back({SerializeScene(registry), step.id, nullptr});
    DeserializeScene(registry, step.snapshot);
    return true;
}

void EditHistory::RewriteSnapshots(const std::function<std::string(const std::string&)>& rewrite) {
    for (auto* stack : {&undo_, &redo_}) {
        for (Step& step : *stack) {
            if (!step.command) step.snapshot = rewrite(step.snapshot);
        }
    }
}

void EditHistory::Clear() {
    undo_.clear();
    redo_.clear();
}

// --- commands ------------------------------------------------------------------------

void CompoundCommand::Undo(Registry& registry) {
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) (*it)->Undo(registry);
}

void CompoundCommand::Redo(Registry& registry) {
    for (auto& command : commands_) command->Redo(registry);
}

void PlaceEntitiesCommand::Undo(Registry& registry) {
    std::vector<Entity> built;
    for (auto entity : EntitiesInOrder(registry)) {
        if (const auto* node = registry.try_get<PlaceNode>(entity); node && node->place == place_) built.push_back(entity);
    }
    text_ = SerializeEntities(registry, built);
    registry.destroy(built.begin(), built.end());
}

void PlaceEntitiesCommand::Redo(Registry& registry) {
    AppendEntities(registry, text_);
    text_.clear();
}

void TileLayerCommand::Undo(Registry& registry) {
    for (auto it = edits_.rbegin(); it != edits_.rend(); ++it) it->Revert(registry);
}

void TileLayerCommand::Redo(Registry& registry) {
    for (const auto& edit : edits_) edit.Apply(registry);
}

size_t TileLayerCommand::Cells() const {
    size_t total = 0;
    for (const auto& edit : edits_) total += edit.changes.size();
    return total;
}

} // namespace aige
