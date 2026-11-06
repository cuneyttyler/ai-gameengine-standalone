# The viewport

The viewport is the scene view in the middle of the editor: how to move around it (pan, zoom, frame), select and edit
things with the mouse and keys, and turn its overlays on and off (grid, markers, paths, physics shapes, frame stats,
interiors).

## What the viewport is

The **Viewport** panel shows the open scene, drawn by the engine as the game draws it. The toolbar runs along its top
and the scene view fills the rest. The view is a separate native window laid exactly over the panel. Because of that,
it's hidden for a moment while you drag a panel tab (a "Viewport" placeholder shows instead), and whenever its tab
isn't the front tab of its group. Menus, dialogs and notices are always drawn above it.

You can move the Viewport like any panel: drag its tab, hide it with **M**, or maximise it with a double-click on its
tab or **F11** (with the viewport last clicked). When the editor opens a scene, the view frames the whole scene.

See also: panels-and-layout.md, toolbar-and-tools.md.

Questions: why did the viewport go blank while dragging a panel? how do I hide the viewport? how do I make the
viewport fill the window?

## Moving the camera: pan, zoom, frame

- **Pan**: drag with the **right** or **middle** mouse button, or drag **empty ground** with the Select tool.
- **Zoom**: the mouse **wheel** zooms in and out around the cursor. Preferences ▸ Viewport ▸ Zoom speed sets how far
  one step goes (slow, medium, normal, fast), and **Invert the wheel** makes wheel-up zoom out.
- **Frame**: **F**, View ▸ Frame scene, the toolbar's **Frame** button or the ground menu's Frame scene. This zooms
  and centres the view so everything in the scene fits, with a margin. An empty scene resets the camera.
- **Go to marker…** on the toolbar centres the view on a named marker.

The wheel does other things with a key held: **Alt+wheel** resizes (the brush while one is armed, otherwise the
selection), and **Shift+wheel** turns (or, for a tile brush, changes how many cells it covers). During Play the camera
follows the player instead, at Project Settings ▸ Camera ▸ playZoom, and your editing view comes back when you stop.

See also: preferences.md.

Questions: how do I pan? how do I zoom? how do I invert the scroll wheel? how do I centre on the whole scene?

## Selecting with the mouse

With the **Select** tool:

- **Click** a thing to select it. **Click empty ground** (without dragging) to select the ground cell there.
- **Ctrl+click** adds a thing to the selection, or takes it off.
- **Shift+drag** draws a box. It selects every thing standing in it, and the ground cells (where no thing is) whose
  middle falls inside. With several things selected, the Inspector shows what they share, and Delete removes them
  all, cells included.
- **Esc** clears the selection and goes back to the Select tool.
- **Right-click** anything for its menu. Right-clicking selects what's under the cursor first.

Selecting in the viewport also selects in the Scene Tree, and the other way round. Right-clicking in the Scene Tree
opens the same menu as in the viewport.

See also: toolbar-and-tools.md, menus.md.

Questions: how do I select multiple entities? how do I box select? how do I select a ground tile? how do I clear the
selection?

## Editing with keys

These keys work on the selection in the viewport (most of them can be rebound in Help ▸ Keyboard Shortcuts):

- **Arrows**: nudge one cell along the grid's axes; **Shift+arrows**: a tenth of a cell. (These are fixed.)
- **R** / **Shift+R**: turn. Flat things turn a quarter; upright pictures are mirrored.
- **]** or **+** / **[** or **-**: 10% bigger or smaller. **}** / **{**: 2% bigger or smaller. While the brush is
  armed, these resize the brush instead.
- **Ctrl+]** / **Ctrl+[**: a flat thing one step forward or back among the ground pieces. Add Shift to go all the way
  to the front or the back.
- **Ctrl+C**, **Ctrl+V** (pastes at the cell under the cursor), **Ctrl+D** (duplicate), **Delete** or **Backspace**.
- **1–9**: pick prefab 1–9 as the brush.
- **Ctrl+Z** / **Ctrl+Y**: undo / redo.

A run of resizes on one thing within a moment counts as one undo step. Keys pressed while the viewport has the focus
go through the same shortcut list as the panels, so a key you rebind works everywhere.

See also: shortcuts.md.

Questions: how do I rotate an entity? how do I nudge by a fraction of a cell? how do I resize precisely?

## Overlays: grid, markers, paths, physics, stats

You can switch these drawings on and off over the scene:

- **Tile grid on empty ground** (G, View menu): the borders of cells where there's no ground yet. Preferences ▸
  Viewport ▸ Show the grid at start decides whether it's on when the editor opens.
- **◇ Markers** (toolbar): the named markers.
- **⤳ Paths** (toolbar, or View ▸ Paths): every path. When it's off, only the selected path shows. In Play, it also
  draws where each character is walking.
- **Physics shapes** (View ▸ Physics shapes (colliders, triggers, bodies)): the colliders, triggers and bodies.
- **Frame stats** (F3, View menu): frame timing, also during Play.

Whether the grid is square or diamond comes from Project Settings ▸ Grid ▸ style (Isometric or TopDown).

Questions: how do I show the grid? how do I see colliders and triggers? how do I see the frame rate? how do I hide
markers?

## Interiors in the viewport

Buildings with a cutaway interior can show their inside or their outside in the editor:

- **H** (View ▸ Selected building's interior / exterior): the selected building shows its interior, then each floor
  in turn, then its outside again.
- **Shift+H** (View ▸ All interiors / exteriors): every building shows its inside, or every building its outside.
- **PageUp** / **PageDown** (View ▸ Floor up / Floor down): one floor up or down, for the selected building or for
  every building.
- The building's right-click menu has **Show interior**, **Show floor N** and **Floors ▸** (how many floors it has).

These are off during Play. A building whose interior is its own scene has **Open interior scene** in its menu
instead.

Questions: how do I look inside a building? how do I see the upper floor? how do I show every interior?
