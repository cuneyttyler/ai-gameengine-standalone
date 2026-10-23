# Phase 1 — Manual Test Checklist

The user-test checkpoints from every Phase 1 milestone (`development_plan.md`), collected
for one end-of-phase pass. Automated coverage (96 GoogleTest cases, including a scripted
playthrough of the demo level) already checks the logic; this list is for what tests can't
judge: feel, usability, and whether it's actually fun.

Build: `cmake --build build --config Debug`, then run `build/apps/editor/Debug/editor.exe`
(opens `projects/demo`). The standalone player is `build/apps/sandbox/Debug/sandbox.exe`.

## 1.6 — Play the vertical slice (most important)

- [ ] Press **Play**. The camera follows the hero; WASD/arrows move in all 8 directions at
      the same speed.
- [ ] **Space** swings the sword the way you're facing; **left-click** swings toward the
      cursor. Swing, hit and death sounds play.
- [ ] Skeletons notice you when you get close, chase, and hit back; hits flash red and
      knock back; health bars appear over damaged characters.
- [ ] Walking over a potion when hurt heals you (it stays put at full health).
- [ ] Trees, rocks, crates, walls, hedges and water block you.
- [ ] Killing all four skeletons shows "Victory!"; dying shows the death message.
- [ ] **Esc** (or Stop) returns to editing with everything exactly as before Play.
- [ ] The same level in the standalone `sandbox.exe` (restarts after death or victory).
- [ ] Is it fun for a minute? Note anything that feels off: speeds, damage, ranges.

## 1.1 — Editor shell

- [ ] Panels resize by dragging the dividers (side panel, Scene Tree/Inspector, Assets);
      sizes are remembered after restarting.
- [ ] The viewport never fights the UI for keyboard focus (typing in an Inspector field
      never moves the player).

## 1.2 — Scene editing feel

- [ ] Click selects (characters before the tile under them); the outline matches what you
      clicked; drag moves (tiles snap by cell); Del deletes; Ctrl+D duplicates.
- [ ] Paint grass/dirt/wall with click-drag; place NPC / Player / Enemy / Potion.
- [ ] Right-drag pans, wheel zooms toward the cursor, **F** frames the level.
- [ ] Undo/redo (Ctrl+Z / Ctrl+Y) across a mix of edits; Ctrl+S saves; closing with
      unsaved changes asks first.

## 1.3 — Assets

- [ ] **Import…** a PNG and a WAV from anywhere on disk; thumbnails appear.
- [ ] Switch an image to **Tileset**, set the tile size, pick a tile, **Paint tiles**
      (with and without Solid).
- [ ] **Place as prop**; **Apply to selected** on a character.
- [ ] Edit a PNG in an image editor, switch back to the editor: the change shows up.
- [ ] **Preview** plays a sound.

## 1.4 — Component authoring

- [ ] Inspector edits of each field type: numbers, checkbox (Tile.solid), text
      (Name.value), dropdowns (Faction.team, MeleeAttack.effect), entity reference
      (Follow.target), texture, audio, script.
- [ ] **+ Add component** / **×** remove (removing something another component needs is
      refused with a reason).
- [ ] **+ New script…** creates and attaches a script; **Open** opens it; saving it while
      playing hot-reloads the behaviour.

## 1.5 — Combat tuning by hand

- [ ] Give the hero a Burn or Slow on-hit effect in `MeleeAttack` and try it on a skeleton.
- [ ] Change a skeleton's `Stats`/`Health`/`Follow.range` and feel the difference.

## Phase 1 exit criteria

- [ ] Build a small scene from scratch (new tiles, an enemy, a potion), script a behaviour,
      and play it — entirely by hand.

## 1.7 — Prefabs and script properties

- [ ] The **Prefabs** tab lists the demo's 15 prefabs with thumbnails; clicking one (or
      pressing 1-9) arms the brush shown in the toolbar. Tile prefabs paint with
      click-drag; others place one per click; placing the hero again moves the existing one.
- [ ] Select a skeleton: the Inspector shows **Prefab: skeleton**. Change its Health —
      the field is marked as overridden (blue bar) and ↺ reverts it.
- [ ] Change a value on one skeleton and **Apply to prefab**: the other skeletons update,
      except fields they override.
- [ ] Edit `projects/demo/prefabs/skeleton.prefab` in a text editor (e.g. `moveSpeed`),
      switch back to the editor: every skeleton updates.
- [ ] Select an entity that isn't a prefab copy, **Save as prefab…**, and place more copies.
- [ ] Assets tab: select a texture (or a tileset tile) and **New tile prefab** / **New
      prop prefab**; it appears in the palette, armed.
- [ ] A skeleton's `LuaScript` section shows the script's properties (`swing_range`,
      `taunt`). Set `taunt` to `assets/audio/hurt.wav` on one skeleton and hear it when it
      spots you; set `swing_range` to 3 and watch it swing from further away.

## Known limitations (expected, not bugs)

- Depth sorting is per entity, so a character brushing a wall's corner can briefly draw
  on the wrong side of it.
- No animation yet: characters are single frames that flip to face their direction.
- Undo is whole-scene snapshots; selection survives undo but very large scenes would be
  slow to snapshot.
- Windows-only editor (WebView2); the sandbox runtime is cross-platform-ready code.
