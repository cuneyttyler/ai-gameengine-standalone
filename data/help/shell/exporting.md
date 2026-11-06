# Exporting a game

File ▸ Export Game: making a stand-alone copy of your game that runs without the editor. Covers the steps, what goes
into the export, the accessibility report, the version, icon and splash, the debug console, and common problems.

## Exporting step by step

1. Choose **File ▸ Export Game…** (it's off during Play).
2. Type **the game's name**. It's used for the window title and the file names, and defaults to the project's name.
   Only letters, digits, spaces, - and _ are kept, up to 64 characters. Click **Next**.
3. Choose **Folder and .zip** to also make a .zip ready to send, or **Folder only**.
4. Choose whether to **keep the debug console** (the ` key) in the exported game. Choose **Leave it off** for a game
   you give to players, or **Keep it** for testing.
5. The editor **saves the scene first** (and the settings) if they have unsaved changes, so the export plays what's
   on screen.
6. Pick the folder to export into.

When it's done, **Game exported** says how many files were written and where, for example "…files in
D:\Games\My Game. Run the .exe inside to play." **Show in Explorer** opens the folder.

### Example: sending "Lanternfall" to a friend as a zip

1. Make sure you're not playing (the Play menu says Play, not Stop), then click File ▸ Export Game….
2. The name box shows `Lanternfall`; keep it and click Next.
3. Choose Folder and .zip.
4. Choose Leave it off for the debug console, since your friend is a player, not a tester.
5. The editor saves the scene and settings if they have unsaved changes.
6. Pick `D:\Exports` as the folder.
7. Game exported says how many files were written, in `D:\Exports\Lanternfall`, and `D:\Exports\Lanternfall.zip` is
   beside it. Click Show in Explorer and send the .zip; your friend unzips it and runs `Lanternfall.exe`.

Questions: how do I export my game? how do I make a zip of my game? should I keep the debug console?

## What an export contains

The export is a folder `<destination>\<Name>\`:

- `<Name>.exe`: the game player, renamed to your game's name. If the project has `assets/icon.png`, it becomes the
  exe's icon.
- `shaders/`: what the player needs next to it.
- `game/`: your game's runtime content: `settings.aige`, `input.json`, `game.json`, and the folders scenes, prefabs,
  scripts, assets, dialogue, quests, schedules, world, modules, records, loc, ui and cutscenes. It also holds
  `export.json` (name, version, start scene) and `accessibility.json`.

Left out: `.cache`, the AI log, hidden files and folders, generation sidecars (`.gen.json`), and anything else at the
top of the project (such as editor-layout.json). With .zip chosen, `<destination>\<Name>.zip` holds the same folder.
The game starts at Project Settings ▸ Game ▸ startScene.

### Example: checking what went into the export

After exporting "Lanternfall" to `D:\Exports`:

1. Open `D:\Exports\Lanternfall`. It holds `Lanternfall.exe` (with your `assets/icon.png` as its icon), `shaders/` and
   `game/`.
2. Open `game/export.json` in a text editor. It records the name, version and start scene, plus when it was
   exported and whether the debug console was kept, something like:

```json
{
  "name": "Lanternfall",
  "exported": "2026-11-06T14:02:11Z",
  "engine": "AIGE",
  "version": "0.3.0",
  "debugConsole": false,
  "startScene": "scenes/village_green.aige"
}
```

3. A notes file you kept at the top of the project, such as `design-notes.txt`, isn't there: only the game's runtime
   content goes in. Neither are `.cache`, the AI log or `.gen.json` sidecars.

Questions: what files are in the exported game? why is a file missing from my export? where does the exported game
start?

## The accessibility report

Every export checks the game's accessibility and writes the report into the game as `game/accessibility.json`. The
Game exported message summarises it, "Accessibility: N/M checks pass", and lists each failing check. The checks cover
text contrast against panels, body text size, whether the health, mana and stamina bars can be told apart with
colour blindness, whether every action can be done on keyboard and gamepad, and what the engine always offers
(rebinding, subtitles, motion and flash options, UI and text scale, assists, hold to toggle). The AI panel also sees
the failing checks, so you can ask it to fix the theme's contrast, for example.

Exported games give players their own options too: text size, high contrast, colour-blind colours, reduce motion,
reduce flashes, hold to toggle, subtitles and UI scale.

### Example: fixing a contrast failure after export

1. Export the game. Game exported reads "Accessibility: 9/10 checks pass" and lists the failing check: the text
   contrast against panels is too low.
2. In the AI panel, type:

> "The export says the theme's text contrast against panels fails. Fix the theme so it passes."

3. Review and accept the change, then export again (into a new folder or after deleting the old export, since
   exports never overwrite). The report now shows the contrast check passing; the full report is in
   `game/accessibility.json`.

Questions: what is the accessibility report? why does my export fail accessibility checks? how do I fix contrast?

## Version, icon, splash and crash log

- **Version**: Project Settings ▸ Game ▸ version. It goes into export.json, the game window's title and the crash
  log.
- **Icon**: `assets/icon.png` is the window's icon and, on export, the exe's icon.
- **Splash**: Project Settings ▸ Game ▸ splash shows a picture for two seconds when the game starts. A key skips it.
- **Crash log**: if the game crashes, it writes what happened and the version to `crash.log` beside its saves, and
  tells the player.
- **Debug console**: it's only in the export if you chose Keep it.

### Example: a version number, icon and splash for the first release

1. Open File ▸ Project Settings… ▸ Game and set version to `1.0.0`.
2. Set splash to `assets/ui/lanternfall_logo.png` (a picture you imported into assets).
3. Put a square picture at `assets/icon.png`.
4. Press Ctrl+S, then File ▸ Export Game….
5. Run the exported game: the logo shows for two seconds (any key skips it), the window's title shows 1.0.0, and the
   exe and window use the lantern icon. If the game ever crashes, `crash.log` beside its saves names version 1.0.0.

Questions: how do I set my game's icon? how do I add a splash screen? where is the crash log?

## Export problems

- **"No built player found (build the sandbox target)."**: the editor couldn't find the game player to copy. An
  installed editor has it in its `player` folder. A source build needs the sandbox (player) target built.
- **A note about the player build**: if the export used a Debug player, the message says so. Build the Release
  configuration for a smaller, faster game.
- **The destination already has a folder with that name**: the export never overwrites. Pick another folder or name,
  or delete the old export.
- **"the game needs a name"**: the name had no usable characters.
- **"Stop playing first."**: Export is off during Play.

Building a game from a Planner design (places, characters, quests) is a different thing, done in the Planner panel.

### Example: exporting a second build next to the first

1. Yesterday you exported Lanternfall to `D:\Exports`. Today you export again to the same folder.
2. The export refuses: `D:\Exports` already has a `Lanternfall` folder, and exports never overwrite.
3. Either rename the old folder to `Lanternfall-0.3` in Explorer and export again, or type the name `Lanternfall 0.4`
   in the export's name box so the new folder gets that name.
4. If the message notes a Debug player, build the Release configuration of the player before your final export for
   a smaller, faster game.

Questions: why did export fail? export says no built player found? why is my exported game slow? can I overwrite an
export?
