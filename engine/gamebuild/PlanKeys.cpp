#include "gamebuild/PlanKeys.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace aige {

using json = nlohmann::json;

std::string NormaliseKeyName(std::string key) {
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c) || c == '-' || c == '_'; }), key.end());
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::map<std::string, std::string> words = {
        {"shift", "LeftShift"},   {"lshift", "LeftShift"},    {"rshift", "RightShift"}, {"ctrl", "LeftCtrl"},
        {"control", "LeftCtrl"},  {"alt", "LeftAlt"},         {"esc", "Escape"},        {"escape", "Escape"},
        {"return", "Enter"},      {"enter", "Enter"},         {"space", "Space"},       {"spacebar", "Space"},
        {"tab", "Tab"},           {"backspace", "Backspace"}, {"lmb", "MouseLeft"},     {"leftclick", "MouseLeft"},
        {"mouse1", "MouseLeft"},  {"rmb", "MouseRight"},      {"rightclick", "MouseRight"}, {"mouse2", "MouseRight"},
        {"mmb", "MouseMiddle"},   {"up", "Up"},               {"down", "Down"},         {"left", "Left"},
        {"right", "Right"},       {"mouseleft", "MouseLeft"}, {"mouseright", "MouseRight"}};
    if (const auto it = words.find(lower); it != words.end()) return it->second;
    if (lower.size() == 1 && std::isdigit(static_cast<unsigned char>(lower[0]))) return "Num" + lower;
    if (lower.size() == 1 && std::isalpha(static_cast<unsigned char>(lower[0]))) return std::string(1, static_cast<char>(std::toupper(lower[0])));
    if (lower.size() >= 2 && lower[0] == 'f' && std::all_of(lower.begin() + 1, lower.end(), ::isdigit)) return "F" + lower.substr(1);
    if (lower.rfind("num", 0) == 0 && lower.size() == 4) return "Num" + lower.substr(3);
    return key; // as given (an engine name already, or unknown)
}

namespace {

bool SameContext(const InputAction& a, const InputAction& b) {
    return a.context == b.context || a.context == "any" || b.context == "any";
}

// Who else has `key` in `action`'s context, not as its group: its name, else "".
std::string TakenBy(const InputMap& map, const InputAction& action, Key key) {
    for (const auto& other : map.actions) {
        if (other.name == action.name || other.axis || !SameContext(other, action)) continue;
        if (!action.group.empty() && other.group == action.group) continue;
        for (const auto& k : other.keys) {
            if (k.key == key) return other.name;
        }
    }
    return {};
}

} // namespace

std::vector<std::string> ApplyPlannedActions(InputMap& map, const json& actions) {
    std::vector<std::string> done;
    // Free keys, the hand's first: near WASD, then further.
    static const char* const kSpare[] = {"F", "R", "G", "Q", "Z", "X", "C", "V", "T", "B", "H", "Y", "N", "M", "U", "O", "P", "L",
                                         "Num5", "Num6", "Num7", "Num8", "Num9", "Num0", "F1", "F2", "F3", "F4"};
    for (const auto& a : actions.is_array() ? actions : json::array()) {
        if (!a.is_object()) continue;
        const std::string name = a.value("name", "");
        if (name.empty()) continue;
        InputAction* action = map.Find(name);
        const bool made = action == nullptr;
        if (made) {
            map.actions.push_back(InputAction{});
            action = &map.actions.back();
            action->name = name;
            action->description = a.value("description", std::string());
        }
        if (a.contains("group") && a["group"].is_string()) action->group = a["group"].get<std::string>();
        if (a.contains("nearOnly") && a["nearOnly"].is_boolean()) action->nearOnly = a["nearOnly"].get<bool>();
        if (a.contains("screen") && a["screen"].is_string()) action->screen = a["screen"].get<std::string>();
        if (a.contains("context") && a["context"].is_string()) action->context = a["context"].get<std::string>();
        // Its key: the one asked for when free; else a free one (an existing action keeps its own unless asked).
        const std::string asked = NormaliseKeyName(a.value("key", std::string()));
        const auto key = asked.empty() ? std::nullopt : KeyFromName(asked);
        if (key) {
            const std::string by = TakenBy(map, *action, *key);
            if (by.empty()) {
                // A new action: that key; one the engine has: that key first, its others (the gamepad's) kept.
                if (made) action->keys = {KeyBinding{*key}};
                else if (std::none_of(action->keys.begin(), action->keys.end(), [&](const KeyBinding& k) { return k.key == *key; }))
                    action->keys.insert(action->keys.begin(), KeyBinding{*key});
                done.push_back(name + ": " + asked);
                continue;
            }
            if (!made && !action->keys.empty()) {
                done.push_back(name + ": asked for " + asked + ", taken by " + by + ": kept its own");
                continue;
            }
            for (const char* spare : kSpare) {
                const auto k = KeyFromName(spare);
                if (k && TakenBy(map, *action, *k).empty()) {
                    action->keys = {KeyBinding{*k}};
                    done.push_back(name + ": asked for " + asked + ", taken by " + by + ": " + spare);
                    break;
                }
            }
            continue;
        }
        if (made) {
            for (const char* spare : kSpare) {
                const auto k = KeyFromName(spare);
                if (k && TakenBy(map, *action, *k).empty()) {
                    action->keys = {KeyBinding{*k}};
                    done.push_back(name + ": " + spare + (asked.empty() ? "" : " (" + asked + " isn't a key)"));
                    break;
                }
            }
        }
    }
    return done;
}

std::vector<std::string> ApplyDesignedInput(InputMap& map, const json& design) {
    std::vector<std::string> done;
    for (const auto& a : design.value("actions", json::array())) {
        if (!a.is_object() || a.value("name", "").empty()) continue;
        // Its keys (the keyboard's and the gamepad's), the ones that are keys.
        std::vector<KeyBinding> keys;
        std::string skipped;
        for (const char* field : {"keys", "pad"}) {
            for (const auto& k : a.value(field, json::array())) {
                if (!k.is_string()) continue;
                if (const auto key = KeyFromName(NormaliseKeyName(k.get<std::string>()))) keys.push_back(KeyBinding{*key});
                else skipped += (skipped.empty() ? "" : ", ") + k.get<std::string>();
            }
        }
        std::vector<std::string> names = {a.value("name", "")};
        for (const auto& n : a.value("also", json::array())) {
            if (n.is_string() && !n.get<std::string>().empty()) names.push_back(n.get<std::string>());
        }
        for (const auto& name : names) {
            InputAction* action = map.Find(name);
            if (action == nullptr) {
                map.actions.push_back(InputAction{});
                action = &map.actions.back();
                action->name = name;
            }
            if (action->axis) continue; // movement stays as the engine binds it (its stick and its two directions)
            if (!a.value("description", "").empty()) action->description = a.value("description", "");
            if (a.contains("context") && a["context"].is_string() && !a["context"].get<std::string>().empty()) action->context = a["context"].get<std::string>();
            if (a.contains("screen") && a["screen"].is_string()) action->screen = a["screen"].get<std::string>();
            if (!keys.empty()) action->keys = keys;
            std::string list;
            for (const auto& k : action->keys) list += (list.empty() ? "" : ", ") + std::string(KeyName(k.key));
            done.push_back(name + ": " + list + (skipped.empty() ? "" : " (not keys: " + skipped + ")"));
        }
    }
    return done;
}

std::vector<std::string> KeyClashes(const InputMap& map) {
    std::vector<std::string> out;
    // (The engine's own actions share keys by design: Escape skips a cutscene and opens the menu.)
    const InputMap defaults = InputMap::Defaults();
    for (size_t i = 0; i < map.actions.size(); ++i) {
        const auto& a = map.actions[i];
        if (a.axis) continue;
        for (size_t j = i + 1; j < map.actions.size(); ++j) {
            const auto& b = map.actions[j];
            if (b.axis || !SameContext(a, b) || (!a.group.empty() && a.group == b.group)) continue;
            if (defaults.Find(a.name) && defaults.Find(b.name)) continue;
            for (const auto& ka : a.keys) {
                for (const auto& kb : b.keys) {
                    if (ka.key == kb.key) out.push_back(a.name + "/" + b.name + ": " + KeyName(ka.key));
                }
            }
        }
    }
    return out;
}

} // namespace aige
