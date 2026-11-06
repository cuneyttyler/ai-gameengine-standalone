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

### Example: jumping to the chapel in a big village scene

The scene `millbrook` is large and you want to work near the chapel, which has a marker `chapel_door`.

1. On the Viewport panel's toolbar, click Go to marker….
2. Type `chap` in the list's search box and click `chapel_door`.
3. The view centres on the chapel door. If Go to marker… isn't on the toolbar, the scene has no markers yet; add one
   with the Marker tool first.

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

### Example: clearing a cluttered corner of the market

Five crates, two sacks and some cobble tiles fill a corner of `harbour_market` and you want them gone.

1. Press V for the Select tool.
2. Hold Shift and drag a box around the corner. The crates and sacks inside are selected, plus the ground cells
   whose middle is inside the box.
3. Check the Inspector: it shows what the selected things share.
4. Press Delete. Everything in the box is removed; Ctrl+Z brings it all back.

### Example: lining up three market stalls

1. Click the first stall, then Ctrl+click the second and third so all three are selected.
2. Press the Left arrow twice: all three move two cells left together.
3. Press Shift+Right arrow three times to push them back three tenths of a cell for a fine fit.

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

### Example: painting a dirt road with neat edges

1. In Assets ▸ Prefabs, click the dirt terrain tile. The brush button reads "Paint dirt" (with your tile's name).
2. Tick Auto-fit on the toolbar.
3. Press Shift+wheel up until the brush covers 2×2 cells.
4. Drag across the grass from the village gate to the well. The road is painted, and its edges and corners are picked
   to match the grass around it.
5. One stroke went too far: press E for Erase and drag over the extra cells; Auto-fit puts back the plain ground and
   refits the edges.

### Example: placing a row of big oak trees

1. Press 3 to pick the third prefab in the palette, the oak tree. The brush button reads "Place oak".
2. Press ] four times: each press makes the brush 10% bigger, so Size % goes up to about 146%, and the ghost
   preview grows.
3. Press Shift+R to face the tree mirrored, so the row doesn't look copied.
4. Click five times along the river bank. Each click places one oak at that size, and the last one placed is selected.

Questions: how do I paint tiles? how do I place a prefab? how do I change the brush size? why does placing the player
move it?

## Erase tool

**Erase** (E) removes what you click or drag over. It deletes the thing under the cursor. Where there's no thing, it
clears that cell of the tile layer (the ground). With Auto-fit on, erasing a terrain tile puts back the terrain's plain
ground and refits the neighbouring pieces instead of leaving a hole. One drag is one undo step, so Ctrl+Z brings back
everything that stroke erased.

To delete only what's selected, use the Select tool and press Delete instead.

### Example: rubbing out a stray patch of water

1. Press E (or click Erase on the toolbar).
2. Drag over the three water cells that ended up in the middle of the field.
3. The cells are cleared (or turned back into the terrain's plain ground if Auto-fit is on). Erased the fence too by
   mistake? Press Ctrl+Z once: the whole stroke comes back, so erase again more carefully.

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

### Example: a night watch patrol around the square

Captain Orrin Vale should walk a loop around the market square.

1. Click Path on the toolbar.
2. Click the ground at the four corners of the square in turn: the path `path-1` gets a point at each cell.
3. Shift+click the first point to join the last corner back to it, closing the loop.
4. Press Enter (or click Finish path).
5. Select the path, and in the Inspector rename it `night_watch`. Packages that use it follow the rename.
6. Right-click the corner by the well, click Wait here… and type `8`: Orrin pauses there for 8 seconds.

Then ask the AI panel:

> "Captain Orrin Vale patrols the path night_watch from 20:00 to 06:00."

### Example: a fork where the path splits to the mill

1. With the Path tool on, click a point halfway along the road path to pick it.
2. Click the ground toward the mill: a new branch starts from the picked point.
3. Keep clicking to the mill door, then press Enter.

Questions: how do I draw a path for an NPC? how do I make a fork? how do I finish a path? how do I make a character
wait at a point?

## Marker tool

**Marker** adds named places to the scene. Click a cell and the editor asks for a name (for example `village_well`).
Packages, gateways and schedules send characters to markers by name, and gateways use them as arrival points. Click an
existing marker to select it, then drag it with the Select tool to move it. You can also add a marker from the
ground's right-click menu (**Add a marker here…**).

**◇ Markers** on the toolbar shows or hides the markers in the viewport. **Go to marker…** centres the view on the
marker you pick.

### Example: an arrival point for the harbour gateway

Players coming from the road scene should arrive at the harbour's north gate.

1. Click Marker on the toolbar.
2. Click the cell just inside the north gate and type `north_gate_arrival`.
3. The marker shows as ◇ in the viewport. To nudge it, press V, click the marker and drag it.
4. Point the gateway in the road scene at `north_gate_arrival`, for example by asking the AI panel:

> "Make the east end of the road a gateway to harbour_market, arriving at north_gate_arrival."

Questions: how do I add a marker? how do I move a marker? how do I find a marker in a big scene?

## Height tool

**Height** shapes the ground of a built place: drag over the ground to change it. Its options show on the toolbar
while it's on:

- **Raise**, **Lower** or **Level**: what the brush does to the ground it passes over.
- **Radius** (0–12 cells): how far around the cursor the ground moves.

Characters walk between heights only where there's a ramp. Right-click the ground and choose **Ramp here (walk
between heights)** to add one. Everywhere else, a height difference is a cliff.

### Example: a hill for the windmill with a way up

1. Click Height on the toolbar, choose Raise and set Radius to 4.
2. Drag in small circles where the windmill will stand until the ground is a hill.
3. Choose Level and drag over the top so the windmill has a flat place to sit.
4. Right-click the ground at the foot of the hill, on the side facing the village, and click Ramp here (walk between
   heights).
5. Press F5 to test: the miller can walk up the ramp; everywhere else the hill's edge is a cliff.

Questions: how do I raise terrain? how do I flatten the ground? how do characters climb between heights?

## Undo, Redo, Save, Frame and Play buttons

The second group on the toolbar repeats the most used commands: **Undo** (Ctrl+Z), **Redo** (Ctrl+Y), **Save**
(Ctrl+S; a dot on it means there are unsaved changes; off during Play) and **Frame** (F: fits the whole scene in the
view). The big **▶ Play** button at the right starts a play-test. While playing it becomes **■ Stop**. Esc in the
viewport also stops.

See also: play-mode.md, undo-and-clipboard.md.

### Example: a quick test after moving the tavern door

1. You moved the tavern's door prop; the toolbar's Save button shows a dot. Click Save: the dot goes.
2. Click ▶ Play at the right end of the toolbar. It turns into ■ Stop and the toolbar's bottom edge turns blue.
3. Walk the player through the door.
4. Click ■ Stop (or press Esc in the viewport). You are back in the editor, and Frame fits the whole scene again if
   you lost your place.

Questions: where is the play button? how do I stop playing?
