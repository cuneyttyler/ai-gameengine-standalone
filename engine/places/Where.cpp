#include "places/Where.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <set>
#include <vector>

namespace aige {

using json = nlohmann::json;

namespace {

struct Area {
    std::string id, name;
    std::vector<int> cells;
    double cx = 0, cy = 0;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
};

struct Geometry {
    int W = 0, H = 0;
    std::vector<Area> zones;
    std::vector<int> owner;                         // zone index per cell, -1
    std::map<std::string, std::vector<int>> paths;  // name -> cells (the river by "river" too)
    std::map<std::string, std::string> pathClass;

    const Area* Zone(const std::string& id) const {
        for (const auto& z : zones) {
            if (z.id == id) return &z;
        }
        return nullptr;
    }
    // Distance (4-neighbour steps) from every cell to a path.
    std::vector<int> DistanceTo(const std::vector<int>& cells) const {
        std::vector<int> d(W * H, 1 << 20);
        std::deque<int> todo;
        for (int c : cells) {
            if (c >= 0 && c < W * H) d[c] = 0, todo.push_back(c);
        }
        while (!todo.empty()) {
            const int c = todo.front();
            todo.pop_front();
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = c % W + dx, ny = c / W + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= H || d[ny * W + nx] <= d[c] + 1) continue;
                d[ny * W + nx] = d[c] + 1;
                todo.push_back(ny * W + nx);
            }
        }
        return d;
    }
    // A path's x at each row it crosses.
    std::map<int, double> XByRow(const std::vector<int>& cells) const {
        std::map<int, std::pair<double, int>> sums;
        for (int c : cells) sums[c / W].first += c % W, sums[c / W].second++;
        std::map<int, double> out;
        for (const auto& [row, s] : sums) out[row] = s.first / s.second;
        return out;
    }
    std::map<int, double> YByColumn(const std::vector<int>& cells) const {
        std::map<int, std::pair<double, int>> sums;
        for (int c : cells) sums[c % W].first += c / W, sums[c % W].second++;
        std::map<int, double> out;
        for (const auto& [col, s] : sums) out[col] = s.first / s.second;
        return out;
    }
};

double Nearest(const std::map<int, double>& axis, int at, double fallback) {
    if (axis.empty()) return fallback;
    auto it = axis.lower_bound(at);
    if (it == axis.end()) return std::prev(it)->second;
    if (it == axis.begin() || it->first == at) return it->second;
    auto before = std::prev(it);
    return at - before->first <= it->first - at ? before->second : it->second;
}

Geometry Read(const json& solved, const std::map<std::string, std::string>& names) {
    Geometry g;
    const json bounds = solved.contains("grid") ? solved["grid"].value("bounds", json::array({0, 0, 0, 0})) : json::array({0, 0, 0, 0});
    g.W = std::max(1, bounds[2].get<int>());
    g.H = std::max(1, bounds[3].get<int>());
    g.owner.assign(g.W * g.H, -1);
    for (const auto& r : solved.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        Area a;
        a.id = r.value("id", "");
        auto it = names.find(a.id);
        a.name = it != names.end() ? it->second : a.id;
        for (const auto& run : r.value("cells", json::array())) {
            const int y = run[0].get<int>();
            for (int x = run[1].get<int>(); x < run[2].get<int>(); ++x) {
                if (x < 0 || y < 0 || x >= g.W || y >= g.H) continue;
                a.cells.push_back(y * g.W + x);
            }
        }
        if (a.cells.empty()) continue;
        for (int c : a.cells) {
            const int x = c % g.W, y = c / g.W;
            a.cx += x + 0.5, a.cy += y + 0.5;
            a.x0 = std::min(a.x0, x), a.y0 = std::min(a.y0, y), a.x1 = std::max(a.x1, x), a.y1 = std::max(a.y1, y);
            g.owner[c] = static_cast<int>(g.zones.size());
        }
        a.cx /= a.cells.size(), a.cy /= a.cells.size();
        g.zones.push_back(std::move(a));
    }
    for (const auto& p : solved.value("paths", json::array())) {
        std::vector<int> cells;
        for (const auto& c : p.value("cells", json::array())) {
            const int x = c[0].get<int>(), y = c[1].get<int>();
            if (x >= 0 && y >= 0 && x < g.W && y < g.H) cells.push_back(y * g.W + x);
        }
        const std::string cls = p.value("class", "");
        for (const std::string& name : {p.value("id", ""), p.value("node", "")}) {
            if (name.empty()) continue;
            auto& into = g.paths[name];
            into.insert(into.end(), cells.begin(), cells.end());
            g.pathClass[name] = cls;
        }
        if (cls == "river") {
            auto& river = g.paths["river"];
            river.insert(river.end(), cells.begin(), cells.end());
            g.pathClass["river"] = "river";
        }
    }
    return g;
}

// "south-west": where b lies seen from a.
std::string Direction(double dx, double dy) {
    const double m = std::max(std::abs(dx), std::abs(dy));
    if (m < 1e-6) return "same place";
    std::string out;
    if (std::abs(dy) >= 0.4 * m) out += dy < 0 ? "north" : "south";
    if (std::abs(dx) >= 0.4 * m) out += (out.empty() ? "" : "-") + std::string(dx < 0 ? "west" : "east");
    return out;
}

bool Touching(const Geometry& g, const Area& a, int other) {
    for (int c : a.cells) {
        const int x = c % g.W, y = c / g.W;
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            // Across a road or a stream of up to three cells too.
            for (int k = 1; k <= 4; ++k) {
                const int nx = x + dx * k, ny = y + dy * k;
                if (nx < 0 || ny < 0 || nx >= g.W || ny >= g.H) break;
                const int o = g.owner[ny * g.W + nx];
                if (o == other) return true;
                if (o >= 0) break;
            }
        }
    }
    return false;
}

// Share of a zone's cells beyond a line in a direction (south of y = ty...).
double Beyond(const Geometry& g, const Area& a, const std::string& type, const std::function<double(int)>& line) {
    int n = 0;
    for (int c : a.cells) {
        const double x = c % g.W + 0.5, y = c / g.W + 0.5, at = line(c);
        if ((type == "south-of" && y > at) || (type == "north-of" && y < at) || (type == "east-of" && x > at) ||
            (type == "west-of" && x < at)) {
            ++n;
        }
    }
    return a.cells.empty() ? 0 : static_cast<double>(n) / a.cells.size();
}

} // namespace

json LayoutWhere(const json& solved, const std::map<std::string, std::string>& names) {
    const Geometry g = Read(solved, names);
    json zones = json::array();
    const std::vector<int>* river = g.paths.contains("river") ? &g.paths.at("river") : nullptr;
    const std::vector<int> toRiver = river ? g.DistanceTo(*river) : std::vector<int>();
    const auto riverX = river ? g.XByRow(*river) : std::map<int, double>();
    for (size_t i = 0; i < g.zones.size(); ++i) {
        const Area& a = g.zones[i];
        json one = {{"id", a.id}, {"name", a.name}, {"area", a.cells.size()},
                    {"centre", {std::lround(a.cx), std::lround(a.cy)}}, {"extent", {a.x0, a.y0, a.x1 + 1, a.y1 + 1}}};
        if (river) {
            int touching = 0, west = 0, nearest = 1 << 20;
            std::set<int> rows;
            for (int c : a.cells) {
                nearest = std::min(nearest, toRiver[c]);
                if (toRiver[c] <= 2) ++touching, rows.insert(c / g.W);
                west += (c % g.W) < Nearest(riverX, c / g.W, g.W / 2.0);
            }
            const double w = static_cast<double>(west) / a.cells.size();
            one["river"] = {{"nearest", nearest},
                            {"bank", w > 0.85 ? "west" : w < 0.15 ? "east" : "both"},
                            {"alongRows", rows.size()},
                            {"height", a.y1 - a.y0 + 1}};
        }
        json others = json::array();
        for (size_t j = 0; j < g.zones.size(); ++j) {
            if (i == j) continue;
            const Area& b = g.zones[j];
            others.push_back({{"zone", b.id},
                              {"lies", Direction(b.cx - a.cx, b.cy - a.cy)},
                              {"distance", std::lround(std::hypot(b.cx - a.cx, b.cy - a.cy))},
                              {"touching", Touching(g, a, static_cast<int>(j))}});
        }
        one["others"] = others;
        zones.push_back(one);
    }
    // A map, about 40 characters across: a letter per zone, ~ the river, = a road, . open land.
    const int step = std::max(1, (std::max(g.W, g.H) + 39) / 40);
    json legend = json::object();
    std::set<int> roadCells, riverCells;
    if (river) riverCells.insert(river->begin(), river->end());
    for (const auto& [name, cells] : g.paths) {
        if (g.pathClass.at(name) != "river") roadCells.insert(cells.begin(), cells.end());
    }
    for (size_t i = 0; i < g.zones.size() && i < 26; ++i) legend[std::string(1, static_cast<char>('A' + i))] = g.zones[i].id;
    json map = json::array();
    for (int by = 0; by < g.H; by += step) {
        std::string row;
        for (int bx = 0; bx < g.W; bx += step) {
            std::map<int, int> count;
            int water = 0, road = 0, total = 0;
            for (int y = by; y < std::min(g.H, by + step); ++y) {
                for (int x = bx; x < std::min(g.W, bx + step); ++x) {
                    const int c = y * g.W + x;
                    ++total;
                    if (riverCells.contains(c)) ++water;
                    else if (roadCells.contains(c)) ++road;
                    else if (g.owner[c] >= 0) count[g.owner[c]]++;
                }
            }
            if (water * 4 >= total) row += '~';
            else if (!count.empty()) {
                const auto best = std::max_element(count.begin(), count.end(), [](const auto& l, const auto& r) { return l.second < r.second; });
                row += best->first < 26 ? static_cast<char>('A' + best->first) : '?';
            } else if (road * 3 >= total) row += '=';
            else row += '.';
        }
        map.push_back(row);
    }
    return {{"zones", zones},
            {"map", map},
            {"legend", legend},
            {"cellsPerCharacter", step},
            {"directions", "x grows east (right), y grows south (down): left is west, below is south"}};
}

json CheckExpectations(const json& solved, const json& expects) {
    const Geometry g = Read(solved, {});
    json out = json::array();
    for (const auto& e : expects) {
        if (!e.is_object()) continue;
        json r = e;
        const std::string node = e.value("node", ""), type = e.value("type", ""), to = e.value("to", "");
        const Area* a = g.Zone(node);
        const Area* b = g.Zone(to);
        const std::vector<int>* path = b ? nullptr : (g.paths.contains(to) ? &g.paths.at(to) : nullptr);
        if (a == nullptr || (b == nullptr && path == nullptr)) {
            r["ok"] = false;
            r["detail"] = std::string("the layout has no ") + (a == nullptr ? node : to);
            out.push_back(r);
            continue;
        }
        bool ok = false;
        std::string detail;
        if (type.ends_with("-of")) {
            double share = 0;
            if (b) {
                share = Beyond(g, *a, type, [&](int) { return type == "south-of" || type == "north-of" ? b->cy : b->cx; });
            } else {
                const auto xs = g.XByRow(*path);
                const auto ys = g.YByColumn(*path);
                share = Beyond(g, *a, type, [&](int c) {
                    return type == "south-of" || type == "north-of" ? Nearest(ys, c % g.W, g.H / 2.0) + 0.5 : Nearest(xs, c / g.W, g.W / 2.0) + 0.5;
                });
            }
            ok = share >= (b ? 0.7 : 0.85);
            if (b) detail = node + " lies " + Direction(a->cx - b->cx, a->cy - b->cy) + " of " + to;
            else detail = std::to_string(std::lround(share * 100)) + "% of " + node + " is " + type.substr(0, type.size() - 3) + " of " + to;
        } else if (type == "along" || (type == "adjacent" && path)) {
            if (path) {
                // Along: it reaches the path over most of its length, as a strip beside it.
                const auto d = g.DistanceTo(*path);
                std::set<int> rows, cols;
                for (int c : a->cells) {
                    if (d[c] <= 2) rows.insert(c / g.W), cols.insert(c % g.W);
                }
                const double span = std::max(rows.size() / double(a->y1 - a->y0 + 1), cols.size() / double(a->x1 - a->x0 + 1));
                ok = span >= 0.6;
                detail = node + " meets " + to + " along " + std::to_string(std::lround(span * 100)) + "% of its length";
            } else {
                const int j = static_cast<int>(b - &g.zones[0]);
                ok = Touching(g, *a, j);
                detail = node + (ok ? " borders " : " doesn't border ") + to;
            }
        } else if (type == "adjacent") {
            ok = Touching(g, *a, static_cast<int>(b - &g.zones[0]));
            detail = node + (ok ? " borders " : " doesn't border ") + to;
        } else if (type == "near") {
            if (b) {
                const double d = std::hypot(a->cx - b->cx, a->cy - b->cy);
                const double close = (std::sqrt(double(a->cells.size())) + std::sqrt(double(b->cells.size()))) * 0.65;
                ok = Touching(g, *a, static_cast<int>(b - &g.zones[0])) || d <= close;
                detail = node + " is " + std::to_string(std::lround(d)) + " cells from " + to;
            } else {
                const auto d = g.DistanceTo(*path);
                int nearest = 1 << 20;
                for (int c : a->cells) nearest = std::min(nearest, d[c]);
                ok = nearest <= 3;
                detail = node + " is " + std::to_string(nearest) + " cells from " + to;
            }
        } else if (type == "far" || type == "away-from") {
            const double d = b ? std::hypot(a->cx - b->cx, a->cy - b->cy) : 0;
            ok = b && d >= std::max(g.W, g.H) * 0.4;
            detail = node + " is " + std::to_string(std::lround(d)) + " cells from " + to;
        } else {
            detail = "can't check '" + type + "'";
        }
        r["ok"] = ok;
        r["detail"] = detail;
        out.push_back(r);
    }
    return out;
}

} // namespace aige
