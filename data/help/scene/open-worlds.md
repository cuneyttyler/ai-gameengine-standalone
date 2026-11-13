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
4. On ② World press **Plan the sections**: each section is laid out and planned as its kind of place (its zones,
   streets and buildings), keeping the world's roads through it, one after another; then they're put into the world,
   which is painted (④ Paint) or assembled as one place.

Questions: how do I build an open world? what is a section? can a world have many villages? what does the World step show?

## The world's places, sizes and spacing (① Confirm)

The request decides what's in the world; ① Confirm shows it and lets you change it before anything is planned:

- **Places**: one row per settled place: its name, its kind (settlement, harbour, castle, camp, farmland), its size
  and what that size makes (*small*: a village of 15-60 buildings, *medium*: a large village, *big*: a town of
  150-500, *huge*: a city; a harbour's *big* is a port, a castle's a fortress). **Configure** opens the place's own
  parts, as for a single place: its zones (residential ward, market, docks...), how many of each and how many
  buildings of each kind (*docks: 1-2 piers*, *an inn and no houses*). **Add a place** and **Remove** change the list.
- **Where** a place is, when the request says it: *by the sea*, *near the forest*, *near* another place (*small
  villages round the big one*). A harbour is by the sea unless you say otherwise.
- **Spacing** (a slider, 1-10): how much open country lies between two places, in small villages' widths (1: close
  neighbours, 9: far apart with long roads). *"Not too far from each other"* is about 2, *"distant"* about 7.

Bigger places take more of the world rather than making the gaps bigger: the open country between them stays as
the spacing says. The world's size follows from its places and the spacing.

### Example: a big village with small ones round it

*"An open world with one big village, several small villages round it, not too far apart, a lake and some
forests"* gives six places (one *big*, five *small*, each small one *near* the big one), spacing 2, a lake and two
forests. Configure the big village to give it two market squares, or set a small one's houses to 20.

Questions: how do I set how big a village is? how do I put villages closer together? how do I put a village by the sea? can I change a place's zones in an open world? how do I add a place to the world?

## How a world is laid out

1. **The land first**: the sea, coast, lakes, mountains, hills and forests grow to their shares of the world, and
   woods (and a range, where mountains are allowed) grow in from its edges, so the border is never bare grass.
2. **Then the places**, on the open land that's left: each one's whole land free of water and roads, off the edge,
   as far apart as the spacing says (a harbour as near the sea as it fits). A place's land is round and wobbling,
   never a square.
3. **Then the roads**: one network joining the settled places, roads leaving over the world's border, never on the
   sea or a beach and kept back from the waterline; a road crosses a river by a **bridge** (drawn dark red-brown in a
   block-out, and painted as a bridge).

Each world gets its own coast, sea and ridges (its own seed): two worlds from the same kind of request differ.
A world's sections have no woods round them (a single place keeps its surrounding woods).

Questions: why is the village not on the coast? why does the road go round the bay? why are there woods at the edge of my world? why are roads never on the beach?

## Gateways: ways into dungeons, caves and crypts

A **gateway** is a way into another place: a cave mouth, dungeon stairs, a crypt door, a mine shaft, a ruined tower's
door, a cellar hatch. It's a thing of its own in the plan (never painted into the picture): the plan places it where
it suits (a cave or a mine at the foot of the hills or mountains, stairs in the forest, a crypt by a graveyard), a
walk from the place it belongs to, with a trail to the nearest road. Assembled builds give it its art like any
building; Paint leaves its spot plain in the painting and puts its art over it (⑥ Assets). When the place it leads
into is built (a dungeon, its own scene), walking into the gateway takes the player there, and coming back they
arrive in front of it.

① Confirm lists the gateways (where each leads, its kind, where it is, in words) to change or remove. The AI Planner
writes them for a game's dungeons and interiors ("a small open-world RPG with several villages and dungeons": one
world with the villages, each dungeon behind a gateway placed in the world).

Questions: how do I add a dungeon entrance? where is the cave entrance placed? how does the player get into the dungeon? are gateways painted?

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

A whole world is painted with FLUX-3 at one density for the whole map (so a house is the same size everywhere), in
as few tiles as cover it: each settlement stands whole in one tile and is described in that tile's prompt from its
own plan (its buildings by kind), and its land shows in the block-out as a soft pale patch under its streets. A small
world fits one call; at 5 px a cell a 900-cell world is 4 calls. FLUX.2 klein isn't used for worlds: over a map it
re-lays the land (roads, rivers, the sea) instead of keeping it.

### Example: how many calls a village takes

Brinemouth is a 319 × 319-cell section. Painted freely at 48 px a cell its painting is 15,312 × 7,740 px: 1 call to
compose it and 30 tiles, 31 calls. At 18 px a cell it's 10 calls. The whole coastal world (7 places, 6 forests and its
land) at 9 px a cell was 22 images.

Questions: why is my place painted in tiles? how many calls does painting take? what happens if painting stops part-way?
