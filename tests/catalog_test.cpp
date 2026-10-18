#include "assets/ArtAnalysis.h"
#include "assets/Catalog.h"
#include "places/Ontology.h"

#include <gtest/gtest.h>

#include <cmath>

// development_plan.md 7.3: the asset catalog v2, on synthetic art.
using namespace aige;

namespace {

struct Canvas {
    ImageData image;
    Canvas(int w, int h) {
        image.width = static_cast<uint32_t>(w);
        image.height = static_cast<uint32_t>(h);
        image.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    }
    void Put(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) return;
        uint8_t* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = a;
    }
    void Fill(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) Put(x, y, r, g, b, a);
        }
    }
};

// A ¾ house: walls 80 px wide standing on row 110, a roof above, a soft shadow below.
Canvas House() {
    Canvas c(128, 128);
    c.Fill(16, 108, 112, 120, 0, 0, 0, 90);    // shadow (drawn first, under the walls)
    c.Fill(24, 60, 104, 110, 200, 170, 120);   // walls
    for (int y = 30; y < 60; ++y) {            // roof
        const int half = (y - 30) * 50 / 30;
        c.Fill(64 - half, y, 64 + half, y + 1, 150, 40, 30);
    }
    c.Fill(56, 86, 72, 110, 90, 60, 30);       // door
    return c;
}

// A tree: a wide crown on a narrow trunk.
Canvas Tree() {
    Canvas c(64, 96);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 64; ++x) {
            if ((x - 32) * (x - 32) + (y - 30) * (y - 30) < 30 * 30) c.Put(x, y, 40, 140, 50);
        }
    }
    c.Fill(28, 60, 36, 94, 100, 70, 40);
    return c;
}

// A fence piece on a 64x64 cell: rails at mid height from `from` to `to` sides.
Canvas Fence(bool n, bool e, bool s, bool w) {
    Canvas c(64, 64);
    c.Fill(28, 28, 36, 36, 120, 80, 40); // the post
    if (e) c.Fill(32, 28, 64, 34, 140, 95, 50);
    if (w) c.Fill(0, 28, 32, 34, 140, 95, 50);
    if (n) c.Fill(29, 0, 35, 32, 140, 95, 50);
    if (s) c.Fill(29, 32, 35, 64, 140, 95, 50);
    return c;
}

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

AssetInfo Asset(const std::string& path, const ImageData& image, std::map<std::string, std::string> analysis) {
    AssetInfo a;
    a.path = path;
    a.width = image.width;
    a.height = image.height;
    analysis[kShapeKey] = FormatShape(AnalyzeShape(image));
    a.analysis = std::move(analysis);
    return a;
}

std::vector<CatalogEntry> Entries(const std::vector<AssetInfo>& assets, const CatalogOptions& options) {
    std::vector<CatalogEntry> out;
    for (const auto& a : assets) {
        auto entries = CatalogEntriesFor(a, &Library(), options);
        out.insert(out.end(), entries.begin(), entries.end());
    }
    return out;
}

bool HasProblem(const CatalogEntry& e, const std::string& text) {
    for (const auto& p : e.problems) {
        if (p.find(text) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST(ArtAnalysisTest, HouseFootprintAnchorAndShadow) {
    const Canvas house = House();
    const ArtShape s = AnalyzeShape(house.image);
    EXPECT_EQ(s.base.x, 24);
    EXPECT_EQ(s.base.w, 80);           // the walls, not the shadow or the roof
    EXPECT_FLOAT_EQ(s.anchorX, 64.0f); // the middle of the base...
    EXPECT_FLOAT_EQ(s.anchorY, 110.0f);// ...at its lowest solid row, above the shadow
    EXPECT_TRUE(s.shadow);
    EXPECT_EQ(s.opaque.y + s.opaque.h, 110); // the shadow isn't part of it
    EXPECT_EQ(s.silhouette, "upright");
    EXPECT_EQ(s.tileType, "partial");
    EXPECT_EQ(PerspectiveCue(s), "upright");
    EXPECT_TRUE(s.symmetric);
    const Footprint f = FootprintOf(s, false, 32.0f); // a pack of 32 px cells
    EXPECT_FLOAT_EQ(f.w, 2.5f);
    EXPECT_EQ(f.cellsW, 3);
    EXPECT_EQ(f.cellsH, 1);
    EXPECT_FALSE(s.palette.empty());
}

TEST(ArtAnalysisTest, TreeStandsOnItsTrunk) {
    const ArtShape s = AnalyzeShape(Tree().image);
    EXPECT_EQ(s.base.w, 8);
    EXPECT_FLOAT_EQ(s.anchorX, 32.0f);
    EXPECT_FLOAT_EQ(s.anchorY, 94.0f);
    EXPECT_EQ(s.silhouette, "upright");
    EXPECT_EQ(FootprintOf(s, false, 32.0f).cellsW, 1);
    EXPECT_FALSE(s.shadow);
}

TEST(ArtAnalysisTest, TileTypesAndSilhouettes) {
    // A seamless texture: a pattern that repeats exactly across the tile.
    Canvas texture(32, 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float v = 0.5f + 0.25f * std::sin(x * 2 * 3.14159f / 32) + 0.25f * std::cos(y * 2 * 3.14159f / 32);
            texture.Put(x, y, static_cast<uint8_t>(60 * v), static_cast<uint8_t>(160 * v), static_cast<uint8_t>(60 * v));
        }
    }
    const ArtShape t = AnalyzeShape(texture.image);
    EXPECT_EQ(t.tileType, "texture");
    EXPECT_EQ(t.silhouette, "rect");
    EXPECT_EQ(PerspectiveCue(t), "top-down");

    // A whole tile that doesn't repeat: dark on the left, light on the right.
    Canvas gradient(32, 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) gradient.Put(x, y, static_cast<uint8_t>(x * 8), 100, 100);
    }
    EXPECT_EQ(AnalyzeShape(gradient.image).tileType, "complete");

    // An isometric ground tile: a 64x32 diamond.
    Canvas diamond(64, 32);
    for (int y = 0; y < 32; ++y) {
        const int half = y < 16 ? (y + 1) * 2 : (32 - y) * 2;
        diamond.Fill(32 - half, y, 32 + half, y + 1, 90, 160, 70);
    }
    const ArtShape d = AnalyzeShape(diamond.image);
    EXPECT_EQ(d.silhouette, "diamond");
    EXPECT_EQ(PerspectiveCue(d), "isometric");
    EXPECT_EQ(FootprintOf(d, true, 64.0f).cellsW, 1);
}

TEST(ArtAnalysisTest, SheetsAreCutIntoWholeObjects) {
    Canvas sheet(200, 120);
    sheet.Fill(10, 10, 50, 50, 200, 0, 0);   // first row: two things
    sheet.Fill(120, 5, 150, 55, 0, 200, 0);
    sheet.Fill(120, 57, 150, 60, 0, 200, 0); // ...the second's foot, 2 px below: the same thing
    sheet.Fill(20, 80, 90, 110, 0, 0, 200);  // second row
    sheet.Fill(180, 100, 182, 102, 255, 255, 255); // a speck
    const auto objects = SegmentObjects(sheet.image, 3, 24);
    ASSERT_EQ(objects.size(), 3u);
    EXPECT_EQ(objects[0], (ImageRect{10, 10, 40, 40}));
    EXPECT_EQ(objects[1], (ImageRect{120, 5, 30, 55}));
    EXPECT_EQ(objects[2], (ImageRect{20, 80, 70, 30}));

    // A tileset of 32 px tiles: a tree two tiles tall is one object.
    Canvas tiles(128, 64);
    tiles.Fill(4, 10, 28, 64, 40, 140, 50);  // tile (0,0) runs into (0,1)...
    tiles.Fill(10, 64, 22, 60 + 32, 100, 70, 40);
    tiles.Fill(40, 40, 60, 60, 200, 200, 0); // tile (1,1) alone
    tiles.Fill(64, 0, 128, 32, 90, 90, 90);  // tiles (2,0) and (3,0) joined across their border
    const auto parts = SegmentTiles(tiles.image, 32, 32);
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], (ImageRect{0, 0, 32, 64}));
    EXPECT_EQ(parts[1], (ImageRect{64, 0, 64, 32}));
    EXPECT_EQ(parts[2], (ImageRect{32, 32, 32, 32}));
}

TEST(ArtAnalysisTest, PieceKindsFromWhereTheyJoin) {
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(false, true, false, true).image), false), "straight-h");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(true, false, true, false).image), false), "straight-v");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(true, true, false, false).image), false), "corner-ne");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(false, true, true, true).image), false), "t-s");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(true, true, true, true).image), false), "cross");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(false, false, true, false).image), false), "end-s");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(false, false, false, false).image), false), "post");
    EXPECT_EQ(PieceKind(AnalyzeShape(Fence(false, true, false, true).image), false, "wood gate"), "gate");

    // An isometric run: down-right at 2:1.
    Canvas iso(64, 48);
    for (int x = 4; x < 60; ++x) iso.Fill(x, 8 + (x - 4) / 2, x + 1, 8 + (x - 4) / 2 + 4, 140, 95, 50);
    const ArtShape run = AnalyzeShape(iso.image);
    EXPECT_EQ(PieceKind(run, true), "straight-x");
    EXPECT_EQ(PieceKind(run, false), "straight-x"); // the same art, flagged later as made for an iso grid
}

TEST(CatalogTest, RolesConceptsAndForm) {
    CatalogOptions square;
    square.isoGrid = false;
    square.pixelsPerCell = [](const std::string&) { return 32.0f; };
    const auto entries = Entries(
        {Asset("assets/town/cottage.png", House().image,
               {{"kind", "building"}, {"tags", "house, cottage"}, {"cells", "2.5"}, {"blocks", "1"}, {"anchors", R"({"door":[0.5,0.86]})"}}),
         Asset("assets/town/oak.png", Tree().image, {{"kind", "nature"}, {"tags", "tree, oak"}, {"blocks", "1"}})},
        square);
    ASSERT_EQ(entries.size(), 2u);
    const CatalogEntry& cottage = entries[0];
    EXPECT_NE(std::find(cottage.roles.begin(), cottage.roles.end(), "house"), cottage.roles.end());
    EXPECT_NE(std::find(cottage.concepts.begin(), cottage.concepts.end(), "cottage"), cottage.concepts.end());
    EXPECT_EQ(cottage.affordance, "environmental-object");
    EXPECT_EQ(cottage.layer, "building");
    EXPECT_EQ(cottage.perspective, "three-quarter");
    EXPECT_FALSE(cottage.flat);
    EXPECT_EQ(cottage.footprint.cellsW, 3);
    EXPECT_NEAR(cottage.anchorV, 110.0f / 128.0f, 1e-4);
    EXPECT_FLOAT_EQ(cottage.spriteW, 4.0f); // 128 px at 32 px per cell
    EXPECT_EQ(cottage.facings, "one");      // symmetric: mirroring adds nothing
    EXPECT_TRUE(cottage.accessible);        // it has a door
    EXPECT_FALSE(cottage.passable);
    EXPECT_TRUE(cottage.problems.empty()) << cottage.problems.front();
    EXPECT_NE(std::find(entries[1].roles.begin(), entries[1].roles.end(), "tree"), entries[1].roles.end());
}

TEST(CatalogTest, PerspectiveMismatchesAreFlagged) {
    Canvas grass(32, 32);
    grass.Fill(0, 0, 32, 32, 70, 150, 60);
    const AssetInfo tile = Asset("assets/ground/grass.png", grass.image, {{"kind", "ground"}, {"tags", "grass"}});
    CatalogOptions iso;
    iso.isoGrid = true;
    const CatalogEntry onIso = CatalogEntriesFor(tile, &Library(), iso).front();
    EXPECT_TRUE(onIso.flat);
    EXPECT_EQ(onIso.perspective, "top-down");
    EXPECT_TRUE(HasProblem(onIso, "drawn top-down but the project's grid is isometric"));
    CatalogOptions square;
    square.isoGrid = false;
    EXPECT_TRUE(CatalogEntriesFor(tile, &Library(), square).front().problems.empty());
    // Upright art stands on either grid.
    EXPECT_TRUE(PerspectiveFitsGrid(CatalogEntriesFor(Asset("assets/t/oak.png", Tree().image, {{"kind", "nature"}, {"tags", "tree"}}), &Library(), iso).front(), true));
}

TEST(CatalogTest, PieceSetsAndTheirGrid) {
    auto piece = [](const std::string& name, const Canvas& c, const std::string& direction) {
        return Asset("assets/fences/" + name + ".png", c.image,
                     {{"kind", "decoration"}, {"tags", "fence, wood"}, {"name", name}, {"direction", direction}});
    };
    std::vector<AssetInfo> wood = {piece("wood-fence-horizontal", Fence(false, true, false, true), "horizontal"),
                                   piece("wood-fence-vertical", Fence(true, false, true, false), "vertical"),
                                   piece("wood-fence-corner", Fence(true, true, false, false), "none"),
                                   piece("stone-fence-horizontal", Fence(false, true, false, true), "horizontal")};
    CatalogOptions square;
    square.isoGrid = false;
    auto entries = Entries(wood, square);
    for (const auto& e : entries) EXPECT_FALSE(e.piece.empty()) << e.id;
    const auto sets = BuildPieceSets(entries, square);
    ASSERT_EQ(sets.size(), 2u);
    const PieceSet& stone = sets[0].id == "assets/fences/stone-fence" ? sets[0] : sets[1];
    const PieceSet& woodSet = sets[0].id == "assets/fences/wood-fence" ? sets[0] : sets[1];
    EXPECT_EQ(woodSet.role, "fence-piece-set");
    EXPECT_EQ(woodSet.grid, "square");
    EXPECT_TRUE(woodSet.usable) << (woodSet.missing.empty() ? "" : woodSet.missing.front());
    EXPECT_EQ(woodSet.pieces.at("corner-ne"), "assets/fences/wood-fence-corner.png");
    EXPECT_FALSE(stone.usable);
    EXPECT_TRUE(HasProblem(entries.back(), "lacks straight-v"));

    // The same pieces in an isometric project: drawn for the wrong grid.
    CatalogOptions iso;
    iso.isoGrid = true;
    auto isoEntries = Entries(wood, iso);
    BuildPieceSets(isoEntries, iso);
    EXPECT_TRUE(HasProblem(isoEntries.front(), "join on a square grid; the project's is isometric"));
}

TEST(CatalogTest, SlotRetrievalFiltersThenRanks) {
    CatalogOptions square;
    square.isoGrid = false;
    square.pixelsPerCell = [](const std::string&) { return 32.0f; };
    Canvas manor(320, 200);
    manor.Fill(10, 40, 310, 190, 220, 220, 220);
    Canvas rose(32, 32), tulip(32, 32);
    rose.Fill(8, 8, 24, 30, 220, 40, 60);
    tulip.Fill(8, 8, 24, 30, 240, 200, 40);
    std::vector<AssetInfo> assets = {
        Asset("assets/town/cottage.png", House().image, {{"kind", "building"}, {"tags", "house, cottage"}, {"roles", "house"}}),
        Asset("assets/town/manor.png", manor.image, {{"kind", "building"}, {"tags", "manor"}, {"roles", "house"}}),
        Asset("assets/garden/rose.png", rose.image, {{"kind", "nature"}, {"tags", "rose"}, {"roles", "flower"}}),
        Asset("assets/garden/tulip.png", tulip.image, {{"kind", "nature"}, {"tags", "tulip"}, {"roles", "flower"}}),
        Asset("assets/web/hut.png", House().image, {{"kind", "building"}, {"tags", "hut"}, {"roles", "house"}, {"source", "web"}}),
        Asset("assets/fences/wood-fence-horizontal.png", Fence(false, true, false, true).image,
              {{"kind", "decoration"}, {"tags", "fence"}, {"name", "wood-fence-horizontal"}, {"direction", "horizontal"}}),
        Asset("assets/fences/wood-fence-vertical.png", Fence(true, false, true, false).image,
              {{"kind", "decoration"}, {"tags", "fence"}, {"name", "wood-fence-vertical"}, {"direction", "vertical"}}),
        Asset("assets/fences/wood-fence-post.png", Fence(false, false, false, false).image,
              {{"kind", "decoration"}, {"tags", "fence"}, {"name", "wood-fence-post"}, {"roles", "fence-piece-set"}}),
    };
    Catalog catalog;
    catalog.entries = Entries(assets, square);
    catalog.pieceSets = BuildPieceSets(catalog.entries, square);

    SlotNeed house;
    house.role = "building"; // a house is a building (the role hierarchy)
    house.minCells = 2;
    house.maxCells = 4;
    house.folder = "assets/town";
    const Retrieval houses = RetrieveForSlot(catalog, Library(), house, false);
    ASSERT_EQ(houses.candidates.size(), 1u);
    EXPECT_EQ(houses.candidates[0].entry->id, "assets/town/cottage.png");
    EXPECT_EQ(houses.candidates[0].grade, "good");
    auto why = [&](const Retrieval& r, const std::string& id) {
        for (const auto& rej : r.rejected) {
            if (rej.id == id) return rej.reason;
        }
        return std::string();
    };
    EXPECT_NE(why(houses, "assets/town/manor.png").find("cells wide"), std::string::npos);
    EXPECT_NE(why(houses, "assets/web/hut.png").find("licence"), std::string::npos);
    EXPECT_NE(why(houses, "assets/garden/rose.png").find("doesn't fill"), std::string::npos);

    SlotNeed flower;
    flower.role = "flower";
    flower.concepts = {"rose"};
    const Retrieval roses = RetrieveForSlot(catalog, Library(), flower, false);
    ASSERT_EQ(roses.candidates.size(), 1u);
    EXPECT_EQ(roses.candidates[0].entry->id, "assets/garden/rose.png");
    EXPECT_NE(why(roses, "assets/garden/tulip.png").find("doesn't show rose"), std::string::npos);
    // Any flower: both, and a need for a broader concept takes the narrower one.
    flower.concepts = {"flower"};
    EXPECT_EQ(RetrieveForSlot(catalog, Library(), flower, false).candidates.size(), 2u);

    SlotNeed fence;
    fence.role = "fence-piece-set";
    fence.joined = true;
    const Retrieval fences = RetrieveForSlot(catalog, Library(), fence, false);
    ASSERT_EQ(fences.candidates.size(), 1u); // one candidate per set
    ASSERT_NE(fences.candidates[0].pieceSet, nullptr);
    EXPECT_EQ(fences.candidates[0].pieceSet->id, "assets/fences/wood-fence");
    // On an isometric grid the square-grid set doesn't join.
    EXPECT_TRUE(RetrieveForSlot(catalog, Library(), fence, true).candidates.empty());
}
