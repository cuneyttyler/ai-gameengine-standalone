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

Set a scene's type in New scene, or its right-click ▸ **Type ▸ Paint** (see scenes.md).

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

Questions: how do I fix where the player can walk in a painted scene? what are the marks? why does Mark say stale?

## Marking walkable cells

Choose the **Walkable** layer. Tools:
- **Brush**: paints cells blocked; **Alt** or the right button makes them walkable;
- **Rectangle**: a rectangle of cells;
- **Fill**: an area of cells of the same kind.

Blocked cells become a solid, invisible tile layer called `walk` in the scene, which pathing and collision read like
any ground. Block cells under buildings, water and cliffs; leave doorways and paths walkable.

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

Questions: why is my character drawn behind a tree it's in front of? how do I put a rug under a table? how is draw
order decided? why does the player disappear behind a building?

## Big painted places

A model paints one picture of limited size (about 4 megapixels for most), so a big place in one picture gets little
detail per house. The design for big painted places is to paint them in **tiles**, each about one model image at a set
density (art pixels per cell, chosen for the camera), so that together they read as one painting with no visible
edges. Today, painted places are painted as one picture; backgrounds of big Assembled scenes are already painted in
tiles and blended (see backgrounds.md).

Questions: can I paint a very large place? why does my big painted town look blurry close up?
