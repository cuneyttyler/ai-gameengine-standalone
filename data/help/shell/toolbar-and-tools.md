# Toolbar and viewport tools

The toolbar sits along the top of the Viewport panel. It holds the editing tools (Select, the prefab brush, Erase,
Path, Marker, Height), their options, quick buttons for Undo, Redo, Save and Frame, and the Play button. This page
explains each one.

## The toolbar at a glance

The toolbar is the strip just above the scene view, inside the Viewport panel. From left to right:

1. **Select**: select, move and delete things (V).
2. **The brush button**: shows the picked prefab ("Paint grass", "Place barrel"), or "No prefab picked".
3. **Turn button** (only when a prefab is picked): ↻ with the angle for flat prefabs, or ⇋ "as drawn" / "mirrored"
   for upright ones.
4. **Auto-fit <terrain>** (only for terrain tiles): a check box.
5. **Size %** (only when a prefab is picked): how big the next piece will be.
6. **Erase** (E).
7. **Path**, and **Finish path** while the Path tool is on.
8. **Marker**.
9. **◇ Markers** and **⤳ Paths**: switch the markers and the paths on or off in the viewport.
10. **Go to marker…**: a searchable list of the scene's markers. Pick one to centre the view on it. It only shows
    when the scene has markers.
11. **Height**, plus its **Raise / Lower / Level** choice and **Radius** while it's on.
12. **Undo**, **Redo**, **Save** (a dot shows on it when there are unsaved changes) and **Frame**.
13. **Play / Stop**, at the right end.

The tools are greyed out during Play, and the toolbar's bottom edge turns blue. The status bar at the bottom of the
window shows a one-line hint for the tool you're using.

See also: viewport.md, status-and-notifications.md.

Questions: where are the tools? what does the Save dot mean? how do I jump to a marker?

## Select tool

**Select** (V, or Esc, which also clears the selection) is the main editing tool.

- **Click** a thing to select it. The Inspector shows its fields and the Scene Tree highlights it. If it's a prefab
  copy, its prefab is highlighted in Assets ▸ Prefabs too.
- **Drag** a thing to move it. One drag is one undo step.
- **Click empty ground** without moving the mouse to select the ground cell there (a tile layer's cell), which you
  can then delete. **Drag on empty ground** to move the camera.
- **Ctrl+click** adds a thing to the selection, or takes it off if it's already selected.
- **Shift+drag** draws a selection box. Every thing standing in the box is selected, plus the ground cells (where
  no thing is) whose middle is inside it. The Inspector then shows what they share, and Delete removes them all.
- A click on a **path point** selects that path. A click on a **marker** selects the marker, and you can then drag it.
- **Arrow keys** nudge the selection one cell (Shift+arrow: a tenth of a cell). R turns it, ] and [ resize it,
  Ctrl+D duplicates it, Delete deletes it.

See also: viewport.md, undo-and-clipboard.md.

Questions: how do I select several things? how do I move an entity? how do I box-select? how do I deselect?

## Prefab brush (Paint and Place)

The prefab brush puts copies of a prefab into the scene. To pick a prefab ("arm" the brush), click a prefab in
Assets ▸ Prefabs (or use its Paint with / Place it), press **1–9** to pick the first nine prefabs in the palette,
choose **Draw with…** in a right-click menu, or click the brush button on the toolbar.

- **Tile prefabs paint**: click or drag, and every cell the stroke crosses gets the tile. **Shift+wheel** changes
  how many cells a tile brush covers, from 1×1 to 4×4.
- **Other prefabs place**: one copy per click, which is then selected. There's only ever one player, so placing the
  player prefab again moves the existing player.
- **Size %** sets how big the next pieces are (10–1000%). ] and [, + and -, or Alt+wheel change it while the brush
  is armed. A ghost preview shows the new size.
- **Turn**: R, Shift+R or Shift+wheel. Flat prefabs turn a quarter at a time; upright pictures can only face as
  drawn or mirrored.
- **Auto-fit <terrain>**: for terrain tiles. Painting picks the right edge, corner and end pieces to match the
  neighbours, and erasing turns the cell back to the plain ground.

If nothing is picked and you click with the brush, the editor says "Pick a prefab in the Prefabs panel first".

See also: shortcuts.md.

Questions: how do I paint tiles? how do I place a prefab? how do I change the brush size? why does placing the player
move it?

## Erase tool

**Erase** (E) removes what you click or drag over. It deletes the thing under the cursor. Where there's no thing, it
clears that cell of the tile layer (the ground). With Auto-fit on, erasing a terrain tile puts back the terrain's plain
ground and refits the neighbouring pieces instead of leaving a hole. One drag is one undo step, so Ctrl+Z brings back
everything that stroke erased.

To delete only what's selected, use the Select tool and press Delete instead.

Questions: how do I erase tiles? how do I delete ground? how do I undo an erase?

## Path tool

**Path** draws the routes characters follow. Packages and procedures use a path by its name. Paths get the names
path-1, path-2 and so on; you can rename them in the Inspector, and the packages that use the path follow the rename.

- **Click on the ground** to add a point at the middle of that cell, linked from the last point. The first click
  starts a new path.
- **Click an existing point** to pick it. The next click then branches from it (a fork). **Drag** a point to move it.
- **Shift+click** a point of the path you're drawing to join the picked point to it (for example, a fork joining
  back up).
- **Enter**, Esc or the **Finish path** button ends the path, so the next click begins another.
- **Right-click a point** for Branch from here, Draw on from here, Wait here…, Play a clip here…, Use a part here…,
  Label…, Delete point and Finish drawing.

**⤳ Paths** on the toolbar (and View ▸ Paths) shows every path. When it's off, only the selected path shows. During
Play, each character's line to the point it's walking to is drawn too. If you delete a path that packages still use,
the editor names them, and Undo brings the path back.

See also: menus.md (path point menu).

Questions: how do I draw a path for an NPC? how do I make a fork? how do I finish a path? how do I make a character
wait at a point?

## Marker tool

**Marker** adds named places to the scene. Click a cell and the editor asks for a name (for example `village_well`).
Packages, gateways and schedules send characters to markers by name, and gateways use them as arrival points. Click an
existing marker to select it, then drag it with the Select tool to move it. You can also add a marker from the
ground's right-click menu (**Add a marker here…**).

**◇ Markers** on the toolbar shows or hides the markers in the viewport. **Go to marker…** centres the view on the
marker you pick.

Questions: how do I add a marker? how do I move a marker? how do I find a marker in a big scene?

## Height tool

**Height** shapes the ground of a built place: drag over the ground to change it. Its options show on the toolbar
while it's on:

- **Raise**, **Lower** or **Level**: what the brush does to the ground it passes over.
- **Radius** (0–12 cells): how far around the cursor the ground moves.

Characters walk between heights only where there's a ramp. Right-click the ground and choose **Ramp here (walk
between heights)** to add one. Everywhere else, a height difference is a cliff.

Questions: how do I raise terrain? how do I flatten the ground? how do characters climb between heights?

## Undo, Redo, Save, Frame and Play buttons

The second group on the toolbar repeats the most used commands: **Undo** (Ctrl+Z), **Redo** (Ctrl+Y), **Save**
(Ctrl+S; a dot on it means there are unsaved changes; off during Play) and **Frame** (F: fits the whole scene in the
view). The big **▶ Play** button at the right starts a play-test. While playing it becomes **■ Stop**. Esc in the
viewport also stops.

See also: play-mode.md, undo-and-clipboard.md.

Questions: where is the play button? how do I stop playing?
