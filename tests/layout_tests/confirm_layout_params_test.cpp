// Generates layout PNGs for each Confirm/Layout parameter option (top tables),
// using the Build panel flow as a user would: prompt -> Brief -> layout.
// Run with AIGE_LAYOUT_PARAM_TEST=1 to produce outputs in builder_tests/layout/.
//
// This is an offline, deterministic pass (no AI service): it uses PlaceBuild's
// fallbacks to prepare the Brief and lay out zones, then varies one parameter at
// a time and saves zones.png as parameter__option.png.
#include "assets/ProjectTemplate.h"
#include "assets/LayoutImage.h"
#include "assets/TextureCook.h"
#include "editing/PlaceBuild.h"
#include "editing/PaintedPlace.h"
#include "editing/PlaceBlockout.h"
#include "editing/PlaceScene.h"
#include "places/Compile.h"
#include "places/Planner.h"
#include "places/Solver.h"
#include "places/Brief.h"
#include "places/Ontology.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

std::string Sanitize(std::string s) {
    for (char& c : s) {
        if (c == ' ' || c == '/' || c == '\\') c = '-';
        else if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) c = '_';
    }
    if (s.empty()) s = "value";
    return s;
}

void CopyImage(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    if (fs::exists(from, ec)) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

void WriteTextFile(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << text;
}

struct Harness {
    fs::path project;
    fs::path out;
    PlaceBuild::Host host;
    std::unique_ptr<PlaceBuild> build;
    json manifest = json::array();

    Harness(const fs::path& outDir, bool clearOut = true) : out(outDir) {
        project = fs::temp_directory_path() / "aige_confirm_layout_params";
        fs::remove_all(project);
        if (clearOut) fs::remove_all(out);
        fs::create_directories(out);
        (void)CreateProject(project, std::nullopt, false);
        host.ontology = []() -> const Ontology& { return Library(); };
        host.catalog = [] { return Catalog{}; };
        host.isoGrid = [] { return true; };
        host.request = [](const std::string&, const json&, int) { return 0; }; // offline/fallback
        host.projectRoot = project;
        host.layoutStep = true;
        host.buildMethod = [] { return std::string("Assembled"); };
    }

    ~Harness() {
        build.reset();
        std::error_code ec;
        fs::remove_all(project, ec);
    }

    // Baseline: a village by river with several parts (marketplace, accommodations, etc.).
    json BaselineBrief() const {
        return CompleteBrief(Library(),
                             {{"buildType", "settlement"},
                              {"name", "Riverside Village"},
                              {"scale", {{"class", "village"}}},
                              {"shape", {{"layout", "organic"}}},
                              {"features", json::array({{{"module", "river"}}})},
                              {"components",
                               {{"residential-ward", {{"enabled", true}, {"count", json::array({2, 3})}}},
                                {"market-ward", {{"enabled", true}}},
                                {"temple-ward", {{"enabled", true}}},
                                {"farmstead-ward", {{"enabled", true}}}}}});
    }

    bool ReachLayout(const json& brief) {
        build = std::make_unique<PlaceBuild>(host);
        // Start from prompt with choices derived from the brief (offline fallbacks).
        json choices = {{"buildType", brief.value("buildType", "settlement")}};
        if (brief.contains("scale") && brief["scale"].contains("class")) choices["scale.class"] = brief["scale"]["class"];
        if (brief.contains("shape") && brief["shape"].contains("layout")) choices["shape.layout"] = brief["shape"]["layout"];
        if (brief.contains("walled")) choices["walled"] = brief["walled"];
        std::string err = build->Start(
            "A village by river with several parts (marketplace, accommodations etc.)", choices);
        (void)err;
        // Offline flow (host.request returns 0): fallbacks drive stages without AI answers.
        for (int step = 0; step < 16; ++step) {
            const std::string stage = build->Stage();
            if (HasLaidOutZones()) return true;
            if (stage == "layout") {
                // layout stage but zones map may not be materialized yet; try to ensure.
                (void)build->ReplanLayout();
                if (HasLaidOutZones()) return true;
            }
            if (stage == "request") {
                // Apply full brief values as user edits on Confirm, then continue.
                ApplyBriefEdits(brief);
                err = build->Confirm();
            } else if (stage == "assets") {
                ApplyBriefEdits(brief);
                err = build->Confirm();
            } else if (stage == "plan") {
                err = build->Approve();
            } else if (stage == "planned" || stage == "sourced" || stage == "built") {
                return HasLaidOutZones();
            } else if (stage == "planning" || stage == "interpreting") {
                // fallbacks should advance; nothing to answer
            } else {
                break;
            }
            if (step > 12) break;
        }
        if (build->Stage() == "layout" && !HasLaidOutZones()) (void)build->ReplanLayout();
        return HasLaidOutZones();
    }

    void ApplyBriefEdits(const json& brief) {
        if (!build) return;
        if (brief.contains("name")) build->Edit("name", brief["name"]);
        if (brief.contains("scale")) {
            const auto& s = brief["scale"];
            if (s.contains("class")) build->Edit("scale.class", s["class"]);
            if (s.contains("buildings")) build->Edit("scale.buildings", s["buildings"]);
            if (s.contains("area")) build->Edit("scale.area", s["area"]);
        }
        if (brief.contains("shape") && brief["shape"].contains("layout")) build->Edit("shape.layout", brief["shape"]["layout"]);
        if (brief.contains("walled")) build->Edit("walled", brief["walled"]);
        if (brief.contains("features")) build->Edit("features", brief["features"]);
        if (brief.contains("components") && brief["components"].is_object()) {
            for (const auto& [id, settings] : brief["components"].items()) {
                if (settings.is_object()) {
                    if (settings.contains("enabled")) build->Edit("components." + id + ".enabled", settings["enabled"]);
                    if (settings.contains("count")) build->Edit("components." + id + ".count", settings["count"]);
                    if (settings.contains("density")) build->Edit("components." + id + ".density", settings["density"]);
                }
            }
        }
    }

    bool HasLaidOutZones() const {
        if (!build) return false;
        const json state = build->State();
        if (state.value("stage", "") == "layout") {
            const json layout = state.value("layout", json::object());
            if (layout.value("zones", json::array()).is_array() && !layout.value("zones", json::array()).empty()) return true;
        }
        return build->HasLayout();
    }

    void Save(const std::string& param, const std::string& option, const std::string& table) {
        if (!build) return;
        const std::string base = Sanitize(param) + "__" + Sanitize(option);
        const fs::path img = build->Folder() / "zones.png";
        if (!fs::exists(img)) {
            // fallback: layout.png
        }
        CopyImage(fs::exists(img) ? img : (build->Folder() / "layout.png"), out / (base + ".png"));
        manifest.push_back({{"table", table},
                            {"parameter", param},
                            {"option", option},
                            {"file", base + ".png"},
                            {"stage", build->Stage()}});
        // keep artifacts per variant for reproducibility
        const fs::path vdir = out / "_artifacts" / base;
        CopyImage(build->Folder() / "zones.png", vdir / "zones.png");
        CopyImage(build->Folder() / "layout.png", vdir / "layout.png");
        CopyImage(build->Folder() / "brief.json", vdir / "brief.json");
        CopyImage(build->Folder() / "spec.json", vdir / "spec.json");
        CopyImage(build->Folder() / "layout.solved.json", vdir / "layout.solved.json");
    }

    void SaveManifest() {
        WriteTextFile(out / "manifest.json", manifest.dump(2));
    }
};

} // namespace

TEST(LayoutParams, SizeComparison5000vs10000) {
    if (std::getenv("AIGE_LAYOUT_PARAM_TEST") == nullptr) {
        GTEST_SKIP() << "set AIGE_LAYOUT_PARAM_TEST=1 to generate layout PNGs";
    }

    const fs::path source = fs::path(AIGE_SOURCE_DIR);
    const fs::path outDir = source / "builder_tests" / "layout" / "zone_tinted";

    for (int area : {10000, 40000}) {  // 100x100 and 200x200 cells
        std::string label = std::to_string(area) + "cells";
        Harness h(outDir, area == 10000);
        json b = h.BaselineBrief();
        // Remove area from brief so ApplyBriefEdits doesn't override
        if (b.contains("scale") && b["scale"].contains("area")) b["scale"].erase("area");
        ASSERT_TRUE(h.ReachLayout(b)) << (h.build ? h.build->State().dump(2) : "no build");
        // Now set the area directly in layout stage
        h.build->Edit("scale.area", json::object({{"area", area}}));
        // Re-layout with new area
        (void)h.build->ReplanLayout();
        // Check actual area
        json brief = h.build->Brief();
        std::cout << "Area " << area << " -> brief scale.area = " << brief.value("scale", json::object()).value("area", json()).dump() << std::endl;
        std::cout << "BriefSide would be ~" << std::sqrt(area) << std::endl;
        h.Save("size", label, "size_comparison");
        // Generate a PROPER plan with real buildings using the planner pipeline (like plan_place tool).
        // This gives us real building objects in proper locations with different kinds.
        json planBrief = h.BaselineBrief();
        // Override the area to match the requested size
        if (!planBrief.contains("scale")) planBrief["scale"] = json::object();
        planBrief["scale"]["area"] = json::object({{"area", area}});
        json rootAnswer = FallbackRootAnswer(Library(), planBrief);
        json planSpec = SpecFromRoot(Library(), planBrief, rootAnswer, "village-" + label);
        // Plan each zone with real buildings.
        json siblings = json::array();
        for (const auto& c : planSpec["root"].value("children", json::array())) {
            siblings.push_back({{"id", c.value("id", "")}, {"module", c.value("module", "")}, {"quantity", c.value("quantity", json())},
                                {"quality", c.value("quality", json())}});
        }
        for (const auto& zone : ZoneIds(Library(), planSpec)) {
            json* zoneNode = SpecNode(planSpec, zone);
            if (zoneNode == nullptr) continue;
            json asked = *zoneNode;
            asked.erase("children");
            json zoneAnswer = FallbackZoneAnswer(Library(), *zoneNode, planSpec["root"]);
            ApplyZoneAnswer(Library(), planBrief, planSpec, zone, zoneAnswer);
        }
        // Expand, tidy, resolve, rebalance.
        ExpandSpec(Library(), planBrief, planSpec);
        TidySpec(Library(), planSpec);
        ResolveSpec(Library(), planBrief, planSpec);
        RebalanceBuildings(Library(), planBrief, planSpec);
        // Compile and solve to get real building objects.
        CompileOptions compileOpts;
        compileOpts.seed = std::hash<std::string>{}(label);
        compileOpts.side = static_cast<int>(std::lround(std::sqrt(area)));
        compileOpts.fixedSide = true; // Don't grow side based on content
        json ir = CompileSpec(Library(), planSpec, compileOpts);
        SolvedLayout solved = SolveLayout(Library(), ir);
        // Count buildings.
        int buildings = 0;
        for (const auto& o : solved.document.value("objects", json::array())) {
            if (o.value("layer", "") == "building" && o.value("primary", false)) ++buildings;
        }
        std::cout << label << " plan: " << buildings << " buildings, " << solved.document.value("regions", json::array()).size() << " regions\n";
        // Write the solved plan.
        WriteTextFile(outDir / ("plan_solved_" + label + ".json"), solved.document.dump(2));
        // Generate blockout WITHOUT buildings: land-only, then tint zones with distinct colors.
        fs::path blockoutFolder = outDir / ("blockout_temp_" + label);
        fs::create_directories(blockoutFolder);
        // Land-only blockout (terrain + roads + water + trees, no buildings)
        PrepareBlockout(solved.document, &Library(), blockoutFolder, 0, true);
        // Tint zones with distinct colors.
        TintZones(solved.document, &Library(), blockoutFolder);
        // Copy the tinted blockout to output.
        fs::copy_file(blockoutFolder / "blockout.png", outDir / ("blockout_" + label + ".png"), fs::copy_options::overwrite_existing);
        if (fs::exists(blockoutFolder / "blockout.legend.json"))
            fs::copy_file(blockoutFolder / "blockout.legend.json", outDir / ("legend_" + label + ".json"), fs::copy_options::overwrite_existing);
        fs::remove_all(blockoutFolder);
        // Render layout image from the solved layout.
        const auto [x0, y0, x1, y1] = OccupiedBounds(solved.document);
        LayoutImageOptions layoutOpts;
        layoutOpts.targetSize = 2000;
        layoutOpts.title = "Village by a river";
        std::map<std::string, std::string> moduleNames;
        for (const auto& [id, m] : Library().Modules()) moduleNames[id] = m.name;
        std::function<void(const json&)> walk = [&](const json& node) {
            layoutOpts.modules[node.value("id", "")] = node.value("module", "");
            for (const auto& c : node.value("children", json::array())) walk(c);
        };
        if (planSpec.contains("root")) walk(planSpec["root"]);
        layoutOpts.moduleNames = moduleNames;
        std::map<std::string, std::string> zoneNames;
        for (const auto& r : solved.document.value("regions", json::array())) {
            const ModuleDef* m = Library().Module(r.value("module", ""));
            if (m == nullptr || m->level != PlaceLevel::Zone) continue;
            const std::string id = r.value("id", "");
            if (!zoneNames.contains(id)) zoneNames[id] = m->name;
        }
        layoutOpts.names = zoneNames;
        ImageData layoutImg = RenderLayoutImage(solved.document, layoutOpts);
        WritePng(outDir / ("size__" + label + ".png"), layoutImg);
        // Also save the plan spec.
        WriteTextFile(outDir / ("plan_spec_" + label + ".json"), planSpec.dump(2));
        WriteTextFile(outDir / ("brief_" + label + ".json"), planBrief.dump(2));
        WriteTextFile(outDir / ("state_" + label + ".json"), solved.document.dump(2));
    }
}

TEST(LayoutParams, ConfirmAndLayoutParameterVariants) {
    if (std::getenv("AIGE_LAYOUT_PARAM_TEST") == nullptr) {
        GTEST_SKIP() << "set AIGE_LAYOUT_PARAM_TEST=1 to generate layout PNGs into builder_tests/layout";
    }

    const fs::path source = fs::path(AIGE_SOURCE_DIR);
    const fs::path outDir = source / "builder_tests" / "layout" / "param_variants";
    Harness h(outDir, true);

    const json baseline = h.BaselineBrief();
    ASSERT_TRUE(h.ReachLayout(baseline)) << (h.build ? h.build->State().dump(2) : "no build");

    // Baseline output.
    h.Save("baseline", "riverside-village", "baseline");
    WriteTextFile(outDir / "baseline_brief.json", h.build->Brief().dump(2));
    WriteTextFile(outDir / "baseline_state.json", h.build->State().dump(2));

    const json baseState = h.build->State();
    const json window = baseState.value("window", json::object());

    // --- Confirm (top): size classes ---
    if (window.contains("size") && window["size"].contains("classes")) {
        for (const auto& c : window["size"]["classes"]) {
            const std::string id = c.value("id", "");
            if (id.empty()) continue;
            Harness v(outDir, false);
            json b = baseline;
            b["scale"]["class"] = id;
            if (!v.ReachLayout(b)) continue;
            v.Save("size_class", id, "confirm");
            for (auto& m : v.manifest) h.manifest.push_back(m);
        }
    }

    // --- Confirm (top): layout/shape format options ---
    if (window.contains("layout") && window["layout"].contains("options")) {
        for (const auto& opt : window["layout"]["options"]) {
            if (!opt.is_string()) continue;
            const std::string id = opt.get<std::string>();
            Harness v(outDir, false);
            json b = baseline;
            b["shape"]["layout"] = id;
            if (!v.ReachLayout(b)) continue;
            v.Save("layout_type", id, "confirm"); // e.g. layout_type__grid.png
            for (auto& m : v.manifest) h.manifest.push_back(m);
        }
    }

    // --- Confirm (top): walled options ---
    if (window.contains("walled") && window["walled"].contains("options")) {
        for (const auto& opt : window["walled"]["options"]) {
            if (!opt.is_string()) continue;
            const std::string id = opt.get<std::string>();
            Harness v(outDir, false);
            json b = baseline;
            b["walled"] = id;
            if (!v.ReachLayout(b)) continue;
            v.Save("walled", id, "confirm");
            for (auto& m : v.manifest) h.manifest.push_back(m);
        }
    }

    // --- Confirm: zone/component toggles and counts for zone-level components ---
    if (window.contains("components") && window["components"].is_array()) {
        for (const auto& comp : window["components"]) {
            if (comp.value("level", "") != "zone") continue;
            const std::string id = comp.value("id", "");
            if (id.empty()) continue;
            // toggle off/on variant
            {
                Harness v(outDir, false);
                json b = baseline;
                if (!b["components"].contains(id)) b["components"][id] = json::object();
                b["components"][id]["enabled"] = !comp.value("enabled", true);
                if (v.ReachLayout(b)) {
                    v.Save("zone_" + id + "_enabled", b["components"][id]["enabled"].get<bool>() ? "on" : "off", "confirm");
                    for (auto& m : v.manifest) h.manifest.push_back(m);
                }
            }
        }
    }

    // --- Layout form (top of Layout page): each field's enumerated options ---
    // Recreate baseline at layout for layout-form edits.
    {
        Harness base(outDir, false);
        ASSERT_TRUE(base.ReachLayout(baseline));
        const json st = base.build->State();
        const json form = st.value("layoutForm", json::array());
        for (const auto& f : form) {
            if (!f.contains("values") || !f["values"].is_array()) continue;
            const std::string field = f.value("field", "");
            if (field.empty()) continue;
            for (const auto& val : f["values"]) {
                if (!val.is_string()) continue;
                const std::string option = val.get<std::string>();
                Harness v(outDir, false);
                if (!v.ReachLayout(baseline)) continue;
                const std::string err = v.build->SetRootField(field, option);
                if (!err.empty()) {
                    h.manifest.push_back({{"table", "layout_form"}, {"parameter", field}, {"option", option}, {"error", err}});
                    continue;
                }
                // after SetRootField layout is re-laid out; save zones
                v.Save(field, option, "layout_form"); // e.g. zone-shape__blocks.png
                for (auto& m : v.manifest) h.manifest.push_back(m);
            }
        }

        // --- Zones table (top of Layout page): vary area/count for first zone if available ---
        const json layout = st.value("layout", json::object());
        const json zones = layout.value("zones", json::array());
        if (!zones.empty()) {
            const json z = zones.front();
            const std::string zid = z.value("id", z.value("module", "zone"));
            // try a couple of area multipliers
            if (z.contains("area")) {
                for (const auto& [label, factor] : std::vector<std::pair<std::string, double>>{{"smaller", 0.6}, {"larger", 1.6}}) {
                    Harness v(outDir, false);
                    if (!v.ReachLayout(baseline)) continue;
                    json areaVal = z["area"];
                    // try to scale numeric area
                    double baseArea = 0;
                    if (areaVal.is_number()) baseArea = areaVal.get<double>();
                    else if (areaVal.is_object() && areaVal.contains("area") && areaVal["area"].is_number()) baseArea = areaVal["area"].get<double>();
                    if (baseArea > 0) {
                        json newVal = areaVal.is_object() ? areaVal : json::object();
                        if (newVal.is_object()) newVal["area"] = static_cast<int>(baseArea * factor);
                        else newVal = static_cast<int>(baseArea * factor);
                        const std::string err = v.build->SetZoneField(zid, "area", newVal);
                        if (err.empty()) {
                            v.Save("zone_" + zid + "_area", label, "zones");
                            for (auto& m : v.manifest) h.manifest.push_back(m);
                        } else {
                            h.manifest.push_back({{"table", "zones"}, {"parameter", "area"}, {"zone", zid}, {"option", label}, {"error", err}});
                        }
                    }
                }
            }
        }
    }

    h.SaveManifest();

    // At least baseline should be produced.
    EXPECT_TRUE(fs::exists(outDir / "baseline__riverside-village.png")) << "baseline layout image not generated in " << outDir.string();
}
