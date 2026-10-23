# Phase 24 — Manual Test Checklist

The user-test checkpoints from Phase 24 (`development_plan.md`, "Shipping quality"), for one
end-of-phase pass. Automated tests cover the profiler, achievements (events, quests, flags,
persistence, secrets), the colour maths and the accessibility report, hold to toggle, the console's
commands, streaming, every template, and the export's version, console and accessibility files
(`shipping_test.cpp`), and the AI's achievement checks (`test_shipping.py`). This list covers
playing and exporting. The design is `shipping.md`.

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.

## Templates

- [ ] File ▸ New Project ▸ type `arpg`: the project opens with a scene where Play shows the player,
      Elder Maren, a wolf and a potion. Talking to her offers "The First Road"; killing the wolf,
      reaching the well and talking to her again completes it.
- [ ] Repeat with `rpg`, `action-adventure` and `life-sim` (pick the three turnips, bring them to
      Ola). Top-down templates have square cells.

## Accessibility

- [ ] Play from the sandbox ▸ Settings: Text size 150% makes the menus' text bigger; High contrast
      turns panels black with white text; Colour-blind colours changes the bars' colours.
- [ ] Reduce motion: hits no longer shake the screen or freeze it; Reduce flashes: no hit flash.
- [ ] Add an action `block` (Q) in input.json; with Hold to toggle, one press holds it.
- [ ] Settings survive quitting and starting again.

## Console and profiler

- [ ] In play, ` opens the console: `give potion 3`, `tp 5 5`, `god`, `speed 0.5`,
      `quest q-welcome start`, `= 1 + 1`, `lua notify("toast", "hi")`, `help`; ` or Esc closes it.
- [ ] F2 shows the profiler: frame time, fps, sections with bars, a sparkline.

## Achievements

- [ ] In a template: killing the wolf shows "Achievement unlocked: First Blood"; completing the quest,
      "First Steps". Pause ▸ Achievements lists them with points; restarting the game keeps them.
- [ ] Records ▸ achievement: add a hidden one; it shows as ??? until unlocked.

## Export

- [ ] Project Settings ▸ Game: version 1.2, splash `assets/splash.png` (any picture); put an
      `assets/icon.png`. File ▸ Export Game (console left off): the result lists the accessibility
      checks that pass; the folder has `game/accessibility.json`.
- [ ] The exported exe shows the icon in Explorer; starting it shows the splash, then the title; the
      window's title says "v1.2"; ` does nothing.
- [ ] Export a planned game (Phase 23) whose start scene isn't `main.aige`: it starts there.

## Streaming

- [ ] Project Settings ▸ Streaming on, chunk 32, radius 1, in a big scene (a planned settlement):
      F2's sections drop; characters far away stand still and act again when you approach.

## AI panel

- [ ] "Add an achievement for defeating ten bandits" → an achievement record (event death, target,
      count 10) that validates.
- [ ] "Is my game's UI readable?" → the AI answers from context shipping.accessibility and, if
      something fails, proposes ui/theme.json tokens.
