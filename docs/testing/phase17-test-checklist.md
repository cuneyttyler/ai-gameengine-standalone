# Phase 17 — Manual Test Checklist

The user-test checkpoints from Phase 17 (`development_plan.md`, "Game UI"), for one
end-of-phase pass. Automated tests cover the logic: theme tokens in styles, meshes to clipped
quads, a menu document showing GameFlow's menu and routing a click back, themes restyling,
a customised screen winning over the engine's, the HUD and dialogue documents, notifications
queuing and coalescing, project screens and actions, UI file checks, glyphs, the AI's `write_ui`,
and the game playing its title and difficulty choice as documents (`game_ui_test.cpp`,
`test_gameui.py`). This list covers how it looks and feels. The design is `game-ui.md`
(see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
  (The first build fetches RmlUi.)
- A project with a start scene, a player, an NPC with Talk and a quest.

## 17.1–17.3 Screens as documents

- [ ] `sandbox.exe <project>`: the title screen is the themed document (the game's name, its
      menu); Up/Down and Enter work as before.
- [ ] New game ▸ the difficulty choice, then play. The HUD shows health (and mana/stamina when
      the player has them), the tracked quest top-left, the clock top-right, "E Talk to …" near
      the NPC.
- [ ] Talk: the dialogue box is a document (speaker, text, numbered choices); 1-9 pick.
- [ ] I, J and a merchant's shop open as windows (list beside details). Esc: the pause menu.

## 17.4 Themes

- [ ] Project ▸ **UI** ▸ Theme: pick *parchment*, Save theme, Preview: the pause menu is
      parchment-coloured. Override `panel` with `#552222`: every screen's panels change.
- [ ] Back to *stone*; try *pixel* and *scifi-glass* in the sandbox: title, menus, HUD all follow.

## 17.5 Notifications

- [ ] Pick up an item: "Picked up …" in the feed on the right (two quickly: merged). Level up:
      a banner in the middle. A quest: a toast at the top, one at a time.
- [ ] UI ▸ Notifications: add `{"event": "death", "channel": "feed", "text": "Something died"}`,
      Save; killing an enemy shows it.
- [ ] Lua `notify("banner", "Boss!")` from a script shows a banner.

## 17.6 Glyphs

- [ ] With a gamepad: after pressing a button, prompts show "A" instead of "E"; touching the
      keyboard brings "E" back. Rebinding interact to F (input.json) shows "F".

## 17.7 The UI tab

- [ ] Screens: each shows its source (engine/project/menu). Preview shows it over the viewport
      with sample data; Stop preview hides it.
- [ ] **Customise** *pause*: the source becomes editable (`ui/screens/pause.rml` appears).
      Change the title line to `<h1>{{title}} — game paused</h1>`, Save: the preview and the
      game's pause menu show it; other menus are unchanged.
- [ ] New screen "map", then a script with `open_screen("map")` and
      `on("ui_action", function(e) if e.text == "close-map" then close_screen("map") end end)`:
      the screen opens; clicking Close in it closes it (mouse in the editor's Play).
- [ ] **Check all** lists nothing for the engine's screens; break a tag in the customised
      pause and it's listed.
- [ ] Project Settings ▸ UI.documents off: the game draws the old overlays again.

## The AI panel

- [ ] Ask the AI "make the game's UI look like old parchment with a dark red accent": a
      write_ui of ui/theme.json (base parchment, accent token), passing the checks.
- [ ] Ask for "a banner when a quest is completed and a feed line for gold": a write_ui of
      ui/notifications.json.
- [ ] Ask for "a simple map screen opened with M": a write_ui screen document with the ui model
      and a script using open_screen.
