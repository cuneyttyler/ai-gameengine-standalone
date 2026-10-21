#include "gameplay/InputMap.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

const std::vector<std::string> kContexts = {"gameplay", "dialogue", "menu", "cutscene", "any"};

InputAction Button(std::string name, std::string description, std::string context, std::vector<Key> keys) {
    InputAction action;
    action.name = std::move(name);
    action.description = std::move(description);
    action.context = std::move(context);
    for (Key key : keys) action.keys.push_back({key, 1.0f});
    return action;
}

InputAction Axis(std::string name, std::string description, std::vector<KeyBinding> keys, std::vector<AxisBinding> axes) {
    InputAction action;
    action.name = std::move(name);
    action.description = std::move(description);
    action.axis = true;
    action.keys = std::move(keys);
    action.axes = std::move(axes);
    return action;
}

} // namespace

const InputAction* InputMap::Find(const std::string& name) const {
    for (const auto& action : actions) {
        if (action.name == name) return &action;
    }
    return nullptr;
}

InputAction* InputMap::Find(const std::string& name) {
    for (auto& action : actions) {
        if (action.name == name) return &action;
    }
    return nullptr;
}

InputMap InputMap::Defaults() {
    InputMap map;
    // Screen directions: right and down are positive (the gamepad's convention).
    map.actions.push_back(Axis("move_x", "Walk left (-1) or right (+1).",
                               {{Key::A, -1}, {Key::Left, -1}, {Key::D, 1}, {Key::Right, 1},
                                {Key::PadLeft, -1}, {Key::PadRight, 1}},
                               {{PadAxis::LeftX, 1.0f, 0.2f}}));
    map.actions.push_back(Axis("move_y", "Walk up (-1) or down (+1) the screen.",
                               {{Key::W, -1}, {Key::Up, -1}, {Key::S, 1}, {Key::Down, 1},
                                {Key::PadUp, -1}, {Key::PadDown, 1}},
                               {{PadAxis::LeftY, 1.0f, 0.2f}}));
    map.actions.push_back(Button("attack", "Swing your weapon the way you face.", "gameplay", {Key::Space, Key::PadX}));
    map.actions.push_back(Button("interact", "Talk, open, pick up: whatever is in front of you.", "gameplay",
                                 {Key::E, Key::PadA}));
    map.actions.push_back(Button("fire", "Shoot your ranged weapon the way you face.", "gameplay", {Key::F, Key::PadRB}));
    map.actions.push_back(Button("block", "Hold to block: most of a hit is stopped, for stamina.", "gameplay",
                                 {Key::LeftCtrl, Key::PadLB}));
    map.actions.push_back(Button("dodge", "A quick dash you can't be hit during.", "gameplay", {Key::LeftShift, Key::PadB}));
    map.actions.push_back(Button("inventory", "Open your inventory.", "gameplay", {Key::I, Key::Tab, Key::PadY}));
    map.actions.push_back(Button("journal", "Open or close your quest journal.", "any", {Key::J, Key::PadBack}));
    map.actions.push_back(Button("skip", "Skip a cutscene.", "cutscene", {Key::Escape, Key::Enter, Key::Space, Key::PadA}));
    map.actions.push_back(Button("confirm", "Continue a conversation; pick the highlighted choice.", "dialogue",
                                 {Key::Enter, Key::Space, Key::E, Key::PadA}));
    map.actions.push_back(Button("nav_up", "Highlight the choice above.", "dialogue", {Key::Up, Key::W, Key::PadUp}));
    map.actions.push_back(Button("nav_down", "Highlight the choice below.", "dialogue", {Key::Down, Key::S, Key::PadDown}));
    for (int i = 1; i <= 9; ++i) {
        map.actions.push_back(Button("choice_" + std::to_string(i), "Pick choice " + std::to_string(i) + ".", "dialogue",
                                     {static_cast<Key>(static_cast<int>(Key::Num0) + i)}));
    }
    map.actions.push_back(Button("ui_up", "Move up in a menu.", "menu", {Key::Up, Key::W, Key::PadUp}));
    map.actions.push_back(Button("ui_down", "Move down in a menu.", "menu", {Key::Down, Key::S, Key::PadDown}));
    map.actions.push_back(Button("ui_select", "Pick the highlighted menu item.", "menu",
                                 {Key::Enter, Key::Space, Key::E, Key::PadA}));
    map.actions.push_back(Button("ui_back", "Go back to the previous menu.", "menu", {Key::Backspace, Key::PadB}));
    map.actions.push_back(Button("menu", "Open the pause menu, or close the top one.", "any", {Key::Escape, Key::PadStart}));
    map.actions.push_back(Button("quicksave", "Save the game to the quick slot.", "any", {Key::F6}));
    map.actions.push_back(Button("quickload", "Load the quick slot.", "any", {Key::F9}));
    map.actions.push_back(Button("debug_stats", "Show frame statistics.", "any", {Key::F3}));
    map.actions.push_back(Button("debug_ai", "Show what every character is doing.", "any", {Key::F4}));
    return map;
}

nlohmann::json InputMap::ToJson() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& action : actions) {
        nlohmann::json keys = nlohmann::json::array();
        for (const auto& binding : action.keys) {
            if (action.axis) {
                keys.push_back({{"key", KeyName(binding.key)}, {"scale", binding.scale}});
            } else {
                keys.push_back(KeyName(binding.key));
            }
        }
        nlohmann::json json = {{"name", action.name},
                               {"description", action.description},
                               {"context", action.context},
                               {"keys", keys}};
        if (action.axis) {
            json["axis"] = true;
            nlohmann::json axes = nlohmann::json::array();
            for (const auto& binding : action.axes) {
                axes.push_back({{"axis", PadAxisName(binding.axis)}, {"scale", binding.scale}, {"deadzone", binding.deadzone}});
            }
            json["axes"] = axes;
        }
        list.push_back(json);
    }
    return {{"schema", "aige.input/1"}, {"actions", list}};
}

InputMap InputMap::FromJson(const nlohmann::json& json, std::vector<std::string>* errors) {
    InputMap map;
    auto report = [&](const std::string& message) {
        if (errors) errors->push_back(message);
    };
    for (const auto& a : json.value("actions", nlohmann::json::array())) {
        if (!a.is_object() || a.value("name", "").empty()) {
            report("an action needs a \"name\"");
            continue;
        }
        InputAction action;
        action.name = a.value("name", "");
        action.description = a.value("description", "");
        action.context = a.value("context", "gameplay");
        action.axis = a.value("axis", false);
        if (std::find(kContexts.begin(), kContexts.end(), action.context) == kContexts.end()) {
            report(action.name + ": unknown context \"" + action.context + "\"");
            action.context = "gameplay";
        }
        for (const auto& k : a.value("keys", nlohmann::json::array())) {
            const std::string name = k.is_string() ? k.get<std::string>() : k.is_object() ? k.value("key", "") : "";
            auto key = KeyFromName(name);
            if (!key) {
                report(action.name + ": unknown key \"" + name + "\"");
                continue;
            }
            action.keys.push_back({*key, k.is_object() ? k.value("scale", 1.0f) : 1.0f});
        }
        for (const auto& x : a.value("axes", nlohmann::json::array())) {
            auto axis = PadAxisFromName(x.is_object() ? x.value("axis", "") : "");
            if (!axis) {
                report(action.name + ": unknown axis");
                continue;
            }
            action.axes.push_back({*axis, x.value("scale", 1.0f), x.value("deadzone", 0.2f)});
        }
        map.actions.push_back(std::move(action));
    }
    return map;
}

InputMap LoadInputMap(const fs::path& projectRoot, std::vector<std::string>* errors) {
    InputMap defaults = InputMap::Defaults();
    std::ifstream in(projectRoot / "input.json");
    if (!in) return defaults;
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (!json.is_object()) {
        if (errors) errors->push_back("input.json isn't valid JSON");
        return defaults;
    }
    InputMap map = InputMap::FromJson(json, errors);
    for (const auto& action : defaults.actions) {
        if (!map.Find(action.name)) map.actions.push_back(action);
    }
    return map;
}

bool SaveInputMap(const fs::path& projectRoot, const InputMap& map) {
    std::ofstream out(projectRoot / "input.json", std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << map.ToJson().dump(2) << "\n";
    return static_cast<bool>(out);
}

InputMap& ActiveInputMap() {
    static InputMap map = InputMap::Defaults();
    return map;
}

void InputState::Update(const InputMap& map, const KeyQueryFn& keys, const AxisQueryFn& axes) {
    before_ = now_;
    now_.clear();
    for (const auto& action : map.actions) {
        if (action.context != "any" && action.context != context_) continue;
        float value = 0.0f;
        for (const auto& binding : action.keys) {
            if (keys && keys(binding.key)) {
                value = action.axis ? value + binding.scale : 1.0f;
            }
        }
        if (axes) {
            for (const auto& binding : action.axes) {
                float v = axes(binding.axis) * binding.scale;
                if (std::abs(v) < binding.deadzone) continue;
                value = action.axis ? value + v : std::max(value, std::abs(v) > 0.5f ? 1.0f : 0.0f);
            }
        }
        value = std::clamp(value, -1.0f, 1.0f);
        if (value != 0.0f) now_[action.name] = value;
    }
    if (contextChanged_) {
        for (const auto& [name, value] : now_) before_[name] = value;
        contextChanged_ = false;
    }
}

bool InputState::Down(const std::string& action) const {
    auto it = now_.find(action);
    return it != now_.end() && it->second != 0.0f;
}

bool InputState::Pressed(const std::string& action) const {
    return Down(action) && !before_.contains(action);
}

bool InputState::Released(const std::string& action) const {
    return !Down(action) && before_.contains(action);
}

float InputState::Value(const std::string& action) const {
    auto it = now_.find(action);
    return it == now_.end() ? 0.0f : it->second;
}

} // namespace aige
