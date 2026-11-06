# Painted places, marks and what draws in front

How a Paint-type scene works: the place painted as one picture, then marked in the Build panel's ⑤ Mark step (walkable
cells and occluders with their base lines), how the painted scene is put together, and how the editor decides what is
drawn in front of what (depth sorting and occlusion) in any scene.

## What a painted place is

A scene of type **Paint** is painted whole: one picture in the game's isometric view, as Baldur's Gate or Pillars of
Eternity draw their areas. The painting is never cut into assets. To make it a level, two things are added on top of
it, by marking:
- **where the player can walk**: cells, walkable or blocked;
- **occluders**: the parts of the painting drawn again over a character who walks behind them (a house front, a tree
  canopy, a wall top).

Only what the player uses (doors, chests, items) and characters get art of their own. Build steps: ① Confirm, ② Layout,
③ Plan, ④ Paint, ⑤ Mark, ⑥ Assets, ⑦ Build. A picture of your own can be the painting: Build panel ▸ From a picture ▸
**As the painting** goes straight to ⑤ Mark with every cell walkable and no occluders.

Set a scene's type in New scene, or its right-click ▸ **Type: … ▸ Paint** (see scenes.md).

### Example: your own watercolour of a mountain village as a level

You painted `highvale.png`, an isometric view of a mountain village.
1. Make a scene `highvale` with **New scene…**, choosing **Paint**.
2. In the Build panel, click **Choose…** beside **From a picture**, pick `highvale.png`, and in the list beside it
   choose **as the painting (Paint: skips Layout, Plan, Paint)**.
3. The build goes straight to ⑤ Mark, with every cell walkable and no occluders.
4. Block the houses and the cliffs on the Walkable layer, add occluders for the houses and the big pine, then go on to
   ⑥ Assets for the doors and the villagers, and ⑦ Build.

Questions: what is a painted scene? how do I use my own painting as a level? what does the Mark step do?

## The Mark step (Build ⑤ Mark)

After ④ Paint, the Mark step shows the painting with the marks pre-filled from the plan:
- **walkable**: blocked where the plan's cell is solid (rock, wall, void, water without a bridge) or covered by something
  that blocks (buildings, tree trunks); walkable elsewhere;
- **occluders**: one for each tall object (a building, tree, wall or tower; a wall or fence run gets one per run), its
  region the object's pixels (a little grown, since the painting spills past the plan), its base line along the two
  front edges of its footprint.

Your job is to check and fix them: marking is the user's step, and the AI never says the marks are done. If the painting
was redone after marking, the step says the marks are stale; walkable cells and base lines stay (they follow the plan)
and the regions should be checked.

The canvas: wheel zooms, **Fit** (F) fits the painting; pan with Space+drag or the middle button. Two layers,
**Occluders** and **Walkable**, one shown at a time, each with an **opacity** slider. **↶** / **↷** (Ctrl+Z / Ctrl+Y)
undo and redo; **Save** (Ctrl+S); it also saves itself every minute and when you leave the step.

### Example: checking the marks of a painted riverside mill

1. After ④ Paint, go to ⑤ Mark. The painting shows with the marks already filled in from the plan.
2. Press **F** (**Fit**) to see it whole, then zoom in on the mill with the wheel; pan with **Space+drag**.
3. Show the **Walkable** layer and lower its **opacity** so you can see the painting under it: the river is blocked,
   but the little bridge is too.
4. Switch to **Occluders**: the mill and the willow each have one. The willow's region misses a branch.
5. Fix both (see the next sections) and press **Save** (**Ctrl+S**).

If you repaint the picture later, the step says the marks are stale: check the occluder regions again.

Questions: how do I fix where the player can walk in a painted scene? what are the marks? why does Mark say stale?

## Marking walkable cells

Choose the **Walkable** layer. Tools:
- **Brush**: paints cells blocked; **Alt** or the right button makes them walkable;
- **Rectangle**: a rectangle of cells;
- **Fill**: an area of cells of the same kind.

Blocked cells become a solid, invisible tile layer called `walk` in the scene, which pathing and collision read like
any ground. Block cells under buildings, water and cliffs; leave doorways and paths walkable.

### Example: opening the bridge over the mill stream

1. In ⑤ Mark, choose the **Walkable** layer.
2. Pick **Brush** and, holding **Alt** (or with the right button), paint along the bridge: its cells become walkable.
3. A painted stone wall by the mill was left walkable: pick **Rectangle** and drag over it to block it.
4. A pond in the corner is all walkable: pick **Fill** and click inside it to block the whole pond.
5. **Save**. After ⑦ Build, the player crosses the bridge and stops at the wall and the pond.

Questions: how do I block an area in a painted scene? how do I make a bridge walkable? why does my character walk
through a painted wall?

## Marking occluders and their base lines

Choose the **Occluders** layer. The list shows each occluder (name, ✎ when changed by you, **●/◌** to hide or show it,
**✕** to delete it); click one to select it. Tools on the selected occluder:
- **Select** (V): pick an occluder;
- **Click-select** (S): click a thing in the painting to add its outline (AI segmentation; **Alt+click** takes it away;
  the first click on a painting takes a few seconds);
- **Brush** (B) adds to it, **Eraser** (E) takes away (Alt or the right button erases too); size with the **⌀** slider,
  **[** and **]**, or Ctrl+wheel;
- **Base line** (L): where it meets the ground: drag a handle, click to add one, right-click to remove one;
- **+ New**: a new occluder drawn by hand.

The **base line** matters most: a character whose feet are behind it is drawn under the occluder, one in front is
drawn over it. Only the part above the base line is redrawn; mask below it is treated as ground in front.

### Example: walking behind the old willow

1. In ⑤ Mark, choose the **Occluders** layer and click `willow` in the list.
2. Pick **Click-select** (**S**) and click the branch the region missed: its outline is added (the first click takes
   a few seconds). **Alt+click** a patch of sky it took by mistake to remove it.
3. Tidy the edge with **Brush** (**B**) and **Eraser** (**E**), sizing with **[** and **]**.
4. Pick **Base line** (**L**) and drag its handles to where the trunk meets the grass.
5. For the well, which had no occluder, press **+ New** and brush its roof and posts, then set its base line.
6. After ⑦ Build, a character walking behind the trunk is drawn under the willow's leaves, and one in front over them.

Questions: how do I make the player walk behind a tree in a painted scene? what is a base line? how do I add an
occluder? why is my character drawn over a house front?

## How the painted scene is built

At ⑦ Build:
1. the painting is laid as pictures at the very back (in pieces of at most 2048 pixels), scaled so its cells match the
   project's tile size;
2. the walkable marks become the `walk` tile layer;
3. each occluder is cut from the painting by its mask and drawn as thin vertical strips (about half a tile wide), each
   strip's depth taken from the base line under it, so a character passes behind or in front of each part correctly;
4. the things from ⑥ Assets (doors, chests, items, characters) are placed.

A painted building with a cutaway interior shows its cutaway while the player is inside and hides its occluders then
(the Building Plan's **hides**). Doors in a painted place are their own entities over the painting (DoorPart).

### Example: building the painted mill and looking at the result

1. With the marks saved and ⑥ Assets done (the mill door, a sack of flour, Miller Edda), go to ⑦ Build.
2. The painting is laid at the back in pieces, the `walk` tile layer is made from the walkable marks, the mill and
   willow occluders are cut into thin strips, and the door, sack and Edda are placed.
3. Press Play and walk round the mill: the player passes behind its front wall and in front of its side.
4. Open the mill door: it's its own entity over the painting, opening as the project's doors do.

Questions: what happens when I build a painted place? how do doors work in a painted scene?

## What is drawn in front: depth sorting

The scene is drawn back to front by depth, worked out every frame from where things stand:
- **ground first**: flat tiles, then flat and ground-lying pieces (rugs, roads), ordered among themselves by their
  Sprite **order** (−9 to 9; Ctrl+] / Ctrl+[) and then by position;
- **standing things** (characters, props, trees) by their position on the grid: further down the screen is in front;
  a standing picture's bottom sits on its cell;
- **buildings on several cells** are compared by the footprint point nearest each character, so a character behind any
  part of a building is drawn behind it and one in front of any part is drawn in front, along its long sides too;
  characters in front are drawn over its doors as well;
- a character inside a building whose interior isn't shown is drawn behind its walls; a building showing its interior
  is drawn behind everything in it;
- held and worn things follow their wearer's point (Attachment **layer**: auto, front, behind, own);
- in painted scenes, occluder strips sort by their base line (above).

**ScreenTransform2D ▸ depth** shows the result (read-only for things with a world position). If something sorts wrongly,
check its footprint (Obstacle), its Sprite's flat/ground/order, or the occluder's base line.

### Example: the player shows through the long side of the barn

In an Assembled farm, the player standing behind the barn's long side is drawn over its wall.
1. Select the barn. Its **Obstacle** has **left** `0`, **right** `0`, **back** `0`: the footprint is a single cell, so
   only that cell is compared with the player.
2. Set **left** `2` and **back** `1`, to cover the cells the barn stands on.
3. Walk the player behind it again: now it's drawn behind the barn along the whole side.
4. A rug in the farmhouse is drawn over the table? A rug should lie on the ground: tick **Sprite ▸ ground** (or
   **flat** for top-down art) on the rug, and it's drawn under what stands.
5. Check **ScreenTransform2D ▸ depth** on both to see their order.

Questions: why is my character drawn behind a tree it's in front of? how do I put a rug under a table? how is draw
order decided? why does the player disappear behind a building?

## Big painted places

A model paints one picture of limited size (about 4 megapixels for most), so a big place in one picture gets little
detail per house. The design for big painted places is to paint them in **tiles**, each about one model image at a set
density (art pixels per cell, chosen for the camera), so that together they read as one painting with no visible
edges. Today, painted places are painted as one picture; backgrounds of big Assembled scenes are already painted in
tiles and blended (see backgrounds.md).

### Example: choosing how to make a big port city

You want a port city of about 200 × 200 cells.
1. As a **Paint** scene, the whole city is one picture of about 4 megapixels, so each house gets only a few pixels and
   looks blurry close up.
2. As an **Assembled** scene, each building has its own art and the background is painted in tiles and blended, so it
   keeps its detail.
3. A good middle way: make the city Assembled, and make one small square (the fish market, say) its own Paint scene,
   joined with a Marker and a Gateway, where a painting's detail shows best.

Questions: can I paint a very large place? why does my big painted town look blurry close up?
