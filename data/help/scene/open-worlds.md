# Open worlds, sections and tiled paintings

How to build a big open world (many villages, camps, castles and forests joined by long roads, rivers and coasts) in
one scene, how such a world is cut into sections, and how big places are painted in tiles.

## What an open world is

In the Build panel, beside the build method, choose **Build ▸ An open world** instead of **A single place**. Above the
request it says *Select build method and type first.* Then describe the world, for example *"A big island with several
villages connected through long-distance roads, forests, camps and other related things"*.

A world is planned whole first: its land (coast, sea, rivers, lakes, hills, mountains), the land of each place in it
(each village, camp, castle, farm), its forests, and one road network joining its settled places. Then it's cut into
**sections**: each place's land is a section of its own kind, built in detail as a place is, and each forest is a
section too. A world can have as many sections as you like.

An open world has a **② World** step after ① Confirm: the world plan, and the list of its sections.

The ontology says what a world is made of: the `world` build type and its sections, the land and water (sea, ocean,
coast, mountains, hills, marsh, forest, river, lake), which kinds of place the world's roads reach (settlements, harbours,
cities, castles, camps and farmland; not forests or dungeons), and how mountains rise.

### Example: a coastal land of four villages

1. In the Build panel choose **Build ▸ An open world** and **Build method ▸ Paint**.
2. Ask: *"A coastal land with four large villages, a castle, a camp and farms, joined by roads, with forests between
   them and mountains in the east"*.
3. ① Confirm the world's size and its landform (coastal), then ② World: the world plan shows the sea and coast, the
   places' land, the forests and the roads in orange.
4. Each section is then laid out and planned as its kind of place, keeping the world's roads through it.

Questions: how do I build an open world? what is a section? can a world have many villages? what does the World step show?

## Sections

A section is one place of the world, built as a place of its own kind (a village with its wards, a castle, a camp,
farmland, a forest). Sections are not rectangles: a section is its place's whole land in the world plan, an organic
shape, so its woods and edges never meet a square corner. Inside, what's built keeps together in the middle and its
meadows and water go round it; round its land it shows the world's own ground.

What a section keeps of the world:
- **the world's roads** through its land, exactly as the world plan laid them (the longest is its main road, so its
  streets join it); a road that ends inside its land carries on to its middle;
- its **ways in**: where the world's roads cross its land's edge;
- no forests of its own: the world's forests are sections of their own.

### Example: why a road runs straight into a village

The world plan joins every settled place to the main road and to its nearest neighbour. When Brinemouth is planned in
detail, the world's road through its land is kept cell for cell and becomes Brinemouth's main road; Brinemouth's own
streets branch off it. In the world plan the world's roads are drawn orange; in the planned layout they look like any road.

Questions: why are sections not rectangles? do the world's roads stay when a village is planned? why does my village have no forest in it?

## Mountains and hills

Mountains and hills have height. The land rises from their edge to ridges and peaks (mountains up to 24 storeys, hills
3), a road through them is their pass, and snowy mountains have snow on their crests. Mountain ground can't be walked
on. In a Paint block-out the mountains are raised rock in their own colours, and the painter is told to paint them as
real mountains.

Questions: how are mountains made? can characters walk on mountains? how do I get snowy peaks?

## Painting a big place in tiles

A painting has a density: pixels across a cell. One image holds only so much, so a big place is painted in **tiles**:
each tile is a part of the place, painted at the model's full size, and the tiles are joined by fading their overlaps.
Each tile is saved as it's painted, so a painting cut short (low credit, a timeout) goes on where it stopped.

For places painted freely (see Painted places), the place is first **composed in one call** on its whole land, then
tiles repaint that painting at full size (48 px a cell) to sharpen it. ③ Plan shows how many calls it takes before you
paint: *Painting: 31 calls* (1 to compose, then the tiles).

A whole world is painted section by section and its own land in a grid, then joined: the world's land as the base,
each section over it, cut to its own land with a soft edge.

### Example: how many calls a village takes

Brinemouth is a 319 × 319-cell section. Painted freely at 48 px a cell its painting is 15,312 × 7,740 px: 1 call to
compose it and 30 tiles, 31 calls. At 18 px a cell it's 10 calls. The whole coastal world (7 places, 6 forests and its
land) at 9 px a cell was 22 images.

Questions: why is my place painted in tiles? how many calls does painting take? what happens if painting stops part-way?
