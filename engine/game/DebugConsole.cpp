#include "game/DebugConsole.h"

#include "gameplay/Achievements.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Items.h"
#include "gameplay/Story.h"
#include "scripting/LuaVM.h"

#include <algorithm>
#include <sstream>

namespace aige {

namespace {

std::vector<std::string> Words(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

} // namespace

DebugConsole::DebugConsole(GameSession& session, Registry& registry, LuaVM& lua) : session_(session), registry_(registry), lua_(lua) {
    Print("Debug console: help for the commands, ` to close.");
}

void DebugConsole::Print(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) log_.push_back(line);
    while (log_.size() > 200) log_.pop_front();
}

void DebugConsole::Type(unsigned codepoint) {
    if (codepoint == '`' || codepoint < 32) return;
    if (codepoint < 0x80) input_ += static_cast<char>(codepoint);
    else if (codepoint < 0x800) {
        input_ += static_cast<char>(0xC0 | (codepoint >> 6));
        input_ += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        input_ += static_cast<char>(0xE0 | (codepoint >> 12));
        input_ += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        input_ += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

void DebugConsole::Backspace() {
    while (!input_.empty()) {
        const unsigned char c = static_cast<unsigned char>(input_.back());
        input_.pop_back();
        if ((c & 0xC0) != 0x80) break; // a whole UTF-8 character
    }
}

void DebugConsole::Submit() {
    const std::string line = input_;
    input_.clear();
    historyAt_ = -1;
    if (line.find_first_not_of(' ') == std::string::npos) return;
    history_.push_back(line);
    Execute(line);
}

void DebugConsole::HistoryUp() {
    if (history_.empty()) return;
    historyAt_ = historyAt_ < 0 ? static_cast<int>(history_.size()) - 1 : std::max(0, historyAt_ - 1);
    input_ = history_[static_cast<size_t>(historyAt_)];
}

void DebugConsole::HistoryDown() {
    if (historyAt_ < 0) return;
    if (++historyAt_ >= static_cast<int>(history_.size())) {
        historyAt_ = -1;
        input_.clear();
    } else {
        input_ = history_[static_cast<size_t>(historyAt_)];
    }
}

std::vector<std::string> DebugConsole::Lines(size_t count) const {
    std::vector<std::string> out;
    const size_t from = log_.size() > count ? log_.size() - count : 0;
    for (size_t i = from; i < log_.size(); ++i) out.push_back(log_[i]);
    out.push_back("> " + input_ + "_");
    return out;
}

std::string DebugConsole::Execute(const std::string& line) {
    Print("> " + line);
    const auto w = Words(line);
    std::string out;
    const auto player = FindPlayer(registry_);
    auto needPlayer = [&]() -> bool {
        if (!player) out = "no player in this scene";
        return player.has_value();
    };
    const std::string cmd = w.empty() ? "" : w[0];
    if (cmd == "help") {
        out = "lua <code> | = <expr> | god | give <item> [n] | tp <x> <y> | scene <path> [marker] | quest <id> start|complete |\n"
              "flag <name> [on|off] | speed <0.1-4> | spawn <prefab> | achieve <id> | heal | kill | profiler | clear";
    } else if (cmd == "clear") {
        log_.clear();
        return {};
    } else if (cmd == "lua" || cmd == "=") {
        std::string code = line.substr(line.find(cmd) + cmd.size());
        if (cmd == "=") code = "return " + code;
        auto result = lua_.State().safe_script(code, sol::script_pass_on_error);
        if (!result.valid()) {
            const sol::error error = result;
            out = std::string("error: ") + error.what();
        } else if (cmd == "=") {
            sol::object value = result;
            out = value.is<std::string>() ? value.as<std::string>()
                  : value.is<double>()    ? std::to_string(value.as<double>())
                  : value.is<bool>()      ? (value.as<bool>() ? "true" : "false")
                  : value.valid() && value.get_type() != sol::type::lua_nil ? "(a " + std::string(sol::type_name(lua_.State(), value.get_type())) + ")"
                                                                            : "nil";
        } else {
            out = "ok";
        }
    } else if (cmd == "god") {
        god_ = !god_;
        out = god_ ? "god mode on: the player can't be hurt" : "god mode off";
    } else if (cmd == "give" && w.size() >= 2) {
        if (needPlayer()) {
            const int n = w.size() >= 3 ? std::max(1, std::atoi(w[2].c_str())) : 1;
            AddItem(registry_.get_or_emplace<Inventory>(*player), w[1], n);
            out = "gave " + std::to_string(n) + " " + w[1];
        }
    } else if (cmd == "tp" && w.size() >= 3) {
        if (needPlayer()) {
            auto& at = registry_.get<WorldTransform2D>(*player);
            at.x = static_cast<float>(std::atof(w[1].c_str())) + 0.5f;
            at.y = static_cast<float>(std::atof(w[2].c_str())) + 0.5f;
            out = "moved to " + w[1] + ", " + w[2];
        }
    } else if (cmd == "scene" && w.size() >= 2) {
        session_.RequestTravel(w[1], w.size() >= 3 ? w[2] : "");
        out = "travelling to " + w[1];
    } else if (cmd == "quest" && w.size() >= 3) {
        const StoryLibrary* library = session_.StoryLibraryInUse();
        if (!library || !library->FindQuest(w[1])) out = "no quest " + w[1];
        else if (w[2] == "start") {
            session_.Story().StartQuest(w[1], *library);
            out = "started " + w[1];
        } else if (w[2] == "complete") {
            session_.Story().CompleteQuest(w[1], *library);
            out = "completed " + w[1];
        } else out = "quest <id> start|complete";
    } else if (cmd == "flag" && w.size() >= 2) {
        const bool on = w.size() < 3 || w[2] == "on" || w[2] == "true" || w[2] == "1";
        session_.Story().SetFlag(w[1], on);
        out = w[1] + (on ? " set" : " cleared");
    } else if (cmd == "speed" && w.size() >= 2) {
        speed_ = std::clamp(static_cast<float>(std::atof(w[1].c_str())), 0.1f, 4.0f);
        out = "speed " + std::to_string(speed_);
    } else if (cmd == "spawn" && w.size() >= 2) {
        if (needPlayer()) {
            const auto& at = registry_.get<WorldTransform2D>(*player);
            out = session_.Spawn(w[1], at.x + 1.0f, at.y) ? "spawned " + w[1] : "couldn't spawn " + w[1];
        }
    } else if (cmd == "achieve" && w.size() >= 2) {
        out = session_.Achievements().Unlock(w[1]) ? "unlocked " + w[1] : "no achievement " + w[1] + " (or already unlocked)";
    } else if (cmd == "heal") {
        if (needPlayer()) {
            if (auto* h = registry_.try_get<Health>(*player)) h->current = h->max;
            out = "healed";
        }
    } else if (cmd == "kill") {
        int n = 0;
        if (needPlayer()) {
            for (auto [e, h] : registry_.view<Health>().each()) {
                if (e != *player && AreHostile(registry_, e, *player) && h.current > 0) {
                    h.current = 0;
                    ++n;
                }
            }
            out = std::to_string(n) + " killed";
        }
    } else if (cmd == "profiler") {
        profiler_ = !profiler_;
        out = profiler_ ? "profiler on" : "profiler off";
    } else {
        out = "unknown command: " + cmd + " (help)";
    }
    if (!out.empty()) Print(out);
    return out;
}

} // namespace aige
