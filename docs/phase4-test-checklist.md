# Phase 4 — Manual Test Checklist

The user-test checkpoints from Phase 4 (`development_plan.md`), plus 5.1 (desktop export),
for one end-of-phase pass. Automated coverage already checks the logic: 201 GoogleTest
and 70 pytest cases, among them reference rewriting on move/rename, the AI chat
surviving a restart, culling, Lua modules, and an exported game run headless. This list
is for what tests can't judge: whether it feels right, looks right and doesn't get in
your way.

Local models (was 4.7) and mobile/3D (was 5.2–5.4) moved to Later.

**Before you start**
- **Rebuild:** close the editor, then run `cmake --build build --config Debug` (this also
  builds the player, which Export needs).
- **Undo:** `git checkout projects/demo` resets the demo's committed files. Your own
  uncommitted demo changes are yours: commit or copy them first if you'll experiment.
- **Shortcuts:** Help ▸ Keyboard Shortcuts (**F1**) lists every shortcut.

## Notes from your testing (docs/notes.md): each should now be fixed

- [ ] **Delete a quest** in the Story tab: an in-editor confirmation appears (no
      "aige-editor.local says" box), the quest is deleted, and the window stays usable.
- [ ] **Saving, quitting, reopening keeps the AI conversation**: ask the AI something,
      save, close the editor, open it again. The chat is back, and a follow-up ("make
      them faster too") still understands what "them" means.
- [ ] **Resize prefabs**: select a skeleton and press **]** / **[** (Shift: finer) or
      **Alt+wheel**. The sprite and its collision grow together. The brush size % in the
      toolbar places new copies at that size.
- [ ] **Remove a prefab**: right-click a prefab card ▸ Delete prefab…. It asks first. Its
      copies in the scene stay, as ordinary entities.
- [ ] **Tile borders on empty ground**: press **G** (or View ▸ Tile grid). Outlines show
      where tiles could go; press G again to hide them.
- [ ] **Right-click menus** on an entity, on empty ground, in the Scene Tree, on prefab
      cards and on asset cards. Destructive items confirm first.
- [ ] **Menu bar**: File (New/Open/Recent Projects, Save, Save Project As…, Export Game…,
      Preferences…), Edit, View, Play, Help. Menus open over the viewport and aren't
      hidden behind it.
- [ ] **Collapsible left panel**, and panel shortcuts: **Ctrl+B** (left), **Ctrl+J**
      (bottom), **Ctrl+I** (AI). They work with focus in the viewport and in the UI, and
      are remembered after a restart.
- [ ] **Grouping prefabs and assets**: see 4.4 below.
- [ ] **Art tab history**: see 4.5 below.

## 4.1 — Editor shell

- [ ] Open a menu or dialog over the viewport: it draws on top. Close it and the
      viewport works as before (click, drag, wheel).
- [ ] Right-click and drag in the viewport still pans. A right-click without moving
      opens the menu.
- [ ] Esc closes menus and dialogs; Enter confirms dialogs.

## 4.2 — Projects

- [ ] File ▸ New Project…: a name, "use the demo palette?", then a folder. The editor
      reopens on the new project, which has prefabs but no scene content.
- [ ] Open Project… on a folder that isn't a project: refused with a message.
- [ ] Recent: the demo is listed; choosing it switches back. With unsaved changes you're
      asked first.
- [ ] Save Project As…: the copy opens, with your unsaved changes in it.

## 4.3 — Scene editing

- [ ] **Arrows** nudge the selection one cell (Shift: a tenth). A tile won't move onto
      another tile.
- [ ] **Ctrl+C**, then hover elsewhere and press **Ctrl+V**: a copy appears under the
      cursor, the same size. Ground menu ▸ Paste here works too. Pasting a tile replaces
      the tile there.
- [ ] **F5** starts and stops Play.
- [ ] Each of these is one **Ctrl+Z**.

## 4.4 — Organizing prefabs and assets

- [ ] Prefabs tab: a folder column on the left, kind chips (Tiles, Characters, Items,
      Other), and Search. Number keys 1–9 still pick the same prefabs.
- [ ] Right-click a prefab ▸ **Move to folder…** "enemies": the folder appears and the
      prefab is in it. The scene's copies still work, and a quest that counts it still
      counts it (Story tab shows no problem). Play to check.
- [ ] **Rename…** a prefab: same checks. **Drag** a card onto a folder: it moves.
- [ ] Assets tab: the same for a texture. Prefabs using it keep their sprite.
- [ ] Ctrl+Z after a move undoes your last edit, not the move, and doesn't break the
      sprite.

## 4.5 — Keeping AI work

- [ ] A proposal you left undecided before the restart shows "Not applied — from before
      the editor restarted" and can't be applied.
- [ ] ⌫ in the AI panel header clears the conversation (after asking).
- [ ] Art tab ▸ **History**: earlier requests with their settings and thumbnails,
      including failed ones and discarded images ("discarded"). **Use again** fills the
      form; **Generate again** sends it. The history survives a restart.

## 4.6 — Performance

- [ ] **F3** shows frame stats (frame and update time, sprites drawn and culled, draw
      calls), while editing and while playing. In View ▸ Frame stats too.
- [ ] Zoom far out on a big scene: everything still draws, with no missing sprites past
      4096.
- [ ] `build\apps\sandbox\Debug\sandbox.exe --bench`: 10,000 tiles and 500 skeletons,
      stats on. It's slow in Debug (Lua is about 50× slower unoptimized). If you can
      build and run Release, `build\apps\sandbox\Release\sandbox.exe --bench` is the real
      measurement: the budget is about 8 ms per step. On this machine Bitdefender blocked
      freshly built Release executables; you may need to allow them.

## 4.8 — Lua modules

- [ ] Inspector ▸ Add Component offers **Lantern** (from `projects/demo/modules/lantern`).
      Add it: radius, lit, color and fuel, with ranges enforced. Save and reopen: still
      there.
- [ ] Ask the AI: "Put out every lantern" (once a few entities have one). It proposes
      `lantern.setAll`, and applying it unticks *lit* on each.
- [ ] Break `module.json` on purpose (e.g. a bad type) and restart: the editor shows
      "Module problem: …", and everything else still works.

## 5.1 — Export Game

- [ ] File ▸ **Export Game…**: name, folder or folder + .zip, then a destination. When it
      finishes, Show in Explorer opens the folder.
- [ ] The folder has `<Name>.exe`, `shaders`, and `game` (no `.cache`, no `ai_log.jsonl`).
      Double-click the .exe: the game plays, with your name in the title bar.
- [ ] Put an `assets/icon.png` in the project and export again: the window has that icon.
- [ ] The .zip opens in Windows Explorer and contains the same folder.
- [ ] Exporting again to the same place with the same name is refused (nothing
      overwritten).
- [ ] The export says it used the Debug player (Release can't run here; see 4.6).
