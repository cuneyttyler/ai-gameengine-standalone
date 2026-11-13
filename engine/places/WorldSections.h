#pragma once

#include "places/Compile.h"
#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace aige {

// An open world's sections (docs/places/tiling.md §5): each settled place and each forest of the world plan, planned
// as a place of its own on the land the world gave it, then put back into the world. The Builder's world build, the
// Planner's game build and the plan_blockout tool all go through these.

// A solved plan's terrain as one name per cell (its run-length rows), and back.
std::vector<std::string> TerrainCells(const nlohmann::json& solved, int width, int height);
void SetTerrain(nlohmann::json& solved, const std::vector<std::string>& cells, int width, int height);

// One section to plan: which of the world's lands it is, what it's built as, and how (its Brief, its compile options:
// its land, its ways in, the world's roads through it, its own seed).
struct SectionJob {
    std::string id;    // its site's id in the world ("heartland-p1/site", "forest/site"): its objects' prefix there
    std::string land;  // the world's region it's built on ("heartland-p1", "forest")
    std::string place; // the world Brief's place it is ("p1"), or "" (a forest)
    std::string name, kind, cls;
    int x = 0, y = 0, side = 0;     // its square in the world
    std::vector<uint8_t> landMask;  // its land in its square (side * side)
    nlohmann::json brief;           // its own Brief: its type, its size's class, its own components
    CompileOptions options;         // fixed side, outside its land, its ways in, the world's roads, its seed
    nlohmann::json rootFields;      // its root's fields as the world type sets them (no woods of its own)
    nlohmann::json withoutParts;    // the parts no section has (forests: sections of their own)
    bool forest = false;            // a forest section: its woods are what it is
    std::string world;              // the world's build (its id), to go back to
};

// A section's job kept with its build (its session), so the build reopened on its own still plans on its land.
nlohmann::json SectionJobJson(const SectionJob& job);
SectionJob SectionJobFrom(const nlohmann::json& saved);

// The sections of a planned world, in its regions' order. `worldBrief` gives each place's name, type and own
// components (its places); `worldSpec` each site's class; `worldSolved` the lands, the roads and the ground.
std::vector<SectionJob> WorldSectionJobs(const Ontology& ontology, const nlohmann::json& worldBrief, const nlohmann::json& worldSpec,
                                         const nlohmann::json& worldSolved);

// A section's Spec as the world type says, after it's planned (whoever planned it: the AI too): its root's fields set,
// the parts no section has taken out. Nothing for a forest.
void ApplySectionSpec(const SectionJob& job, nlohmann::json& spec);

// A planned section put into the world (`world`: the world's solved plan, changed): round its land the world's own
// ground, then the section itself inside its ragged edge (its buildings, streets, yards and fields), its objects and
// regions under its id; beyond it the world's land carries on.
void PutSection(nlohmann::json& world, nlohmann::json section, const SectionJob& job, const std::vector<std::string>& worldGround);

// The world with its sections in, as a plan to build or paint: its world roads ordinary roads (their own colour is
// only the world plan's).
void FinishTogether(nlohmann::json& together);

} // namespace aige
