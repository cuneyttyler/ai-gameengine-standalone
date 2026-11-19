// Regenerate a place's Paint block-out through the editor's own code (PaintedPlace.cpp), without decorations.
//
//     regen_blockout <project root> <place dir> [width]
//
// Loads <place dir>/layout.solved.json, then PrepareBlockout(landOnly=true) -> TintZones, writing blockout.png and
// painting.json into the place folder exactly as the editor's Paint method does.
#include "editing/PaintedPlace.h"
#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

using nlohmann::json;
namespace fs = std::filesystem;
using namespace aige;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: regen_blockout <project root> <place dir> [width]\n";
        return 2;
    }
    const fs::path root = argv[1];
    const fs::path folder = argv[2];
    const int width = argc >= 4 ? std::atoi(argv[3]) : 0;

    Ontology ontology;
    for (const auto& e : ontology.Load({root / "data" / "places"})) std::cerr << "ontology: " << e << "\n";

    std::ifstream in(folder / "layout.solved.json");
    if (!in) { std::cerr << "no layout.solved.json in " << folder << "\n"; return 1; }
    json solved = json::parse(in);

    // The editor's Paint (Free) path: land-only block-out at the paint density, then solid zone tints.
    PrepareBlockout(solved, &ontology, folder, width, true);
    TintZones(solved, &ontology, folder);
    std::cout << "regenerated blockout in " << folder << "\n";
    return 0;
}
