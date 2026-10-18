#pragma once

#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aige {

// The PLL solver (development_plan.md 7.6, docs/layout-ir.md §7-9): turns a
// layout document (aige.ir/1) into a solved one (aige.solved/1): every
// region's cells, every object's footprint and facing, every path's cells,
// every line's edges, the terrain of every cell, relation scores and checks.
//
// It works top-down through the region tree. Inside each region, statements
// are taken in a fixed order of steps:
//   b path, network   c partition, subdivide   d place (primary)
//   e region, anchor  f place                  g line
//   h rows, grid      i scatter
// then each sub-region, large first; then fill (j) and check (k) over the
// whole document. Each statement draws from its own random stream, seeded by
// hash(seed, id), so results are reproducible and one statement more doesn't
// shift the others.
//
// Not yet: floorplan, graph and connect (interiors and dungeons, 7.10), and
// annealing over the soft objective (placement is greedy best-score with
// seeded tie-breaks).

struct SolveOptions {
    int maxCandidates = 4000; // placement positions tried per object, at most
};

struct SolvedLayout {
    nlohmann::json document;               // aige.solved/1
    std::vector<nlohmann::json> problems;  // {code, statement, node, message}
    bool ok() const;
};

SolvedLayout SolveLayout(const Ontology& ontology, const nlohmann::json& ir, const SolveOptions& options = {});

// A region's cells as row runs [[y, x0, x1], ...] (x1 exclusive) and back.
nlohmann::json EncodeCells(const std::vector<int>& cells, int width);
std::vector<int> DecodeCells(const nlohmann::json& runs, int width);

} // namespace aige
