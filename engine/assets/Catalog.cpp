#include "assets/Catalog.h"

#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

json RectJson(const ImageRect& r) { return json::array({r.x, r.y, r.w, r.h}); }
ImageRect RectFrom(const json& j) {
    if (!j.is_array() || j.size() != 4) return {};
    return {j[0].get<int>(), j[1].get<int>(), j[2].get<int>(), j[3].get<int>()};
}

json ShapeJson(const ArtShape& s) {
    std::string touches;
    for (bool t : s.touches) touches += t ? '1' : '0';
    return {{"region", RectJson(s.region)},
            {"opaque", RectJson(s.opaque)},
            {"base", RectJson(s.base)},
            {"anchor", {std::round(s.anchorX * 10) / 10, std::round(s.anchorY * 10) / 10}},
            {"shadow", s.shadow},
            {"share", std::round(s.opaqueShare * 1000) / 1000},
            {"edgesOpaque", s.edgesOpaque},
            {"seamless", s.seamless},
            {"tileType", s.tileType},
            {"silhouette", s.silhouette},
            {"symmetric", s.symmetric},
            {"touches", touches},
            {"axis", std::round(s.axisDegrees * 10) / 10},
            {"elongation", std::round(s.elongation * 100) / 100},
            {"palette", s.palette}};
}

ArtShape ShapeFrom(const json& j) {
    ArtShape s;
    if (!j.is_object()) return s;
    s.region = RectFrom(j.value("region", json()));
    s.opaque = RectFrom(j.value("opaque", json()));
    s.base = RectFrom(j.value("base", json()));
    if (j.contains("anchor") && j["anchor"].is_array() && j["anchor"].size() == 2) {
        s.anchorX = j["anchor"][0].get<float>();
        s.anchorY = j["anchor"][1].get<float>();
    }
    s.shadow = j.value("shadow", false);
    s.opaqueShare = j.value("share", 0.0f);
    s.edgesOpaque = j.value("edgesOpaque", false);
    s.seamless = j.value("seamless", false);
    s.tileType = j.value("tileType", "");
    s.silhouette = j.value("silhouette", "");
    s.symmetric = j.value("symmetric", false);
    const std::string touches = j.value("touches", "0000");
    for (size_t i = 0; i < 4 && i < touches.size(); ++i) s.touches[i] = touches[i] == '1';
    s.axisDegrees = j.value("axis", 0.0f);
    s.elongation = j.value("elongation", 1.0f);
    for (const auto& c : j.value("palette", json::array())) {
        if (c.is_string()) s.palette.push_back(c.get<std::string>());
    }
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream in(text);
    while (std::getline(in, item, ',')) {
        item.erase(0, item.find_first_not_of(' '));
        item.erase(item.find_last_not_of(' ') + 1);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

// "Wood Fence_Corner 2" -> {"wood", "fence", "corner", "2"}
std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : Lower(text)) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            word += c;
        } else if (!word.empty()) {
            out.push_back(word);
            word.clear();
        }
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

bool Has(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

const std::set<std::string> kPieceWords = {"horizontal", "vertical", "corner", "end", "straight", "post", "gate",
                                           "left", "right", "up", "down", "n", "e", "s", "w", "ne", "nw", "se", "sw",
                                           "t", "cross", "piece", "h", "v", "x", "y", "variant", "alt"};

// The name without numbers and direction words: what variants share.
std::string Stem(const std::string& name) {
    std::string out;
    for (const auto& w : Words(name)) {
        if (kPieceWords.contains(w) || std::all_of(w.begin(), w.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) continue;
        out += (out.empty() ? "" : "-") + w;
    }
    return out.empty() ? Lower(name) : out;
}

bool RoleIsA(const Ontology& ontology, std::string role, const std::string& target) {
    for (int guard = 0; guard < 16 && !role.empty(); ++guard) {
        if (role == target) return true;
        const Role* r = ontology.FindRole(role);
        role = r ? r->broader : "";
    }
    return false;
}

bool IsPieceRole(const std::string& role) { return role.size() > 10 && role.substr(role.size() - 10) == "-piece-set"; }

// Roles from the words of the name, tags and kind, when the vision model gave none.
std::vector<std::string> InferRoles(const Ontology& ontology, const std::string& kind, const std::vector<std::string>& words,
                                    const std::string& direction) {
    std::vector<std::string> roles;
    auto add = [&](const std::string& r) {
        if (ontology.FindRole(r) && !Has(roles, r)) roles.push_back(r);
    };
    if (kind == "ground") {
        bool specific = false;
        for (const auto& w : words) {
            if (w == "road" || w == "path" || w == "street" || w == "cobble" || w == "cobblestone") {
                add("road-terrain");
                specific = true;
            }
            if (w == "water" || w == "river" || w == "lake" || w == "sea") {
                add("water-terrain");
                specific = true;
            }
            if (w == "tilled" || w == "farmland" || w == "soil") {
                add("tilled-terrain");
                specific = true;
            }
        }
        if (!specific) add("ground-tile");
        return roles;
    }
    for (const auto& w : words) {
        if (ontology.FindRole(w + "-piece-set") && (direction != "none" || Has(words, "corner") || Has(words, "piece"))) {
            add(w + "-piece-set");
        } else {
            add(w);
        }
        // Plurals: "trees" -> tree.
        if (w.size() > 3 && w.back() == 's') add(w.substr(0, w.size() - 1));
    }
    // A fence or wall that runs one way is a piece of a set.
    for (const char* piece : {"fence", "wall", "hedge"}) {
        if (Has(words, piece) && direction != "none") add(std::string(piece) + "-piece-set");
    }
    if (roles.empty()) {
        if (kind == "building") add("building");
        else if (kind == "nature") add("plant");
        else if (kind == "decoration" || kind == "item") add("prop");
    }
    return roles;
}

std::string AffordanceOfKind(const std::string& kind) {
    if (kind == "ground") return "terrain";
    if (kind == "item") return "item";
    if (kind == "character") return "character";
    return "environmental-object";
}

std::string LayerOf(const std::string& kind, bool flat, const std::string& tileType) {
    if (kind == "ground") return tileType == "partial" ? "decal" : "ground";
    if (kind == "building") return "building";
    if (kind == "effect") return "overhead";
    return flat ? "decal" : "prop";
}

std::string Get(const std::map<std::string, std::string>& analysis, const std::string& key) {
    auto it = analysis.find(key);
    return it == analysis.end() ? std::string() : it->second;
}

} // namespace

bool AnalyzeArtPixels(AssetDatabase& assets, const std::string& path) {
    const AssetInfo* asset = assets.Find(path);
    if (asset == nullptr || asset->type == AssetType::Audio || !asset->error.empty()) return false;
    const auto image = assets.LoadTexture(path);
    if (!image || image->width == 0) return false;
    const ArtShape whole = AnalyzeShape(*image);
    std::vector<ImageRect> objects;
    if (asset->type == AssetType::Tileset) {
        objects = SegmentTiles(*image, static_cast<int>(asset->tileWidth), static_cast<int>(asset->tileHeight));
    } else if (whole.tileType == "partial") {
        // A sheet: several separate things of some size on a transparent background.
        const int area = static_cast<int>(image->width * image->height);
        const int gap = std::max(2, static_cast<int>(std::max(image->width, image->height)) / 100);
        objects = SegmentObjects(*image, gap, std::max(24, area / 200));
        if (objects.size() < 2) objects.clear();
    }
    json list = json::array();
    for (const auto& rect : objects) list.push_back({{"rect", RectJson(rect)}, {"shape", ShapeJson(AnalyzeShape(*image, rect))}});
    return assets.SetAnalysis(path, {{kShapeKey, ShapeJson(whole).dump()}, {kObjectsKey, objects.empty() ? "" : list.dump()}});
}

std::string FormatShape(const ArtShape& shape) { return ShapeJson(shape).dump(); }

bool SetCatalogFields(AssetDatabase& assets, const std::string& id, const std::map<std::string, std::string>& fields) {
    const size_t hash = id.rfind('#');
    if (hash == std::string::npos) return assets.SetAnalysis(id, fields);
    const std::string path = id.substr(0, hash);
    const AssetInfo* asset = assets.Find(path);
    if (asset == nullptr) return false;
    json objects = json::parse(Get(asset->analysis, kObjectsKey), nullptr, false);
    const int index = std::atoi(id.c_str() + hash + 1);
    if (!objects.is_array() || index < 0 || index >= static_cast<int>(objects.size())) return false;
    json& own = objects[index]["fields"];
    if (!own.is_object()) own = json::object();
    for (const auto& [key, value] : fields) {
        if (value.empty()) own.erase(key);
        else own[key] = value;
    }
    return assets.SetAnalysis(path, {{kObjectsKey, objects.dump()}});
}

const CatalogEntry* Catalog::Find(const std::string& id) const {
    for (const auto& e : entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

bool PerspectiveFitsGrid(const CatalogEntry& entry, bool isoGrid) {
    if (!entry.flat) return true;
    if (entry.perspective == "top-down") return !isoGrid;
    if (entry.perspective == "isometric") return isoGrid;
    return true;
}

std::vector<CatalogEntry> CatalogEntriesFor(const AssetInfo& asset, const Ontology* ontology, const CatalogOptions& options) {
    std::vector<CatalogEntry> out;
    if (asset.type == AssetType::Audio || !asset.error.empty()) return out;
    const auto& whole = asset.analysis;
    const std::string folder = fs::path(asset.path).parent_path().generic_string();

    struct Part {
        std::string id;
        ImageRect rect;
        ArtShape shape;
        json fields; // the object's own analysis (vision, review), over the image's
    };
    std::vector<Part> parts;
    const json objects = json::parse(Get(whole, kObjectsKey), nullptr, false);
    if (objects.is_array() && !objects.empty()) {
        for (size_t i = 0; i < objects.size(); ++i) {
            parts.push_back({asset.path + "#" + std::to_string(i), RectFrom(objects[i].value("rect", json())),
                             ShapeFrom(objects[i].value("shape", json())), objects[i].value("fields", json::object())});
        }
    } else {
        const json shape = json::parse(Get(whole, kShapeKey), nullptr, false);
        parts.push_back({asset.path, {0, 0, static_cast<int>(asset.width), static_cast<int>(asset.height)},
                         shape.is_object() ? ShapeFrom(shape) : ArtShape{}, json::object()});
    }

    for (const Part& part : parts) {
        // An object of a sheet: its own fields over the image's (except the
        // image's name and vision fields, which describe the sheet).
        std::map<std::string, std::string> a = whole;
        if (part.fields.is_object() && !part.fields.empty()) {
            for (const char* key : {"name", "kind", "description", "tags", "direction", "cells", "blocks", "roles",
                                    "perspective", "anchors", "accessible", "affordance", "layer", "facings"}) {
                a.erase(key);
            }
            for (const auto& [key, value] : part.fields.items()) {
                if (value.is_string()) a[key] = value.get<std::string>();
            }
        }
        CatalogEntry e;
        e.id = part.id;
        e.path = asset.path;
        e.rect = part.rect;
        e.shape = part.shape;
        e.name = Get(a, "name").empty() ? fs::path(asset.path).stem().string() : Get(a, "name");
        e.kind = Get(a, "kind");
        e.description = Get(a, "description");
        e.tags = SplitList(Get(a, "tags"));
        const std::string direction = Get(a, "direction").empty() ? "none" : Get(a, "direction");
        const bool known = !e.shape.region.Empty();

        // Form.
        e.tileType = !Get(a, "tileType").empty() ? Get(a, "tileType") : e.shape.tileType;
        const std::string cue = PerspectiveCue(e.shape);
        e.perspective = !Get(a, "perspective").empty() ? Get(a, "perspective")
                        : cue == "upright"           ? std::string("three-quarter")
                                                     : cue;
        if (!Get(a, "flat").empty()) e.flat = Get(a, "flat") == "1";
        else e.flat = e.kind == "ground" || e.shape.silhouette == "rect" || e.shape.silhouette == "diamond";

        // Scale: the pack's, else the vision model's size, else a ground tile is a cell.
        const float visionCells = Get(a, "cells").empty() ? 0.0f : std::stof(Get(a, "cells"));
        float ppc = options.pixelsPerCell ? options.pixelsPerCell(folder) : 0.0f;
        if (ppc <= 0 && visionCells > 0 && known) ppc = (e.flat ? e.shape.opaque.w : e.shape.base.w) / visionCells;
        if (ppc <= 0 && e.flat) ppc = static_cast<float>(part.rect.w);
        if (ppc <= 0) ppc = 64.0f;
        e.pixelsPerCell = ppc;
        if (known) {
            e.footprint = FootprintOf(e.shape, e.flat, ppc);
            e.anchorU = (e.shape.anchorX - part.rect.x) / std::max(1, part.rect.w);
            e.anchorV = (e.shape.anchorY - part.rect.y) / std::max(1, part.rect.h);
        } else if (visionCells > 0) {
            e.footprint.w = e.footprint.h = visionCells;
            e.footprint.cellsW = e.footprint.cellsH = std::max(1, static_cast<int>(std::lround(visionCells)));
        }
        if (!Get(a, "footprint").empty()) {
            int w = 0, h = 0;
            if (std::sscanf(Get(a, "footprint").c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                e.footprint = {static_cast<float>(w), static_cast<float>(h), w, h};
            }
        }
        e.spriteW = part.rect.w / ppc;
        e.spriteH = part.rect.h / ppc;
        e.layer = !Get(a, "layer").empty() ? Get(a, "layer") : LayerOf(e.kind, e.flat, e.tileType);
        e.facings = !Get(a, "facings").empty() ? Get(a, "facings") : e.flat ? "any" : e.shape.symmetric ? "one" : "two";
        e.variantOf = (folder.empty() ? "" : folder + "/") + Stem(e.name);
        const json anchors = json::parse(Get(a, "anchors"), nullptr, false);
        if (anchors.is_object()) {
            for (const auto& [key, uv] : anchors.items()) {
                if (uv.is_array() && uv.size() == 2) e.anchors[key] = {uv[0].get<float>(), uv[1].get<float>()};
            }
        }
        e.passable = Get(a, "blocks") != "1";
        e.accessible = Get(a, "accessible") == "1" || e.anchors.contains("door");

        // Meaning.
        std::vector<std::string> words = Words(e.name);
        for (const auto& t : e.tags) {
            for (const auto& w : Words(t)) words.push_back(w);
        }
        if (!Get(a, "roles").empty()) {
            e.roles = SplitList(Get(a, "roles"));
        } else if (ontology) {
            e.roles = InferRoles(*ontology, e.kind, words, direction);
        }
        if (ontology) {
            for (const auto& w : words) {
                for (const Concept* c : ontology->ConceptsNamed(w)) {
                    if (c->scheme != "density-level" && c->scheme != "affordance" && !Has(e.concepts, c->id)) e.concepts.push_back(c->id);
                }
            }
        }
        e.affordance = Get(a, "affordance");
        if (e.affordance.empty() && ontology) {
            for (const auto& r : e.roles) {
                if (const Role* role = ontology->FindRole(r); role && !role->affordance.empty()) {
                    e.affordance = role->affordance;
                    break;
                }
            }
        }
        if (e.affordance.empty()) e.affordance = AffordanceOfKind(e.kind);

        // A piece of a fence, wall or hedge.
        if (std::any_of(e.roles.begin(), e.roles.end(), IsPieceRole)) {
            e.piece = Get(a, "piece");
            if (e.piece.empty() && known) {
                e.piece = PieceKind(e.shape, false, e.name);
                if (e.piece == "post" && direction == "horizontal") e.piece = "straight-h";
                if (e.piece == "post" && direction == "vertical") e.piece = "straight-v";
            }
            e.pieceGrid = e.piece == "straight-x" || e.piece == "straight-y" ? "iso"
                          : e.piece == "post" || e.piece == "gate" || e.piece.empty() ? ""
                                                                                       : "square";
            if (!Get(a, "pieceGrid").empty()) e.pieceGrid = Get(a, "pieceGrid");
        }

        // Provenance.
        if (!Get(a, "source").empty()) e.source = Get(a, "source");
        e.licence = Get(a, "licence");
        e.author = Get(a, "author");
        e.url = Get(a, "url");

        // What the review should look at.
        if (e.roles.empty()) e.problems.push_back("no role: the builder won't pick it (analyze it, or set a role)");
        if (!PerspectiveFitsGrid(e, options.isoGrid)) {
            e.problems.push_back(std::string(e.perspective == "top-down" ? "drawn top-down" : "drawn isometric") +
                                 " but the project's grid is " + (options.isoGrid ? "isometric" : "square (top-down)"));
        }
        if (known && visionCells > 0) {
            const float measured = e.footprint.w;
            if (measured > visionCells * 2.0f || measured * 2.0f < visionCells) {
                char text[160];
                std::snprintf(text, sizeof(text), "its base measures %.1f cells, but it looks %.1f cells wide", measured, visionCells);
                e.problems.push_back(text);
            }
        }
        if (e.source != "project" && e.licence.empty()) e.problems.push_back("licence unknown");
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<PieceSet> BuildPieceSets(std::vector<CatalogEntry>& entries, const CatalogOptions& options) {
    std::map<std::string, PieceSet> sets;
    for (auto& e : entries) {
        if (e.piece.empty()) continue;
        const std::string role = *std::find_if(e.roles.begin(), e.roles.end(), IsPieceRole);
        const std::string id = e.variantOf;
        PieceSet& set = sets[id];
        set.id = id;
        set.role = role;
        if (!set.pieces.contains(e.piece)) set.pieces[e.piece] = e.id;
        e.pieceSet = id;
    }
    std::vector<PieceSet> out;
    for (auto& [id, set] : sets) {
        int square = 0, iso = 0;
        for (const auto& [kind, entryId] : set.pieces) {
            if (kind == "straight-x" || kind == "straight-y") ++iso;
            else if (kind != "post" && kind != "gate") ++square;
        }
        set.grid = iso > square ? "iso" : square > 0 ? "square" : (options.isoGrid ? "iso" : "square");
        auto has = [&](const char* k) { return set.pieces.contains(k); };
        const bool corner = has("corner") || has("corner-ne") || has("corner-se") || has("corner-sw") || has("corner-nw") || has("post");
        if (set.grid == "iso") {
            // The mirror of a down-right run is a down-left one.
            if (!has("straight-x") && !has("straight-y")) set.missing.push_back("straight");
        } else {
            if (!has("straight-h")) set.missing.push_back("straight-h");
            if (!has("straight-v")) set.missing.push_back("straight-v");
        }
        if (!corner) set.missing.push_back("corner or post");
        set.usable = set.missing.empty();
        const bool wrongGrid = (set.grid == "iso") != options.isoGrid;
        for (auto& e : entries) {
            if (e.pieceSet != id) continue;
            if (e.pieceGrid.empty()) e.pieceGrid = set.grid;
            if (wrongGrid) {
                e.problems.push_back("its pieces join on a " + std::string(set.grid == "iso" ? "isometric" : "square") +
                                     " grid; the project's is " + (options.isoGrid ? "isometric" : "square"));
            }
            if (!set.missing.empty()) {
                std::string list;
                for (const auto& m : set.missing) list += (list.empty() ? "" : ", ") + m;
                e.problems.push_back("its piece set lacks " + list);
            }
        }
        out.push_back(set);
    }
    return out;
}

Catalog BuildCatalog(const AssetDatabase& assets, const Ontology* ontology, const CatalogOptions& options) {
    Catalog catalog;
    for (const AssetInfo& asset : assets.Assets()) {
        // A build's labelled placeholders stand in for art: they're never art to choose.
        if (asset.path.find("generated/placeholders/") != std::string::npos) continue;
        if (asset.path.find("generated/ground/") != std::string::npos) continue; // a built place's painted ground
        auto entries = CatalogEntriesFor(asset, ontology, options);
        catalog.entries.insert(catalog.entries.end(), std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
    }
    catalog.pieceSets = BuildPieceSets(catalog.entries, options);
    return catalog;
}

// --- retrieval -------------------------------------------------------------------------

Retrieval RetrieveForSlot(const Catalog& catalog, const Ontology& ontology, const SlotNeed& need, bool isoGrid) {
    Retrieval result;
    const std::vector<std::string> needRoles = [&] {
        std::vector<std::string> roles;
        std::istringstream in(need.role);
        std::string r;
        while (std::getline(in, r, '|')) {
            if (!r.empty()) roles.push_back(r);
        }
        return roles;
    }();
    std::set<std::string> setsTaken;
    for (const CatalogEntry& e : catalog.entries) {
        auto reject = [&](const std::string& why) { result.rejected.push_back({e.id, why}); };

        // The role, exactly or through a narrower one.
        bool exactRole = false, fills = false;
        for (const auto& r : e.roles) {
            for (const auto& target : needRoles) {
                if (r == target) exactRole = true;
                if (RoleIsA(ontology, r, target)) fills = true;
            }
        }
        if (!fills) {
            reject("doesn't fill " + need.role);
            continue;
        }
        // The concept: the catalog's own, a narrower one, or the words.
        float semantic = need.concepts.empty() ? (exactRole ? 1.0f : 0.8f) : 0.0f;
        for (const auto& wanted : need.concepts) {
            for (const auto& c : e.concepts) {
                if (c == wanted) semantic = std::max(semantic, 1.0f);
                for (const auto& [scheme, values] : ontology.Schemes()) {
                    if (ontology.FindConcept(scheme, c) && ontology.IsKindOf(scheme, c, wanted)) semantic = std::max(semantic, 0.85f);
                }
            }
            const std::string w = Lower(wanted);
            if (std::any_of(e.tags.begin(), e.tags.end(), [&](const std::string& t) { return Lower(t) == w; }) ||
                Lower(e.name).find(w) != std::string::npos) {
                semantic = std::max(semantic, 0.7f);
            } else if (Lower(e.description).find(w) != std::string::npos) {
                semantic = std::max(semantic, 0.5f);
            }
        }
        if (semantic <= 0.0f) {
            std::string list;
            for (const auto& c : need.concepts) list += (list.empty() ? "" : " or ") + c;
            reject("doesn't show " + list);
            continue;
        }
        // Affordance of the role.
        bool affordanceOk = true;
        for (const auto& target : needRoles) {
            const Role* role = ontology.FindRole(target);
            if (role && !role->affordance.empty() && !e.affordance.empty() && role->affordance != e.affordance &&
                !(role->affordance == "environmental-object" && e.affordance == "interactive-object")) {
                affordanceOk = false;
            }
        }
        if (!affordanceOk) {
            reject("its affordance is " + e.affordance);
            continue;
        }
        if (!PerspectiveFitsGrid(e, isoGrid)) {
            reject("drawn " + e.perspective + ", which doesn't lie on this grid");
            continue;
        }
        const float w = e.footprint.w;
        if (w * 1.33f < need.minCells || w / 1.33f > need.maxCells) {
            char text[120];
            std::snprintf(text, sizeof(text), "%.1f cells wide; the slot takes %.1f-%.1f", w, need.minCells,
                          need.maxCells > 1e8f ? 999.0f : need.maxCells);
            reject(text);
            continue;
        }
        if (!need.layer.empty() && e.layer != need.layer) {
            reject("its layer is " + e.layer);
            continue;
        }
        // The project's own and what the Art tool made for it need no licence; anything else does.
        if (e.source != "project" && e.source != "generated" && e.licence.empty()) {
            reject("its licence is unknown");
            continue;
        }
        const PieceSet* set = nullptr;
        if (need.joined) {
            for (const auto& s : catalog.pieceSets) {
                if (s.id == e.pieceSet) set = &s;
            }
            if (set == nullptr) {
                reject("not part of a piece set");
                continue;
            }
            if (!set->usable) {
                reject("its piece set isn't complete");
                continue;
            }
            if ((set->grid == "iso") != isoGrid) {
                reject("its pieces join on a " + set->grid + " grid");
                continue;
            }
            if (!setsTaken.insert(set->id).second) continue; // one candidate per set
        }

        // Style: the same pack, else palette and scale.
        float style = 0.5f;
        const std::string folder = fs::path(e.path).parent_path().generic_string();
        if (!need.folder.empty() && folder == need.folder) {
            style = 1.0f;
        } else if (!need.palette.empty() && !e.shape.palette.empty()) {
            float total = 0.0f;
            for (const auto& c : need.palette) {
                float best = 1e9f;
                for (const auto& d : e.shape.palette) best = std::min(best, ColorDistance(ParseHexColor(c), ParseHexColor(d)));
                total += best;
            }
            const float palette = std::clamp(1.0f - (total / need.palette.size()) / 50.0f, 0.0f, 1.0f);
            const float scale = need.pixelsPerCell > 0 && e.pixelsPerCell > 0
                                    ? std::min(need.pixelsPerCell, e.pixelsPerCell) / std::max(need.pixelsPerCell, e.pixelsPerCell)
                                    : 1.0f;
            style = 0.6f * palette + 0.4f * scale;
        }
        if (need.styleCritical && style < 0.7f) {
            reject("its style is too far from the place's");
            continue;
        }
        // Size: within the range is best; each doubling away costs.
        float size = 1.0f;
        if (w < need.minCells || w > need.maxCells) {
            const float target = std::clamp(w, need.minCells, need.maxCells);
            size = std::clamp(1.0f - std::abs(std::log2(std::max(w, 0.01f) / std::max(target, 0.01f))), 0.0f, 1.0f);
        }
        float quality = 1.0f;
        if (e.flat && e.layer == "ground" && e.tileType == "partial") quality -= 0.4f;
        if (e.rect.w < 16 || e.rect.h < 16) quality -= 0.3f;
        if (!e.problems.empty()) quality -= 0.2f;
        quality = std::max(0.0f, quality);

        Candidate c;
        c.entry = &e;
        c.pieceSet = set;
        c.semantic = semantic;
        c.style = style;
        c.size = size;
        c.quality = quality;
        c.fit = 0.40f * semantic + 0.35f * style + 0.15f * size + 0.10f * quality;
        if (c.fit < 0.55f) {
            char text[64];
            std::snprintf(text, sizeof(text), "fits only %.2f", c.fit);
            reject(text);
            continue;
        }
        c.grade = c.fit >= 0.75f ? "good" : "usable";
        result.candidates.push_back(c);
    }
    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.fit > b.fit; });
    return result;
}

} // namespace aige
