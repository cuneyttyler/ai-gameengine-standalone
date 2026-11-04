#include "gameplay/Paths.h"

#include "physics2d/Components.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace aige {

namespace {

nlohmann::json ParseList(const std::string& text) {
    if (text.empty()) return nlohmann::json::array();
    const nlohmann::json list = nlohmann::json::parse(text, nullptr, false);
    return list.is_array() ? list : nlohmann::json::array();
}

std::string Text(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_array() || value.is_object()) return value.dump();
    return {};
}

} // namespace

PathGraph ReadPath(const Path& path, float ox, float oy) {
    PathGraph graph;
    graph.name = path.name;
    graph.end = path.end.empty() ? std::string("stop") : path.end;
    for (const auto& p : ParseList(path.points)) {
        if (!p.is_object()) continue;
        PathPoint point;
        point.x = ox + p.value("x", 0.0f);
        point.y = oy + p.value("y", 0.0f);
        point.wait = p.value("wait", 0.0f);
        point.clip = p.value("clip", std::string());
        point.use = p.value("use", std::string());
        point.doEffects = Text(p.value("do", nlohmann::json()));
        point.label = p.value("label", std::string());
        graph.points.push_back(point);
    }
    const int n = static_cast<int>(graph.points.size());
    const nlohmann::json links = ParseList(path.links);
    if (links.empty()) {
        for (int i = 0; i + 1 < n; ++i) graph.links.push_back({i, i + 1});
        if (graph.end == "loop" && n > 2) graph.links.push_back({n - 1, 0});
        return graph;
    }
    for (const auto& l : links) {
        if (!l.is_object()) continue;
        PathLink link;
        link.from = l.value("from", 0);
        link.to = l.value("to", 0);
        link.weight = l.value("weight", 1.0f);
        link.conditions = Text(l.value("if", nlohmann::json()));
        link.priority = l.value("priority", false);
        link.fallback = l.value("default", false);
        link.doEffects = Text(l.value("do", nlohmann::json()));
        link.label = l.value("label", std::string());
        graph.links.push_back(link);
    }
    return graph;
}

void WritePath(const PathGraph& graph, Path& path, float ox, float oy) {
    path.name = graph.name;
    path.end = graph.end;
    nlohmann::json points = nlohmann::json::array();
    for (const auto& p : graph.points) {
        nlohmann::json item = {{"x", std::round((p.x - ox) * 100.0f) / 100.0f}, {"y", std::round((p.y - oy) * 100.0f) / 100.0f}};
        if (p.wait > 0.0f) item["wait"] = p.wait;
        if (!p.clip.empty()) item["clip"] = p.clip;
        if (!p.use.empty()) item["use"] = p.use;
        if (!p.doEffects.empty()) item["do"] = p.doEffects;
        if (!p.label.empty()) item["label"] = p.label;
        points.push_back(item);
    }
    path.points = points.dump();
    // Links simply in order (and round for a loop) aren't written.
    const int n = static_cast<int>(graph.points.size());
    bool inOrder = true;
    size_t expected = n > 0 ? static_cast<size_t>(n - 1) + (graph.end == "loop" && n > 2 ? 1 : 0) : 0;
    if (graph.links.size() != expected) inOrder = false;
    for (size_t i = 0; inOrder && i < graph.links.size(); ++i) {
        const PathLink& l = graph.links[i];
        const int to = static_cast<int>(i) + 1 < n ? static_cast<int>(i) + 1 : 0;
        if (l.from != static_cast<int>(i) || l.to != to || l.weight != 1.0f || !l.conditions.empty() || l.priority || l.fallback ||
            !l.doEffects.empty() || !l.label.empty()) {
            inOrder = false;
        }
    }
    if (inOrder) {
        path.links.clear();
        return;
    }
    nlohmann::json links = nlohmann::json::array();
    for (const auto& l : graph.links) {
        nlohmann::json item = {{"from", l.from}, {"to", l.to}};
        if (l.weight != 1.0f) item["weight"] = l.weight;
        if (!l.conditions.empty()) item["if"] = l.conditions;
        if (l.priority) item["priority"] = true;
        if (l.fallback) item["default"] = true;
        if (!l.doEffects.empty()) item["do"] = l.doEffects;
        if (!l.label.empty()) item["label"] = l.label;
        links.push_back(item);
    }
    path.links = links.dump();
}

std::optional<Entity> FindPathEntity(Registry& registry, const std::string& name) {
    if (name.empty()) return std::nullopt;
    for (auto [e, path] : registry.view<Path>().each()) {
        if (path.name == name) return e;
    }
    return std::nullopt;
}

std::optional<PathGraph> FindNamedPath(Registry& registry, const std::string& name) {
    const auto e = FindPathEntity(registry, name);
    if (!e) return std::nullopt;
    float ox = 0.0f, oy = 0.0f;
    if (const auto* w = registry.try_get<WorldTransform2D>(*e)) ox = w->x, oy = w->y;
    return ReadPath(registry.get<Path>(*e), ox, oy);
}

int OtherEnd(const PathLink& link, int at) { return link.from == at ? link.to : link.from; }

std::vector<int> LinksFrom(const PathGraph& graph, int at, bool both, int cameFrom) {
    std::vector<int> out;
    for (size_t i = 0; i < graph.links.size(); ++i) {
        const PathLink& l = graph.links[i];
        if (l.from == at || (both && l.to == at)) out.push_back(static_cast<int>(i));
    }
    if (cameFrom >= 0 && out.size() > 1) {
        std::vector<int> onward;
        for (int i : out) {
            if (OtherEnd(graph.links[static_cast<size_t>(i)], at) != cameFrom) onward.push_back(i);
        }
        if (!onward.empty()) out = onward;
    }
    return out;
}

int NearestPoint(const PathGraph& graph, float x, float y) {
    int best = -1;
    float nearest = 1e9f;
    for (size_t i = 0; i < graph.points.size(); ++i) {
        const float d = std::hypot(graph.points[i].x - x, graph.points[i].y - y);
        if (d < nearest) nearest = d, best = static_cast<int>(i);
    }
    return best;
}

int ChooseLink(const PathGraph& graph, const std::vector<int>& links, const std::function<bool(const PathLink&)>& holds,
               std::mt19937& rng, const nlohmann::json& weights) {
    std::vector<int> open;
    int fallback = -1;
    for (int i : links) {
        const PathLink& l = graph.links[static_cast<size_t>(i)];
        if (l.fallback && fallback < 0) fallback = i;
        if (!holds || holds(l)) open.push_back(i);
    }
    for (int i : open) {
        if (graph.links[static_cast<size_t>(i)].priority) return i;
    }
    auto weightOf = [&](int i) {
        const PathLink& l = graph.links[static_cast<size_t>(i)];
        if (!l.label.empty() && weights.is_object() && weights.contains(l.label) && weights[l.label].is_number()) {
            return std::max(0.0f, weights[l.label].get<float>());
        }
        return std::max(0.0f, l.weight);
    };
    float total = 0.0f;
    for (int i : open) total += weightOf(i);
    if (open.empty() || total <= 0.0f) return fallback;
    float pick = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (int i : open) {
        pick -= weightOf(i);
        if (pick <= 0.0f) return i;
    }
    return open.back();
}

bool LinkConditionsHold(const std::string& conditions, const std::function<bool(const nlohmann::json&)>& special,
                        const std::function<bool(const std::string&)>& story) {
    if (conditions.empty()) return true;
    const nlohmann::json list = nlohmann::json::parse(conditions, nullptr, false);
    if (!list.is_array()) return false;
    nlohmann::json rest = nlohmann::json::array();
    for (const auto& c : list) {
        if (c.is_object() && (c.contains("playerNear") || c.contains("partOpen") || c.contains("free"))) {
            if (special && !special(c)) return false;
        } else {
            rest.push_back(c);
        }
    }
    return rest.empty() || !story || story(rest.dump());
}

std::vector<std::string> CheckPath(const PathGraph& graph) {
    std::vector<std::string> problems;
    const int n = static_cast<int>(graph.points.size());
    if (n < 2) problems.push_back("path " + graph.name + ": it needs at least two points");
    static const std::set<std::string> kEnds = {"stop", "loop", "back"};
    if (!kEnds.contains(graph.end)) problems.push_back("path " + graph.name + ": end is stop, loop or back, not " + graph.end);
    for (size_t i = 0; i < graph.links.size(); ++i) {
        const PathLink& l = graph.links[i];
        const std::string where = "path " + graph.name + ", link " + std::to_string(i);
        if (l.from < 0 || l.from >= n || l.to < 0 || l.to >= n) problems.push_back(where + ": it joins points the path hasn't");
        if (l.from == l.to) problems.push_back(where + ": it joins a point to itself");
        if (l.weight <= 0.0f && !l.fallback) problems.push_back(where + ": its weight must be above zero");
        if (!l.conditions.empty() && !nlohmann::json::parse(l.conditions, nullptr, false).is_array()) {
            problems.push_back(where + ": its conditions must be a JSON list");
        }
    }
    return problems;
}

std::vector<int> PathRoute(const PathGraph& graph, int start, const std::function<bool(const PathLink&)>& holds,
                           std::mt19937& rng, int maxSteps) {
    std::vector<int> route;
    if (start < 0 || start >= static_cast<int>(graph.points.size())) return route;
    route.push_back(start);
    int at = start, cameFrom = -1;
    for (int step = 0; step < maxSteps; ++step) {
        const int link = ChooseLink(graph, LinksFrom(graph, at, false, cameFrom), holds, rng);
        if (link < 0) break;
        const int next = OtherEnd(graph.links[static_cast<size_t>(link)], at);
        route.push_back(next);
        cameFrom = at;
        at = next;
        if (at == start) break; // a lap
    }
    if (graph.end == "back" && route.size() > 1) {
        for (int i = static_cast<int>(route.size()) - 2; i >= 0; --i) route.push_back(route[static_cast<size_t>(i)]);
    }
    return route;
}

} // namespace aige
