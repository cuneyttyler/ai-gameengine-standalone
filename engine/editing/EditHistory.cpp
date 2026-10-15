#include "editing/EditHistory.h"

#include "assets/SceneIO.h"

namespace aige {

void EditHistory::Record(Registry& registry) {
    Push(SerializeScene(registry));
}

void EditHistory::Push(std::string snapshot) {
    undo_.push_back(std::move(snapshot));
    if (undo_.size() > maxDepth_) {
        undo_.erase(undo_.begin());
    }
    redo_.clear(); // a new edit invalidates the redo branch
}

bool EditHistory::Undo(Registry& registry) {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(SerializeScene(registry));
    DeserializeScene(registry, undo_.back());
    undo_.pop_back();
    return true;
}

bool EditHistory::Redo(Registry& registry) {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(SerializeScene(registry));
    DeserializeScene(registry, redo_.back());
    redo_.pop_back();
    return true;
}

void EditHistory::Clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace aige
