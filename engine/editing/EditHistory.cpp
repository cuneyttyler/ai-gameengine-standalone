#include "editing/EditHistory.h"

#include "assets/SceneIO.h"

namespace aige {

uint64_t EditHistory::Record(Registry& registry) {
    return Push(SerializeScene(registry));
}

uint64_t EditHistory::Push(std::string snapshot) {
    const uint64_t id = nextId_++;
    undo_.push_back({std::move(snapshot), id});
    if (undo_.size() > maxDepth_) {
        undo_.erase(undo_.begin());
    }
    redo_.clear(); // a new edit invalidates the redo branch
    return id;
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
    redo_.push_back({SerializeScene(registry), undo_.back().id});
    DeserializeScene(registry, undo_.back().snapshot);
    undo_.pop_back();
    return true;
}

bool EditHistory::Redo(Registry& registry) {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back({SerializeScene(registry), redo_.back().id});
    DeserializeScene(registry, redo_.back().snapshot);
    redo_.pop_back();
    return true;
}

void EditHistory::RewriteSnapshots(const std::function<std::string(const std::string&)>& rewrite) {
    for (auto* stack : {&undo_, &redo_}) {
        for (Step& step : *stack) {
            step.snapshot = rewrite(step.snapshot);
        }
    }
}

void EditHistory::Clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace aige
