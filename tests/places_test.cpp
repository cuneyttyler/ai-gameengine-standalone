#include "places/Expr.h"
#include "places/Ontology.h"
#include "places/OntologyExport.h"
#include "places/PlaceValidate.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace p = aige::places;

namespace {

const Ontology& Library() {
    static Ontology ontology = [] {
        Ontology o;
        o.Load({EnginePlacesDir()});
        return o;
    }();
    return ontology;
}

bool Mentions(const std::vector<PlaceProblem>& problems, const std::string& where, const std::string& text) {
    for (const auto& pr : problems) {
        if (pr.where == where && pr.message.find(text) != std::string::npos) return true;
    }
    return false;
}

std::string Describe(const std::vector<PlaceProblem>& problems) {
    std::string out;
    for (const auto& pr : problems) out += (pr.warning ? "  warning " : "  error ") + pr.where + ": " + pr.message + "\n";
    return out;
}

std::vector<PlaceProblem> Errors(const std::vector<PlaceProblem>& problems) {
    std::vector<PlaceProblem> out;
    for (const auto& pr : problems) {
        if (!pr.warning) out.push_back(pr);
    }
    return out;
}

// Fields by name, nothing below.
class FieldScope : public p::Scope {
public:
    std::map<std::string, p::Value> fields;
    std::optional<p::Value> Name(const std::string& name) override {
        if (auto it = fields.find(name); it != fields.end()) return it->second;
        return std::nullopt;
    }
    p::Value NodeField(int, const std::string&) override { return p::Value::Unknown(); }
    p::Value Below(int, bool, const std::string&, const std::string&) override { return p::Value::Unknown(); }
    int SelfNode() override { return 0; }
};

p::Tri Holds(const std::string& text, FieldScope& scope) {
    std::string error;
    auto expr = p::Expr::Parse(text, &error);
    EXPECT_TRUE(expr.has_value()) << text << ": " << error;
    return expr ? expr->Holds(scope) : p::Tri::False;
}

json VillageSpec() {
    return json::parse(R"({
      "schema": "aige.spec/1", "seed": 7,
      "root": {
        "id": "riverford", "module": "settlement",
        "quantity": {"buildings": {"count": 30}, "area": {"area": 6400}},
        "quality": {"layout": "organic", "condition": "lived-in", "walled": "none"},
        "children": [
          {"id": "ground", "module": "ground", "quality": {"terrain": "grass"}},
          {"id": "main-road", "module": "road", "quantity": {"width": {"width": 2}}, "quality": {"class": "main", "surface": "dirt"}},
          {"id": "w-old", "module": "residential-ward",
           "quantity": {"plots": {"count": 28}, "area": {"area": [2600, 3200]}},
           "quality": {"wealth": "common", "density": "dense", "street-style": "organic"},
           "rel": [{"type": "adjacent", "to": "market"}],
           "children": [
             {"id": "w-old/plots", "module": "house-plot", "repeat": 28,
              "quantity": {"size": {"w": [5, 7], "h": [7, 10]}},
              "quality": {"house-kind": "cottage", "fenced": true},
              "children": [
                {"id": "w-old/plots/veg-garden", "module": "garden", "share": 0.36,
                 "quantity": {"size": {"w": [3, 5], "h": [2, 4]}, "beds": {"count": [2, 4]}},
                 "quality": {"kind": "vegetable", "crop-mix": {"mix": {"cabbage": 0.5, "carrot": 0.5}}, "fenced": true}},
                {"id": "w-old/plots/flower-garden", "module": "garden", "share": 0.24,
                 "quantity": {"size": {"w": [3, 5], "h": [2, 4]}, "beds": {"count": [1, 3]}, "flowers": "common"},
                 "quality": {"kind": "flower", "flower-mix": {"mix": {"rose": 0.5, "tulip": 0.3, "daisy": 0.2}},
                             "fenced": true, "path": "gravel"}}
              ]}
           ]},
          {"id": "centre", "module": "market-ward", "quantity": {"area": {"area": 1200}},
           "children": [
             {"id": "market", "module": "market-square",
              "quantity": {"stalls": {"count": [6, 10]}, "size": {"w": [10, 14], "h": [8, 12]}},
              "quality": {"centrepiece": "well"}},
             {"id": "centre/shops", "module": "shop-plot", "repeat": 2}
           ]}
        ]
      }
    })");
}

json& Node(json& spec, const std::string& id) {
    std::vector<json*> todo = {&spec["root"]};
    while (!todo.empty()) {
        json* n = todo.back();
        todo.pop_back();
        if ((*n)["id"] == id) return *n;
        if (n->contains("children")) {
            for (auto& c : (*n)["children"]) todo.push_back(&c);
        }
    }
    throw std::runtime_error("no node " + id);
}

json VillageBrief() {
    return json::parse(R"({
      "schema": "aige.brief/1", "buildType": "settlement", "name": "Riverford",
      "scale": {"class": "village", "buildings": {"count": [30, 40]}},
      "features": [{"module": "river"}],
      "components": {
        "residential-ward": {"enabled": true, "count": [1, 2], "density": "common", "assets": {"source": "auto"}},
        "market-square": {"enabled": true, "count": 1},
        "garden": {"enabled": true, "density": "common", "params": {"kind": "vegetable", "fenced": true}},
        "wall": {"enabled": false}
      }
    })");
}

json Layout() {
    return json::parse(R"({
      "schema": "aige.ir/1",
      "statements": [
        {"op": "fixed", "id": "site", "node": "riverford", "cells": [0, 0, 100, 100], "size": {"w": 100, "h": 100}},
        {"op": "region", "id": "ward", "node": "w-old", "in": "site", "shape": "rect", "size": {"w": 40, "h": 40}},
        {"op": "subdivide", "id": "plots", "node": "w-old/plots", "in": "ward", "style": "organic"},
        {"op": "place", "id": "plots[*].house", "node": "w-old/plots", "in": "plots[*]", "slot": "house", "primary": true},
        {"op": "line", "id": "plots[*].fence", "node": "w-old/plots", "slot": "fence", "along": "plots[*].edge"},
        {"op": "scatter", "id": "flowers", "node": "w-old", "in": "ward.inner - plots", "slot": "flower", "density": 0.15},
        {"op": "relation", "id": "r1", "node": "w-old/plots", "type": "faces", "members": ["plots[*].house", "plots[*].street"]},
        {"op": "check", "id": "c1", "node": "w-old/plots", "predicate": "closed", "args": {"ref": "plots[*].fence"}}
      ]
    })");
}

} // namespace

// --- the ontology --------------------------------------------------------------------

TEST(PlacesOntologyTest, LibraryLoadsAndIsConsistent) {
    Ontology o;
    const auto loadProblems = o.Load({EnginePlacesDir()});
    std::string text;
    for (const auto& pr : loadProblems) text += "  " + pr + "\n";
    EXPECT_TRUE(loadProblems.empty()) << text;
    const auto problems = o.Check();
    text.clear();
    for (const auto& pr : problems) text += "  " + pr + "\n";
    EXPECT_TRUE(problems.empty()) << text;
    EXPECT_GE(o.Modules().size(), 60u);
    ASSERT_NE(o.Build("settlement"), nullptr);
    EXPECT_EQ(o.Build("settlement")->root, "settlement");
    EXPECT_NE(o.Build("settlement")->Scale("large-village"), nullptr);
}

TEST(PlacesOntologyTest, ClassesConceptsAndChildren) {
    const Ontology& o = Library();
    EXPECT_TRUE(o.IsA("house-plot", "BuildingParcel")); // Dwelling ⊑ BuildingParcel
    EXPECT_TRUE(o.IsA("well-plaza", "WaterSource"));
    EXPECT_FALSE(o.IsA("garden", "BuildingParcel"));
    EXPECT_TRUE(o.IsKindOf("flower-kind", "rose", "flower"));
    EXPECT_FALSE(o.IsKindOf("flower-kind", "flower", "rose"));

    const auto smith = o.ConceptsNamed("Blacksmith");
    ASSERT_EQ(smith.size(), 1u);
    EXPECT_EQ(smith[0]->scheme, "trade");
    EXPECT_EQ(smith[0]->id, "smith");

    EXPECT_NE(o.ChildRule("settlement", "residential-ward"), nullptr); // by class:Ward
    EXPECT_NE(o.ChildRule("house-plot", "garden"), nullptr);
    EXPECT_EQ(o.ChildRule("garden", "house-plot"), nullptr);

    const json sub = o.Subgraph("garden");
    ASSERT_TRUE(sub.is_object());
    EXPECT_EQ(sub["level"], "feature");
    bool bed = false;
    for (const auto& c : sub["children"]) bed = bed || (c["module"] == "vegetable-bed" && c["count"] == "beds");
    EXPECT_TRUE(bed);
    EXPECT_TRUE(sub["concepts"].contains("flower-kind"));
    bool rule = false;
    for (const auto& r : sub["rules"]) rule = rule || r["id"] == "garden-behind-house";
    EXPECT_TRUE(rule);
}

TEST(PlacesOntologyTest, ProjectLibraryAddsAndReplaces) {
    const fs::path dir = fs::temp_directory_path() / "aige_places_project" / "places";
    fs::remove_all(dir.parent_path());
    fs::create_directories(dir / "modules");
    std::ofstream(dir / "modules" / "lighthouse.module.json")
        << R"({"schema": "aige.module/1", "id": "lighthouse", "name": "Lighthouse", "level": "parcel",
              "quantity": {"size": {"type": "size", "default": {"w": 4, "h": 4}}},
              "quality": {"light": {"type": "enum", "vocabulary": "light-kind", "default": "brazier"}},
              "slots": {"tower": {"role": "tower", "primary": true}}})";
    std::ofstream(dir / "modules" / "bad.module.json") << R"({"schema": "aige.module/1", "id": "other"})";
    Ontology o;
    const auto problems = o.Load({EnginePlacesDir(), dir});
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems[0].find("bad.module.json"), std::string::npos);
    ASSERT_NE(o.Module("lighthouse"), nullptr);
    EXPECT_EQ(o.Module("lighthouse")->source, "aige_places_project");
    EXPECT_TRUE(o.Check().empty());
    fs::remove_all(dir.parent_path());
}

// --- the expression language ---------------------------------------------------------

TEST(PlacesExprTest, IntervalsMakeComparisonsMaybe) {
    FieldScope s;
    s.fields["beds"] = p::Value::Num(p::Interval{2, 4});
    p::Value size;
    size.kind = p::Value::Kind::Object;
    size.object = {{"w", p::Value::Num(p::Interval{3, 5})}, {"h", p::Value::Num(p::Interval{2, 4})}};
    s.fields["size"] = size;
    EXPECT_EQ(Holds("size.w * size.h >= 4", s), p::Tri::True);   // 6..20
    EXPECT_EQ(Holds("beds * 2 <= size.w * size.h", s), p::Tri::Maybe); // 4..8 vs 6..20: holds for some choices
    EXPECT_EQ(Holds("beds * 10 >= size.w * size.h", s), p::Tri::True); // 20..40 vs 6..20
}

TEST(PlacesExprTest, LogicWordsAndLists) {
    FieldScope s;
    s.fields["kind"] = p::Value::Str("flower");
    s.fields["fenced"] = p::Value::Boolean(p::Tri::True);
    s.fields["n"] = p::Value::Num(p::Interval{8, 12});
    EXPECT_EQ(Holds("kind in [flower, mixed]", s), p::Tri::True);
    EXPECT_EQ(Holds("kind == 'vegetable'", s), p::Tri::False);
    EXPECT_EQ(Holds("not (kind == 'vegetable') and fenced", s), p::Tri::True);
    EXPECT_EQ(Holds("kind == 'vegetable' implies n > 100", s), p::Tri::True);
    EXPECT_EQ(Holds("n >= 10", s), p::Tri::Maybe);
    EXPECT_EQ(Holds("n >= 8", s), p::Tri::True);
    EXPECT_EQ(Holds("n > 12", s), p::Tri::False);
    s.fields["m"] = p::Value::Num(30);
    EXPECT_EQ(Holds("within(m, 31, tol: 'count')", s), p::Tri::True); // ±5%
    EXPECT_EQ(Holds("within(m, 40, tol: 'count')", s), p::Tri::False);
}

TEST(PlacesExprTest, ParseErrorsAreReported) {
    std::string error;
    EXPECT_FALSE(p::Expr::Parse("size.w * ", &error).has_value());
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(p::Expr::Parse("count(children(garden)", &error).has_value());
    EXPECT_TRUE(p::Expr::Parse("count(descendants(class: BuildingParcel)) >= 1").has_value());
    EXPECT_TRUE(p::Expr::Parse("house-plot.size.w > 2").has_value()); // '-' between letters is part of a name
}

// --- the Brief -------------------------------------------------------------------------

TEST(PlacesBriefTest, ExampleBriefIsValid) {
    const auto problems = ValidateBrief(Library(), VillageBrief());
    EXPECT_TRUE(problems.empty()) << Describe(problems);
}

TEST(PlacesBriefTest, ProblemsAreLocated) {
    json brief = VillageBrief();
    brief["scale"] = {{"class", "hamlet"}, {"buildings", {{"count", 200}}}};
    brief["components"]["walls"] = {{"enabled", true}};
    brief["components"]["garden"]["params"]["kind"] = "cactus";
    brief["components"]["garden"]["params"]["roof"] = "red";
    brief["components"]["residential-ward"]["density"] = "lots";
    brief["components"]["wall"] = {{"enabled", false}, {"count", 2}};
    const auto problems = ValidateBrief(Library(), brief);
    EXPECT_TRUE(Mentions(problems, "scale.class", "make it a town")) << Describe(problems);
    EXPECT_TRUE(Mentions(problems, "components.walls", "isn't a component"));
    EXPECT_TRUE(Mentions(problems, "components.garden.params.kind", "'cactus' isn't one of"));
    EXPECT_TRUE(Mentions(problems, "components.garden.params.roof", "no field"));
    EXPECT_TRUE(Mentions(problems, "components.residential-ward.density", "rare"));
    EXPECT_TRUE(Mentions(problems, "components.wall", "disabled"));
}

// --- the Spec --------------------------------------------------------------------------

TEST(PlacesSpecTest, ExampleVillageSpecValidates) {
    const auto problems = Errors(ValidateSpec(Library(), VillageSpec(), SpecStage::Final, "settlement"));
    EXPECT_TRUE(problems.empty()) << Describe(problems);
}

TEST(PlacesSpecTest, BrokenAggregateIsReportedAgainstItsNode) {
    json spec = VillageSpec();
    Node(spec, "market")["quantity"] = {{"stalls", {{"count", 30}}}, {"size", {{"w", 6}, {"h", 6}}}};
    const auto problems = Errors(ValidateSpec(Library(), spec, SpecStage::Final));
    ASSERT_EQ(problems.size(), 1u) << Describe(problems);
    EXPECT_EQ(problems[0].where, "market");
    EXPECT_NE(problems[0].message.find("stalls * 6"), std::string::npos);
}

TEST(PlacesSpecTest, BuildingCountMustMatchTheParcels) {
    json spec = VillageSpec();
    Node(spec, "riverford")["quantity"]["buildings"] = {{"count", 80}};
    const auto final = Errors(ValidateSpec(Library(), spec, SpecStage::Final));
    EXPECT_TRUE(Mentions(final, "riverford", "within(self.buildings")) << Describe(final);
    // A draft has no parcels yet, so it isn't held to it.
    const auto draft = Errors(ValidateSpec(Library(), spec, SpecStage::Draft));
    EXPECT_FALSE(Mentions(draft, "riverford", "within(self.buildings")) << Describe(draft);
}

TEST(PlacesSpecTest, StructureAndFieldErrors) {
    json spec = VillageSpec();
    Node(spec, "w-old/plots/veg-garden")["quality"]["flower-mix"] = {{"mix", {{"rose", 1.0}}}};
    Node(spec, "w-old/plots/flower-garden")["share"] = 0.8;
    Node(spec, "w-old/plots")["quality"]["house-kind"] = "igloo";
    Node(spec, "market")["children"] = json::array({{{"id", "oops"}, {"module", "settlement"}}});
    Node(spec, "w-old")["rel"].push_back({{"type", "near"}, {"to", "nowhere"}});
    Node(spec, "w-old")["quantity"]["plots"] = {{"count", 2.5}};
    const auto problems = Errors(ValidateSpec(Library(), spec, SpecStage::Final));
    EXPECT_TRUE(Mentions(problems, "w-old/plots/veg-garden.flower-mix", "only applies when")) << Describe(problems);
    EXPECT_TRUE(Mentions(problems, "w-old/plots", "shares of its garden children"));
    EXPECT_TRUE(Mentions(problems, "w-old/plots.house-kind", "'igloo'"));
    EXPECT_TRUE(Mentions(problems, "oops", "can't contain a settlement"));
    EXPECT_TRUE(Mentions(problems, "w-old", "'nowhere'"));
    EXPECT_TRUE(Mentions(problems, "w-old.plots", "whole number"));
}

TEST(PlacesSpecTest, MinimumChildrenOnlyAtTheFinalStage) {
    json spec = VillageSpec();
    Node(spec, "centre")["children"] = json::array();
    EXPECT_TRUE(Mentions(Errors(ValidateSpec(Library(), spec, SpecStage::Final)), "centre", "at least 1 market-square"));
    EXPECT_FALSE(Mentions(Errors(ValidateSpec(Library(), spec, SpecStage::Draft)), "centre", "at least 1"));
}

TEST(PlacesSpecTest, RequirementsAcrossThePlace) {
    json spec = VillageSpec();
    Node(spec, "riverford")["children"].push_back(
        {{"id", "harbour"}, {"module", "docks"}, {"quantity", {{"area", {{"area", 400}}}}},
         {"children", json::array({{{"id", "harbour/pier"}, {"module", "dock"}}})}});
    const auto problems = ValidateSpec(Library(), spec, SpecStage::Final);
    EXPECT_TRUE(Mentions(problems, "harbour", "needs a class:WaterBody")) << Describe(problems);
    Node(spec, "riverford")["children"].push_back({{"id", "the-river"}, {"module", "river"}});
    EXPECT_FALSE(Mentions(ValidateSpec(Library(), spec, SpecStage::Final), "harbour", "needs a class:WaterBody"));
}

// --- PLL -----------------------------------------------------------------------------------

TEST(PlacesLayoutTest, ExampleLayoutIsValid) {
    const auto problems = ValidateLayout(Library(), Layout());
    EXPECT_TRUE(problems.empty()) << Describe(problems);
}

TEST(PlacesLayoutTest, ReferencesOrderAndArity) {
    json ir = Layout();
    auto& st = ir["statements"];
    st.push_back({{"op", "path"}, {"id", "lane"}, {"node", "w-old"}, {"in", "ward"}, {"from", "plots[*].house.door"}, {"to", "ward.edge"}});
    st.push_back({{"op", "relation"}, {"id", "r2"}, {"node", "w-old"}, {"type", "between"}, {"members", {"ward", "plots"}}});
    st.push_back({{"op", "place"}, {"id", "ghost"}, {"node", "w-old"}, {"in", "yard"}, {"slot", "well"}});
    st.push_back({{"op", "line"}, {"id", "wobble"}, {"node", "w-old"}, {"slot", "fence"}, {"along", "ward.wiggle"}});
    st.push_back({{"op", "region"}, {"id", "big"}, {"node", "w-old"}, {"in", "ward"}, {"size", {{"w", 50}, {"h", 50}}}});
    st.push_back({{"op", "teleport"}, {"id", "t"}, {"node", "w-old"}});
    st.push_back({{"op", "scatter"}, {"id", "s2"}, {"in", "ward"}, {"slot", "rock"}, {"density", 3}});
    const auto problems = ValidateLayout(Library(), ir);
    EXPECT_TRUE(Mentions(problems, "statement lane", "solved after it")) << Describe(problems);
    EXPECT_TRUE(Mentions(problems, "statement r2", "between takes 3"));
    EXPECT_TRUE(Mentions(problems, "statement ghost", "'yard', which isn't a statement"));
    EXPECT_TRUE(Mentions(problems, "statement wobble", "unknown selector 'wiggle'"));
    EXPECT_TRUE(Mentions(problems, "statement ward", "need 2500 cells but it has 1600"));
    EXPECT_TRUE(Mentions(problems, "statement t", "unknown statement 'teleport'"));
    EXPECT_TRUE(Mentions(problems, "statement s2", "names no Spec node"));
    EXPECT_TRUE(Mentions(problems, "statement s2", "between 0 and 1"));
}

// --- the published ontology --------------------------------------------------------------

namespace {

fs::path OntologyDir() { return fs::path(AIGE_SOURCE_DIR) / "ontology"; }

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// AIGE_WRITE_ONTOLOGY=1 regenerates the published files instead of comparing.
void CheckPublished(const std::string& file, const std::string& generated) {
    const fs::path path = OntologyDir() / file;
    const char* write = std::getenv("AIGE_WRITE_ONTOLOGY");
    if (write && std::string(write) == "1") {
        std::ofstream(path, std::ios::binary) << generated;
        return;
    }
    EXPECT_EQ(ReadText(path), generated) << file << " is out of date with data/places/: run the tests with AIGE_WRITE_ONTOLOGY=1";
}

} // namespace

TEST(PlacesOntologyTest, PublishedTurtleIsCurrent) {
    CheckPublished("gbo-library.ttl", ExportLibraryTurtle(Library()));
    CheckPublished("gbo-shapes.ttl", ExportShapesTurtle(Library()));
}

TEST(PlacesOntologyTest, TurtleNamesAndEscapes) {
    EXPECT_EQ(GboClassName("house-plot"), "HousePlot");
    EXPECT_EQ(GboClassName("market-square"), "MarketSquare");
    const std::string library = ExportLibraryTurtle(Library());
    EXPECT_NE(library.find("gbl:HousePlot\n    a owl:Class, gbo:Module"), std::string::npos);
    EXPECT_NE(library.find("gbv:trade.smith"), std::string::npos);
    EXPECT_NE(library.find("skos:altLabel \"blacksmith\"@en"), std::string::npos);
    EXPECT_NE(library.find("gbo:constraint \"stalls * 6 <= size.w * size.h\"^^gbo:Expression"), std::string::npos);
    // Every module class and module sits under exactly the right level.
    EXPECT_NE(library.find("gbl:Garden\n    a owl:Class, gbo:Module ;\n    rdfs:subClassOf gbo:Feature"), std::string::npos);
}

// docs/places/ontology-in-use.md, Relative sizes: each role's size against what it appears with, resolved at load.
TEST(PlacesTest, RelativeSizesAgree) {
    const Ontology& o = Library();
    auto height = [&](const char* role) { return o.RoleSize(role) * o.RoleTall(role); };
    // The tree as tall as a one-storey building (the building role's storey, 2 cells).
    EXPECT_NEAR(height("tree"), 2.0f, 0.01f);
    // A bush a third of a tree; a door 0.7 of a storey; a barrel under half a door.
    EXPECT_NEAR(height("bush"), 0.3f * height("tree"), 0.01f);
    EXPECT_NEAR(height("door"), 1.4f, 0.01f);
    EXPECT_LT(height("barrel"), 0.5f * height("door"));
    // A street lamp taller than a door (it was a third of one before).
    EXPECT_GT(height("lamp"), height("door"));
    // A role with no size of its own gets its width from its relative size: a chest 0.7 of a door's width.
    EXPECT_NEAR(o.RoleSize("chest"), 0.7f, 0.01f);
    // A role that keeps its own width: the bush stays 0.6 across.
    EXPECT_NEAR(o.RoleSize("bush"), 0.6f, 0.01f);
}
