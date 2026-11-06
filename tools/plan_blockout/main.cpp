// A dungeon's saved spec planned again with each packing (apart, packed), no AI, and its whole level drawn as the
// Paint method's block-out: what an image model is tested on (builder_tests/bfl_paint).
//
//     plan_blockout <spec.json> <out dir>
//
// Writes <packing>-solved.json, <packing>-blockout.png, <packing>-blockout-ids.png, <packing>-blockout.legend.json and
// <packing>-blockout-plain.png (no things in it); and <packing>-editor/ as the editor's Paint method prepares it.
#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "editing/PlaceBlockout.h"
#include "places/Compile.h"
#include "places/Ontology.h"
#include "places/Solver.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;
using namespace aige;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "plan_blockout <spec.json> <out dir>\n";
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
