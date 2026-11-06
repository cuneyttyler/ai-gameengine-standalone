# Build methods: Assembled, matched art and Paint

The two ways the Build panel and the Planner make a place's look (Assembled, with matched art over a painted background, and Paint, one painting marked by hand), where the method is set, and how painting, marking and pre-filling work.

## The two build methods

Every scene says how it's built:

- **Assembled**: each building, tree and prop gets its own art, put together on the grid over a painted background; every piece can be moved and edited. Steps: Confirm, Layout, Plan, Assets, Result.
- **Paint**: the place is painted whole as one picture (as Baldur's Gate's areas are), then you mark where one can walk and what a character goes behind. Only doors, chests, items and characters get art of their own. Steps: Confirm, Layout, Plan, Paint, Mark, (Animate), Assets, Result.

Where it's set:
- **Project Settings ▸ Places ▸ Method**: the default for new scenes (File ▸ New Project asks too; File ▸ Preferences… ▸ New projects ▸ Build method is what New Project starts with);
- **a scene's type**: File ▸ New Scene asks for it; right-click a scene in the Scenes panel or Scene Tree ▸ **Type ▸ Assembled / Paint**;
- **the Build panel's first page**: its Build method shows the open scene's type; choosing the other sets the scene's type too;
- **the Planner's home page**: every place the plan builds starts with it.

In ④ Paint, **Use Assembled instead** switches a build to Assembled.

### Example: a painted swamp next to an assembled town

The town Greywater should have buildings you can move and edit; the swamp outside it should look like one hand-painted picture.

1. In the Scenes panel, right-click the scene `greywater` ▸ **Type ▸ Assembled**.
2. Choose **File ▸ New Scene**, name it `blackfen_swamp` and pick the type Paint.
3. With `blackfen_swamp` open, press **U**: the Build panel's **Build method** already shows Paint ("the scene's type").
4. Type "a misty swamp with a sunken shrine and a fisherman's hut on stilts" and click **Start**.

The swamp build has the steps Confirm, Layout, Plan, Paint, Mark, Animate, Assets, Result; a build in `greywater` has Confirm, Layout, Plan, Assets, Result.

Questions: what is the difference between Assembled and Paint? how do I change a scene's build method? which method should I use?

## Matched art: Assembled pieces made to fit together

In an Assembled build, art made one piece at a time from words looks fine alone but doesn't belong together (palettes, outlines, light, sizes and angles differ). Matched art makes the place's art like a brief to a designer:

1. The plan's needs (each role with a footprint) are drawn as block-outs at their exact grid size.
2. A **style sheet** of a few representative pieces is painted first; its best building and best small thing become the examples everything else follows.
3. **Buildings** are painted in batches of up to 6 on a sheet, with the example as reference. **Small things** are each an edit of the example small thing, keeping its angle and finish.
4. Things that work together (a door and its wall, a torch and its wall) are made on one sheet.
5. Every piece is **checked** once by a quick vision model (one thing, of its kind, cleanly separated); a wrong one is made again with the reason.
6. Pieces are cut from their sheet, scaled to their footprint, pulled toward the style's palette, and added to the project's assets.

Which provider draws (BFL, PixelLab or Mistral) is the build's **made by** (Project Settings ▸ AI ▸ art by default, or File ▸ Preferences… ▸ Art and painting ▸ Prefab art). In Build ▸ Assets ▸ The assets you can Keep, Reject or Make again any piece.

### Example: a harbour town whose pieces belong together

1. Start an Assembled build: "a harbour town with a lighthouse, warehouses and a fish market".
2. At ④ Assets, set **made by** to BFL.
3. Watch **The assets**: first a style sheet of a few pieces is painted; its best warehouse and best crate become the examples. The rest of the buildings follow in sheets of up to 6, and the barrels, nets and lamps are each made as edits of the crate, at its angle and finish.
4. One lamp came back as two lamps on a post: the check rejects it and it is made again with the reason. A fish stall you don't like: click **Make it again** with "an awning in faded red".

The pieces end up in the project's assets, scaled to their footprints and pulled toward the same palette, so the warehouses, the stall and the crates look like one place.

Questions: what is matched art? why do my generated assets not match? how are building assets made?

## The painted background

An Assembled place's ground (roads, paths, floors, grass) is painted as one picture from its plan, matched to the grid so each cell shows its own square of it. Build ▸ Assets ▸ **Background** shows what it's painted from: the place and its look, then a line per kind of ground ("asphalt: road with white lane markings"), which you can edit. Big places are painted in tiles, checked and joined.

The background model is chosen in File ▸ Preferences… ▸ Art and painting ▸ Background (BFL FLUX.3 Image by default, which keeps the map best), per project in Project Settings ▸ AI, or per build ("Background painted by"). It needs a BFL key.

### Example: muddy roads instead of cobbles

1. In an Assembled build of a frontier village, at ④ Assets, before the art is made, look at what the background is painted from: the place and its look, then a line per kind of ground, such as "road: grey cobblestones" and "grass: short green grass".
2. Change the road line to "road: muddy cart tracks with puddles and straw" and the grass to "grass: dry yellow grass, patchy".
3. Start the art. The **Background** tab shows "Painting the background" with its steps (the whole place, then windows painted again for detail), then the picture and who painted it.

The ground is painted from your lines, matched to the grid, and the buildings stand on muddy tracks. Without `BFL_API_KEY` in `ai_service/.env`, the background can't be painted.

Questions: how is the ground painted? which model paints the background?

## Paint: painting the place whole

In a Paint build, **④ Paint** ("Paint the place whole?") paints the place from its plan: the plan's block-out, colour-coded by kind, is given to the paint model, and comes back as one painting in the game's view (about a minute). You can add notes ("a darker, rainier mood; the river wider; no snow").

- **Paint the place**, then **Paint again** with your notes (the marks you made by hand are kept);
- the painting is shown beside its plan (click to zoom);
- **⑤ Mark →** goes on.

The paint model is File ▸ Preferences… ▸ Art and painting ▸ Paint model (FLUX-3-Image keeps the layout best; FLUX-2-Klein-4b is a fast rough draft; FLUX-2-Pro, FLUX-2-Max; FLUX-3-Image-to-Pro and -to-Klein repaint the look after), or Project Settings ▸ AI ▸ paint.

From the AI panel, "paint it again ..." (or "repaint") paints the open place again with your words as its notes.

### Example: a rainier swamp

1. In the Blackfen swamp's Paint build, at ④ Paint, click **Paint the place**. About a minute later the painting shows beside its plan.
2. It is too sunny. Type in the notes "a darker, rainier mood; the water wider; no flowers" and click **Paint again**.
3. Click the painting to zoom and check the shrine is still where the plan put it, then click **⑤ Mark →**.

The same can be asked from the AI panel with the swamp's build open:

> Paint it again, darker and rainier, with the water wider

Marks you already made by hand are kept when it is painted again.

Questions: how does the Paint method work? how do I repaint a place? which model paints the place?

## ⑤ Mark: walkable cells and occluders

A painting needs two things added to be a level, and you mark them in **⑤ Mark**:
- **Walkable**: each cell walkable or blocked. Tools: brush (sizes 1, 2, 4), rectangle and fill; left-click blocks, right-click or Alt makes walkable.
- **Occluders**: the parts of the painting drawn again over a character who walks behind them (a house front, a tree canopy, a wall top). Tools: **Brush** and **Eraser** (size with Ctrl+wheel or [ and ]), **Base** (the line where it meets the ground: drag its handles or click to add a point), **Select** / Click-select (S: click to add the object under the cursor, Alt+click to take it away), and **New**.

Zoom with the wheel, pan with Space+drag or the middle button. Undo and redo with Ctrl+Z / Ctrl+Y; save with Ctrl+S (or it saves when you leave the step).

The step opens **pre-filled from the plan**: cells under solid things blocked, an occluder for each tall object (building, tree, wall, tower) with its base along its front edges. **Pre-fill again** redoes it, keeping what you edited. If the place was painted again since, it says to check each occluder's region against the new painting.

**Continue →** goes on (in a Paint build, **⑥ Animate the doors →** animates the painted buildings' doors first; they open as someone comes near).

From the AI panel: "pre-fill the marks" (add "replace" or "from scratch" to drop your edits) and "check the marks" (every tall thing has an occluder, no building can be walked through). Marking is your step: the AI never says the marks are done or correct.

### Example: marking the fisherman's hut

1. ⑤ Mark opens pre-filled: the hut's stilts and the shrine are blocked, and the hut and the dead trees have occluders with their bases along their front edges.
2. The plank walkway to the hut is painted but blocked. In **Walkable**, pick the brush size 2 and right-click along the planks to make them walkable.
3. In **Occluders**, the hut's occluder misses its roof overhang: pick **Brush** and paint over the overhang. Drag a handle of its **Base** so the line runs along the bottom of the stilts.
4. Press **S** and click a reed bed that should hide the player's legs to add it as an occluder.
5. Ask the AI panel "check the marks". It reports, for example, that one dead tree has no occluder; add it with **New**.
6. Press **Ctrl+S**, then **Continue →**.

In Play, the player walks the planks to the hut and is drawn behind its front and the reeds.

Questions: what are occluders? how do I mark where the player can walk? what does pre-fill do? can the AI do the marks?

## Building a painted place into the scene

At the end of a Paint build, the painting is laid in the scene as sprites behind everything, a hidden **walk** layer makes blocked cells solid, and each occluder is cut from the painting and drawn in narrow strips, so a character whose feet are behind its base line is drawn under it. Then the things from Assets are placed (doors, chests, items, characters) and the built place is checked by the vision model as for any build.

Once a place has a painting, its Assets step makes only what the painting doesn't hold: things the player uses (doors, chests, items), characters and anything that moves.

In the Planner's automatic build, a Paint place is painted, its marks pre-filled from the plan, and built; you refine the marks in ⑤ Mark later.

### Example: what the swamp's Assets step makes

1. After ⑥ Animate, the swamp's Assets step lists only the hut's door, a chest in the shrine, a lantern item and the fisherman Old Tam: the hut, trees and water are in the painting already.
2. Continue to Result and click **Build into the scene**.

In the scene, the painting lies behind everything, the hidden walk layer blocks the water and the hut's stilts, and Old Tam walking behind the hut is drawn under its front. The vision model then checks the built place as for any build.

Questions: how does a painted place work in the game? why doesn't a Paint build make building assets?

See also: build-panel.md, revising-places.md, ai-providers-and-keys.md.
