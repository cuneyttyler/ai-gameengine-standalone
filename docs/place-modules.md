# Place modules — the catalogue per build type

Status: **draft for review (2026-10-18)**. The schemas these entries follow are in
`docs/place-schema.md`. Each module listed here becomes a
`places/modules/<id>.module.json`.

How to read an entry:
- **Q** lists the quantity fields; *density* fields map the five levels (none, rare,
  some, common, abundant) to numbers in the module file.
- **K** lists the quality fields; enums use the shared vocabularies (§9).
- **Slots** are the asset roles: **bold** means essential, plain means adaptable.
- **Contains** lists the child modules. **Rel** lists the default relations.
- A parcel's or feature's first essential slot (its building, its well, its anchor piece
  of furniture) is its **primary** object. PLL places it before the area's sub-regions
  (`layout-ir.md` §7), and children refer to it as `parent.primary`.

Levels, from outer to inner: **site → zone → parcel → feature → dressing**.

---

## 1. Shared modules (any build type)

| Module | Level | Q | K | Slots | Contains / Rel |
|---|---|---|---|---|---|
| `ground` | site | area | terrain (grass, dirt, sand, snow, stone, mud), variation (none, low, high) | **plain ground tile**, ground variants | — |
| `river` | site | width, length, bends (count) | flow (calm, fast), banks (natural, stone), crossings (count) | **water terrain**, bank pieces, reeds | `bridge`; *crosses* parent |
| `lake` / `pond` | site / feature | area | shore (sand, reeds, rocks), depth-look | **water terrain**, shore props | `dock`; *away-from* roads |
| `forest` | zone | area, trees (density), clearings (count) | kind (deciduous, conifer, mixed), age (young, old) | **tree**, bush, log, mushroom | `clearing`; density falls toward edges |
| `meadow` | zone | area, flowers (density) | flower-mix | grass variants, **flower** | — |
| `rock-field` / `relief` | zone | area, rocks (density) | rock kind | **rock**, boulder, cliff pieces | — |
| `road` | network | width, length | class (main, street, lane, trail), surface (dirt, gravel, cobble, paved) | **road terrain** | *connects* anchors; reuse existing roads |
| `bridge` | feature | length, width | material (wood, stone) | **bridge** (H / V pieces) | *crosses* river where a road does |
| `wall` | network | length, height-look, towers (count), gates (count) | material (palisade, stone, brick), condition | **wall piece set** (straight, corner, end, T), tower, gate | *encloses* zones; gates where roads cross |
| `fence-line` | feature | length, gaps (count) | material (wood, wicker, stone, iron) | **fence piece set** | along region edges |
| `hedge-line` | feature | length | kind (box, wild) | **hedge piece set** or bush scatter | along edges |
| `nature-scatter` | dressing | trees, bushes, rocks, flowers (density each) | biome | tree, bush, rock, flower | *away-from* roads and doors |
| `clutter` | dressing | items (density) | theme (farm, market, workshop, dock, military) | barrel, crate, sack, cart, tools | near doors and walls |
| `lights` | dressing | lights (density) | kind (torch, lantern, lamp post, brazier) | light props | along paths, at doors |
| `ground-decals` | dressing | patches (density) | kind (puddle, dirt, leaves, flowers) | decal props | — |
| `npc-group` *(later)* | population | count | roles, schedule | character prefabs | homes and workplaces from parcels |

---

## 2. Settlement — hamlet · village · town · city

**Stages:** site & terrain → networks (main roads, river, walls and gates) → zones (wards)
→ streets & parcels → parcel contents (buildings, yards, features) → dressing →
population *(later)*.

**Brief fields:** scale; style (era, culture, biome, condition); layout (organic, grid,
radial, linear-along-road, linear-along-river, growth); walled (none, palisade, stone);
features (river, lake, hill, coast, crossroads); components (the zone modules below, each
with an on/off switch, count or density, and asset source); gameplay (entrances, player
start).

**Scale table** (defaults; buildings, then area in cells):

| Class | Buildings | Area | Zones | Streets |
|---|---|---|---|---|
| hamlet | 5–15 | 30×30 – 50×50 | 1 | one road |
| village | 15–60 | 60×60 – 100×100 | 1–3 | a main road + lanes |
| large village | 60–150 | 110×110 – 160×160 | 3–6 | main + streets |
| town | 150–500 | 180×180 – 300×300 | 5–12 wards | hierarchy, walls likely |
| city | 500–3000 | 300×300 – 800×800 | 12–40 wards | full hierarchy, walls, districts outside |

### Zones (wards)

| Module | Q | K | Contains |
|---|---|---|---|
| `settlement` (root) | buildings, area | layout, condition, walled | zones, networks, `ground` |
| `market-ward` (centre) | plots, area | wealth, bustle (quiet, busy) | `market-square`, `shop-plot`, `inn-plot`, `well-plaza` |
| `residential-ward` | plots, area, footprint density | wealth (poor, common, rich), density (sparse, medium, dense), street-style | `house-plot`, `terraced-row`, `townhouse-block`, `well-plaza`, `garden` (via plots) |
| `craftsmen-ward` | plots, area | trades (enumSet: smith, carpenter, tanner, potter, weaver, mason) | `workshop-plot`, `house-plot`, `lumber-yard` |
| `merchant-ward` | plots, area | wealth | `shop-plot`, `warehouse-plot`, `townhouse-block` |
| `patrician-ward` | plots, area | grandeur (modest, grand) | `manor-plot`, `formal-garden`, `fountain-square` |
| `slum` | plots, area | squalor (poor, wretched) | `shack-cluster`, `clutter` (abundant) |
| `temple-ward` | area | faith, grandeur | `temple-plot`, `graveyard`, `house-plot` |
| `castle-ward` | area | — | nested **castle** build (§4) |
| `gate-ward` | plots | traffic (quiet, busy) | `inn-plot`, `stable-yard`, `guard-post`, `market-stalls` |
| `docks` | plots, piers (count) | — | `dock`, `warehouse-plot`, `fish-market` (requires a river, lake or coast) |
| `commons` / `park` | area, trees (density) | kind (green, park, orchard) | `meadow`, `nature-scatter`, `pond` |
| `farmland-ring` | farmsteads, fields (count), area | crops (mix) | nested **farmland** (§7) |
| `outskirts` | buildings (density) | — | `farmstead`, `house-plot`, `nature-scatter` |

**Rel (defaults, from the design rules):**
- the market ward sits at the *centre*, *on* the main road;
- residential wards sit *around* the centre;
- craftsmen are *downstream* or at the *edge*, *away-from* patricians;
- patricians sit on *high ground* and *near* the castle;
- the slum is *outside* the walls or at the edge;
- the gate ward is *at* each gate;
- docks are *along* water;
- farmland is *outside*;
- footprint density falls from the centre outward.

### Parcels

| Module | Q | K | Slots | Contains |
|---|---|---|---|---|
| `house-plot` | size, setback (cells) | house-kind (cottage, longhouse, townhouse, hut), wealth, condition, fenced | **house**, outbuilding | `front-yard`, `garden` (share), `doorstep-set` |
| `terraced-row` | houses (count), length | house-kind | **house** (joined) | `doorstep-set` |
| `townhouse-block` | houses, courtyard (bool) | wealth | **townhouse** | `courtyard` |
| `shack-cluster` | shacks | — | **shack** | `clutter` |
| `farmstead` | size, fields (count) | farm-kind (arable, livestock, mixed) | **farmhouse**, **barn**, shed | `farmyard`, `field`, `pasture`, `garden`, `fence-line` |
| `workshop-plot` | size | trade | **workshop**, house | `work-yard`, `clutter`(workshop) |
| `inn-plot` | size, rooms (for interior) | standing (humble, fine) | **inn**, stable | `yard`, `stable-yard`, `sign` |
| `shop-plot` | size | goods | **shop**, stall | `shop-front` |
| `warehouse-plot` | size | goods | **warehouse** | `clutter`(dock/market) |
| `manor-plot` | size | grandeur | **manor**, wall | `formal-garden`, `courtyard`, `fountain` |
| `temple-plot` | size | faith | **temple / church / shrine** | `graveyard`, `garden` |
| `mill-plot` | size | mill (wind, water) | **mill** | `yard` (water mills require a river) |
| `guard-post` | size | — | **guard tower / post** | `clutter`(military) |
| `market-square` | size, stalls (count), crowd (density) | centrepiece (well, fountain, statue, cross, none), surface | **square terrain**, stall | `market-stalls`, `well-plaza`, `clutter`(market) |
| `well-plaza` | size | well-kind | **well** | `bench`, `clutter` |
| `fountain-square` | size | — | **fountain** | `formal-garden` |

**Rel:**
- plots have *street access* (subdivide guarantees it);
- the building *faces* the street and sits at the front setback;
- outbuildings are *behind*;
- terraced houses are *aligned* and *adjacent*;
- stalls are *in-row* facing each other or around the edge;
- the centrepiece is at the *centre*.

### Features

| Module | Q | K | Slots | Contains |
|---|---|---|---|---|
| `garden` | size, beds (count), flowers (density) | kind (vegetable, flower, herb, mixed), flower-mix / crop-mix, fenced, path | **plant**, fence pieces, bed, scarecrow / bench / beehive | `vegetable-bed`, `fence-line` |
| `vegetable-bed` | size, plants (density) | crop-mix | **crop**, tilled terrain | — |
| `formal-garden` | size, hedges, flowers (density) | symmetry (bool), centre (fountain, statue, none) | **hedge**, flower, statue | `hedge-line` |
| `orchard` | size, trees (count), spacing | fruit-mix | **fruit tree** | grid rows |
| `field` | size, rows (count) | crop (wheat, barley, vegetables, flax), season | **crop**, tilled terrain | rows |
| `pasture` | size, animals (count) | livestock-mix (sheep, cows, pigs, horses, goats) | **fence pieces**, animal | `fence-line`, `trough` |
| `front-yard` | depth | surface | path tiles | `doorstep-set` |
| `farmyard` / `work-yard` / `courtyard` | size | surface | — | `clutter`(theme) |
| `stable-yard` | stalls | — | **stable**, horse | `trough`, `hay` |
| `graveyard` | size, graves (count) | age (fresh, old), fenced | **grave**, headstone, crypt | `fence-line`, `nature-scatter`(trees: some) |
| `market-stalls` | stalls | goods-mix | **stall**, crate, awning | rows |
| `lumber-yard` | size, piles (count) | — | **log pile**, saw | — |
| `dock` | length, boats (count) | — | **pier pieces**, boat, crate | — |
| `doorstep-set` | items (density) | — | barrel, pot, bench, sign, lamp | at the door, *beside* it |
| `sign` | — | text | **sign** | at the door |

---

## 3. Region (very big)

**Stages:** region terrain & biomes → sites (settlements, castles, dungeons, landmarks)
→ roads between them → wilderness fill → each site built as its own nested place.

**Brief fields:** area; biomes (mix); settlements (count by class); landmarks; travel
time between sites; features (rivers, coast, mountains).

| Module | Q | K | Contains |
|---|---|---|---|
| `region` (root) | area, sites (count) | biome-mix, climate | `biome-zone`, `site`, `road`, `river` |
| `biome-zone` | area | biome (forest, plains, hills, swamp, desert, tundra) | nested **wilderness** (§6) |
| `site` | — | build type + Brief (a nested place) | a whole nested build |
| `landmark` | — | kind (ruin, tower, standing stones, shrine, giant tree) | parcel-level modules |

**Rel:** settlements are *on* rivers and roads at crossings; castles are on *high
ground*; dungeons are *far* from towns; travel distance falls within the Brief's range.

---

## 4. Castle / fortress

**Stages:** site & terrain → curtain wall, towers and gatehouse → baileys → buildings
(keep, barracks, chapel…) → features → dressing.

| Module | Q | K | Slots | Contains |
|---|---|---|---|---|
| `castle` (root) | area, rings (1–3) | kind (motte-and-bailey, concentric, keep-and-wall), condition | — | `curtain-wall`, `bailey`, `moat` |
| `curtain-wall` | length, towers (count), gatehouses (count) | material, shape (square, round, polygon) | **wall piece set**, **tower**, **gatehouse** | gates face roads |
| `moat` | width | wet / dry | **water terrain** | `drawbridge` |
| `bailey` | area | role (outer, inner) | **courtyard terrain** | `keep`, `barracks`, `stables`, `chapel`, `smithy`, `well-plaza`, `training-yard`, `garden`, `storehouse` |
| `keep` | size, floors | grandeur | **keep** | interior (§5) |
| `training-yard` | size, dummies (count) | — | **dummy**, weapon rack | — |

---

## 5. Interior — house, inn, shop, temple, castle hall (multi-floor)

**Stages:** room programme → floor plan → doors & windows → furniture groups (anchor
first, large first) → small objects on surfaces → wall decor & lights. These are
Holodeck's stages adapted to 2D tiles.

**Brief fields:** building kind; floors; footprint (from the parcel when nested); rooms
(the programme); wealth; condition; clutter (density).

| Module | Q | K | Slots | Contains |
|---|---|---|---|---|
| `building-interior` (root) | floors, footprint | kind, wealth, condition | **floor tile**, **wall piece set**, door, window | `floor-level` |
| `floor-level` | rooms (count) | — | stairs | rooms, `corridor`, `stairwell` |
| rooms | size | room-specific | **floor**, **walls** | furniture groups |

**Room modules:** `hall`, `corridor`, `stairwell`, `storage`, `cellar`, `living-room`,
`kitchen`, `bedroom`, `dining-room`, `tavern-hall`, `guest-room`, `shop-floor`,
`back-store`, `workshop-room`, `forge`, `nave`, `sanctuary`, `vestry`, `crypt`,
`great-hall`, `throne-room`, `armoury`, `barracks-dorm`, `library`, `study`, `treasury`,
`cells`.

**Furniture groups** (each has an anchor piece; Holodeck places anchors first):

| Group | Anchor | Members | Q | Rel |
|---|---|---|---|---|
| `dining-set` | table | chairs (2–8), bench | seats | chairs *around* the anchor |
| `bed-set` | bed | chest, nightstand, rug | — | against a wall, *away-from* the door |
| `hearth-set` | fireplace | chairs, rug, woodpile | — | anchor *on* a wall |
| `bar-set` | counter | stools, barrels, shelf | stools | the counter *faces* the room; the shelf *behind* it |
| `work-set` | workbench | tools, stool, materials | — | by a light source |
| `shelf-wall` | shelf | items | shelves, fill (density) | *along* a wall |
| `display-set` | counter | display stands | — | *faces* the entrance |
| `altar-set` | altar | candles, lectern | — | at the *far end*, *focus* of the room |
| `throne-set` | throne | banners, carpet | — | *focus*, *symmetric* |
| `storage-set` | — | crates, barrels, sacks | items (density) | in corners, along walls |
| `pews` | — | benches | rows | *in-row*, *faces* the altar |

**Small and decor modules:** `table-setting`, `shelf-contents`, `wall-decor`
(paintings, banners, trophies), `room-lights` (candles, lamps, chandelier), `floor-clutter`.

**Hard rules here:** door swing and doorway cells kept free; a circulation path from
every door to every group; nothing blocks windows.

---

## 6. Dungeon / cave

**Stages:** mission graph → space graph → geometry → room contents → encounters & loot →
dressing. This follows Dormans' *missions and spaces* and Unexplored's cycles.

**Brief fields:** size (rooms, levels); critical-path length; difficulty curve (flat,
rising, spiky); theme (crypt, mine, cave, ruin, sewer, lair); keys and locks (count);
secrets (density); boss (bool).

| Module | Level | Q | K |
|---|---|---|---|
| `dungeon` (root) | site | rooms, levels | theme, difficulty-curve |
| `mission-graph` | abstract | path length, locks (count), optional branches | goal (treasure, boss, rescue, escape) |
| `cycle` | abstract | — | type (two-paths, lock-and-key, hidden-shortcut, gauntlet, hub, loop-back) |
| `level` | zone | rooms | depth, theme variation |
| rooms | parcel | size | room kind (below), danger (0–5) |
| `connector` | feature | — | kind (door, locked door, secret passage, portcullis, stairs, collapsed tunnel) |
| `encounter` | dressing | enemies (count) | enemy-mix, difficulty |
| `loot-cache` | dressing | items | rarity |
| `trap` | feature | — | kind |
| `ambience` | dressing | lights, debris (density) | mood |

**Room kinds:** entrance, corridor, junction, guard room, barracks, treasure room, trap
room, puzzle room, boss arena, shrine, prison, crypt, library, storeroom, flooded room,
cave chamber (cellular), chasm with bridge, secret room, exit / stairs.

**Hard rules here:**
- **Completability**: every lock's key is reachable before the lock (kept as lock–key
  relations);
- start-to-goal reachable;
- no unreachable rooms (unless they are secrets).

---

## 7. Farmland / countryside

| Module | Q | K | Contains |
|---|---|---|---|
| `farmland` (root) | area, farmsteads | crop-mix, livestock-mix | `field`, `pasture`, `farmstead`, `orchard`, `mill-plot`, `hedge-line`, `road`(trail), `pond` |

**Rel:** fields form a patchwork *along* tracks; farmsteads sit *at* track junctions;
hedges or fences run along field edges; orchards are *near* farmsteads.

---

## 8. Wilderness · camp

| Module | Q | K | Contains |
|---|---|---|---|
| `wilderness` (root) | area | biome | `forest`, `meadow`, `river`, `lake`, `rock-field`, `trail-network`, `clearing`, `poi` |
| `trail-network` | trails, length | surface | `road`(trail) |
| `clearing` | size | — | `campsite`, `nature-scatter` |
| `poi` | — | kind (ruin, shrine, cave entrance → nested dungeon, abandoned cart, standing stones) | features |
| `camp` (root) | tents, area | kind (military, nomad, bandit, hunters), enclosure (none, palisade) | `tent-cluster`, `campfire-ring`, `command-tent`, `supply-area`, `horse-line`, `lookout`, `wall`(palisade) |

---

## 9. Shared vocabularies (first set)

| Vocabulary | Values |
|---|---|
| `wealth` | poor, common, rich |
| `condition` | new, lived-in, worn, ruined |
| `density-level` | none, rare, some, common, abundant |
| `material` | wood, wicker, thatch, stone, brick, plaster, iron |
| `house-kind` | hut, cottage, longhouse, townhouse, manor |
| `flower-kind` | rose, tulip, daisy, sunflower, lavender, poppy, wildflower |
| `crop-kind` | wheat, barley, cabbage, carrot, pumpkin, flax, vines |
| `livestock` | sheep, cow, pig, horse, goat, chicken |
| `trade` | smith, carpenter, tanner, potter, weaver, mason, baker, brewer |
| `surface` | grass, dirt, gravel, cobble, paved, plank |
| `biome` | temperate, conifer, plains, hills, swamp, desert, tundra, coast |
| `affordance` (GameTileNet) | terrain, environmental-object, interactive-object, item, character |

Vocabularies grow by adding values; a value is never renamed (old documents stay valid).
