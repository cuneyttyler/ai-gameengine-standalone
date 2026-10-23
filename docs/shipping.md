# Shipping quality

Status: **built, to be tried (2026-10-23)**. Phase 24 in `development_plan.md`: what makes a
game made with the engine ready to give to players.

## Accessibility

**The player's options** (Settings, saved in `settings.json` beside the saves):

| Option | What it does |
|---|---|
| Text size | 100–200%: the themed UI's text (the `font-size` token), apart from the UI's scale |
| High contrast | white text on near-black panels, a yellow accent and highlight |
| Colour-blind colours | protanopia / deuteranopia / tritanopia: health, mana, stamina, XP and danger in the Okabe-Ito palette |
| Reduce motion | no screen shake, hit-stop or slow motion (effects' `ActiveFxAccessibility`) |
| Reduce flashes | no hit flashes |
| Hold to toggle | block, sprint, aim and crouch latch on a press until the next |
| Subtitles, UI scale, assists | as before (12.7, 16.4) |

The options become theme tokens over the game's theme (`DocumentUi::SetAccessibilityTokens`,
`AccessibilityTokens`).

**The accessibility report** (`game/Accessibility`): WCAG contrast of text and dim text on
panels (blended over black), body text size, the bars told apart under simulated protanopia,
deuteranopia and tritanopia (Machado 2009, ΔE in Lab), every action on keyboard and gamepad, and
what the engine always offers (rebinding, subtitles, motion and flash options, UI and text
scale, assists, hold to toggle). The export writes it as `game/accessibility.json` and the
editor shows its summary; the AI sees what fails (context `shipping.accessibility`).

## The debug console and the profiler

- **`** in play opens the console (Project Settings ▸ Game ▸ debug console; exports keep it only
  when asked). Commands: `help`, `lua <code>`, `= <expr>`, `god`, `give <item> [n]`, `tp <x> <y>`,
  `scene <path> [marker]`, `quest <id> start|complete`, `flag <name> [on|off]`, `speed <0.1-4>`,
  `spawn <prefab>`, `achieve <id>`, `heal`, `kill`, `profiler`, `clear`; Up/Down for history. The
  game waits while it's open.
- **F2** (or `profiler`) shows the profiler: frame time and fps, the worst frame, each section's
  average (scripts, gameplay, characters, magic, physics) with a bar, and a sparkline of the last
  60 frames (`gameplay/Profiler`).

## Achievements

Records `achievement` (`gameplay/Achievements`): name, description, icon, hidden, points, and what
unlocks it — a quest's state, a story flag, a count of game events (`event` + `target` +
`count`: deaths of a prefab or a name, pickups, trigger entries...), or a script
(`unlock_achievement`). Unlocks show as a banner and go through a platform layer:
`LocalAchievements` keeps `achievements.json` beside the saves; a store's platform implements
`AchievementPlatform`. The pause menu lists them (secret ones as ???). Lua:
`unlock_achievement(id)`, `achievement_unlocked(id)`, `achievement_progress(id)`.

## Export polish

- **Version**: Project Settings ▸ Game ▸ version; in `export.json`, the window's title and the
  crash log.
- **Icon**: `assets/icon.png` is the window's icon and, on export, the player exe's icon (a PNG
  embedded as its icon resource).
- **Splash**: Project Settings ▸ Game ▸ splash shows a picture for two seconds (a key skips it).
- **Crash log**: an unhandled exception (or an uncaught error) appends what happened, where (the
  module and offset) and the version to `crash.log` beside the saves and tells the player.
- An exported game is recognised by `game/export.json`, so its start scene can be any scene.

## Streaming big scenes

Project Settings ▸ Streaming: the scene is cut into chunks; only the chunks within `radius` of the
player's are simulated. Scripts, characters, routines and spawners outside them are `Dormant`
and wake where they were when the player comes near (`gameplay/Streaming`). Drawing already culls
what's off screen; this saves the simulation, which is what big scenes cost.

## Template projects

File ▸ New Project offers a template per v1.0 genre (`game/Templates`): **arpg** (isometric),
**rpg**, **action-adventure** and **life-sim** (top-down). Each plays from the start: the starter
scene with the player, a character to talk to with a dialogue and a first quest (`q-welcome`), an
enemy and a potion (action games) or a field of turnips and a shopkeeper (life-sim), items,
achievements, a theme and `game.json`.

## AI panel

Achievement records are checked (`ai_service/app/shipping.py`); the SHIPPING prompt section
covers achievements, the shipping settings and fixing the theme's contrast; context `shipping`
lists the achievements, the version and the accessibility report's failing checks.

## Not yet

Real colour filters on the whole picture (the palettes cover the UI), sound captions, a screen
reader, loading scenes in chunks from disk (streaming saves simulation, not memory), store
platforms (Steam) behind `AchievementPlatform`, symbolised stack traces in the crash log.
