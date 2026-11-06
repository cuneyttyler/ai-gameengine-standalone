# Projects

Working with AIGE projects: the welcome window, making a new project (the New Project window and its templates),
opening, saving, Save Project As, closing, scenes, crash recovery of unsaved work, and what's in a project folder.

## The welcome window

When the editor starts without a project, it shows the **welcome window**:

- **New Project…**: the New Project window.
- **Open Folder…**: pick a project folder. A folder that isn't a project (no scenes, settings or prefabs) is refused
  with a message.
- **Open the demo**: opens the demo project shipped beside the editor. If there isn't one, it makes a fresh demo from
  the isometric action RPG template in your user folder (`%LOCALAPPDATA%\aige\demo`) and opens that.
- **Recent projects**: click one to open it. Each shows its name, path and when it was last opened. **×** removes it
  from the list (the folder stays). A project whose folder has gone is marked **missing**.
- **Open the last project directly at launch**: skip this window next time. File ▸ Close Project brings it back.

Opening a project starts the editor on it; the welcome window closes.

### Example: tidying the recent list and opening a project from a USB stick

1. Start the editor. The welcome window lists Recent projects; "Old Jam Entry" is marked missing because you deleted
   its folder.
2. Click × beside "Old Jam Entry". It leaves the list (nothing on disk is touched).
3. Click Open Folder… and pick `E:\Projects\Lanternfall`. The editor starts on Lanternfall and the welcome window
   closes.
4. Picked `E:\Projects` by mistake? It's refused with a message, since it has no scenes, settings or prefabs.

Questions: how do I open a project? how do I remove a project from the recent list? how do I skip the welcome screen?
where is the demo?

## Making a new project

Open the **New Project** window from the welcome window or with **File ▸ New Project…** (Ctrl+N). Its fields:

- **Name** and **Location** (**Browse…**): the project is created as `<location>\<name>`. The folder must be new or
  empty. A name can't contain `\ / : * ? " < > |`. The last folder you used is offered next time.
- **Grid**: Isometric (diamond cells) or Top-down (square cells).
- **Template** (only from File ▸ New Project inside the editor): Blank project, Action RPG (isometric), RPG
  (top-down), Action-adventure, or Life sim.
- **Build method**: Paint (painted whole, marked by you) or Assembled (each piece's art). The default comes from
  Preferences ▸ New projects.
- **AI** (this project's own, changed later in Project Settings ▸ AI): LLM provider, LLM model, prefab art,
  background and paint model. "Use app default" leaves each one following your Preferences.

Click **Create**. The editor makes the project and opens it.

### Example: a new top-down farming game, "Wheatmoor"

1. Click File ▸ New Project… (Ctrl+N).
2. Type Name `Wheatmoor`, click Browse… and pick `D:\Games` as the Location. The project will be `D:\Games\Wheatmoor`.
3. Set Grid to Top-down and Template to Life sim.
4. Set Build method to Assembled, so every building and crop gets its own art you can move.
5. Leave the AI choices on "Use app default", so they follow your Preferences.
6. Click Create. The editor makes Wheatmoor and opens it; press F5 to walk around the starting field.

A name like `Wheat:moor` is refused, since `:` can't be in a folder name.

Questions: how do I make a new project? how do I make a top-down game? can I choose an AI model per project?

## Project templates

The templates in File ▸ New Project make a project that plays straight away:

- **Action RPG (isometric)**: diamond tiles, real-time melee, an elder's first quest, a wolf to fight.
- **RPG (top-down)**: square tiles, a village elder, a first quest and a foe on the road.
- **Action-adventure**: top-down rooms, a guide, a monster and a potion.
- **Life sim**: top-down, a field to tend, a shopkeeper, a neighbour's first errand.

Each starts with a walled field, the player, a character to talk to with a dialogue and a first quest, a `well`
marker, and its genre's own extras: an enemy and a health pickup in the action templates (whose player can fight), or
turnips to pick up and a shopkeeper in the life sim. It also gets items, achievements, a theme and game.json. Press F5
to play it at once. **Blank project** makes the empty folders and the settings only.

### Example: a playable action RPG in a minute

1. Click File ▸ New Project…, name it `Ashen Vale`, keep Grid Isometric and pick Template Action RPG (isometric).
2. Click Create, then press F5.
3. You start in a walled field. Walk to the elder and press E (interact) to talk; he gives the first quest.
4. Fight the wolf with the left mouse button (attack). Take the health pickup if you're hurt.
5. Press F5 to stop. Now change it to your own game, for example by asking the AI panel:

> "Rename the elder to Hesk Morrow and make the wolf a pair of bandits."

Questions: what templates are there? which template has combat? what does a blank project contain?

## Opening, saving and closing

- **File ▸ Open Project…** (Ctrl+O) or **File ▸ Recent Projects ▸**: the editor asks about unsaved changes, then
  restarts itself on the other project, so everything (assets, prefabs, the AI service, undo) starts clean.
- **File ▸ Save** (Ctrl+S, or the toolbar's Save): saves the open scene to its `.aige` file. If they changed, it also
  saves the Project Settings (settings.aige) and the panel layout (editor-layout.json). You can't save during Play
  ("Stop Play mode before saving"). If a save fails, a notice says why and the scene stays unsaved.
- **Unsaved changes** show as a dot after the scene name (menu bar, status bar, the Save button) and a * in the
  window title. Opening another scene or project, closing the project, or exiting asks "Save changes…?" with Yes, No
  and Cancel. If your edits were all undone and the scene matches its file, it doesn't ask.
- **File ▸ Close Project**: stops Play if running, asks about changes, and goes back to the welcome window.
- **File ▸ Exit**: closes the editor, asking about unsaved changes first.

### Example: switching from "Lanternfall" to "Wheatmoor" without losing work

1. In Lanternfall you've placed some lamp posts; the menu bar shows `main •`.
2. Click File ▸ Recent Projects ▸ Wheatmoor.
3. The editor asks "Save changes…?". Click Yes: the scene is saved, then the editor restarts on Wheatmoor with a
   clean undo history.
4. If you had pressed F5 first, saving would have failed with "Stop Play mode before saving"; press F5 to stop,
   then save.

Questions: how do I save? why does it ask to save when I didn't change anything? why can't I save while playing? how
do I switch projects?

## Save Project As

**File ▸ Save Project As…** (Ctrl+Shift+S) makes a copy of the whole project:

1. Type a name for the copy (it suggests "<name>-copy").
2. Pick the folder to put it in. The copy goes to `<folder>\<name>`, which must be new or empty.
3. The editor copies every file, except the `.cache` folder and the AI log. It writes the open scene into the copy
   as it is now, saved or not, plus the current settings, then opens the copy.

The original project's files are left as they were.

### Example: trying a risky redesign on a copy

You want to try turning Wheatmoor's village into a walled town without touching the real project.

1. Click File ▸ Save Project As… (Ctrl+Shift+S).
2. The name offered is `Wheatmoor-copy`; change it to `Wheatmoor-walled`.
3. Pick `D:\Games` as the folder. The copy goes to `D:\Games\Wheatmoor-walled`.
4. The editor copies everything except `.cache` and the AI log, writes the open scene as it is now (unsaved edits
   included), and opens the copy. The original Wheatmoor stays as it was on disk.

Questions: how do I duplicate a project? does Save As keep my unsaved changes? where does Save As put the copy?

## Scenes in a project

A project holds many scenes (`scenes/<name>.aige`), with one open in the editor at a time. The **start scene** is
where the game begins (Project Settings ▸ Game ▸ startScene).

- **File ▸ New Scene…**: a name and a type (**Assembled**: each building, tree and prop its own art; **Paint**: the
  place painted whole as one picture, then marked). The scene is created empty and opened. The type offered first is
  Project Settings ▸ Places ▸ method.
- **File ▸ Scenes…**: the Project panel's Scenes tab. The Scene panel also lists the scenes at the top of its tree.
- **A scene's right-click menu**: Open, Set as start scene, Rename… (gateways that lead to it, and the start scene
  setting, follow), Duplicate…, Type ▸ Assembled / Paint, New scene…, and Delete… (deletes the file; not allowed for
  the start scene or the open scene).

Opening another scene stops Play, asks about unsaved changes, and clears the undo history.

### Example: renaming the first scene and making the tavern the start

1. Click File ▸ Scenes…. The Project panel's Scenes tab lists `main` and `tavern`.
2. Right-click `main`, click Rename… and type `village_green`. Gateways that lead to it, and the start scene setting,
   follow the rename.
3. Right-click `tavern` and click Set as start scene. A new game now begins in the tavern.
4. Try to delete `tavern` with Delete…: it isn't allowed, because it's the start scene. Set another start scene
   first.

Questions: how do I add a scene? how do I set the start scene? how do I rename a scene? why can't I delete this
scene?

## Crash recovery of unsaved work

While the open scene has unsaved changes, the editor writes a copy of it to the project's `.cache/recovery` folder
every two minutes (Preferences ▸ General ▸ Keep unsaved changes: every 1, 2, 5 or 10 minutes, or never). It never does
this during Play.

If the editor didn't close properly (a crash, or it was killed), then the next time the project opens and that copy
is newer than the scene file, a dialog asks **"Recover unsaved changes?"**, naming the scene and when the changes are
from. **Restore them** loads them (opening that scene if needed). They stay **unsaved until you save**, and the undo
history starts fresh. Cancel discards them. Saving the scene normally removes the recovery copy.

### Example: getting back an hour of village work after a power cut

1. Your laptop switched off while you were laying out `village_green`, unsaved.
2. Start the editor and open the project. A dialog asks "Recover unsaved changes?", naming `village_green` and when
   the changes are from.
3. Click Restore them. The scene opens with your changes (up to the last recovery copy, every 2 minutes by default).
4. The menu bar shows a dot: the changes are unsaved. Press Ctrl+S to keep them. Undo starts fresh, so earlier steps
   can't be undone.

To save recovery copies more often, set File ▸ Preferences… ▸ General ▸ Keep unsaved changes to every minute.

Questions: the editor crashed, can I get my work back? how often is recovery saved? where is the recovery file? how
do I turn off autosave?

## What's in a project folder

A project is a folder. Every path stored inside it is relative to that folder, so you can move or copy it freely.

- `settings.aige`: Project Settings. `input.json`: the game's input map (Project Settings ▸ Input).
- `scenes/`: the scenes (`*.aige`). `scenes/main.aige` is the usual first scene.
- `prefabs/`: placeable things (the Assets ▸ Prefabs palette).
- `assets/`: imported and generated art and audio, each with a `.meta`. `assets/icon.png` becomes the game's icon.
- `scripts/`: Lua gameplay scripts.
- `records/`: data records (items, packages, achievements…; Project ▸ Records).
- `world/`: the World Bible (canon). `dialogue/`, `quests/`, `schedules/`, `cutscenes/`: story and routines.
- `ui/`, `loc/`, `modules/`, `game.json`: the game's UI, localisation, modules and game description, when used.
- `editor-layout.json`: the panel layout. `README.md`: made with the project.
- `.cache/`: derived data, not part of the game: cooked assets, crash recovery, play-test saves (`.cache/saves`).

New blank projects get scenes, prefabs, scripts, assets, world, dialogue, quests and schedules folders.

See also: exporting.md.

### Example: giving the game its own icon and moving the project

1. Save a square picture of a lantern as `assets/icon.png` in the Lanternfall folder. It becomes the game's icon.
2. Close the editor and move the whole `Lanternfall` folder from `D:\Games` to `E:\Projects`. Every path inside is
   relative, so nothing breaks.
3. Open it with File ▸ Open Project… (or Open Folder… on the welcome window). Your scenes are in `scenes/`, the
   records in `records/`, and the panel layout comes back from `editor-layout.json`.
4. To make the folder smaller before zipping it for a friend, you can leave out `.cache/`: it's derived data, not
   part of the game (though play-test saves in `.cache/saves` go with it).

Questions: what folders does a project have? where are scenes stored? what is the .cache folder? can I move my
project folder?
