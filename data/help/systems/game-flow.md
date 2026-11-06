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

### Example: the title and credits for "The Golden Goose"

1. Open the project's `game.json` and set:

```json
{
  "title": "The Golden Goose",
  "subtitle": "A tale of Ravenford",
  "titleScreen": true,
  "titleBackground": "assets/ui/title-inn.png",
  "titleMusic": "assets/audio/title-theme.mp3",
  "autosave": true,
  "credits": "Story and art: Ana Reyes\nMusic: Tom Blake\nMade with AIGE"
}
```

2. In Project Settings ▸ Game, set the start scene to `scenes/ravenford.aige`.
3. Export and run the game.

The title screen shows "The Golden Goose" and "A tale of Ravenford" over the inn picture with the title theme;
Credits shows the three lines. With `"titleScreen": false` the game would start straight in Ravenford.

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

### Example: testing that the bandit stays dead

1. Press Play in `scenes/ravenford.aige`, kill the bandit by the bridge and pick up the potion he dropped.
2. Press **F6** (quick save). Walk through the gateway into the forest, then back: the bandit and the potion are still
   gone, because Ravenford remembers its changes.
3. Drink the potion, then press **F9**: the quick save loads, with the potion back in the inventory.
4. Stop Play. Choose **Play ▸ Continue from save** to start straight from that save (it lives in the project's
   `.cache/saves/`).

If you now insert a new barrel near the top of the Hierarchy and load the save again, a different entity may come back
changed, because saved changes follow the entities' order in the scene file. Start a new game after big scene edits.

Questions: how do I save the game? where are saves stored? do killed enemies stay dead when I come back? why did my save
load wrong after editing the scene?

## Kinds of game

Today the engine makes 2D games on an isometric or top-down grid: top-down action-adventures, RPGs, life-sims and
farming games, 2D isometric RPGs and action RPGs, and dungeon crawlers. Screen-only games are partly supported. Side
view (platformers, metroidvanias), shoot 'em ups, turn-based tactics, strategy at scale and 3D are planned for later
versions. The AI Planner records a game's `genre` and which `systems` are on in game.json.

### Example: a cosy farming game, and a platformer that isn't possible yet

1. In the AI panel, type:

   > "a top-down farming life-sim in a small valley village, with crops, an inn and a few villagers with daily lives"

2. The AI Planner builds it on the top-down grid and records the kind of game in `game.json` (its `genre`, and
   `systems` such as farming, crafting and daily routines).
3. Asked for "a side-scrolling platformer with wall jumps", the editor can't make it today: side view and platformer
   controllers are planned for later versions. A top-down action-adventure is the
   nearest you can build now.

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

### Example: a pushable crate and a pressure plate

1. Select the crate and add from **+ Add component**: **Collider2D** (shape `box`), and **RigidBody2D** (type
   `dynamic`, density `2`, friction `0.6`, damping `4`, **fixed rotation** ticked).
2. Select the pressure plate: add a **Trigger** with **name** `plate`, **radius** `0.5`, and a script:

```lua
on("trigger_enter", function(e)
  if e.text ~= "plate" then return end
  local gate = entity_named("vault-gate")
  if gate then set_field(gate, "Door", "open", true) end
end)
on("trigger_exit", function(e)
  if e.text ~= "plate" then return end
  local gate = entity_named("vault-gate")
  if gate then set_field(gate, "Door", "open", false) end
end)
```

3. Press Play, then **F5**: colliders are yellow, the plate's trigger green, the crate orange.

Walking into the crate pushes it; slide it onto the plate and the vault gate opens, and it shuts again if the crate is
pushed off.

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

### Example: the inn's music, the crypt's battle theme, and a skeleton's death rattle

1. In `scenes/golden-goose.aige`, select any entity (or an empty one named `music`) and add **Music**: **track**
   `assets/audio/inn-lute.mp3`, **ambience** `assets/audio/crowd-murmur.wav`.
2. In `scenes/crypt.aige`, add **Music**: **track** `assets/audio/crypt-drone.flac`, **combat**
   `assets/audio/crypt-battle.mp3`.
3. On the skeleton prefab, add **Sounds** with its death sound `assets/audio/bone-rattle.wav`.
4. For the boss, a script switches the music when the fight starts:

```lua
on("hit", function(e)
  if e.entity == self_id() then play_music("assets/audio/boss-theme.mp3") end
end)
on("death", function(e)
  if e.entity == self_id() then play_music("") end    -- back to the scene's track
end)
```

In the inn a lute plays over a murmuring crowd; walking down to the crypt cross-fades to the drone, which turns into the
battle theme while a skeleton chases the player. Skeletons rattle as they fall, and the boss has its own theme until it
dies.

Questions: how do I add background music to a scene? how do I play combat music? how do I give an enemy a death sound?
