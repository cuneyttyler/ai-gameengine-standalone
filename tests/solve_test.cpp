#include "places/Brief.h"
#include "places/Compile.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

// development_plan.md 7.6: compile and solve.
using namespace aige;
using json = nlohmann::json;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

json Brief(const std::string& cls, bool walled, bool fenced) {
    return CompleteBrief(Library(), {{"buildType", "settlement"}, {"name", "Riverford"}, {"scale", {{"class", cls}}},
                                     {"shape", {{"layout", {{"mix", {{"grid", 0.5}, {"organic", 0.5}}}}}}},
                                     {"walled", walled ? "stone" : "none"},
                                     {"components", {{"residential-ward", json::object()},
                                                     {"market-ward", json::object()},
                                                     {"house-plot", {{"params", {{"fenced", fenced}}}}},
                                                     {"garden", {{"density", "common"}, {"params", {{"kind", "vegetable"}, {"fenced", true}}}}}}}});
}

json Village(int wards, int houses, const json& brief) {
    json children = json::array({{{"id", "market"}, {"module", "market-ward"}, {"quantity", {{"plots", {{"count", 4}}}, {"area", {{"area", 700}}}}}}});
    for (int i = 0; i < wards; ++i) {
        children.push_back({{"id", "ward-" + std::to_string(i + 1)}, {"module", "residential-ward"},
                            {"quantity", {{"plots", {{"count", houses}}}, {"area", {{"area", 1500}}}}},
                            {"quality", {{"wealth", "common"}, {"street-style", i % 2 ? "grid" : "organic"}}}});
    }
    json spec = SpecFromRoot(Library(), brief, {{"children", children}}, "riverford");
    for (const auto& zone : ZoneIds(Library(), spec)) {
        json answer;
        if (SpecNode(spec, zone)->at("module") == "market-ward") {
            answer = {{"children", json::array({{{"id", "square"}, {"module", "market-square"},
                                                 {"quantity", {{"stalls", {{"count", 6}}}, {"size", {{"w", 10}, {"h", 8}}}}}},
                                                {{"id", "shops"}, {"module", "shop-plot"}, {"repeat", 3}}})}};
        } else {
            answer = {{"children", json::array({{{"id", "cottages"}, {"module", "house-plot"}, {"repeat", houses},
                                                 {"quantity", {{"size", {{"w", 6}, {"h", 8}}}}}}})}};
        }
        ApplyZoneAnswer(Library(), brief, spec, zone, answer);
    }
    ExpandSpec(Library(), brief, spec);
    RebalanceBuildings(Library(), brief, spec);
    return spec;
}

// AIGE_PRINT_LAYOUT=1: the solved layout as text: # road, ~ water, H building,
// o object, letters for parcels (each its own), . open ground.
void PrintLayout(const json& doc, int side) {
    const char* print = std::getenv("AIGE_PRINT_LAYOUT");
    if (!print || std::string(print) != "1") return;
    std::vector<std::string> map(side, std::string(side, '.'));
    // Zones (the partition's regions) as digits under everything else.
    int zone = 0;
    for (const auto& r : doc["regions"]) {
        if (r.value("statement", "") != "site/zones") continue;
        const char mark = static_cast<char>('0' + (zone++ % 10));
        for (int c : DecodeCells(r["cells"], side)) map[c / side][c % side] = mark;
    }
    int letter = 0;
    for (const auto& r : doc["regions"]) {
        const std::string id = r["id"];
        if (id.find('#') == std::string::npos || id.find('/', id.find('#')) != std::string::npos) continue;
        const char mark = static_cast<char>('a' + (letter++ % 26));
        for (int c : DecodeCells(r["cells"], side)) map[c / side][c % side] = mark;
    }
    for (const auto& c : doc["roads"]) map[c[1].get<int>()][c[0].get<int>()] = '#';
    for (const auto& o : doc["objects"]) {
        const char mark = o["layer"] == "building" ? 'H' : 'o';
        for (int y = o["y"].get<int>(); y < o["y"].get<int>() + o["h"].get<int>(); ++y) {
            for (int x = o["x"].get<int>(); x < o["x"].get<int>() + o["w"].get<int>(); ++x) map[y][x] = mark;
        }
    }
    for (const auto& line : map) std::cout << line << "\n";
}

std::map<std::string, json> ById(const json& list) {
    std::map<std::string, json> out;
    for (const auto& item : list) out[item["id"]] = item;
    return out;
}

} // namespace

TEST(CompileTest, ASpecBecomesAValidLayout) {
    const json brief = Brief("village", true, true);
    const json spec = Village(2, 12, brief);
    ASSERT_TRUE(ValidateSpec(Library(), spec, SpecStage::Final, "settlement").empty() ||
                std::all_of(ValidateSpec(Library(), spec, SpecStage::Final, "settlement").begin(),
                            ValidateSpec(Library(), spec, SpecStage::Final, "settlement").end(), [](const PlaceProblem& p) { return p.warning; }));
    const json ir = CompileSpec(Library(), spec, {.seed = 7, .side = 90});
    ASSERT_EQ(ir["schema"], "aige.ir/1");
    std::map<std::string, int> ops;
    std::set<std::string> plots;
    for (const auto& s : ir["statements"]) {
        ++ops[s["op"].get<std::string>()];
        if (s["op"] == "subdivide") {
            for (const auto& p : s["into"]) plots.insert(p["id"]);
        }
    }
    EXPECT_EQ(ops["partition"], 1);
    EXPECT_EQ(ops["subdivide"], 3); // a market and two wards
    EXPECT_GE(ops["path"], 1);
    EXPECT_GE(ops["line"], 1);      // the wall, fences
    EXPECT_TRUE(plots.contains("ward-1/cottages#11"));
    // Every plot has its house; 60% of plots have a garden ("common").
    int houses = 0, gardens = 0;
    for (const auto& s : ir["statements"]) {
        const std::string id = s["id"];
        if (s["op"] == "place" && s.value("primary", false) && id.rfind("ward-1/cottages#", 0) == 0) ++houses;
        if (s["op"] == "region" && id.rfind("ward-1/cottages#", 0) == 0 && id.find("/garden") != std::string::npos) ++gardens;
    }
    EXPECT_EQ(houses, 12);
    EXPECT_NEAR(gardens, 0.6 * 12, 1.0);
    // The static checks of the language pass.
    const auto problems = ValidateLayout(Library(), ir);
    std::string text;
    for (const auto& p : problems) text += "  " + p.where + ": " + p.message + "\n";
    EXPECT_TRUE(problems.empty()) << text;
}

TEST(SolveTest, AVillageSolvesWithinTheRules) {
    const json brief = Brief("village", true, true);
    const json spec = Village(2, 12, brief);
    const json ir = CompileSpec(Library(), spec, {.seed = 7, .side = 90});
    const auto start = std::chrono::steady_clock::now();
    const SolvedLayout solved = SolveLayout(Library(), ir);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const json& doc = solved.document;
    ASSERT_EQ(doc["schema"], "aige.solved/1");
    std::cout << "[ village solve ] " << ms << " ms, " << doc["stats"].dump() << std::endl;
    std::string problems;
    for (const auto& p : solved.problems) problems += "  " + p.dump() + "\n";
    EXPECT_TRUE(solved.ok()) << problems;

    const int W = 90;
    const auto regions = ById(doc["regions"]);
    std::map<std::string, std::set<int>> cellsOf;
    for (const auto& [id, r] : regions) {
        const auto cells = DecodeCells(r["cells"], W);
        cellsOf[id] = std::set<int>(cells.begin(), cells.end());
    }
    // Hard rule 1: every region lies inside its parent.
    for (const auto& [id, r] : regions) {
        if (r["parent"].is_null()) continue;
        for (int c : cellsOf[id]) ASSERT_TRUE(cellsOf[r["parent"]].contains(c)) << id << " leaves " << r["parent"];
    }
    // Hard rule 2: no two solid footprints overlap.
    std::map<int, std::string> solid;
    for (const auto& o : doc["objects"]) {
        if (o["layer"] != "building" && o["layer"] != "prop") continue;
        for (int y = o["y"].get<int>(); y < o["y"].get<int>() + o["h"].get<int>(); ++y) {
            for (int x = o["x"].get<int>(); x < o["x"].get<int>() + o["w"].get<int>(); ++x) {
                auto [it, fresh] = solid.emplace(y * W + x, o["id"]);
                EXPECT_TRUE(fresh) << o["id"] << " overlaps " << it->second;
            }
        }
    }
    // Every plot fronts a street, and its house faces it.
    int plots = 0;
    for (const auto& [id, r] : regions) {
        if (id.find("cottages#") == std::string::npos || id.find('/', id.find('#')) != std::string::npos) continue;
        ++plots;
        ASSERT_TRUE(r.contains("facing")) << id;
    }
    EXPECT_GE(plots, 22); // 24 asked; a couple may not fit
    const auto objects = ById(doc["objects"]);
    int facingStreet = 0, houses = 0;
    for (const auto& [id, o] : objects) {
        if (!o.value("primary", false) || o["layer"] != "building") continue;
        ++houses;
        const std::string plot = id.substr(0, id.rfind('/'));
        if (regions.contains(plot) && regions.at(plot)["facing"] == o["facing"]) ++facingStreet;
    }
    EXPECT_EQ(facingStreet, houses);
    // Checks: doors reachable, fences closed, wall closed with gates where the road crosses.
    for (const auto& c : doc["checks"]) EXPECT_TRUE(c["ok"].get<bool>()) << c.dump();
    bool wallWithGate = false;
    for (const auto& l : doc["lines"]) {
        if (l["slot"] == "wall") wallWithGate = !l["gaps"].empty() && l["edges"].size() > 100;
    }
    EXPECT_TRUE(wallWithGate);
    // Terrain: the ground, the roads.
    const json& terrain = doc["terrain"];
    ASSERT_EQ(terrain["rows"].size(), static_cast<size_t>(W));
    std::set<std::string> palette(terrain["palette"].begin(), terrain["palette"].end());
    EXPECT_TRUE(palette.contains("grass"));
    EXPECT_TRUE(palette.contains("dirt"));
}

TEST(SolveTest, TheSameSeedGivesTheSameLayout) {
    const json brief = Brief("hamlet", false, false);
    const json spec = Village(1, 8, brief);
    const json ir = CompileSpec(Library(), spec, {.seed = 11, .side = 60});
    const json a = SolveLayout(Library(), ir).document, b = SolveLayout(Library(), ir).document;
    EXPECT_EQ(a.dump(), b.dump());
    json other = ir;
    other["seed"] = 12;
    EXPECT_NE(SolveLayout(Library(), other).document["objects"].dump(), a["objects"].dump());
}

TEST(SolveTest, PathsJoinExistingRoads) {
    const json ir = {{"schema", "aige.ir/1"}, {"seed", 1}, {"grid", {{"bounds", {0, 0, 40, 40}}}},
                     {"statements", json::array({
                         {{"op", "region"}, {"id", "site"}, {"node", "s"}, {"at", {0, 0}}, {"size", {{"w", 40}, {"h", 40}}}},
                         {{"op", "anchor"}, {"id", "w"}, {"node", "s"}, {"at", {0, 20}}, {"kind", "entrance"}},
                         {{"op", "anchor"}, {"id", "e"}, {"node", "s"}, {"at", {39, 20}}, {"kind", "entrance"}},
                         {{"op", "anchor"}, {"id", "n"}, {"node", "s"}, {"at", {30, 0}}, {"kind", "entrance"}},
                         {{"op", "path"}, {"id", "main"}, {"node", "s"}, {"in", "site"}, {"from", "w"}, {"to", "e"}, {"class", "main"}, {"wiggle", 0}},
                         {{"op", "path"}, {"id", "lane"}, {"node", "s"}, {"in", "site"}, {"from", "n"}, {"to", "w"}, {"class", "lane"}, {"wiggle", 0}},
                     })}};
    const json doc = SolveLayout(Library(), ir).document;
    const auto paths = ById(doc["paths"]);
    // The lane goes down to the main road and runs along it, rather than beside it.
    std::set<std::pair<int, int>> main;
    for (const auto& c : paths.at("main")["cells"]) main.insert(std::pair{c[0].get<int>(), c[1].get<int>()});
    int shared = 0;
    for (const auto& c : paths.at("lane")["cells"]) shared += main.contains(std::pair{c[0].get<int>(), c[1].get<int>()});
    EXPECT_GT(shared, 20);
}

TEST(SolveTest, ZonesFollowTheirSitePreferences) {
    const json ir = {{"schema", "aige.ir/1"}, {"seed", 3}, {"grid", {{"bounds", {0, 0, 60, 60}}}},
                     {"statements", json::array({
                         {{"op", "region"}, {"id", "site"}, {"node", "s"}, {"at", {0, 0}}, {"size", {{"w", 60}, {"h", 60}}}},
                         {{"op", "partition"}, {"id", "zones"}, {"node", "s"}, {"in", "site"},
                          {"into", json::array({{{"id", "core"}, {"area", 500}, {"prefers", {{"centre", 1.0}}}},
                                                {{"id", "rim"}, {"area", 1200}, {"prefers", {{"edge", 0.8}}}}})}},
                     })}};
    const json doc = SolveLayout(Library(), ir).document;
    const auto regions = ById(doc["regions"]);
    const auto core = DecodeCells(regions.at("core")["cells"], 60);
    EXPECT_NEAR(static_cast<double>(core.size()), 500, 60);
    double cx = 0, cy = 0;
    for (int c : core) {
        cx += c % 60;
        cy += c / 60;
    }
    cx /= core.size();
    cy /= core.size();
    EXPECT_LT(std::hypot(cx - 30, cy - 30), 8.0); // the core is central
    EXPECT_GT(DecodeCells(regions.at("rim")["cells"], 60).size(), 900u);
}

TEST(SolveTest, ScatterKeepsDensityAndSpacing) {
    const json ir = {{"schema", "aige.ir/1"}, {"seed", 5}, {"grid", {{"bounds", {0, 0, 30, 30}}}},
                     {"statements", json::array({
                         {{"op", "region"}, {"id", "site"}, {"node", "s"}, {"at", {0, 0}}, {"size", {{"w", 30}, {"h", 30}}}},
                         {{"op", "scatter"}, {"id", "trees"}, {"node", "s"}, {"in", "site"}, {"slot", "tree"}, {"density", 0.1},
                          {"spacing", {{"min", 1}}}, {"layer", "prop"}},
                     })}};
    const json doc = SolveLayout(Library(), ir).document;
    const auto& objects = doc["objects"];
    EXPECT_NEAR(static_cast<double>(objects.size()), 90, 10);
    for (size_t i = 0; i < objects.size(); ++i) {
        for (size_t j = i + 1; j < objects.size(); ++j) {
            const int d = std::max(std::abs(objects[i]["x"].get<int>() - objects[j]["x"].get<int>()),
                                   std::abs(objects[i]["y"].get<int>() - objects[j]["y"].get<int>()));
            ASSERT_GT(d, 1);
        }
    }
}

TEST(SolveTest, EveryScaleSolves) {
    for (const auto& [cls, wards, houses, side] : {std::tuple{"hamlet", 1, 8, 50}, std::tuple{"village", 2, 16, 90},
                                                   std::tuple{"large-village", 4, 24, 140}}) {
        const json brief = Brief(cls, false, false);
        const json spec = Village(wards, houses, brief);
        const auto start = std::chrono::steady_clock::now();
        const SolvedLayout solved = SolveLayout(Library(), CompileSpec(Library(), spec, {.seed = 2, .side = side}));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "[ " << cls << " ] " << ms << " ms, " << solved.document["stats"].dump() << std::endl;
        PrintLayout(solved.document, side);
        int buildings = 0;
        for (const auto& o : solved.document["objects"]) buildings += o["layer"] == "building";
        EXPECT_GE(buildings, CountBuildings(Library(), spec) * 8 / 10) << cls;
        std::string problems;
        for (const auto& p : solved.problems) problems += "  " + p.dump() + "\n";
        EXPECT_TRUE(solved.ok()) << cls << "\n" << problems;
    }
}
