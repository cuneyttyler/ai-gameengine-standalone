#include "places/Brief.h"
#include "places/Compile.h"
#include "places/Ontology.h"
#include "places/OntologyEdit.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

// development_plan.md 9: the user's own modules and extensions, kept in their folder,
// checked as the engine's are, used by every build, exported and imported.
using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A user folder laid out as …/aige/ontology (so its entries are the user's).
fs::path UserDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / name / "aige" / "ontology";
    fs::remove_all(dir.parent_path().parent_path());
    fs::create_directories(dir);
    return dir;
}

json Watermill() {
    return json::parse(R"({
        "id": "watermill-plot",
        "name": "Watermill",
        "level": "parcel",
        "description": "A mill whose wheel the river turns.",
        "classes": ["Workplace"],
        "quantity": {"size": {"type": "size", "default": {"w": [7, 8], "h": [7, 8]}}},
        "quality": {"wheel": {"type": "enum", "values": ["undershot", "overshot"], "default": "undershot", "ask": true}},
        "slots": {"mill": {"role": "workshop", "essential": true, "primary": true}},
        "extends": {"parents": {"craftsmen-ward": {"min": 0, "max": 3, "weight": 0.3}}, "components": ["settlement"]}
    })");
}

} // namespace

TEST(OntologyEditTest, AUsersModuleIsCheckedKeptAndBuilt) {
    const fs::path user = UserDir("aige_ontology_user");
    ASSERT_EQ(SaveUserModule(user, EnginePlacesDir(), Watermill()), "");
    ASSERT_TRUE(fs::exists(user / "modules" / "watermill-plot.module.json"));

    Ontology o;
    const auto problems = o.Load({EnginePlacesDir(), user});
    EXPECT_TRUE(problems.empty()) << problems.front();
    const ModuleDef* mill = o.Module("watermill-plot");
    ASSERT_NE(mill, nullptr);
    EXPECT_EQ(mill->source, "user");
    // It's where it said: a child of the craftsmen's ward, a component a settlement offers.
    EXPECT_NE(o.ChildRule("craftsmen-ward", "watermill-plot"), nullptr);
    const auto& components = o.Build("settlement")->components;
    EXPECT_NE(std::find(components.begin(), components.end(), "watermill-plot"), components.end());
    // The panel sees it, editable; a built-in isn't.
    const json all = OntologyJson(o);
    bool seen = false;
    for (const auto& m : all["modules"]) {
        if (m["id"] == "watermill-plot") seen = m["editable"].get<bool>() && m["parents"].dump().find("craftsmen-ward") != std::string::npos;
        if (m["id"] == "house-plot") EXPECT_FALSE(m["editable"].get<bool>());
    }
    EXPECT_TRUE(seen);

    // A craftsmen's ward planned with one: compiled and solved, the mill is there.
    const json brief = CompleteBrief(o, {{"buildType", "settlement"}, {"scale", {{"class", "village"}}}, {"components", {{"craftsmen-ward", json::object()}}}});
    json spec = SpecFromRoot(o, brief, {{"children", json::array({{{"id", "crafts"}, {"module", "craftsmen-ward"}, {"quantity", {{"plots", {{"count", 4}}}}}}})}}, "v");
    ApplyZoneAnswer(o, brief, spec, "crafts",
                    {{"children", json::array({{{"id", "mill"}, {"module", "watermill-plot"}}, {{"id", "smiths"}, {"module", "workshop-plot"}, {"repeat", 3}}})}});
    ExpandSpec(o, brief, spec);
    const json solved = SolveLayout(o, CompileSpec(o, spec, {.seed = 3, .side = 70})).document;
    bool built = false;
    for (const auto& obj : solved["objects"]) built = built || obj.value("node", "").rfind("crafts/mill", 0) == 0;
    EXPECT_TRUE(built) << "the watermill wasn't placed";
    fs::remove_all(user.parent_path().parent_path());
}

TEST(OntologyEditTest, BuiltInsAndBrokenModulesAreRefused) {
    const fs::path user = UserDir("aige_ontology_refused");
    json same = Watermill();
    same["id"] = "house-plot";
    EXPECT_NE(SaveUserModule(user, EnginePlacesDir(), same).find("built-in"), std::string::npos);
    json bad = Watermill();
    bad["quantity"]["size"]["type"] = "furlongs";
    EXPECT_NE(SaveUserModule(user, EnginePlacesDir(), bad), "");
    json nowhere = Watermill();
    nowhere["extends"]["parents"] = {{"no-such-ward", json::object()}};
    EXPECT_NE(SaveUserModule(user, EnginePlacesDir(), nowhere), "");
    EXPECT_FALSE(fs::exists(user / "modules" / "watermill-plot.module.json")) << "a refused module isn't kept";
    fs::remove_all(user.parent_path().parent_path());
}

TEST(OntologyEditTest, ExtensionsAddFieldsAndTheWholeIsExportedAndImported) {
    const fs::path user = UserDir("aige_ontology_export");
    ASSERT_EQ(SaveUserModule(user, EnginePlacesDir(), Watermill()), "");
    const json extensions = {{"fields", {{"house-plot", {{"quality", {{"chimney", {{"type", "bool"}, {"default", true}}}}}}}}},
                             {"children", json::object()},
                             {"components", json::object()}};
    ASSERT_EQ(SaveUserExtensions(user, EnginePlacesDir(), extensions), "");
    {
        Ontology o;
        o.Load({EnginePlacesDir(), user});
        ASSERT_NE(o.Module("house-plot")->Field("chimney"), nullptr);
    }
    const json bundle = ExportUserOntology(user);
    EXPECT_EQ(bundle["schema"], "aige.ontology-extension/1");
    EXPECT_EQ(bundle["modules"].size(), 1u);

    // On another installation: imported, the same.
    const fs::path other = UserDir("aige_ontology_import");
    ASSERT_EQ(ImportUserOntology(other, EnginePlacesDir(), bundle), "");
    Ontology o;
    o.Load({EnginePlacesDir(), other});
    EXPECT_NE(o.Module("watermill-plot"), nullptr);
    EXPECT_NE(o.Module("house-plot")->Field("chimney"), nullptr);
    EXPECT_NE(ImportUserOntology(other, EnginePlacesDir(), json{{"schema", "something"}}), "");
    fs::remove_all(user.parent_path().parent_path());
    fs::remove_all(other.parent_path().parent_path());
}
