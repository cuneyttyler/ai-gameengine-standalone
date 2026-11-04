#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aige {

// Paths characters follow (docs/ai/ai-paths.md): drawn in the viewport, named, shared by any number of packages. A path
// is a small network: points, and links between them that fork (chosen at random by weight, or by conditions) and join
// again. Each character keeps its own place on it.
struct Path {
    std::string name;   // packages refer to it by this ("market-loop")
    // Its points, a JSON list, in cells from the path's own position (moving it moves them all):
    // [{"x", "y", "wait": seconds, "clip": "<clip played there>", "use": "<a part used there: cook>", "do": "<effects>",
    //   "label"}]
    std::string points;
    // Its links, a JSON list (empty: point to point in order): [{"from", "to", "weight": 1, "if": "<conditions>",
    //   "priority": false, "default": false, "do": "<effects when taken>", "label"}]
    std::string links;
    // At an end (no way on): "stop" (done), "loop" (the last point goes on to the first, when its links are in order),
    // or "back" (the way it came, backwards).
    std::string end = "stop";
    bool shown = true; // drawn in the editor's viewport (View ▸ Paths shows or hides them all)
};

struct PathPoint {
    float x = 0.0f, y = 0.0f; // in the world (cells)
    float wait = 0.0f;
    std::string clip, use, doEffects, label;
};

struct PathLink {
    int from = 0, to = 0;
    float weight = 1.0f;
    std::string conditions; // as dialogue's conditions (JSON text), plus playerNear, partOpen, free (§3)
    bool priority = false;  // taken whenever its conditions hold
    bool fallback = false;  // taken when none holds ("default")
    std::string doEffects, label;
};

struct PathGraph {
    std::string name;
    std::vector<PathPoint> points;
    std::vector<PathLink> links;
    std::string end = "stop";
};

// The path as a graph, its points in the world (the path's position `ox`, `oy` added); links in order when none are
// given (and the last to the first for end "loop").
PathGraph ReadPath(const Path& path, float ox = 0.0f, float oy = 0.0f);
// Back into the component: points relative to (`ox`, `oy`), links only when they aren't simply in order.
void WritePath(const PathGraph& graph, Path& path, float ox = 0.0f, float oy = 0.0f);
// The path entity named so (nullopt: none).
std::optional<Entity> FindPathEntity(Registry& registry, const std::string& name);
// The graph of the path named so, in the world.
std::optional<PathGraph> FindNamedPath(Registry& registry, const std::string& name);

// The links leaving a point (`both`: either way, as a wanderer walks them; `cameFrom` left out unless it's the only
// one).
std::vector<int> LinksFrom(const PathGraph& graph, int at, bool both = false, int cameFrom = -1);
// The other end of a link from `at`.
int OtherEnd(const PathLink& link, int at);
// The point nearest (x, y); -1 when it has none.
int NearestPoint(const PathGraph& graph, float x, float y);

// Which link to take at a fork (§3): those whose conditions hold; of them a priority one; else by weight (`weights`
// overrides by link label, a package's own preference); none holding: the default one; -1 when there's none.
int ChooseLink(const PathGraph& graph, const std::vector<int>& links, const std::function<bool(const PathLink&)>& holds,
               std::mt19937& rng, const nlohmann::json& weights = nlohmann::json::object());

// A link's conditions: the path's own ({"playerNear": cells}, {"partOpen": "<part name>"}, {"free": true}: nobody else
// at its far point) checked by `special`, the rest (dialogue's: flags, quests, items, time) by `story` as JSON text.
bool LinkConditionsHold(const std::string& conditions, const std::function<bool(const nlohmann::json&)>& special,
                        const std::function<bool(const std::string&)>& story);

// What's wrong with a path, in words: no points, links to points it hasn't, weights not above zero, conditions that
// aren't JSON lists.
std::vector<std::string> CheckPath(const PathGraph& graph);

// A whole walk from `start` to an end (a loop: one lap), choosing at each fork: the points in order (for scripts'
// follow_path).
std::vector<int> PathRoute(const PathGraph& graph, int start, const std::function<bool(const PathLink&)>& holds,
                           std::mt19937& rng, int maxSteps = 200);

} // namespace aige
