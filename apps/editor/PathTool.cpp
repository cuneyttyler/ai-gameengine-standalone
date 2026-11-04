// Paths characters follow (docs/ai/ai-paths.md): drawn with the viewport's Path tool, shown over the scene (each path's
// links and points, forks coloured by how they're chosen), edited in the Inspector, renamed with the packages that use
// them following. In Play, each character's line to the point it's walking to.

#include "EditorApp.h"

#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Isometric.h"
#include "gameplay/Paths.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace aige {

namespace {

constexpr float kPickPixels = 9.0f; // how near (scene pixels at zoom 1) a click picks a point

Path& PathOf(Registry& registry, Entity e) { return registry.get<Path>(e); }

std::pair<float, float> Origin(Registry& registry, Entity e) {
    const auto* w = registry.try_get<WorldTransform2D>(e);
    return w ? std::pair{w->x, w->y} : std::pair{0.0f, 0.0f};
}

// The graph with explicit links (a fork or a join needs them).
PathGraph Explicit(const PathGraph& graph) { return graph; } // ReadPath already lists links in order

void Line(std::vector<OverlayLine>& out, const ScreenPose& a, const ScreenPose& b, const float* rgb, float alpha, float thickness) {
    out.push_back({a.x, a.y, b.x, b.y, thickness, rgb[0], rgb[1], rgb[2], alpha});
}

} // namespace

std::optional<std::pair<Entity, int>> EditorApp::PathPointAt(float sceneX, float sceneY) {
    std::optional<std::pair<Entity, int>> best;
    float nearest = kPickPixels / std::max(0.2f, camera_.zoom);
    for (auto [e, path] : registry_.view<Path>().each()) {
        if (!path.shown && e != pathEditing_ && !showPaths_) continue;
        const auto [ox, oy] = Origin(registry_, e);
        const PathGraph graph = ReadPath(path, ox, oy);
        for (size_t i = 0; i < graph.points.size(); ++i) {
            const ScreenPose p = ProjectIso(graph.points[i].x, graph.points[i].y, iso_);
            const float d = std::hypot(p.x - sceneX, p.y - sceneY);
            // The path being drawn first, then the nearest.
            if (d < nearest || (d < kPickPixels * 1.5f / std::max(0.2f, camera_.zoom) && e == pathEditing_ && (!best || best->first != pathEditing_))) {
                nearest = d;
                best = std::pair{e, static_cast<int>(i)};
            }
        }
    }
    return best;
}

void EditorApp::PathToolDown(float sceneX, float sceneY, bool shift) {
    // On a point: picked (dragged to move it); with Shift, the picked point is joined to it (a fork rejoining).
    if (const auto hit = PathPointAt(sceneX, sceneY)) {
        if (shift && hit->first == pathEditing_ && pathPoint_ >= 0 && pathPoint_ != hit->second) {
            const int from = pathPoint_, to = hit->second;
            ApplyOneShotEdit([&] {
                const auto [ox, oy] = Origin(registry_, pathEditing_);
                PathGraph graph = Explicit(ReadPath(PathOf(registry_, pathEditing_), ox, oy));
                const bool there = std::any_of(graph.links.begin(), graph.links.end(), [&](const PathLink& l) { return l.from == from && l.to == to; });
                if (!there) graph.links.push_back({from, to});
                WritePath(graph, PathOf(registry_, pathEditing_), ox, oy);
                return !there;
            });
            pathPoint_ = to;
        } else {
            pathEditing_ = hit->first;
            pathPoint_ = hit->second;
            pathDragging_ = true;
            BeginEdit();
        }
        Select(pathEditing_);
        inspectorStale_ = true;
        return;
    }
    // On the ground: a point there (a cell's middle), linked from the picked point (the last, or one picked to branch
    // from: a fork), on the path being drawn; none: a new path begins.
    float wx = 0.0f, wy = 0.0f;
    UnprojectIso(sceneX, sceneY, iso_, wx, wy);
    wx = std::floor(wx) + 0.5f, wy = std::floor(wy) + 0.5f;
    ApplyOneShotEdit([&] {
        if (!registry_.valid(pathEditing_) || !registry_.all_of<Path>(pathEditing_)) {
            int n = 1;
            while (FindPathEntity(registry_, "path-" + std::to_string(n))) ++n;
            pathEditing_ = registry_.create();
            registry_.emplace<Name>(pathEditing_, "Path path-" + std::to_string(n));
            registry_.emplace<WorldTransform2D>(pathEditing_, wx, wy);
            registry_.emplace<ScreenTransform2D>(pathEditing_);
            Path& made = registry_.emplace<Path>(pathEditing_);
            made.name = "path-" + std::to_string(n);
            made.points = nlohmann::json::array({{{"x", 0.0f}, {"y", 0.0f}}}).dump();
            pathPoint_ = 0;
            return true;
        }
        const auto [ox, oy] = Origin(registry_, pathEditing_);
        PathGraph graph = Explicit(ReadPath(PathOf(registry_, pathEditing_), ox, oy));
        const int from = pathPoint_ >= 0 && pathPoint_ < static_cast<int>(graph.points.size()) ? pathPoint_
                                                                                              : static_cast<int>(graph.points.size()) - 1;
        PathPoint point;
        point.x = wx, point.y = wy;
        graph.points.push_back(point);
        const int to = static_cast<int>(graph.points.size()) - 1;
        if (from >= 0) graph.links.push_back({from, to});
        WritePath(graph, PathOf(registry_, pathEditing_), ox, oy);
        pathPoint_ = to;
        return true;
    });
    Select(pathEditing_);
    inspectorStale_ = true;
}

void EditorApp::PathToolMove(float sceneX, float sceneY) {
    if (!pathDragging_ || !registry_.valid(pathEditing_) || !registry_.all_of<Path>(pathEditing_)) return;
    float wx = 0.0f, wy = 0.0f;
    UnprojectIso(sceneX, sceneY, iso_, wx, wy);
    wx = std::floor(wx) + 0.5f, wy = std::floor(wy) + 0.5f;
    const auto [ox, oy] = Origin(registry_, pathEditing_);
    PathGraph graph = ReadPath(PathOf(registry_, pathEditing_), ox, oy);
    if (pathPoint_ < 0 || pathPoint_ >= static_cast<int>(graph.points.size())) return;
    PathPoint& p = graph.points[static_cast<size_t>(pathPoint_)];
    if (p.x == wx && p.y == wy) return;
    p.x = wx, p.y = wy;
    WritePath(graph, PathOf(registry_, pathEditing_), ox, oy);
    MarkChanged();
    inspectorStale_ = true;
}

void EditorApp::PathToolUp() {
    if (!pathDragging_) return;
    pathDragging_ = false;
    EndEdit();
}

void EditorApp::FinishPath() {
    pathEditing_ = entt::null;
    pathPoint_ = -1;
    pathDragging_ = false;
}

std::vector<std::string> EditorApp::PackagesUsingPath(const std::string& name) {
    // A package names it in its data (data.route) or its own procedure ("path": "<name>").
    std::vector<std::string> out;
    if (name.empty()) return out;
    const std::string quoted = "\"" + name + "\"";
    for (const Record* r : ActiveRecords().OfType("package")) {
        const auto& d = r->data;
        const bool byData = d.contains("data") && d["data"].is_object() && d["data"].value("route", nlohmann::json()) == name;
        const std::string procedure = d.contains("procedure") ? (d["procedure"].is_string() ? d["procedure"].get<std::string>() : d["procedure"].dump()) : std::string();
        if (byData || procedure.find(quoted) != std::string::npos) out.push_back(r->id);
    }
    return out;
}

std::vector<Entity> EditorApp::CharactersUsingPath(const std::string& name) {
    const auto packages = PackagesUsingPath(name);
    std::vector<Entity> out;
    for (auto [e, behaviour] : registry_.view<Behaviour>().each()) {
        for (const auto& p : packages) {
            if ((", " + behaviour.packages + ",").find(" " + p + ",") != std::string::npos ||
                ("," + behaviour.packages + ",").find("," + p + ",") != std::string::npos) {
                out.push_back(e);
                break;
            }
        }
    }
    return out;
}

void EditorApp::AddPathOverlay(std::vector<OverlayLine>& overlay) {
    static constexpr float kPath[3] = {0.45f, 0.85f, 0.95f};     // a link
    static constexpr float kChosen[3] = {0.98f, 0.62f, 0.25f};   // a link with conditions
    static constexpr float kPriority[3] = {0.95f, 0.35f, 0.35f}; // taken whenever it can be
    static constexpr float kDefault[3] = {0.6f, 0.6f, 0.65f};    // taken when no other can
    static constexpr float kAccent[3] = {0.98f, 0.89f, 0.45f};
    static constexpr float kWalker[3] = {0.55f, 0.95f, 0.55f};
    if (playing_) {
        // Each character on a path: a line to the point it's walking to.
        if (!showPaths_) return;
        for (auto [e, s, w] : registry_.view<BehaviourState, WorldTransform2D>().each()) {
            auto pathIt = s.blackboard.find("path");
            auto pointIt = s.blackboard.find("pathPoint");
            if (pathIt == s.blackboard.end() || pointIt == s.blackboard.end() || !pathIt->second.is_string() || !pointIt->second.is_number()) continue;
            const auto graph = FindNamedPath(registry_, pathIt->second.get<std::string>());
            const int i = pointIt->second.get<int>();
            if (!graph || i < 0 || i >= static_cast<int>(graph->points.size())) continue;
            Line(overlay, ProjectIso(w.x, w.y, iso_), ProjectIso(graph->points[static_cast<size_t>(i)].x, graph->points[static_cast<size_t>(i)].y, iso_),
                 kWalker, 0.85f, 1.5f);
        }
        return;
    }
    // The paths the selected character's packages use: drawn bright.
    std::set<std::string> ofSelected;
    if (selected_ && registry_.valid(*selected_)) {
        if (const auto* b = registry_.try_get<Behaviour>(*selected_)) {
            for (auto [e, path] : registry_.view<Path>().each()) {
                const auto users = CharactersUsingPath(path.name);
                if (std::find(users.begin(), users.end(), *selected_) != users.end()) ofSelected.insert(path.name);
            }
            (void)b;
        }
    }
    for (auto [e, path] : registry_.view<Path>().each()) {
        const bool focus = e == pathEditing_ || (selected_ && *selected_ == e) || ofSelected.contains(path.name);
        if (!focus && (!showPaths_ || !path.shown)) continue;
        const auto [ox, oy] = Origin(registry_, e);
        const PathGraph graph = ReadPath(path, ox, oy);
        const float alpha = focus ? 0.95f : 0.45f;
        const float thick = focus ? 2.0f : 1.2f;
        for (const PathLink& l : graph.links) {
            if (l.from < 0 || l.to < 0 || l.from >= static_cast<int>(graph.points.size()) || l.to >= static_cast<int>(graph.points.size())) continue;
            const PathPoint& a = graph.points[static_cast<size_t>(l.from)];
            const PathPoint& b = graph.points[static_cast<size_t>(l.to)];
            const float* rgb = l.priority ? kPriority : l.fallback ? kDefault : !l.conditions.empty() ? kChosen : kPath;
            const ScreenPose pa = ProjectIso(a.x, a.y, iso_), pb = ProjectIso(b.x, b.y, iso_);
            Line(overlay, pa, pb, rgb, alpha, thick);
            // Its way: an arrowhead at its middle; a weighted one thicker for its share.
            const float mx = (pa.x + pb.x) * 0.5f, my = (pa.y + pb.y) * 0.5f;
            const float dx = pb.x - pa.x, dy = pb.y - pa.y, len = std::max(0.001f, std::hypot(dx, dy));
            const float ux = dx / len, uy = dy / len, s = 6.0f + std::min(4.0f, l.weight);
            const ScreenPose tip{mx + ux * s * 0.5f, my + uy * s * 0.5f, 0.0f};
            Line(overlay, tip, ScreenPose{tip.x - ux * s - uy * s * 0.6f, tip.y - uy * s + ux * s * 0.6f, 0.0f}, rgb, alpha, thick);
            Line(overlay, tip, ScreenPose{tip.x - ux * s + uy * s * 0.6f, tip.y - uy * s - ux * s * 0.6f, 0.0f}, rgb, alpha, thick);
        }
        // Its points: a small diamond each, a stop (wait, clip, part, effects) filled with a cross; the picked one bright.
        for (size_t i = 0; i < graph.points.size(); ++i) {
            const PathPoint& p = graph.points[i];
            const ScreenPose c = ProjectIso(p.x, p.y, iso_);
            const bool picked = e == pathEditing_ && static_cast<int>(i) == pathPoint_;
            const float r = picked ? 6.0f : focus ? 4.5f : 3.5f;
            const float* rgb = picked ? kAccent : kPath;
            const ScreenPose n{c.x, c.y - r, 0}, east{c.x + r, c.y, 0}, south{c.x, c.y + r, 0}, west{c.x - r, c.y, 0};
            Line(overlay, n, east, rgb, alpha, thick);
            Line(overlay, east, south, rgb, alpha, thick);
            Line(overlay, south, west, rgb, alpha, thick);
            Line(overlay, west, n, rgb, alpha, thick);
            if (p.wait > 0.0f || !p.clip.empty() || !p.use.empty() || !p.doEffects.empty()) {
                Line(overlay, n, south, rgb, alpha, 1.0f);
                Line(overlay, west, east, rgb, alpha, 1.0f);
            }
        }
    }
}

bool EditorApp::HandlePathMessage(const std::string& type, const nlohmann::json& message) {
    const Entity e = static_cast<Entity>(message.value("id", 0u));
    const bool isPath = registry_.valid(e) && registry_.all_of<Path>(e);
    if (type == "path:show") {
        showPaths_ = message.value("shown", !showPaths_);
        editorStateStale_ = true;
        return true;
    }
    if (type == "path:finish") {
        FinishPath();
        return true;
    }
    if (type == "path:edit" && isPath) {
        // Draw on (more points, a fork from a point) with the Path tool.
        pathEditing_ = e;
        pathPoint_ = message.value("point", -1);
        SetTool(Tool::Path);
        return true;
    }
    if (type == "path:point" && isPath) {
        // A point's menu (right-click on it): branch from it, delete it, or set what's done there.
        const int index = message.value("point", -1);
        const std::string op = message.value("op", "");
        if (op == "branch") {
            pathEditing_ = e;
            pathPoint_ = index;
            SetTool(Tool::Path);
            post_({{"type", "notify"}, {"text", "Click where the branch goes: it forks from this point (Shift+click a point to join it)"}});
            return true;
        }
        ApplyOneShotEdit([&] {
            const auto [ox, oy] = Origin(registry_, e);
            PathGraph graph = ReadPath(PathOf(registry_, e), ox, oy);
            if (index < 0 || index >= static_cast<int>(graph.points.size())) return false;
            if (op == "delete") {
                // Its links go; the links past it close the gap (a point in a line: its neighbours joined).
                std::vector<int> into, outOf;
                std::vector<PathLink> kept;
                for (const PathLink& l : graph.links) {
                    if (l.to == index) into.push_back(l.from);
                    else if (l.from == index) outOf.push_back(l.to);
                    else kept.push_back(l);
                }
                if (into.size() == 1 && outOf.size() == 1) kept.push_back({into[0], outOf[0]});
                for (PathLink& l : kept) {
                    if (l.from > index) --l.from;
                    if (l.to > index) --l.to;
                }
                graph.links = kept;
                graph.points.erase(graph.points.begin() + index);
                if (pathPoint_ >= static_cast<int>(graph.points.size())) pathPoint_ = static_cast<int>(graph.points.size()) - 1;
                if (graph.points.empty()) {
                    registry_.destroy(e);
                    if (pathEditing_ == e) FinishPath();
                    return true;
                }
            } else {
                PathPoint& p = graph.points[static_cast<size_t>(index)];
                if (op == "wait") p.wait = std::max(0.0f, message.value("value", 0.0f));
                else if (op == "clip") p.clip = message.value("value", std::string());
                else if (op == "use") p.use = message.value("value", std::string());
                else if (op == "label") p.label = message.value("value", std::string());
                else if (op == "do") p.doEffects = message.value("value", std::string());
                else return false;
            }
            WritePath(graph, PathOf(registry_, e), ox, oy);
            return true;
        });
        inspectorStale_ = true;
        return true;
    }
    if (type == "path:rename" && isPath) {
        // Renamed: every package that names it follows (data.route, its procedure's "path").
        const std::string from = PathOf(registry_, e).name, to = message.value("name", std::string());
        if (to.empty() || to == from) return true;
        if (FindPathEntity(registry_, to)) {
            post_({{"type", "notify"}, {"text", "There's already a path called " + to}});
            return true;
        }
        const auto packages = PackagesUsingPath(from);
        ApplyOneShotEdit([&] {
            PathOf(registry_, e).name = to;
            if (auto* name = registry_.try_get<Name>(e)) name->value = "Path " + to;
            return true;
        });
        for (const auto& id : packages) {
            const Record* r = ActiveRecords().Find("package", id);
            if (!r) continue;
            nlohmann::json data = r->data;
            if (data.contains("data") && data["data"].is_object() && data["data"].value("route", nlohmann::json()) == from) data["data"]["route"] = to;
            if (data.contains("procedure")) {
                std::string text = data["procedure"].is_string() ? data["procedure"].get<std::string>() : data["procedure"].dump();
                const std::string a = "\"" + from + "\"", b = "\"" + to + "\"";
                for (size_t at = text.find(a); at != std::string::npos; at = text.find(a, at + b.size())) text.replace(at, a.size(), b);
                if (data["procedure"].is_string()) data["procedure"] = text;
                else data["procedure"] = nlohmann::json::parse(text, nullptr, false);
            }
            ActiveRecords().Write("package", id, data);
        }
        post_({{"type", "notify"}, {"text", "Renamed path " + from + " to " + to +
                                                (packages.empty() ? std::string() : " (" + std::to_string(packages.size()) + " package(s) follow)")}});
        inspectorStale_ = true;
        sceneTreeStale_ = true;
        return true;
    }
    if (type == "path:info" && isPath) {
        // Who uses it: the packages naming it and the characters whose stacks hold them.
        const std::string name = PathOf(registry_, e).name;
        nlohmann::json characters = nlohmann::json::array();
        for (Entity c : CharactersUsingPath(name)) {
            const auto* n = registry_.try_get<Name>(c);
            characters.push_back({{"id", static_cast<uint32_t>(c)}, {"name", n ? n->value : std::string()}});
        }
        const auto [ox, oy] = Origin(registry_, e);
        post_({{"type", "path:info"}, {"id", static_cast<uint32_t>(e)}, {"name", name}, {"packages", PackagesUsingPath(name)},
               {"characters", characters}, {"problems", CheckPath(ReadPath(PathOf(registry_, e), ox, oy))}});
        return true;
    }
    return false;
}

} // namespace aige
