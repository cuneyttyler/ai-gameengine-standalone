# Project Settings

File ▸ Project Settings: the project-wide settings saved in the project's settings.aige. Covers how the window works
and what each section is for (Renderer, Camera, Combat, Streaming, Physics, Grid, AI, Places, Game, Time, Difficulty,
Lighting, UI, Story, and Input). The exact list of fields is described elsewhere from the engine's own descriptions.

## The Project Settings window

Open **File ▸ Project Settings…**. The sections are listed on the left, with **Input** last. The chosen section is on
the right with its description, and each setting has a row: its name, its control and a line saying what it does.
Close the window with × or Esc, or by clicking outside it.

- **Changes apply right away**. They're kept in memory and **saved with the project (settings.aige) when you save**
  (File ▸ Save, Ctrl+S). Settings aren't part of the scene's undo history.
- A value that isn't allowed shows an error under its row.
- When the AI is ready, a **✦** next to a setting's name asks the AI panel about that setting (Ask AI about this
  setting).
- Some fields can be locked, with a tooltip saying why.

Project Settings belong to the project and travel with it, and with exported games. Your personal choices live in
File ▸ Preferences instead.

See also: preferences.md, input-settings.md.

### Example: asking the AI what a setting does

1. Open File ▸ Project Settings… and click Streaming on the left.
2. Click the ✦ next to a setting's name (Ask AI about this setting). The AI panel gets a question about it.
3. Read the answer, change the value if you want, and press Ctrl+S: the change is saved to `settings.aige` with the
   project. Until then it applies in memory only, and closing without saving loses it.

Questions: where are project settings saved? do I need to save after changing a setting? how do I ask the AI about a
setting?

## Renderer, Camera, Grid and Lighting sections

- **Renderer**: how the game is drawn. The background colour as red, green and blue (0–1).
- **Camera**: the play camera. How stiffly it follows the player, the zoom while playing (playZoom), the dead zone
  before it follows, how far it looks ahead, and whether it's kept inside the level's edges.
- **Grid**: how the world is laid out. **Isometric** (diamond cells seen at an angle) or **TopDown** (square cells
  seen from above, for art drawn from above). Changing it re-lays the whole scene. New Project sets it.
- **Lighting**: 2D lighting, day and night. Whether lighting is on, ambient light following the game clock, the day
  and night ambient colours, the dawn and dusk hours, and a vignette.

### Example: day and night for the fishing village Gullhaven

1. Open File ▸ Project Settings… ▸ Lighting.
2. Tick enabled and dayNight, so the ambient light follows the game clock.
3. Set dayAmbient to `#fff4e0` (warm daylight) and nightAmbient to `#30385a` (cold blue night).
4. Set dawnHour to 6 and duskHour to 20, and tick vignette to darken the screen's edges.
5. Press F5: as the game clock passes 20:00 the village darkens. Press Ctrl+S to keep the settings.

### Example: a dark background behind the dungeon

1. In Project Settings ▸ Renderer, set clearR 0.05, clearG 0.04 and clearB 0.08.
2. The empty space around the dungeon's tiles is now almost black with a hint of purple, instead of the default
   colour.

Questions: how do I change the background colour? how do I change the zoom in play? how do I switch from isometric to
top-down? how do I turn on day and night?

## Combat, Physics, Streaming and Difficulty sections

- **Combat**: rules shared by every attack. Minimum damage, how long a hit flashes, a knockback multiplier, how much a
  block stops, and dodge distance and stamina cost.
- **Physics**: movement and collision. Whether the player collides with walls (off lets the player walk through
  them), and rigid bodies' gravity in X and Y (0 for top-down games).
- **Streaming**: for big scenes. When it's on, only the chunks around the player are simulated, and characters,
  scripts, routines and spawners far away sleep until the player comes near. You set the chunk size in cells and how
  many chunks around the player stay awake.
- **Difficulty**: the level a new game starts at, whether the player can change it during play, adaptive offers, how
  enemies' levels are chosen (None, FixedZones, Scaled, Hybrid) and a level offset.

### Example: walking through walls while testing a big map

You want to reach the far end of the castle quickly without fighting the corridors.

1. Open File ▸ Project Settings… ▸ Physics and untick playerCollides.
2. Press F5: the player walks straight through walls.
3. Press F5 to stop and tick playerCollides again before you save, so the game ships with its walls.

### Example: falling crates in a side-on puzzle room

1. In Project Settings ▸ Physics, set gravityX to 0 and gravityY to 10 (about right for a side-on puzzle; top-down
   games keep 0).
2. Crates with a RigidBody2D now fall in Play. A crate whose RigidBody2D gravityScale is 0.5 falls at half that.

Questions: how do I let the player walk through walls? how do I add gravity? what is streaming? how do enemy levels
scale?

## Game, Time, UI and Story sections

- **Game**: the game as a whole. The **start scene** (where the game begins; a scene's right-click menu ▸ Set as start
  scene changes it too), the **version** (shown in the window title, the crash log and exports), the **debug console**
  (the ` key in play), a **splash** picture, how doors open (by use, or automatically as someone comes near), which
  input actions open doors, and whether a double-click opens a door.
- **Time**: the game clock that routines follow. The starting hour, game minutes per real second (2 makes a day last
  12 minutes), and whether the clock always shows.
- **UI**: the game's own UI. Themed documents or the built-in overlays, how speech shows (bubble, subtitle or both)
  and for how long, damage numbers, item numbers, and labels.
- **Story**: quest help on screen. Objective markers and the quest journal window (J).

### Example: starting the game in the lighthouse at dawn

1. In the Project panel's Scenes tab, right-click `lighthouse` and click Set as start scene (Project Settings ▸
   Game ▸ startScene now names it).
2. In Project Settings ▸ Game, set version to `0.3.0`; it shows in the window title, the crash log and exports.
3. In Project Settings ▸ Time, set startHour to 5.5 (half past five) and minutesPerSecond to 2, so a game day lasts
   12 real minutes. Tick showClock.
4. In Project Settings ▸ UI, set speech to subtitle, so the lighthouse keeper's lines show as a subtitle line instead
   of bubbles.
5. Press Ctrl+S, then F5: the game opens in the lighthouse at 05:30.

Questions: how do I change the start scene? where do I set the game version? how long is a game day? how do I show
speech as subtitles?

## AI and Places sections

- **AI**: this project's own AI choices. Each one left empty uses the app's choice from Preferences ▸ AI: the
  language model provider and model, prefab art, background, paint model, humanoid animation (animate),
  non-humanoid animation (objectAnimate), and the **art style** used in every art prompt (16-bit or 32-bit pixel art,
  painted, flat vector; empty means it's taken from the art already made). The Planner's art style sets it. A change
  to the provider or model reaches the AI service at once, with no restart.
- **Places**: how the AI builds places. **Assembled** (each building, tree and prop gets its own art, put together on
  the grid over a painted background; every piece can be moved) or **Paint** (the place is painted whole as one
  picture, then you mark where one can walk and what a character goes behind). Each build and plan starts with this
  and can choose the other. New scenes take it as their default type.

See also: preferences.md.

### Example: a painted look and Assembled places for one project

Your project "Emberfall" should look painted and have movable buildings, whatever your Preferences say.

1. Open File ▸ Project Settings… ▸ AI and set artStyle to painted. Every art prompt for this project (prefabs,
   backgrounds, characters) now asks for a painted look.
2. Leave the provider and model empty to keep using the app's choice from Preferences, or set them to use another
   model here only; the AI service picks the change up at once.
3. Click Places and set method to Assembled. New builds and new scenes in Emberfall start as Assembled.
4. Press Ctrl+S to save `settings.aige`.

Questions: how do I use a different model for this project only? how do I set the art style? what's the difference
between Assembled and Paint?

## The Input page

The last entry, **Input**, isn't a regular settings section. It edits the game's input map, the project's
`input.json`: the actions (move, attack, interact, inventory, and the project's own like farm or parry) and the keys,
mouse buttons and gamepad buttons and sticks that trigger them. Changes there are saved to input.json at once.

See input-settings.md for how to add actions, bind keys and search.

### Example: opening the game's controls

1. Open File ▸ Project Settings… and click Input, the last entry on the left.
2. The page lists the game's actions (move_x, attack, interact, inventory and the project's own) with their
   bindings.
3. On the interact row, click × on the E chip, then click + Key and press R. "Saved" shows: the change is written to
   `input.json` at once; no Ctrl+S is needed for it.

Questions: where do I change the game's controls? where is input.json edited?
