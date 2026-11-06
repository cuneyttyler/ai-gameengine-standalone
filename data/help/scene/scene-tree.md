# The Scene panel, the Scene Tree and selecting things

How the Scene panel is laid out, what the Scene Tree lists and how it groups entities, how to select one or many
things (in the tree and the viewport), the right-click menus for things and for the ground, and the editing shortcuts
for what's selected.

## The Scene panel

The **Scene panel** holds the **Scene Tree** and the **Inspector**, one above the other when the panel is tall and side
by side when it's wide. Drag the splitter between them to give either more room; the split is remembered with the
layout. Show or hide the Scene panel with **B** (or the panel's entry in the menu bar).

- The **Scene Tree** (top or left) lists the project's scenes, with the open scene expanded into its entities.
- The **Inspector** (bottom or right) shows what's selected: its components and fields. With nothing selected it says
  "Select something in the viewport or Scene Tree to inspect it." and offers **Project Settings…**.

See also: inspector.md, scenes.md.

Questions: where is the scene tree? where is the inspector? how do I show the scene panel? how do I make the inspector
bigger?

## What the Scene Tree shows

At the top, **Scenes** with a **+** button (new scene). Below it, every scene of the project in name order. Scenes
before and after the open one are rows with **▸**; click one to open it (disabled while playing). A **★** marks the
start scene. If a scene shows a World Bible place, its place's name is shown beside it.

The open scene's row has **▾** (click to fold or unfold it) and the number of entities in it. Under it, its entities
are grouped:
- **Characters**: the player, enemies, NPCs and scripted NPCs;
- **Props**: props, pickups and decorations;
- **Tiles**: hand-painted ground tiles. Tiles vastly outnumber the rest, so this group starts folded.

Each group header shows its count; click it to fold or unfold. Each entity is listed by its label (its Name, or a
description when it has none). How an entity is grouped depends on its components: a Tile is a tile; a Pickup is a
pickup; something with Velocity2D and Collider2D is the player; a Faction of Enemy is an enemy; a LuaScript makes a
scripted NPC; a WorldTransform2D with a Faction is an NPC; with only a WorldTransform2D a prop; anything else a
decoration. Ground cells of a built place's tile layers are not entities and aren't listed.

Questions: why are tiles hidden in the scene tree? what are the groups in the scene tree? why is my NPC listed under
props? where are my ground tiles?

## Selecting things

- **Click** an entity in the Scene Tree, or click it in the viewport with the **Select** tool (**V**). The Inspector
  shows it, its prefab card is highlighted in Assets ▸ Prefabs, and the tree unfolds its group and scrolls to it.
- **Click empty ground** without moving: selects the ground cell there (a built place's tile layer cell shows as
  "Ground cell (x, y)" in the Inspector). **Drag on empty ground** pans the view.
- **Shift+drag** from empty ground: a box selection. The Inspector says how many things and ground cells are in it
  ("N thing(s), M ground cell(s)") with **Delete them (Delete)**.
- **Ctrl+click** a thing adds it to the selection or takes it off.
- Clicking a **marker** or a **path point** where no picture is selects the marker, or that path.
- **Escape** deselects and goes back to the Select tool.

There is no search box in the Scene Tree; to find a marker use the toolbar's **Go to marker…**, and to find a prefab's
copies use the Prefabs tab. To rename an entity, select it and edit **Name ▸ value** in the Inspector; the tree's label
follows.

Questions: how do I select several objects? how do I box select? how do I deselect? how do I rename an entity? can I
search the scene tree?

## Moving, copying and changing selected things

With something selected (Select tool):
- **Drag** it in the viewport to move it; **arrow keys** nudge it a cell (**Shift**: a tenth of a cell).
- **Ctrl+C** copies, **Ctrl+V** pastes at the cursor, **Ctrl+D** duplicates; **Delete** or **Backspace** deletes.
- **]** or **+** makes it bigger, **[** or **-** smaller (**}** and **{** a little); **Alt+wheel** resizes too. Sprite
  and collision scale together.
- **R** turns it (a flat thing a quarter turn; an upright picture is mirrored), **Shift+R** the other way; **Shift+wheel**
  turns too. A character's facing turns an eighth with R.
- **Ctrl+]** / **Ctrl+[** bring a flat ground piece forward or send it backward among the ground pieces;
  **Ctrl+Shift+]** / **Ctrl+Shift+[** to front or back.
- **Ctrl+Z** undo, **Ctrl+Y** or **Ctrl+Shift+Z** redo. Each edit is one undo step. Opening another scene clears the
  undo history.

These are also in the **Edit** menu (Copy, Paste at cursor, Bigger, Smaller, Bring forward, Send backward, Bring to
front, Send to back, Duplicate, Delete). Keys can be rebound; **F1** lists them.

Questions: how do I move an object? how do I resize a sprite? how do I rotate or mirror a prop? how do I copy and paste
entities? what are the shortcuts in the scene?

## The right-click menu for a thing

Right-click an entity in the Scene Tree, or a thing in the viewport, for its menu. Depending on what it is:
- **Draw with <prefab>**: its prefab becomes the brush.
- **Show interior / Show exterior**, **Show floor k**, **Floors ▸ N**: for a building with a cutaway interior.
- **Animate a part…**: animate a door, window or sign of its picture.
- **Animate the interior…**: animate the moving things inside a cutaway (needs the AI service).
- **Add interior: cutaway** / **Add interior: its own scene**: for a thing with no interior yet.
- **Open interior scene**, **Build its interior scene…** (again…): for a building whose interior is a scene.
- **Replace prefab…**: put a copy of another prefab in its place.
- **Duplicate <name>** (Ctrl+D), **Copy**, **Bigger**, **Smaller**, **Turn a quarter**, **Bring forward (ground)**,
  **Send backward (ground)**.
- **Save as prefab…** (only when it isn't a prefab's copy yet), **Revert to prefab** (only for a copy).
- **Ask AI about this…** (when the AI service is ready).
- **Delete**.

Questions: what's in the right-click menu? how do I make a prefab from an object in the scene? how do I ask the AI
about an object?

## The right-click menu for the ground

Right-click empty ground (or a ground tile) in the viewport:
- **Draw with <tile>**: the tile there becomes the brush; **Duplicate** for a tile entity there.
- **Make an asset from this cell**: on a built place's ground, its picture made into a tile prefab to paint elsewhere.
- **Paint / Place <brush> here**: the current brush put on that cell.
- **Paste here** (Ctrl+V).
- **Add a marker here…**: asks a name.
- **Ramp here (walk between heights)**: characters can walk between heights only where a Ramp is.
- **Connect dialogues to characters**: gives each dialogue that no one has (no Talk) to its character by name.
- **Show interiors (N)** / **Show exteriors**: when the scene has buildings with interiors (Shift+H).
- **Frame scene** (F): fits the whole scene in the view.

Right-click on a path's point opens the path point menu instead (see markers-and-paths.md).

Questions: how do I place a prefab at a specific cell? what does Connect dialogues to characters do? how do I add a ramp?
