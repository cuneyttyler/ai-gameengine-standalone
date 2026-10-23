# Phase 5 — Manual Test Checklist

The user-test checkpoints from Phase 5 (`development_plan.md`), for one end-of-phase pass.
5.1 (Export) was covered in `phase4-test-checklist.md`. Automated tests cover the logic:
the diamond mapping and draw order, scene operations and travel, the recent-projects
list, and the multi-select rules. This list covers what tests can't judge: how it looks
and feels.

**Before you start**
- **Rebuild:** close the editor, then run `cmake --build build --config Debug`.
- **Launching:** start `build\apps\editor\Debug\editor.exe` with no arguments. It now opens
  the **welcome window** (5.4) instead of the demo; choose the demo there.
- **Undo:** `git checkout projects/demo` resets the demo's committed files. Your own
  uncommitted demo changes are yours: commit or copy them first if you'll experiment.

## 5.2 — Top-down art on the isometric grid (most important)

- [ ] Your road prefab (made before 5.2): right-click it in Prefabs ▸ **Lay flat on the
      grid (top-down art)**. Every copy in the scene now lies inside its diamond cell,
      instead of standing as a big square.
- [ ] Paint a straight road across several cells: the pieces join edge to edge along the
      grid's axis.
- [ ] Press **R** while painting (the toolbar shows ↻ 90°, 180°, …): the next pieces turn,
      so the road can run the other way. Paint a corner if you have a corner piece.
- [ ] Select a painted flat tile and press **R**: it turns; Ctrl+Z turns it back.
- [ ] Import a new top-down square image ▸ Make a prefab: "Drawn" suggests
      **Top-down**. A 2:1 isometric image suggests **Isometric / upright**.
- [ ] A flat prop (make a prefab from a rug-like image as top-down, as a prop): it lies
      on the ground, under characters walking over it.
- [ ] Art tab ▸ Kind **ground tile (top-down)** ▸ Generate ▸ Make prefab: a flat tile you
      can paint. (Needs AI on.)
- [ ] The demo's existing ground and walls look exactly as before.

## 5.2 — Resizing (from your report)

- [ ] Select a skeleton or prop: **]** / **[** (or **+** / **-**) make it bigger and smaller,
      and **Shift+]** / **Shift+[** (`{` `}`) in finer steps. Alt+wheel works too. This works
      on your keyboard layout, with the viewport or the UI focused.
- [ ] Select a tile: it resizes its image too, and stays in its cell.
- [ ] Resize with nothing selected: a message says to select something.
- [ ] The toolbar's size % also applies when painting tiles.

## 5.3 — Scenes and gateways

- [ ] Bottom panel ▸ **Scenes**: `main` with *start* and *open* badges. **New scene…**
      "cellar": it opens empty. Paint some floor, add a marker (right-click the ground ▸
      Add a marker here…) named `entrance`, place a player, then Save.
- [ ] Open `main` (double-click). Place the **gateway** prefab (a glowing ring lying on
      the ground). Inspector ▸ Gateway: scene `scenes/cellar.aige`, marker `entrance`.
      Save. The Scenes tab's Gateways list shows no problems; set a wrong marker, and a
      problem appears.
- [ ] Play: walk into the ring. You arrive in the cellar at the marker, with the same
      health (get hurt first to check). A quest in progress is still in progress.
      Standing on an arrival ring doesn't bounce you back; stepping off and on again does.
- [ ] Stop: you're back in the scene you were editing, as you left it.
- [ ] Rename `cellar` (right-click ▸ Rename…): the gateway in `main` still works. Delete
      it: refused, because a gateway leads there.
- [ ] Set `cellar` as the start scene, then run `sandbox.exe`: it starts in the cellar.
      Set `main` back.
- [ ] File ▸ Export Game… with two scenes: the exported game walks between them.
- [ ] Ask the AI "add a gateway from here to the cellar's entrance": it proposes a Gateway
      with that scene and marker.

## 5.4 — Start window

- [ ] Run `editor.exe` with no arguments: the welcome window lists recent projects with
      folder and last-opened time. A folder you deleted shows as *missing*; × removes it.
- [ ] Open Folder… on a folder that isn't a project: refused with a message. New
      Project… creates one and opens it. Open the demo works.
- [ ] Tick "Open the last project directly at launch", close, and start again: the last
      project opens straight away. File ▸ **Close Project** brings the welcome window
      back (after asking about unsaved changes). The same option is in Preferences.

## 5.5 — Editor refinements

- [ ] File ▸ **Recent Projects ▸** opens a submenu; hovering an entry shows its full path.
      The submenu draws over the viewport.
- [ ] Prefabs or Assets: **Ctrl+click** several cards, **Shift+click** a range, **Ctrl+A**
      (click a card first) picks everything shown in the folder/filter. Right-click a
      picked card ▸ Move N to folder… or Delete N… (one confirmation listing them). Drag
      several picked cards onto a folder. Esc clears the picks.
- [ ] **Ctrl+M** hides the viewport: the bottom panel fills the middle (handy for Story or
      Art); Ctrl+M or "▾ Viewport" brings it back. It's remembered after a restart, and
      View ▸ Viewport shows it.
- [ ] Help ▸ Keyboard Shortcuts: F1 reads "Shortcuts"; Ctrl+M and R are listed.
- [ ] File ▸ **Project Settings…** opens a window with × and Esc; changing a value (e.g.
      Camera ▸ playZoom) applies, and an out-of-range value shows an error. The Inspector
      with nothing selected just offers the button.
