#include "ai_bridge/DryRun.h"

#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "scripting/LuaVM.h"
#include "gameplay/Components.h"
#include "gameplay/InputMap.h"
#include "gameplay/Items.h"
#include "physics2d/Components.h"

#include <cmath>
#include <set>

#include <algorithm>

namespace aige {

std::vector<ScriptProblem> DryRunScripts(const std::string& sceneText, const Project& project, double seconds,
                                         const std::vector<std::string>& onlyScripts) {
    // lua must outlive registry (scripts hold environments in its state).
    LuaVM lua;
    Registry registry;
    DeserializeScene(registry, sceneText);
    GameSession session(registry, lua, BootstrapIso(), project.root);

    auto noInput = [](Key) { return false; };
    for (double elapsed = 0.0; elapsed < seconds; elapsed += GameSession::kFixedDt) {
        session.Advance(GameSession::kFixedDt, noInput);
    }

    std::vector<ScriptProblem> problems;
    for (const auto& error : session.ScriptErrors()) {
        if (!onlyScripts.empty() &&
            std::find(onlyScripts.begin(), onlyScripts.end(), error.path) == onlyScripts.end()) {
            continue;
        }
        auto same = std::find_if(problems.begin(), problems.end(), [&](const ScriptProblem& p) {
            return p.path == error.path && p.message == error.message;
        });
        if (same != problems.end()) {
            ++same->count;
        } else {
            problems.push_back({error.path, error.message, 1});
        }
    }
    return problems;
}

nlohmann::json PlaytestScene(const std::string& sceneText, const Project& project, double seconds,
                             const std::vector<std::string>& press) {
    LuaVM lua;
    Registry registry;
    DeserializeScene(registry, sceneText);
    GameSession session(registry, lua, BootstrapIso(), project.root);
    const auto player = FindPlayer(registry);
    const auto* start = player ? registry.try_get<WorldTransform2D>(*player) : nullptr;
    const float x0 = start ? start->x : 0.0f, y0 = start ? start->y : 0.0f;
    // Each press: its action's first key held for a quarter second (walking: a second), then let go.
    struct Press {
        Key key;
        double from, to;
    };
    std::vector<Press> presses;
    double at = 0.3;
    for (const std::string& want : press) {
        std::string action = want;
        float scale = 1.0f;
        if (!action.empty() && (action.back() == '+' || action.back() == '-')) {
            scale = action.back() == '+' ? 1.0f : -1.0f;
            action.pop_back();
        }
        const InputAction* a = ActiveInputMap().Find(action);
        if (!a) continue;
        for (const auto& k : a->keys) {
            if (a->axis ? k.scale * scale > 0 : true) {
                const double hold = a->axis ? 1.0 : 0.25;
                presses.push_back({k.key, at, at + hold});
                at += hold + 0.3;
                break;
            }
        }
    }
    double now = 0.0;
    auto keys = [&](Key k) {
        for (const auto& p : presses) {
            if (p.key == k && now >= p.from && now < p.to) return true;
        }
        return false;
    };
    nlohmann::json events = nlohmann::json::array();
    size_t seen = 0;
    const double total = std::max(seconds, at + 0.5);
    for (; now < total; now += GameSession::kFixedDt) {
        session.Advance(GameSession::kFixedDt, keys);
        const auto& delivered = session.DeliveredEvents();
        for (; seen < delivered.size() && events.size() < 60; ++seen) {
            const auto& e = delivered[seen];
            nlohmann::json row = {{"t", std::round(now * 10) / 10}, {"event", e.name}};
            if (!e.text.empty()) row["text"] = e.text;
            if (registry.valid(e.entity)) {
                if (const auto* n = registry.try_get<Name>(e.entity)) row["entity"] = n->value;
            }
            events.push_back(row);
        }
    }
    nlohmann::json out = {{"seconds", std::round(total * 10) / 10}, {"events", events}};
    if (player && registry.valid(*player)) {
        if (const auto* w = registry.try_get<WorldTransform2D>(*player)) {
            out["player"] = {{"from", {x0, y0}}, {"to", {std::round(w->x * 10) / 10, std::round(w->y * 10) / 10}}};
        }
        if (const auto* inv = registry.try_get<Inventory>(*player)) out["player"]["inventory"] = inv->items, out["player"]["gold"] = inv->gold;
        if (const auto* h = registry.try_get<Health>(*player)) out["player"]["health"] = h->current;
    }
    nlohmann::json errors = nlohmann::json::array();
    std::set<std::string> told;
    for (const auto& error : session.ScriptErrors()) {
        if (told.insert(error.path + error.message).second) errors.push_back({{"script", error.path}, {"message", error.message}});
    }
    out["errors"] = errors;
    return out;
}

} // namespace aige
