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

### Example: testing the new bridge before saving

You just placed a rope bridge across the gorge in `ravine_pass` and haven't saved.

1. Press F5. Play starts with the scene as it is now, bridge included, and the camera follows the player.
2. Press W and D to walk across (the viewport already has the keyboard focus, no click needed).
3. The bridge works: press F5 (or Esc in the viewport) to stop, then Ctrl+S to save.
4. The game clock started at Project Settings ▸ Time ▸ startHour, and the dialogue and quests were read fresh from
   disk, so a dialogue you edited a moment ago is the one you heard.

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

### Example: tuning a wolf's speed while playing

1. Press F5 and find the wolf. It's too quick to dodge.
2. Click the wolf in the Scene Tree and lower its speed in the Inspector while the game runs; try a few values.
3. A value of 2.5 feels right. Note it down.
4. Press F5 to stop: the wolf, the player and everything else snap back to how they were when Play started,
   including the speed you changed.
5. Select the wolf again and set 2.5 in the Inspector, then press Ctrl+S. Now it sticks.

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

### Example: a first conversation with Vara Bell, the innkeeper

1. Press F5 and walk to Vara Bell with WASD.
2. Press E (or Space) beside her: the conversation opens.
3. Press 1 for her first answer ("A room for the night"), or use Up/Down and Enter.
4. Press I to check your inventory for the room key she gave you, then J for the journal.
5. Esc stops Play rather than opening the game's menu, because the editor keeps Esc while play-testing.

Questions: what are the play controls? how do I talk to an NPC? how do I attack? why does Esc stop the game instead
of opening the menu?

## Saving and loading during Play

- **Play ▸ Save game** (F6, while playing) saves the game to the quick-save slot: where you are, the player, the clock
  and story, and what changed in every scene you visited. "Game saved" shows in the game.
- **Play ▸ Load saved game** (F9, while playing) goes back to that save.
- **Play ▸ Continue from save** (F9, while editing) starts Play and loads the save.

Play-test saves are kept in the project's `.cache/saves` folder. If nothing has been saved yet, F9 says "No saved
game yet: press F6 while playing to save one". If the saved scene no longer exists, it says so.

### Example: testing the boss fight again and again

1. Press F5 and play up to the door of the troll's cave.
2. Press F6 (Play ▸ Save game). "Game saved" shows in the game.
3. Fight the troll. Lost? Press F9 (Play ▸ Load saved game): you're back at the cave door, with the clock and story as
   they were.
4. Press F5 to stop and change the troll's health. Then press F9 while editing (Play ▸ Continue from save): Play
   starts and drops you straight at the cave door.

If F9 says "No saved game yet: press F6 while playing to save one", nothing is in `.cache/saves` yet.

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

### Example: skipping to the end of the quest with the debug console

1. Open File ▸ Project Settings… ▸ Game and tick debugConsole.
2. Press F5, then the ` key. The console opens.
3. Type these, one per line:

```
god
give healing_potion 5
quest lost_ring complete
flag met_the_witch on
speed 2
```

4. The player can't be hurt, carries 5 healing potions, the quest `lost_ring` is done, the flag is set and the game
   runs twice as fast. Type `help` for every command, or press F2 for the profiler.

### Example: checking the dungeon on a harder level

1. In the Project panel, open the Difficulty tab and set "The level Play starts at" to your hardest level.
2. Press F5: the game plays at that level. Change the level there while playing and it switches at once.

Questions: how do I test on hard difficulty? how do I open the debug console? how do I see what characters are
doing?

## Travelling between scenes in Play

Walking through a **gateway** in Play loads the scene it leads to, at its arrival marker. The scene you're editing
starts as it is on screen; other scenes start as saved on disk. What changed in each scene you visit is remembered
for the rest of the Play (and kept in a saved game). When you stop, the editor comes back to the scene you were
editing, as it was when you pressed Play.

### Example: walking from the village into the inn

You edited both `village_green` and the inn's scene `gilded_goose`, but only saved the village.

1. With `village_green` open, press F5 and walk through the inn's door (a gateway).
2. `gilded_goose` loads at its arrival marker, but as saved on disk: the tables you moved there aren't moved.
3. Press F5 to stop: you're back in `village_green` as it was when you pressed Play.
4. Open `gilded_goose`, save it (Ctrl+S), and try again: now the inn shows your changes.

Questions: does Play load other scenes? why does the other scene not have my changes? (save it first)
