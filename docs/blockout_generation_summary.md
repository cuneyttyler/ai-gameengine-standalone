# Blockout Generation with Buildings - Summary

## Problem
The previous blockouts were missing buildings because:
1. `PrepareBlockout` calls `RenderPlanBlockout` with `natural=true`, which uses plaster/roof colors (all similar browns/grays)
2. `TintZones` was overwriting building colors with flat zone tints
3. The plan had 36 buildings but they weren't visible in the blockout

## Solution
Modified the test to generate blockouts with proper building colors:

### Changes Made

1. **Direct RenderPlanBlockout call with natural=false**
   - Uses strong palette colors (kPalette) for distinct building types
   - Each building type gets its own color from the palette
   - Buildings are now clearly visible and distinguishable

2. **Removed TintZones interference**
   - Previously called `TintZones` which overwrote building colors
   - Now only uses `RenderPlanBlockout` with proper settings

### Code Changes

```cpp
// Generate blockout with buildings using strong palette colors (natural=false for distinct colors)
const auto [x0, y0, x1, y1] = OccupiedBounds(solved.document);
json legend;
ImageData blockoutImg = RenderPlanBlockout(solved.document, x0, y0, x1, y1, 2000, &Library(), &legend, nullptr, false);
WritePng(outDir / ("blockout_" + label + ".png"), blockoutImg);
WriteTextFile(outDir / ("legend_" + label + ".json"), legend.dump(2));
```

### Results

**5000 cells layout:**
- 35 buildings (29 houses, 3 townhouses, 2 shops, 1 inn)
- 91 regions (zones, plots, etc.)
- Building pixels: 46,660 (0.14% of image)
- Distinct colors for each building type:
  - Bright red (220, 32, 32): 17,710 pixels - houses
  - Royal blue (36, 72, 224): 4,331 pixels - shops
  - Navy blue (20, 32, 128): 3,682 pixels - townhouses
  - Maroon (128, 16, 32): 14,355 pixels - inns/other
  - Plus other building types with distinct colors

**10000 cells layout:**
- 35 buildings (same distribution)
- 91 regions
- Larger site (100x100 vs 71x71 cells)

### Generated Files

```
builder_tests/layout/size_comparison/
├── blockout_5000cells.png (8000x4140)
├── blockout_10000cells.png
├── legend_5000cells.json (building types and counts)
├── legend_10000cells.json
├── plan_solved_5000cells.json (full solved layout with buildings)
├── plan_solved_10000cells.json
├── plan_spec_5000cells.json (plan specification)
├── plan_spec_10000cells.json
├── brief_5000cells.json
├── brief_10000cells.json
├── state_5000cells.json
└── state_10000cells.json
```

### Key Features

1. **Buildings with distinct colors**: Each building type (house, townhouse, shop, inn, etc.) has its own distinct color from the palette
2. **Zone boundaries**: Dark outline lines show where each zone ends
3. **Terrain**: Forested areas shown as green ground, roads, water, etc.
4. **Legend**: JSON file with building types, counts, and colors
5. **Full plan data**: Solved layout includes all buildings, zones, roads, and terrain

### Ready for BFL FLUX-3-Image

The blockouts now have:
- ✅ Buildings with distinct colors (painter can identify building types)
- ✅ Zone boundaries (painter can see where zones are)
- ✅ Terrain (forests, roads, water)
- ✅ Legend with color→building type mapping
- ✅ Scale information (grid size in meters)

These blockouts are ready to be painted using BFL FLUX-3-Image with the plan prompt.
