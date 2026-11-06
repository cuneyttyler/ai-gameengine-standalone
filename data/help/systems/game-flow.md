# Game flow, saves, physics and audio

How a finished game runs as a whole: game.json, the title, pause, game over and victory screens, saving and loading,
the world's memory between scenes, what kinds of game the engine makes now, game physics, and scene music.

## game.json and the screens

The game's manifest `game.json` in the project folder: `title` and `subtitle` (window title and title screen; else the
folder's name), `titleScreen` (false goes straight into play), `titleBackground`, `titleMusic`, `autosave` (save to the
auto slot at every scene change), `credits`, and `genre` and `systems` (written by the AI Planner). The start scene is in
Project Settings ▸ Game.

| screen | items |
|---|---|
| Title | Continue (the newest save), New game, Load game, Credits, Quit |
| Paused (Esc) | Resume, Save game, Load game, Quit to title, Quit game |
| Game over (death) | Load last save, Start again, Quit to title |
| Victory | Play again, Quit to title (all enemies defeated with no quest under way) |
| Credits | the manifest's credits, Back |

Menus use the `menu` input context (ui_up, ui_down, ui_select, ui_back; `menu` opens and closes the pause menu), so
keyboard and gamepad both work. Scene changes through gateways fade from black. In the editor, Play starts straight in
the scene being edited (no title screen); Esc or Stop returns to editing.

Questions: how do I set the game's title? how do I skip the title screen? when does the game show Victory? how do I add
credits?

## Saves and the world's memory

Slots: **quick** (F6 saves, F9 loads), **auto** (each scene change, with autosave on), and **slot1-slot6** (Save game).
A save holds the scene, the player entity, the clock, the story (flags, variables, journal, quests) and each visited
scene's changes. Saves go to `%LOCALAPPDATA%\AIGE\saves\<game>\<slot>.json`; the editor's Play saves to the project's
`.cache/saves/`. **Play ▸ Continue from save** starts Play from the save. A save is written to a temporary file and
renamed, so a crash keeps the old one.

Every scene remembers what changed since it was loaded (an enemy killed, a pickup taken, a field changed, a component
added or removed, something spawned), when the player comes back through a gateway or loads a game. Entities are
identified by their order in the scene file, so editing a scene between a save and a load can shift what a save applies
to.

Questions: how do I save the game? where are saves stored? do killed enemies stay dead when I come back? why did my save
load wrong after editing the scene?

## Kinds of game

Today the engine makes 2D games on an isometric or top-down grid: top-down action-adventures, RPGs, life-sims and
farming games, 2D isometric RPGs and action RPGs, and dungeon crawlers. Screen-only games are partly supported. Side
view (platformers, metroidvanias), shoot 'em ups, turn-based tactics, strategy at scale and 3D are planned for later
versions. The AI Planner records a game's `genre` and which `systems` are on in game.json.

Questions: what kinds of games can I make? can I make a platformer? does the engine do 3D?

## Game physics

- **Collision:** characters move against the tile grid and Obstacle footprints with collide-and-slide. **Collider2D**
  has a shape (box or circle), radius, `layer` and `mask` (a comma list; empty: everything). An entity's layer is its
  collider's, else projectile, trigger, its team's (player, enemy), ally, or default; solid tiles are `world`.
- **Triggers:** the **Trigger** component (name, mask, once, radius or the collider) sends `trigger_enter` and
  `trigger_exit` with its name.
- **Controllers:** **CharacterController2D**: top-down, or grid-step (`stepTime`, `diagonal`) moving tile by tile.
- **Rigid bodies** (Box2D): **RigidBody2D** (static, kinematic, dynamic; density, friction, restitution, damping,
  gravity scale, fixed rotation, bullet) and **Joint2D** (revolute, distance, weld). Characters push crates. Gravity:
  Project Settings ▸ Physics gravityX/Y. A scene without rigid bodies never makes a physics world.
- **Queries** in Lua: `raycast`, `overlap`, `line_of_sight`; `apply_impulse`, `set_velocity`.
- **Debug:** F5 in play (View ▸ Physics shapes in the editor) draws colliders yellow, triggers green, dynamic bodies
  orange and static grey.

Not yet: dragging collider gizmos, one-way tiles, platformer controllers.

Questions: how do I make a trigger area? how do I make a crate that can be pushed? how do I see colliders? how do I make
grid movement?

## Music and sound

- The **Music** component sets a scene's audio: `track` (looping music: WAV, MP3 or FLAC; empty: silence), `ambience`
  (a loop under it: wind, a crowd, the sea), and `combat` (music while an enemy chases the player; empty: the game's).
  The first entity with Music in the scene decides; changing scene cross-fades.
- The **Sounds** component plays an entity's attack, hurt, death and collect sounds.
- Lua: `play_sound(path)`, `play_sound_at(path, x, y)` (quieter with distance, panned), `play_music(path)` (`""` back
  to the scene's). Cutscenes have an audio track (music, sound); effects can include a sound; game.json's titleMusic
  plays on the title screen.

Questions: how do I add background music to a scene? how do I play combat music? how do I give an enemy a death sound?
