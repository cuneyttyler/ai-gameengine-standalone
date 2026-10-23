#pragma once

#include "ecs/Registry.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace aige {

class GameSession;
class LuaVM;

// The in-game debug console (development_plan.md 24): ` opens it in play (when the
// project allows it: Project Settings ▸ Game ▸ debug console; off in exports unless asked).
// A line is a command or Lua:
//   help                      the commands
//   lua <code> / = <expr>     runs Lua in the game's state; "= expr" prints its value
//   god                       the player can't be hurt (again: off)
//   give <item> [count]       into the player's inventory
//   tp <x> <y>                moves the player (cells)
//   scene <path> [marker]     travels there
//   quest <id> start|complete
//   flag <name> [on|off]
//   speed <0.1-4>             the game's speed
//   spawn <prefab>            beside the player
//   achieve <id>              unlocks an achievement
//   heal / kill               the player's health full / every hostile in the scene dies
//   profiler                  the profiler overlay on or off
//   clear
class DebugConsole {
public:
    DebugConsole(GameSession& session, Registry& registry, LuaVM& lua);

    bool Open() const { return open_; }
    void Toggle() { open_ = !open_; }
    void SetOpen(bool open) { open_ = open; }

    // Typing: characters, Backspace, Enter (runs the line), Up/Down (history).
    void Type(unsigned codepoint);
    void Backspace();
    void Submit();
    void HistoryUp();
    void HistoryDown();
    const std::string& Input() const { return input_; }

    // Runs one line; its output is added to the log (and returned).
    std::string Execute(const std::string& line);
    const std::deque<std::string>& Log() const { return log_; }
    // The overlay's lines: the last ones of the log, then the prompt.
    std::vector<std::string> Lines(size_t count = 14) const;

    bool God() const { return god_; }
    float Speed() const { return speed_; }
    bool ProfilerShown() const { return profiler_; }

private:
    GameSession& session_;
    Registry& registry_;
    LuaVM& lua_;
    bool open_ = false;
    std::string input_;
    std::deque<std::string> log_;
    std::vector<std::string> history_;
    int historyAt_ = -1;
    bool god_ = false;
    float speed_ = 1.0f;
    bool profiler_ = false;

    void Print(const std::string& text);
};

} // namespace aige
