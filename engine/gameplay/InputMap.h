#pragma once

#include "platform/Input.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Input actions (development_plan.md 12.3, engine-gaps.md §2.1): the game asks
// for "attack" or "move_x", not for Space or the A key. Each action has
// bindings to keys, mouse buttons and gamepad buttons and sticks, and belongs
// to a context (gameplay, dialogue, menu, or any), so the same key can mean
// different things in different situations. The map is the project's
// input.json (defaults when there's none); players rebind in their own copy.

struct KeyBinding {
    Key key = Key::Unknown;
    float scale = 1.0f; // axes: what holding the key contributes (-1 or +1)
};

struct AxisBinding {
    PadAxis axis = PadAxis::LeftX;
    float scale = 1.0f;    // -1 inverts
    float deadzone = 0.2f; // below this the stick counts as centred
};

struct InputAction {
    std::string name;
    std::string description;
    std::string context = "gameplay"; // gameplay | dialogue | menu | cutscene | any
    bool axis = false;                // a value in [-1, 1] rather than pressed or not
    std::vector<KeyBinding> keys;
    std::vector<AxisBinding> axes;
};

struct InputMap {
    std::vector<InputAction> actions;

    const InputAction* Find(const std::string& name) const;
    InputAction* Find(const std::string& name);

    // The engine's actions with their default bindings (WASD and arrows and the
    // left stick to move, Space or X to attack, E or A to interact...).
    static InputMap Defaults();

    nlohmann::json ToJson() const;
    // Unknown keys or contexts are reported in `errors` and skipped.
    static InputMap FromJson(const nlohmann::json& json, std::vector<std::string>* errors = nullptr);
};

// The project's map (input.json), with any default action it lacks added (so
// a project made before an action existed still gets it). Defaults when the
// file is missing or unreadable.
InputMap LoadInputMap(const std::filesystem::path& projectRoot, std::vector<std::string>* errors = nullptr);
bool SaveInputMap(const std::filesystem::path& projectRoot, const InputMap& map);

// The open project's map, process-wide like ActiveSettings().
InputMap& ActiveInputMap();

using KeyQueryFn = std::function<bool(Key)>;
using AxisQueryFn = std::function<float(PadAxis)>;

// The actions' state, updated once per frame from the devices.
class InputState {
public:
    void Update(const InputMap& map, const KeyQueryFn& keys, const AxisQueryFn& axes = {});

    // Only actions of the current context (and "any") respond; the others
    // read as released. Gameplay by default.
    // Changing it treats what is held as already held, so the key that opened
    // a conversation doesn't also answer it.
    void SetContext(const std::string& context) {
        if (context != context_) contextChanged_ = true;
        context_ = context;
    }
    const std::string& Context() const { return context_; }

    bool Down(const std::string& action) const;
    bool Pressed(const std::string& action) const;  // went down this frame
    bool Released(const std::string& action) const; // went up this frame
    float Value(const std::string& action) const;   // axes: [-1, 1]; buttons: 0 or 1

    // Forget what was held (entering play), so a held key isn't a press.
    void Reset() {
        now_.clear();
        before_.clear();
    }
    // What is held at the next Update counts as already held (after a scene
    // change: the key held while walking through a gateway isn't a new press).
    void HoldCurrent() { contextChanged_ = true; }

private:
    std::string context_ = "gameplay";
    bool contextChanged_ = false;
    std::map<std::string, float> now_;
    std::map<std::string, float> before_;
};

} // namespace aige
