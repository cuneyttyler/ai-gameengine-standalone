# Menus

Every menu in the AIGE editor's menu bar (File, Edit, View, Play, Help) and every item in it, plus the right-click
menus of the viewport and of panel tabs: what each item does and its default shortcut.

## The menu bar

The menu bar is the thin strip at the very top of the editor window. From left to right it shows the AIGE cube, then
the menus **File**, **Edit**, **View**, **Play** and **Help**, and at the right end the name of the open scene. A dot
(•) after the scene name means the scene has unsaved changes. The Windows title bar shows the same thing as
"project — scene* - AIGE", where the * means unsaved changes.

Click a menu name to open it, and click it again to close it. While one menu is open, moving the mouse onto another
menu name opens that one instead, as in other desktop programs. Each item shows its keyboard shortcut on the right.
If you change a shortcut in Help ▸ Keyboard Shortcuts, the menus show the new keys straight away. Greyed-out items
can't be used right now: most editing items are off while the game is playing (Play mode), and items that act on the
selection are off when nothing is selected. Items that delete something ask you to confirm first.

See also: shortcuts.md, play-mode.md.

Questions: where is the menu bar? why is a menu item greyed out? what does the dot after the scene name mean?

## File menu

The **File** menu holds projects, scenes, saving, exporting and the settings windows:

- **New Project…** (Ctrl+N): opens the New Project window to make a new project.
- **Open Project…** (Ctrl+O): pick a project folder with the system folder picker. The editor restarts on that
  project, asking first about unsaved changes. A folder that isn't an AIGE project is refused with a notice.
- **New Game with the Planner…**: opens the Planner panel, where you describe a whole game and the AI designs it.
- **Recent Projects ▸**: a submenu of up to 10 recently opened projects. Hover an entry to see its full path.
- **New Scene…**: asks for a scene name and a type (Assembled or Paint), creates `scenes/<name>.aige` and opens it.
- **Scenes…**: brings the Project panel's Scenes tab to the front.
- **Save** (Ctrl+S): saves the open scene. It also saves the Project Settings (`settings.aige`) and the panel layout
  (`editor-layout.json`) if they changed.
- **Save Project As…** (Ctrl+Shift+S): copies the whole project under a new name and opens the copy.
- **Close Project**: back to the welcome window, after asking about unsaved changes.
- **Export Game…**: makes a stand-alone copy of the game that runs without the editor.
- **Project Settings…**: the project-wide settings window.
- **Preferences…**: your own settings on this computer.
- **Ontology…**: opens the Ontology window (the place vocabulary the AI builds places from).
- **Exit**: closes the editor, asking about unsaved changes first.

New, Open, the Planner, Recent, New Scene, Save, Save As and Export are greyed out during Play.

See also: projects.md, exporting.md, preferences.md, project-settings.md.

Questions: how do I save? how do I open a recent project? where is Export Game? how do I close a project?

## Edit menu

The **Edit** menu works on the open scene and its selection:

- **Undo** (Ctrl+Z) and **Redo** (Ctrl+Y or Ctrl+Shift+Z): step back and forward through scene edits.
- **Replace prefab…**: opens the Replace prefab dialog, to swap every copy of one prefab for another.
- **Background Image…**: use a picture as the scene's ground (a top-down picture of the whole site), with or
  without the AI.
- **Copy** (Ctrl+C): copies the selected entity to the editor's clipboard.
- **Paste at cursor** (Ctrl+V): pastes the copy at the grid cell under the mouse.
- **Bigger** (] or +) and **Smaller** ([ or -): resize the selection by 10%.
- **Bring forward** (Ctrl+]), **Send backward** (Ctrl+[), **Bring to front** (Ctrl+Shift+]), **Send to back**
  (Ctrl+Shift+[): change a flat thing's drawing order among the ground pieces. The order goes from -9 to 9; "to front"
  sets 9 and "to back" sets -9. Only flat (lying-on-the-ground) things use this order.
- **Duplicate** (Ctrl+D): makes a copy of the selection beside it. Tiles and the player can't be duplicated.
- **Delete** (Delete or Backspace): deletes the selection. If several things or cells are selected, all of them go.

Copy, Bigger, Smaller, the ordering items, Duplicate and Delete need something selected. Replace prefab and
Background Image are off during Play.

See also: undo-and-clipboard.md, viewport.md.

Questions: how do I copy and paste an entity? how do I send something behind the ground? how do I replace every copy
of a prefab?

## View menu

The **View** menu controls the panels and what the viewport draws:

- **One entry per panel**: Viewport, Scene, Assets, Project, Generate, AI, Build, Planner. A ✓ marks the panels that
  are open. Each entry has a submenu with **Show** or **Hide** (with the panel's shortcut), then **Dock left edge**,
  **Dock top edge**, **Dock right edge** and **Dock bottom edge**.
- **Reset layout**: puts every panel back where it starts.
- **Frame scene** (F): zooms and centres the viewport on everything in the scene.
- **Frame stats** (F3): shows or hides frame timing over the viewport. F3 also works during Play.
- **Physics shapes (colliders, triggers, bodies)**: draws the physics shapes over the scene.
- **Tile grid on empty ground** (G): shows the borders of the tile cells where there's no ground yet. A ✓ shows when
  it's on.
- **Paths (and, in Play, where each character is walking)**: draws every path. When it's off, only the selected
  path shows.
- **Selected building's interior / exterior** (H): steps the selected building through its interior floors and back
  to its outside.
- **All interiors / exteriors** (Shift+H): every building in the scene shows its inside, or every building shows its
  outside.
- **Floor up** (PageUp) and **Floor down** (PageDown): one floor up or down, for the selected building or for every
  building.

The interior items are off during Play.

See also: panels-and-layout.md, viewport.md.

Questions: how do I show a hidden panel? how do I turn the grid on? how do I see colliders? how do I look inside a
building?

## Play and Help menus

The **Play** menu:

- **Play** / **Stop** (F5): starts a play-test of the open scene, or stops it. The label changes to Stop while
  playing.
- **Save game** (F6): saves the game being played to the quick-save slot. Only works while playing.
- **Load saved game** (F9) while playing, or **Continue from save** (F9) while editing: loads the quick save. When
  you aren't playing, it starts Play first. It's greyed out until a save exists.

The **Help** menu:

- **Keyboard Shortcuts…** (F1): the list of every shortcut, where you can also change them.
- **Documentation**: opens the editor's documentation folder in Windows Explorer.

You can also ask the AI panel questions about the editor: it answers from this help.

See also: play-mode.md, shortcuts.md.

Questions: how do I play my game? how do I load a saved game? where are the keyboard shortcuts?

## The viewport's right-click menu on a thing

Right-click a thing in the viewport (or in the Scene Tree) and it gets selected and a menu opens. Which items show
depends on the thing:

- **Draw with <prefab>**: makes its prefab the brush, so you can paint or place more of it.
- **Show interior / Show exterior**, **Show floor 2…**, **Floors ▸**: for a building with a cutaway interior.
- **Animate a part…**: animate a part of its picture (a door, a window, a sign).
- **Animate the interior…**: find and animate the moving things inside (needs the AI).
- **Add interior: cutaway** / **Add interior: its own scene**: for a thing with no interior yet.
- **Open interior scene**, **Build its interior scene…** / **Build its interior scene again…**: for buildings whose
  inside is a separate scene.
- **Replace prefab…**: make this one (or every copy) a copy of another prefab.
- **Duplicate <name>** (Ctrl+D), **Copy** (Ctrl+C), **Bigger**, **Smaller**, **Turn a quarter** (R), **Bring
  forward (ground)**, **Send backward (ground)**.
- **Save as prefab…**: asks for a file name (letters, digits, - and _). This thing becomes the prefab's first copy.
  It's off for things that are already prefab copies.
- **Revert to prefab**: drops this copy's own changes.
- **Ask AI about this…**: puts the thing into the AI panel's request (needs the AI to be ready).
- **Delete** (Delete).

See also: viewport.md, undo-and-clipboard.md.

Questions: how do I make a prefab from an entity? how do I revert a prefab copy? how do I ask the AI about one thing?

## The viewport's right-click menu on the ground and on paths

Right-click empty ground (or a ground tile) to get the ground menu:

- **Draw with <tile>**: when there's a ground tile there, makes it the brush.
- **Duplicate** the tile, and **Make an asset from this cell**: on a built place's painted ground, cuts out that
  cell's picture as a tile prefab you can paint elsewhere.
- **Paint/Place <brush> here**: puts the current brush there. It's off if no prefab is picked.
- **Paste here** (Ctrl+V): pastes the clipboard at that cell.
- **Add a marker here…**: asks for a name (for example `village_well`) and adds a marker. Routines, packages,
  gateways and schedules send characters to markers.
- **Ramp here (walk between heights)**: characters can walk between heights only where there's a ramp.
- **Connect dialogues to characters**: gives each dialogue that nobody uses to the character with its name, so "Talk
  to" shows.
- **Show interiors (N)** / **Show exteriors** (Shift+H): when the scene has buildings with interiors.
- **Frame scene** (F).

Right-click a **path point** to get its menu: **Branch from here (a fork)**, **Draw on from here**, **Wait here…**
(seconds), **Play a clip here…**, **Use a part here…**, **Label…**, **Delete point** and **Finish drawing**.

See also: toolbar-and-tools.md, viewport.md.

Questions: how do I add a marker? how do I paste at a specific cell? how do I make a ramp? how do I branch a path?

## A panel tab's right-click menu

Right-click any panel's tab (the name strip on top of a panel) to get:

- **Dock left edge**, **Dock top edge**, **Dock right edge**, **Dock bottom edge**: move the panel along that edge of
  the window.
- **Hide** (with the panel's shortcut): take the panel out of the layout. It shows on the left side rail, ready to
  bring back.
- **Maximize (double-click the tab)**: the panel fills the whole window.
- **Reset layout**: every panel back where it starts.

See also: panels-and-layout.md.

Questions: how do I dock a panel on the right? how do I hide a panel? how do I maximise a panel?
