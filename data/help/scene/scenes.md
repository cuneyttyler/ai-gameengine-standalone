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

### Example: finding the scenes of a village game

A small game has a village, the inn inside it and a cellar under the inn.
1. Open **Project ▸ Scenes**: three rows, `village`, `inn` and `cellar`. `village` has the **start** badge and, since
   it's the one being edited, the **open** badge too; hover a name to see its file, `scenes/village.aige`.
2. Look at the top of the Scene Tree: the same three scenes, with `village` expanded into its entities (Player, the
   Well, Vara's Inn, the oak trees, the markers).
3. Move the oak by the well a little, and see the dot appear on the toolbar's **Save** button.
4. Press **Ctrl+S**: the dot goes and `scenes/village.aige` is written.

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

### Example: checking a project's scenes before a test run

You want to know where the game will start and whether any door is broken.
1. Choose **File ▸ Scenes…** (or click **Scenes** in the Project panel).
2. Find the row with the **start** badge: `village`. The bold row with **open** is `cellar`, the scene you're editing.
3. Hover the marker count of `inn` (say "3 markers"): it lists `from_village`, `kitchen` and `vara_bed`.
4. Look at the **Gateways** column on the right. It reads "Every gateway leads somewhere…", so every way between the
   scenes works.
5. Double-click `village` to open it before pressing Play.

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

### Example: adding a cellar under the inn

1. Choose **File ▸ New Scene…** (or **New scene…** on Project ▸ Scenes).
2. Type the name `Inn Cellar`.
3. Leave how it's built on **Assembled** (the project's method) and press **Create**.
4. The file is `scenes/inn-cellar.aige` (lowercase, the space became `-`), and the new scene opens, empty, with the
   "Empty scene…" hint in the Scene Tree.
5. Try making another scene called `inn cellar`: it's refused as "already exists", because it makes the same file.

The new scene can now be painted with a tile prefab and joined to the inn with a Marker and a Gateway.

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

### Example: making the forest glade a painted scene

The project builds places Assembled, but the glade by the river should be one painted picture.
1. Save the scene `glade` first (**Ctrl+S**) if it was never saved.
2. In Project ▸ Scenes, right-click `glade` and choose **Type: Assembled ▸ Paint**.
3. Open it and go to the Build panel: its first page's Build method now shows Paint, and the steps are Confirm,
   Layout, Plan, Paint, Mark, Assets, Result.
4. The scene file's header now has the line:

```
# type Paint
```

The village and inn stay Assembled; only the glade is painted.

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

### Example: renaming the cave and trying a variant of it

1. In Project ▸ Scenes, right-click `cave` and choose **Rename…**; type `bat-cave` and confirm. The gateway in
   `village` that led to `scenes/cave.aige` now leads to `scenes/bat-cave.aige` without you touching it.
2. Right-click `bat-cave` ▸ **Duplicate…**, keep the name `bat-cave-copy` and confirm: a copy with the same entities and
   tile layers.
3. Try out a flooded version in the copy. You decide against it: right-click `bat-cave-copy` ▸ **Delete…** and
   confirm. (If you'd placed a gateway to the copy in another scene, the delete would be refused until that gateway was
   changed.)

Questions: how do I rename a scene? can I copy a scene? why can't I delete a scene? how do I delete a level?

## The start scene

The start scene is where the game begins when it's played or exported. It's marked **start** on Project ▸ Scenes and
with a **★** in the Scene Tree.

To change it, right-click another scene ▸ **Set as start scene**. The editor says "<scene> is now where the game
starts". It's stored in the project settings (Project Settings ▸ Game ▸ startScene) and saved at once. With no start
scene set, the game starts in `scenes/main.aige`.

The start scene can't be deleted: make another scene the start first. Renaming the start scene keeps it the start.

### Example: starting the game in a prologue scene

You've added a scene `shipwreck` where the story begins, before the village.
1. In Project ▸ Scenes (or the Scene Tree), right-click `shipwreck` and choose **Set as start scene**.
2. The editor says "shipwreck is now where the game starts"; the **start** badge moves to `shipwreck` and the **★** in
   the Scene Tree moves with it.
3. Project Settings ▸ Game ▸ startScene now reads `scenes/shipwreck.aige`.
4. Press Play: the game opens on the beach. `village` can now be deleted if you want, as it isn't the start any more.

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

### Example: marking the well where villagers meet

1. Open `village` and pick the toolbar's **Marker** tool.
2. Click the cell in front of the well, type the name `village_well` and confirm: the marker is added there.
3. Later, in a crowded village, open **Go to marker…** on the toolbar, type `well` and pick `village_well`: it's
   selected and the view centres on it.
4. If it doesn't show, switch on the toolbar's **◇ Markers** toggle.
5. To call it `well_square` instead, select it and change **Marker ▸ name** in the Inspector.

A character's **Behaviour ▸ work** (or home) can now be `village_well`, and a gateway can land the player there.

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

### Example: a ladder from the village down to the cellar

1. Open `cellar`, pick the **Marker** tool, click the foot of the ladder and name the marker `from_village`.
2. Open `village`. From **Assets ▸ Prefabs**, place the **gateway** prefab on the trapdoor behind the inn.
3. With it selected, set in the Inspector **Gateway ▸ scene** to `scenes/cellar.aige` and **Gateway ▸ marker** to
   `from_village`.
4. For the way back, add a marker `from_cellar` beside the trapdoor in `village`, and place a gateway on the top of the
   ladder in `cellar` with scene `scenes/village.aige` and marker `from_cellar`.
5. Press Play and walk onto the trapdoor: the cellar loads and the player stands at the foot of the ladder, carrying
   what they carried.

In the AI panel you can ask for the same: "Connect the trapdoor behind the inn to the cellar, both ways."

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

### Example: fixing a door after renaming a marker

You renamed the cellar's marker `from_village` to `ladder_foot`. Project ▸ Scenes now shows:

```
⚠ village: Gateway "Trapdoor" arrives at marker "from_village", which scenes/cellar.aige doesn't have
```

1. Open `village` and select the gateway `Trapdoor` (in the Scene Tree or the viewport).
2. In the Inspector, open **Gateway ▸ marker** and pick `ladder_foot` from the cellar's markers.
3. Save. Back on Project ▸ Scenes the warning is gone and the column reads "Every gateway leads somewhere…".

Renaming the marker back to `from_village` in the cellar would fix it too.

Questions: why does the scenes page show a warning? what does "leads to … which doesn't exist" mean? how do I fix a
broken gateway?
