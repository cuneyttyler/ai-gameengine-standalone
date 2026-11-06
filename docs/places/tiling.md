# Tiling — open worlds made of sections, painted in tiles

Status: **design, v1 (2026-11-06)**. Phase 31 (`docs/project/development_plan.md`). Builds on the region build type
(`data/places/modules/region.module.json`), its sites (PlaceBuild `Sites`, "Build this site"), the Paint method
(`painted-places.md`) and the tiled-painting design for one big place (`big-scenes.md`).

---

## 1. What this is for

The Build panel builds **one place** of one kind: a village, a camp, a castle, a dungeon. An open-world RPG's map
is many of those: villages joined by long roads, forests, rivers, camps in the hills, a dungeon under a ruin. It
is far too big for one plan, one painting or one background.

So a **world** is made of **sections**. A section is exactly what the Build panel builds today: one place of one
kind, with its own zones, layout, plan, assets and painting. A world puts any number of them side by side in
**one scene**: there's no limit, a world of 100 sections is fine. The world is one level above the zones: a place
is made of zones, a world is made of sections.

Two things make it work:

1. **One plan for the whole world first.** It fixes where every section is, and where the things that cross
   sections run: roads, rivers, coasts, forests, lakes, mountains. Each section is then planned in detail inside
   that, keeping the world plan's things where they are, so roads and forests meet across the seams. This is how
   the Assembled background already works: the whole place painted first, then its parts in detail.
2. **Tiles, by cell count.** A painting or background is made of as many model images as its cells need, each
   painted at full size and blended into one. A small room is one tile; a dungeon level of 50 × 50 cells is 2 × 2;
   a world is its sections' tiles.

## 2. Terms

| Term | What |
|---|---|
| **World** | A scene built from many sections. Its build type is `world` (the ontology's, §3a). |
| **World plan** | The region's solved plan: the sections' bounds and kinds, the roads, rivers, lakes, coast, forests and relief, in world cells. |
| **Section** | One place of one build type, built by a normal place build inside a rectangle of the world. |
| **Site section** | A section for one of the world plan's sites: a village, a town, a camp, a castle, a dungeon's entrance… |
| **Wild section** | A section of the land between sites, built as `wilderness`: forests, fields, hills, the roads through. |
| **Carried** | What a section takes from the world plan and must keep: the cells of its roads, rivers and lakes, the edges of its forests, its relief, where its neighbours' roads come in. |
| **Margin** | A band of cells along a section's edges, shared with its neighbour, where both are the world plan's (§5). |
| **Tile** | One model image of a painting or background (§6). |

## 3. Choosing a world in the Build panel

Next to the method (**Assembled**, **Paint**) the panel asks what to build:

- **A single place**: as today; the build type comes from the request (village, camp, dungeon…).
- **An open world**: a world made of sections (this document).

Above the request: *"Select build method and type first."* Then a request such as *"A big island with several
villages connected through long-distance roads, forests, camps and other related things"* builds a world.

The panel follows the ontology: "An open world" is offered when a build type has `"scope": "world"`; the Place menu
lists the others; a world's size choices, its asked fields (landform, relief, woods, biomes) and its parts are its
world type's; a build whose type is a world has the ② World step, whatever started it.

## 3a. The ontology of a world

Everything a world is made of is defined in `data/places`, as for any place:

| What | Where | |
|---|---|---|
| The **world** build type | `buildtypes.json` `world` | `"scope": "world"`; its words (open world, island, archipelago, continent…), unit (sites), scale classes (island 250–400 cells a side, land 400–700, continent 700–1400), paint guide, and its **`sections`**: `from` (the modules each made a section: `site`), `wild` (the build type of the land between: `wilderness`), `wildSide` (64), `margin` (2), `carried` (what a section keeps of the world plan) |
| The **world** root | `modules/world.module.json` | sites (count), area; `landform` (inland, coastal, island, archipelago, peninsula), biome mix, climate, relief (flat → mountainous), woods; its children below |
| **Sea** | `modules/sea.module.json` | salt water along one side (prefers the edge); its shore: sand, rocks, cliffs |
| **Ocean** | `modules/ocean.module.json` | deep water off the land; its compile's `ring` puts it all round the land when the landform is an island or an archipelago (the land's ways in move inside the ring) |
| **Coast** | `modules/coast.module.json` | the strip between land and sea: sand, shingle, rocks or cliffs; near the place's water (its first: the sea) |
| **Mountains** | `modules/mountains.module.json` | high rock (`mountain` terrain: not walkable), peaks rocky, snowy or volcanic, boulders and scree |
| **Hills** | `modules/hills.module.json` | rolling high ground, walkable, rocks and scattered trees |
| **Marsh** | `modules/marsh.module.json` | wet low ground by the water: mud, reeds, pools |
| **Forest**, **river**, **lake**, **road**, **biome**, **heartland**, **site**, **landmark** | existing modules | a lake is now laid out as a zone of water at a place's top level (it was never filled there) |
| Classes | `classes.json` | `World`, `Section`; sea and ocean are `WaterBody` and `NatureArea` |
| Vocabulary | `vocabularies.json` `landform` | inland, coastal, island, archipelago, peninsula, with the words for each |
| Rules | `rules.json` `world-*` | an island has ocean round it; a coast lies by the sea; mountains block the way; every site is reached by a road |

A zone's compile `fill` may now be one terrain (`"water"`) as well as a field's mapping. A region can have the new
land and water too. Planned from a Brief alone (no AI: `tools/plan_blockout --brief`), a coastal world and an island
both come out whole (`builder_tests/world_test`).

## 4. The steps

| Step | Assembled | Paint |
|---|---|---|
| ① Confirm | the world's Brief: its size, its sites and their kinds, its climate, relief, woods, water, coast | same |
| ② **World** | the world plan, as an image with the sections drawn on it, and the list: each section's name, kind, size and what it carries. *Lay out the zones* lays the sections out one by one | same |
| ③ Layout | the section list above; below, the selected section's layout image on the left and its description on the right (the page as today) | same |
| ④ Plan | the section list above; below, the selected section's plan image and information | same |
| ⑤ | **Assets**: the backgrounds first, every section's, joined into one; then the assets, shared across sections | **Paint**: each section painted in its tiles, then joined |
| ⑥ | **Build**: every section into the one scene | **Mark**: the sections listed on the left, the selected section's mark page on the right |
| ⑦ | | **Assets**, **Animate**, **Build** as for one place |

Each section's own steps run as a normal place build: the Layout, Plan, Paint and Mark pages are today's pages,
showing the selected section. A section can be laid out, planned, painted or revised again on its own; only its
neighbours' margins are re-checked (§5).

## 5. Keeping sections connected

### The world plan

The `region` root is solved over the whole world, at full cell resolution (a region is 200–900 cells a side).
It makes the biomes, the relief, the coast and water, the sites with their footprints, the roads between them
(the network solver, bridges where they cross rivers), and the forests. Nothing is detailed yet: no buildings,
no props, no zones inside sites.

### Sections are the places' land, organic

The world plan gives each place (each site) a **land of its own**: a settled zone, one per place, spread apart over the
world with forests, hills, water and roads between (`heartland`, its compile's `edge`: an organic edge round a middle
that is claimed first and big enough for its site, which starts only where that square is clear of river and road; it
keeps inland, `prefersSite.edge: -1.5`, so the woods along the world's edges run on unbroken). Natural zones (classes
`NatureArea`, `WaterBody`) grow with a wandering edge (smooth noise on their growth), so forests, coasts and seas have
bays and tongues, not growth circles.

A **section is a place's land**, not a rectangle: it is planned as a place of its kind on its land's square, with the
cells that aren't its land given as `outside` (nothing planned on them) and its ways in on its land's edge
(`CompileOptions.outside`, `entrances`). Inside, what's built keeps together in the middle (`centre`), its own woods,
meadows and water go round it towards the land's edge (`edge`), each with its organic edge: no forest of a section meets
a square corner. A site's own size has room round it for that land (its compile's `padding`, 1.35).

Its square's cells outside its land get **the world's ground** after the solve: the land round the place, as the world
plan has it, for its block-out (Paint) and its background (Assembled).

### Cutting it into sections

Every world cell belongs to exactly one section (the first rule below is now the land of each place, above):

1. Each **site** becomes a site section: its footprint's bounding rectangle, grown to whole cells and by its
   margin.
2. The **rest** of the world is cut into **wild sections**: rectangles of at most `wildSide` cells a side (64 by
   default: about one painting tile at the default density, §6), along a grid, clipped against the site sections.
   A small leftover joins its neighbour.
3. Each section's **kind**: its site's build type, or `wilderness`; its name from the site, or from what's in
   it ("the Fenwood", "the north road").

Rectangles keep everything simple: a section's grid, its block-out, its tiles, its background and its scene
chunk are all rectangles.

### Carried things

A section's build gets, in its own cells:

- **The terrain and relief** of its rectangle, as the world plan has them (the biome per cell: forest floor,
  grass, sand, water, rock).
- **Paths**: every road and river cell inside it, with the points where each crosses its edge (its *entries*).
  The section's layout keeps them as fixed paths and lays its zones around them. A village's main street is the
  world road through it; a wild section's road runs where the world plan put it.
- **Masses**: lakes, coast, forests and mountains as fixed areas. A section may detail them (a clearing, a path
  through the wood) but not move them.
- **Its margin**: the `margin` cells (2 by default) along each edge it shares with a neighbour are the world
  plan's exactly: nothing tall stands there unless the world plan's forest or building covers both sides. So two
  neighbours always agree where they meet.

This is the same mechanism as `keepSeeds` (a revision keeps the zones it isn't about): the section's compile gets
the carried things as fixed statements, solved first.

### Revising

A revision is either of the world plan (move a site, add a road: the sections it touches are laid out again,
the others keep their plans) or of a section (as today, inside its carried things).

## 6. Tiles

### How many

A painting or background has a **density**: pixels per cell. It is fixed per project (Project Settings ▸ Places ▸
`paintCell`, default 80 px across a cell's diamond: the painting's block-out is `(W + H) × 40` px wide). A model
image (FLUX.3 Image, 4 MP) holds about **60 cells across its diamond** at that density, with its overlap. So, for a
place of `W × H` cells:

- tiles per side `n = ceil((W + H) / 60)`; `n × n` tiles, each overlapping its neighbours by 12% of its size;
- a room (24 × 24) is 1 tile; a dungeon level (50 × 50) is 2 × 2; a village (116 × 116) is 4 × 4.

The block-out is drawn at the final size, so the painting, the block-out and its id map stay the same size, pixel
for pixel: the Mark step, the occluders and the interiors are unchanged.

### Painting the tiles

- Each tile is cut from the block-out with its overlap and painted at the model's full size, with the place's
  prompt (its kind's paint guide, its own words, the key) plus a note that it is one part of a bigger map (paint
  up to the cut edges, no frame) and the scale of its part. The whole place's counts become "of the whole map's".
- The tiles of one place are painted in parallel (4 at a time), with the same seed.
- They are joined by **cross-fading** the overlaps (each tile weighted 1 inside, fading to 0 across its overlap).
  Walls, roads and edges line up because the block-out under them is the same; the floor's texture blends.
- Tested (`builder_tests/bfl_paint/dungeon_test/packed/tiles-1`): a 50 × 50 dungeon in 2 × 2 tiles at 4000 ×
  2140 holds about 2.5 times the detail of the one-image painting, with no visible seam.

### Sections are painted the same way

An image is always a rectangle; a section's isn't. So a world is painted in layers, each a rectangle, joined by masks:

1. **The base**: the whole world from the world plan, in tiles (its wild land, water, forests, the land round every
   place).
2. **Each section**, on its square: its land in detail and the world's land round it (its block-out carries both), the
   base painting of that square given to the painter as a reference so the land round the place matches. It goes over
   the base with a mask that is whole on its land and fades to nothing across a ragged band along its land's edge
   (2.6–9.4 cells, smooth noise): in that band both show the same land, so the fade doesn't show.
3. Neighbouring sections overlap only on world land, so the order they're laid in doesn't matter.

The Assembled backgrounds are made and joined the same way. Mark, the occluders and the scene are per section, on its own
land.

### Backgrounds (Assembled)

The Assembled method's background (`background.md`) is made per section, in the same tiles, and joined the same
way. The assets are planned once for the world: a kind of house used in five villages is made once.

## 7. The scene

One scene holds the whole world:

- each section's entities under a group entity of its own (`Section: <name>`), placed at its origin;
- each section's painting or background as its own sprite (or its tiles' sprites), never one huge texture: a
  section is the natural chunk for loading and streaming later;
- the world's roads, rivers and walk layer continuous across sections (their cells are the world plan's).

## 8. Storage

```
places/<world-id>/
  world.json          the world: its Brief, its plan's summary, its sections (id, kind, name, bounds, margin, stage)
  world.solved.json   the world plan
  world.png           the world plan drawn, with the sections on it (the World page)
  sections/<section-id>/   a normal place folder: brief, spec, layout, plan, block-out, painting, marks…
```

A section's folder is a place's folder, so everything that works on one place works on a section.

## 9. Limits and later

- **Size**: no limit on the number of sections. A world's scene loads every section today; streaming sections in
  and out by the camera is later work, made easy by sections being chunks.
- **Time and cost**: a section costs what a place costs. A world of 20 sections of 2 × 2 tiles is 80 paintings
  (about 20 minutes, 4 at a time).
- **Load**: the jobs run a few at a time, so a long world build doesn't hold the machine at full load.
- **Later**: sections of other shapes than rectangles; a world map view; streaming; roads drawn by hand on the
  World page.
