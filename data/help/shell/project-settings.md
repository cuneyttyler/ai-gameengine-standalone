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

Questions: how do I use a different model for this project only? how do I set the art style? what's the difference
between Assembled and Paint?

## The Input page

The last entry, **Input**, isn't a regular settings section. It edits the game's input map, the project's
`input.json`: the actions (move, attack, interact, inventory, and the project's own like farm or parry) and the keys,
mouse buttons and gamepad buttons and sticks that trigger them. Changes there are saved to input.json at once.

See input-settings.md for how to add actions, bind keys and search.

Questions: where do I change the game's controls? where is input.json edited?
