# Phase 12 — Manual Test Checklist

The user-test checkpoints from Phase 12 (`development_plan.md`, "Game foundations"), for
one end-of-phase pass. Automated tests cover the logic: records (schemas, bases,
references, renames, the AI's record operations, Lua), events, timers, coroutines and
module systems, input actions and contexts, the game flow's menus, save slots and the
world's memory, localization, player settings, audio placement and music direction, and
the camera rig. This list covers what tests can't judge: whether it feels right.

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- **Use a test project** with at least two scenes joined by a gateway, a few enemies, a
  potion, and a talkable NPC (a Phase 6 test project works).
- A **gamepad** (Xbox-style) for 12.3, and speakers for 12.6.

## 12.1 — Records

- [ ] Project panel ▸ **Records**: the `item` type is listed. **+** makes a new item with
      its defaults; fill name, kind, icon (a real texture path), value, tags (+ Add).
      **Create** writes `records/item/<id>.json`.
- [ ] Make a second item with **Base** set to the first: unticked fields show the base's
      values greyed; tick one and change it. Save, reopen: only the ticked fields are in
      its file.
- [ ] Type a wrong value (a negative value, an icon that doesn't exist): ⚠ marks the row,
      the problem names the field. Saving a record that breaks its type is refused with
      the reason.
- [ ] Change an item's **Id** and save: anything that referred to it (the second item's
      base) follows. **Delete…** warns when something still uses it.
- [ ] AI panel: "add a healing potion item worth 25 gold and a greater potion based on
      it". Diff Review shows two **Record** changes; apply; they appear in the tab.
- [ ] A script: `local p = record("item", "<id>"); print(p.name, p.value)` prints in the
      console when played.
- [ ] A module with a record type (`modules.md`, "Record types"): restart the editor; its
      type appears in the tab with its fields.

## 12.2 — Events, timers, coroutines, module systems

- [ ] A script on the player with `on("death", function(e) print("died", e.text) end)`:
      killing a skeleton prints its prefab.
- [ ] `every(1, function() print("tick") end)` ticks once a second; `after(3, ...)` fires
      once; `start(function() wait(2) print("later") end)` waits.
- [ ] Saving a script while playing restarts its timers (no doubled ticks).
- [ ] An error inside a handler shows in the console and the game goes on.
- [ ] A module with `"systems"` (`modules.md`, "Systems") runs every step in Play.

## 12.3 — Input actions

- [ ] File ▸ Project Settings ▸ **Input**: every action with its keys. **+ Key** then
      press a key adds it; × removes one; **Reset to defaults** restores them. The file is
      `input.json`.
- [ ] Rebind **attack** to J: in Play, J swings and Space doesn't.
- [ ] **Gamepad:** the left stick walks (slower when pushed part way), the d-pad walks,
      X attacks, A talks and continues dialogue, Start opens the pause menu (in the
      player).
- [ ] In a conversation, the key that started it (E) doesn't skip the first line.
- [ ] Typing in an Inspector field never moves the player (the viewport must have focus).

## 12.4 — Game flow (run the player: `build/apps/sandbox/Release/sandbox.exe --project <dir>`)

- [ ] The **title screen** shows the game's title (game.json, else the folder name) over
      the start scene: Continue (greyed with no saves), New game, Load game, Settings,
      Credits, Quit. Keyboard (arrows, Enter) and gamepad both work.
- [ ] **New game** starts play. **Esc** opens **Paused**: Resume, Save game, Load game,
      Settings, Quit to title, Quit game. Esc again resumes.
- [ ] Walking through a gateway **fades** from black into the next scene.
- [ ] Dying shows **You died** after a moment: Load last save, Start again, Quit to title.
- [ ] Killing every enemy (with no quest active) shows **Victory!**
- [ ] `game.json` with `"titleScreen": false` starts straight in play; `"credits"` shows
      on the Credits screen.

## 12.5 — Saves and the world's memory

- [ ] Pause ▸ **Save game** ▸ Slot 1. The slot shows the place, the time and play time.
- [ ] Kill a skeleton, pick up the potion, walk to the other scene and back: the skeleton
      stays dead, the potion stays gone.
- [ ] Kill another, save to slot 2, **Quit to title**, **Load game** ▸ slot 2: both dead,
      your health as it was, the quests as they were.
- [ ] **Continue** on the title loads the newest save (the autosave after a scene change,
      if it's newer).
- [ ] F6 / F9 quick save and load work in the player and in the editor's Play.

## 12.6 — Audio

- [ ] Add **Music** to an entity in each scene (track, ambience): it plays in Play and
      cross-fades when you change scene; Stop fades it out.
- [ ] With an enemy chasing you, the combat track (Music.combat, or game.json
      `combatMusic`) takes over, and goes back after.
- [ ] Music ducks while someone talks.
- [ ] `play_sound_at(path, x, y)` from a script: louder near, to the left or right.
- [ ] Settings ▸ Music volume 0% silences music, not effects.

## 12.7 — Camera, localization, settings

- [ ] Project Settings ▸ Camera: **deadZone** lets you walk a little before the camera
      follows; **lookAhead** leads where you walk; **clampToLevel** stops at the level's
      edge.
- [ ] Add `loc/fr.json` with `{"menu.new_game": "Nouvelle partie"}` and a dialogue line by
      its key (`dialogue/<file>/<node>`): Settings ▸ Language ▸ fr shows them; untranslated
      text stays in the source language.
- [ ] Settings ▸ **Fullscreen** switches the player's window and back; settings survive a
      restart (`%LOCALAPPDATA%\AIGE\saves\<game>\settings.json`).
- [ ] An exported game (File ▸ Export) has its `records/`, `input.json`, `game.json` and
      `loc/`, and plays the same.

## The AI panel

- [ ] Ask the AI "add an item record for a healing potion worth 25 gold": the proposal is a
      write_record for records/item/…, it passes the checks, and Records shows it after Apply.
- [ ] Ask for "a script that plays a bell sound 2 seconds after any enemy dies": it uses
      `on("death", …)` and `wait`/`after`, not a polling `update`.
