# Play mode (play-testing)

How to play-test your game inside the editor: starting and stopping Play, the controls, what's reset when you stop,
saving and loading during Play, the difficulty Play uses, and the debug tools.

## Starting and stopping Play

- **Start**: press **F5**, click **▶ Play** at the right end of the toolbar, or choose **Play ▸ Play**.
- **Stop**: press **F5** again, press **Esc** in the viewport, click **■ Stop**, or choose **Play ▸ Stop**.

Play runs the **open scene as it is now**, saved or not. The viewport gets the keyboard focus, so WASD works without
clicking first. The camera switches to following the player at Project Settings ▸ Camera ▸ playZoom. The toolbar's
editing tools grey out, its bottom edge turns blue, and the status bar reads "Playing · WASD move · Space or click
attack · Esc stop · changes revert on stop".

Each Play starts fresh. Dialogue, quests, animations, the World Bible, schedules and the game's UI are read from disk
again, and the day starts at Project Settings ▸ Time ▸ startHour.

Questions: how do I play my game? how do I stop playing? does Play use my unsaved changes? why doesn't WASD work?

## What is reset when you stop

When Play starts, the editor takes a snapshot of the scene, including its ground layers, and **puts it back when you
stop**. Everything that happened in the game is undone: characters moved, enemies killed, items picked up. So is any
edit you made in the Inspector during Play. Your editing camera and selection come back too, and Play's music fades
out.

So a play-test never changes the scene you're editing. If you tweak a value in the Inspector while playing and like
it, note it and set it again after stopping.

During Play you can't save the scene (Ctrl+S says "Stop Play mode before saving"), and undo, the tools, New Scene,
Export and the other editing items are off. Opening another scene or project stops Play first.

Questions: why did my changes disappear after stopping? do edits during play stick? can I save while playing?

## Controls while playing

The game's keys come from **Project Settings ▸ Input** (your project's input.json). With the default input map:

- **Move**: WASD or the arrow keys (gamepad: the d-pad or left stick).
- **Attack**: left click toward the cursor, Q (pad X), for a player that has a melee attack.
- **Interact** (talk, open, pick up, use): E, Space (pad A). **Double-click** a door to open it when the player is
  near (Project Settings ▸ Game ▸ doorDoubleClick).
- **Conversations**: 1–9 pick a choice; Up/Down and Enter.
- **Inventory** I or Tab, **crafting** C, **journal** J, **spellbook** K, **abilities** 1–4.
- With an item in your hand, a click uses it on what you click instead of attacking.

The editor keeps a few keys during Play: **F5** stop, **Esc** stop, **F6** save game, **F9** load game, **F3** frame
stats, **F1** shortcuts. All other keys go to the game. When a game window (a menu or inventory) is open over Play,
mouse clicks go to that window.

See also: input-settings.md.

Questions: what are the play controls? how do I talk to an NPC? how do I attack? why does Esc stop the game instead
of opening the menu?

## Saving and loading during Play

- **Play ▸ Save game** (F6, while playing) saves the game to the quick-save slot: where you are, the player, the clock
  and story, and what changed in every scene you visited. "Game saved" shows in the game.
- **Play ▸ Load saved game** (F9, while playing) goes back to that save.
- **Play ▸ Continue from save** (F9, while editing) starts Play and loads the save.

Play-test saves are kept in the project's `.cache/saves` folder. If nothing has been saved yet, F9 says "No saved
game yet: press F6 while playing to save one". If the saved scene no longer exists, it says so.

Questions: how do I save the game while play-testing? how do I continue from a save? where are play-test saves?

## Play at a difficulty, and debug tools

- **Difficulty**: Play uses the game's default level (Project Settings ▸ Difficulty ▸ defaultLevel), unless you
  choose a level in the Project panel's Difficulty tab ("The level Play starts at"). Choosing a level while playing
  switches to it at once.
- **Frame stats**: F3 or View ▸ Frame stats.
- **Physics shapes**: View ▸ Physics shapes (colliders, triggers, bodies).
- **Paths**: View ▸ Paths also draws where each character is walking during Play.
- **Debug console**: the ` key opens it in play when Project Settings ▸ Game ▸ debugConsole is on. Commands include
  help, lua, god, give, tp, scene, quest, flag, speed, spawn, heal and kill. **F2** (or `profiler`) shows the
  profiler.

Questions: how do I test on hard difficulty? how do I open the debug console? how do I see what characters are
doing?

## Travelling between scenes in Play

Walking through a **gateway** in Play loads the scene it leads to, at its arrival marker. The scene you're editing
starts as it is on screen; other scenes start as saved on disk. What changed in each scene you visit is remembered
for the rest of the Play (and kept in a saved game). When you stop, the editor comes back to the scene you were
editing, as it was when you pressed Play.

Questions: does Play load other scenes? why does the other scene not have my changes? (save it first)
