#pragma once

#include "assets/ArtAnalysis.h"
#include "assets/AssetDatabase.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aige {

class Ontology;

// The asset catalog v2 (development_plan.md 7.3, ai-building.md §6): what
// the builder knows about each piece of art, so a slot ("a fence piece set
// in wood", "a cottage") finds the right asset by meaning, size,
// perspective and style rather than by file name.
//
// Kept in each asset's .meta with the rest of its analysis:
// - from the pixels (AnalyzeArtPixels): `shape` (the ArtShape, as JSON) and,
//   for a sheet of several things, `objects` (each one's region and shape);
// - from the vision model (6.2, extended here): kind, description, tags,
//   name, direction, cells, blocks, and now roles, perspective, layer,
//   facings, anchors (door, gate...), accessible;
// - by hand in the review (Assets panel): any of these, which then win.

// The analysis keys this version writes.
inline constexpr const char* kShapeKey = "shape";
inline constexpr const char* kObjectsKey = "objects";

// Reads an image's pixels and stores `shape` (and `objects` for a sheet:
// several separate things on a transparent background, or a tileset whose
// tiles join into bigger things). Returns false if it can't be read.
bool AnalyzeArtPixels(AssetDatabase& assets, const std::string& path);

// An ArtShape as the one-line JSON kept in the meta (`shape`).
std::string FormatShape(const ArtShape& shape);

// Sets catalog fields of an entry: the asset's analysis, or for an object
// of a sheet ("<path>#<n>") that object's own fields (an empty value removes one).
bool SetCatalogFields(AssetDatabase& assets, const std::string& id, const std::map<std::string, std::string>& fields);

struct CatalogEntry {
    std::string id;   // the asset path, or "<path>#<n>" for the n-th object of a sheet
    std::string path;
    ImageRect rect;   // its region in the image
    ArtShape shape;

    // Meaning (vision, or inferred from it).
    std::string name, kind, description;
    std::vector<std::string> tags;
    std::vector<std::string> roles;    // ontology roles it can fill (canFill)
    std::vector<std::string> concepts; // ontology concepts it shows ("rose", "cottage")
    std::string affordance;            // terrain, environmental-object, interactive-object, item, character

    // Form.
    std::string perspective;  // top-down, three-quarter, isometric
    bool flat = false;        // lies on the ground (drawn as the cell) rather than standing up
    std::string tileType;     // texture, complete, partial
    std::string layer;        // ground, decal, prop, building, overhead
    Footprint footprint;      // cells on the ground, from the base
    float anchorU = 0.5f, anchorV = 1.0f; // where it stands, 0-1 across its region
    float spriteW = 1.0f, spriteH = 1.0f; // its size on screen in cells (from the footprint)
    float pixelsPerCell = 0.0f;
    std::string facings;      // any (flat), two (drawn + mirrored), one (symmetric)
    std::string variantOf;    // the variant group (name without numbers and directions)
    std::map<std::string, std::array<float, 2>> anchors; // named points on the art: door, gate...
    bool passable = true;
    bool accessible = false;  // can be entered (has a door)

    // Linear features.
    std::string pieceSet;     // the set it belongs to, if it's a fence / wall / hedge piece
    std::string piece;        // straight-h, corner-ne, ... (ArtAnalysis PieceKind)
    std::string pieceGrid;    // square or iso: the grid its pieces join on

    // Provenance.
    std::string source = "project"; // project, library, web, generated
    std::string licence, author, url;
    bool own = false; // the user's own art (from their library: made by them or the Art tool): no licence needed

    // What the review should look at.
    std::vector<std::string> problems;
};

// A fence, wall or hedge drawn as joining pieces.
struct PieceSet {
    std::string id;     // "<folder>/<stem>", e.g. "assets/fences/wood-fence"
    std::string role;   // fence-piece-set, wall-piece-set, hedge-piece-set
    std::string grid;   // square or iso
    std::map<std::string, std::string> pieces; // piece kind -> entry id
    std::vector<std::string> missing;          // what a line needs and the set lacks
    bool usable = false; // a straight in each direction (one may be a mirror) and a corner or post
};

struct CatalogOptions {
    // The grid the project builds on: true for an isometric grid.
    bool isoGrid = true;
    // Pixels per cell for a folder (PackScaleFor); 0 for the default.
    std::function<float(const std::string& folder)> pixelsPerCell;
};

struct Catalog {
    std::vector<CatalogEntry> entries;
    std::vector<PieceSet> pieceSets;
    const CatalogEntry* Find(const std::string& id) const;
};

// The catalog of every image asset. `ontology` (optional) supplies the roles
// and concepts the words name.
Catalog BuildCatalog(const AssetDatabase& assets, const Ontology* ontology, const CatalogOptions& options);

// One entry from an asset's info (for a single asset, or a test).
std::vector<CatalogEntry> CatalogEntriesFor(const AssetInfo& asset, const Ontology* ontology, const CatalogOptions& options);

// Groups piece entries into sets and checks each, flagging set problems on
// the entries (wrong grid, missing pieces).
std::vector<PieceSet> BuildPieceSets(std::vector<CatalogEntry>& entries, const CatalogOptions& options);

// Whether art of this perspective works on the grid: flat art must be drawn
// for it (top-down on a square grid, isometric on an isometric one);
// upright art stands on either.
bool PerspectiveFitsGrid(const CatalogEntry& entry, bool isoGrid);

// --- Slot retrieval (place-assets.md §4) ---------------------------------------------

struct SlotNeed {
    std::string role;                  // an ontology role, or several "crop|flower"
    std::vector<std::string> concepts; // what it must show (any of them), e.g. {"rose"}
    float minCells = 0.0f, maxCells = 1e9f; // footprint width range
    std::string layer;                 // "" = any
    bool joined = false;               // needs a piece set (fences, walls, hedges)
    bool styleCritical = false;
    // The style to match: the palette and pixels per cell of the place's
    // other art, and its pack folder (same pack = full style score).
    std::vector<std::string> palette;
    float pixelsPerCell = 0.0f;
    std::string folder;
};

struct Candidate {
    const CatalogEntry* entry = nullptr;
    const PieceSet* pieceSet = nullptr; // joined needs: the set
    float fit = 0.0f;
    float semantic = 0.0f, style = 0.0f, size = 0.0f, quality = 0.0f;
    std::string grade; // good (>= 0.75), usable (>= 0.55)
};

struct Rejection {
    std::string id;
    std::string reason;
};

struct Retrieval {
    std::vector<Candidate> candidates; // best first
    std::vector<Rejection> rejected;   // with why (the review and "Why?" show them)
};

// Hard filters (role and concept, affordance, perspective vs grid, footprint
// within the range after scaling by up to a third, layer, licence known, a
// usable piece set), then the fit score.
Retrieval RetrieveForSlot(const Catalog& catalog, const Ontology& ontology, const SlotNeed& need, bool isoGrid);

} // namespace aige
