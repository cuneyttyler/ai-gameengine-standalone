# Scenes, the start scene and gateways

How a project's scenes work in the AIGE editor: the Scenes page of the Project panel, the scene list at the top of the
Scene Tree, creating, opening, renaming, duplicating and deleting scenes, scene types, the start scene, and gateways
and markers that connect scenes.

## What a scene is

A scene is one level or place of the game: a town, a cellar, a dungeon floor, the inside of an inn. A project holds
many scenes and the editor has one of them open at a time. Each scene is a file in the project's `scenes/` folder,
named `scenes/<name>.aige`; a scene's tile layers (the ground of a built place) are saved in a folder next to it.

Everything in a scene is an entity: the player, characters, buildings, trees, props, ground tiles, markers, paths and
gateways. Each entity is made of components (Sprite, WorldTransform2D, Health, Talk…) that you see and edit in the
Inspector.

The project's scenes are listed in two places:
- **Project ▸ Scenes** (the Scene Manager page of the Project panel; also **File ▸ Scenes…**);
- the top of the **Scene Tree** in the Scene panel, with the open scene expanded into its entities.

Save the open scene with **File ▸ Save**, the toolbar's **Save** button or **Ctrl+S**. A dot on the Save button means
there are unsaved changes.

See also: scene-tree.md, inspector.md, project-panel.md.

Questions: what is a scene? where are scenes saved? how many scenes can a project have? where do I see my scenes?

## The Scenes page (Project ▸ Scenes)

**Project ▸ Scenes** (also **File ▸ Scenes…**) lists every scene of the project. Each row shows:
- the scene's name (bold when it's open in the editor; hover for its file path);
- a **start** badge on the scene the game starts in;
- an **open** badge on the scene open in the editor;
- how many markers it has (hover to see their names).

At the top, **New scene…** makes a new scene. The hint says what to do: **double-click** a row to open that scene,
**right-click** for more. The right-hand side, **Gateways**, lists every gateway that leads nowhere (see "Gateway
problems"). When all is well it says: "Every gateway leads somewhere. Place the Gateway prefab, then set its scene and
marker in the Inspector."

New scene and Rename are disabled while the game is playing.

Questions: where is the scene manager? how do I see which scene is the start scene? how do I open another scene?

## Creating a new scene

Make a scene with **File ▸ New Scene…**, the **New scene…** button on Project ▸ Scenes, the **+** button at the top of
the Scene Tree, or **New scene…** in any scene's right-click menu.

The **New scene** dialog asks for:
- **a name** (for example `cellar`). The file becomes `scenes/<name>.aige`: the name is made lowercase, spaces, `-`
  and `_` become `-`, other characters are dropped, and it's cut to 48 characters. A name with no letters or digits is
  refused ("A scene needs a name with letters or digits"), and a name already used is refused ("already exists").
- **how it is built**: **Assembled** or **Paint** (see "Scene types"). The choice starts on the project's method
  (Project Settings ▸ Places ▸ Method).

Press **Create**: the scene is added to the list and opened, empty. An empty scene's tree says: "Empty scene. Paint its
ground with a tile prefab from Assets ▸ Prefabs, place things, and connect it to other scenes with a Marker and a
Gateway."

Questions: how do I create a new scene? how do I add a level? why is my scene name refused? what does New Scene do?

## Scene types: Assembled and Paint

Every scene says how it's built, which the Build panel and the AI Planner follow:
- **Assembled**: each building, tree and prop has its own art, put together on the grid over a painted background;
  every piece can be moved and edited. Build steps: Confirm, Layout, Plan, Assets, Result.
- **Paint**: the place is painted whole as one picture (or a picture you give), then marked: where one can walk and
  what a character goes behind. Only doors, chests, items and characters get art of their own. Build steps: Confirm,
  Layout, Plan, Paint, Mark, Assets, Result.

Where the type is set:
- in the **New scene** dialog;
- in a scene's right-click menu (Project ▸ Scenes or the Scene Tree): **Type: … ▸ Assembled / Paint** (the current one
  ticked);
- on the Build panel's first page: its Build method shows the open scene's type, and choosing the other changes the
  scene's type too;
- scenes the Planner makes take the plan's method.

The type is stored in the scene file's header (`# type Paint`). A scene without one uses the project's method
(Project Settings ▸ Places ▸ Method). A scene that was never saved can't keep a type: "Save the scene first: its type
is kept in its file".

See also: painted-places.md.

Questions: what is the difference between Assembled and Paint? how do I change a scene's type? what is a painted scene?

## Opening, renaming, duplicating and deleting scenes

Right-click a scene in **Project ▸ Scenes** or in the **Scene Tree** for its menu:
- **Open**: opens it in the editor (disabled for the open one and while playing). Double-click a row on the Scenes
  page, or click a scene's row in the Scene Tree, does the same.
- **Set as start scene**: the game starts here (see "The start scene").
- **Rename…**: a new name. Gateways that lead to it, and the start scene setting, follow the rename (every reference
  to the file is rewritten).
- **Duplicate…**: a copy under a new name (default `<name>-copy`). If you're copying the open scene with unsaved
  changes, it's saved first, so the copy is the scene as it is now. Its tile layers are copied too.
- **Type: …**: Assembled or Paint.
- **New scene…**.
- **Delete…**: deletes the file after a confirmation. It's disabled for the start scene and the open scene. The delete
  is also refused when gateways in other scenes lead to it ("gateways lead to it from …; change or remove them first").
  Deleting removes its tile layers too.

Opening another scene while the current one has unsaved changes asks you about saving first.

Questions: how do I rename a scene? can I copy a scene? why can't I delete a scene? how do I delete a level?

## The start scene

The start scene is where the game begins when it's played or exported. It's marked **start** on Project ▸ Scenes and
with a **★** in the Scene Tree.

To change it, right-click another scene ▸ **Set as start scene**. The editor says "<scene> is now where the game
starts". It's stored in the project settings (Project Settings ▸ Game ▸ startScene) and saved at once. With no start
scene set, the game starts in `scenes/main.aige`.

The start scene can't be deleted: make another scene the start first. Renaming the start scene keeps it the start.

Questions: how do I choose which scene the game starts in? what is the start scene? why can't I delete the main scene?

## Markers

A **Marker** is a named place in a scene (for example `village_well`, `herb_garden`, `cave_entrance`). It's invisible
in play. Markers are where gateways land the player, where packages and schedules send characters (Behaviour's home,
work and bed are markers), and what Go to marker jumps to.

To add one:
- the toolbar's **Marker** tool: click a cell, give the marker a name, and it's added there; click an existing marker
  to select it, then drag it with the Select tool;
- right-click the ground ▸ **Add a marker here…**.

The toolbar's **◇ Markers** toggle shows or hides markers in the viewport (a selected marker always shows, as a yellow
cell outline). **Go to marker…** (a searchable list on the toolbar) selects a marker and centres the view on it. Each
scene's marker count is on Project ▸ Scenes.

Rename a marker in the Inspector (Marker ▸ name). See also markers-and-paths.md.

Questions: what is a marker? how do I add a marker? how do I find a marker in a big scene? why can't I see my markers?

## Gateways: going from one scene to another

A **Gateway** is a way into another scene: when the player steps into its cell during play, the game loads the other
scene and puts the player at a marker there. The player (with what it carries) goes with them; it replaces the other
scene's own player.

To connect two scenes:
1. In the scene you arrive in, add a **Marker** where the player should appear (for example `from_village`).
2. In the scene you leave from, place the **gateway** prefab (a project starts with one in Assets ▸ Prefabs: a blue
   diamond until you give it art) where the way out is.
3. Select it and set, in the Inspector, **Gateway ▸ scene** (a searchable list of the project's scenes) and
   **Gateway ▸ marker** (the markers of that scene).
4. For a way back, do the same in the other direction.

If the marker isn't found when travelling, the player stays where the other scene's player was placed (or at the first
marker), and a warning says "no marker … in that scene".

A building whose interior is its own scene works the same way through its doorway (see buildings-and-interiors.md).

Questions: how do I connect two scenes? how do I make a door to another level? how does the player go to another map?
what is a gateway?

## Gateway problems

The **Gateways** column of Project ▸ Scenes lists each gateway that leads nowhere, with ⚠, for every scene in the
project:
- "<scene>: Gateway "<name>" has no scene to lead to": its scene field is empty;
- "… leads to scenes/x.aige, which doesn't exist": the scene was deleted or the name is wrong;
- "… arrives at marker "<name>", which scenes/x.aige doesn't have": the marker is missing or misspelt in the target.

Doorways of buildings whose interior is its own scene are checked the same way once linked ("The doorway of "<name>"
…"). A building whose interior scene isn't built yet is not a problem; stepping in during play says the interior isn't
built yet.

Fix one by selecting that gateway and choosing a scene and marker in the Inspector, or by adding the missing marker in
the other scene. The same problems are shown when the game is exported and by the AI panel's checks.

Questions: why does the scenes page show a warning? what does "leads to … which doesn't exist" mean? how do I fix a
broken gateway?
