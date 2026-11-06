// A dungeon's saved spec planned again with each packing (apart, packed), no AI, and its whole level drawn as the
// Paint method's block-out: what an image model is tested on (builder_tests/bfl_paint).
//
//     plan_blockout <spec.json> <out dir>
//
// Writes <packing>-solved.json, <packing>-blockout.png, <packing>-blockout-ids.png, <packing>-blockout.legend.json and
// <packing>-blockout-plain.png (no things in it); and <packing>-editor/ as the editor's Paint method prepares it.
//
//     plan_blockout --brief <brief.json> <out dir>
//
// A place planned from a Brief alone, as the editor does with the AI off (docs/places/tiling.md: a world from its
// definitions): the ontology's load problems, the spec, the solved plan and its block-out (spec.json, solved.json,
// blockout.png, problems.txt).
#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "editing/PlaceBlockout.h"
#include "places/Brief.h"
#include "places/Compile.h"
#include "places/Ontology.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;
using namespace aige;

namespace {

int FromBrief(const fs::path& briefFile, const fs::path& out) {
    fs::create_directories(out);
    Ontology ontology;
    std::ofstream problems(out / "problems.txt");
    for (const auto& e : ontology.Load({EnginePlacesDir()})) problems << "ontology: " << e << "\n", std::cout << "ontology: " << e << "\n";
    json brief;
    {
        std::ifstream in(briefFile);
        brief = json::parse(in);
    }
    brief = CompleteBrief(ontology, brief);
    json spec = SpecFromRoot(ontology, brief, FallbackRootAnswer(ontology, brief), "test-place");
    const json root = spec.value("root", json::object());
    for (const auto& zone : ZoneIds(ontology, spec)) {
        if (json* node = SpecNode(spec, zone)) ApplyZoneAnswer(ontology, brief, spec, zone, FallbackZoneAnswer(ontology, *node, root));
    }
    ExpandSpec(ontology, brief, spec);
    for (const auto& p : TidySpec(ontology, spec)) problems << "tidy: " << p.message << "\n";
    for (const auto& p : ResolveSpec(ontology, brief, spec)) problems << "resolve: " << p.message << "\n";
    std::ofstream(out / "brief.json") << brief.dump(2);
    std::ofstream(out / "spec.json") << spec.dump(2);
    CompileOptions options;
    const json ir = CompileSpec(ontology, spec, options);
    std::ofstream(out / "ir.json") << ir.dump(2);
    const SolvedLayout solved = SolveLayout(ontology, ir);
    std::ofstream(out / "solved.json") << solved.document.dump(2);
    for (const auto& p : solved.problems) problems << "solve: " << p.dump() << "\n";
    const json b = solved.document["grid"]["bounds"];
    json legend;
    const ImageData image = RenderPlanBlockout(solved.document, b[0], b[1], b[2], b[3], 3000, &ontology, &legend);
    WritePng(out / "blockout.png", image);
    std::ofstream(out / "blockout.legend.json") << legend.dump(2);
    std::cout << brief.value("buildType", "") << ": bounds " << b.dump() << ", " << solved.document.value("regions", json::array()).size()
              << " regions, " << solved.problems.size() << " problems; children:";
    for (const auto& c : spec["root"].value("children", json::array())) std::cout << " " << c.value("module", "");
    std::cout << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--brief") return FromBrief(argv[2], argv[3]);
    if (argc < 3) {
        std::cerr << "plan_blockout <spec.json> <out dir>  |  plan_blockout --brief <brief.json> <out dir>\n";
        return 2;
    }
    json source;
    {
        std::ifstream in(argv[1]);
        source = json::parse(in);
    }
    const fs::path out = argv[2];
    fs::create_directories(out);
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    for (const std::string packing : {"apart", "packed"}) {
        json spec = source;
        spec["root"]["quality"]["packing"] = packing;
        spec["root"].erase("layoutSide");
        CompileOptions options;
        if (const json s = spec["root"].value("layoutSeed", json()); s.is_number_unsigned()) options.seed = s.get<uint64_t>();
        const json ir = CompileSpec(ontology, spec, options);
        const SolvedLayout solved = SolveLayout(ontology, ir);
        std::ofstream(out / (packing + "-solved.json")) << solved.document.dump(2);
        for (const auto& p : solved.problems) std::cout << "  " << packing << " problem: " << p.dump() << "\n";
        const json b = solved.document["grid"]["bounds"];
        json legend;
        ImageData ids;
        const ImageData image = RenderPlanBlockout(solved.document, b[0], b[1], b[2], b[3], 2000, &ontology, &legend, &ids);
        WritePng(out / (packing + "-blockout.png"), image);
        WritePng(out / (packing + "-blockout-ids.png"), ids);
        std::ofstream(out / (packing + "-blockout.legend.json")) << legend.dump(2);
        // And with no things at all: the rooms and walls alone (nothing an image model could read a thing from).
        json bare = solved.document;
        bare["objects"] = json::array();
        json bareLegend;
        ImageData bareIds;
        WritePng(out / (packing + "-blockout-plain.png"), RenderPlanBlockout(bare, b[0], b[1], b[2], b[3], 2000, &ontology, &bareLegend, &bareIds));
        // As the editor's Paint method prepares it (PrepareBlockout): only the things painted in, in their own colours.
        PrepareBlockout(solved.document, &ontology, out / (packing + "-editor"));
        std::cout << packing << ": bounds " << b.dump() << ", " << solved.document.value("regions", json::array()).size()
                  << " regions\n";
    }
    return 0;
}
