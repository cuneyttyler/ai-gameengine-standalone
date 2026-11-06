# Effects and particles

Particle effects (fire, smoke, sparks, rain) and effects (what a hit, a spell or a death looks and sounds like) in the
editor: where to find them, how to preview them, how to make your own, and how to use them on entities, frames, scripts
and cutscenes.

## Project ▸ Effects ▸ Particles & effects

Open the Project panel's **Effects** tab and pick **Particles & effects**. Two lists:

- **Particles**: emitters such as fire, embers, smoke, sparks, sparks-fire, blood, dust, heal, magic-trail, explosion,
  rain, snow, leaves and fireflies (the engine's presets), plus the project's own.
- **Effects**: combined cues such as hit, hit-fire, death, fireball, footstep and heal (presets), plus the project's own.

Each row shows its id and a badge saying where it comes from (`engine` for a preset, else the project's record), and:

- **Try**: plays it in the middle of the viewport, so you can see it while editing.
- **Customise** (presets only): copies the preset into the project's records, to change it in Project ▸ Records
  (`particles` or `effect`). The project's record then takes the preset's place under the same id.
- **+ New** (beside each list's title): a new one from nothing. Give a name (lowercase, dashes); a particles record
  starts empty, an effect starts with sparks and a short white flash. Then set its parts in Records.

### Example: Trying the fire and making a project copy of it

1. In the Project panel, click **Effects**, then **Particles & effects**.
2. In **Particles**, find `fire` (badge `engine`) and click **Try**: flames play in the middle of the viewport.
3. Click **Customise** on the same row.

Result: `fire` is copied into the project's records (Project ▸ Records ▸ particles) and its badge changes from
`engine`; every campfire using `fire` now uses your copy, so you can make the flames taller or redder there. To start
from nothing instead, click **+ New** beside **Effects**, type `harvest-sparkle` and click **Create**.

Questions: how do I preview a particle effect? where are the effects? how do I change the fire effect? how do I make a new effect?

## Particles records: how an emitter looks and moves

A **particles** record (Project ▸ Records ▸ particles, or an engine preset) describes an emitter. Its fields:

| Field | Meaning |
|---|---|
| `rate` | particles per second while playing |
| `burst` | particles at once when it starts |
| `duration` | seconds it emits (0: until stopped) |
| `lifetime` | [min, max] seconds each particle lives |
| `shape`, `radius` | point, circle or box; the circle's radius or box's half-size, in cells |
| `speed` | [min, max] cells per second |
| `direction`, `spread` | degrees on screen (0 right, −90 up) and the spread around it |
| `gravity`, `drag`, `rise` | pull down (pixels/s², + down), slowing per second, extra upward speed |
| `size` | [start, end] in pixels |
| `color`, `alpha` | start and end colour (`["#ffd27a", "#ff3a00"]`) and opacity (`[0.9, 0]`) |
| `texture` | a picture for each particle (empty: a soft dot) |
| `layer` | `world` (sorted with the scene) or `above` (drawn over everything, sparks in the air) |
| `max` | the most alive at once |

Particles run on the CPU, up to 4000 in a scene, drawn as sprites.

### Example: Chaff blowing off the threshing floor

1. In Project ▸ Effects ▸ Particles & effects, click **+ New** beside **Particles**, type `wheat-chaff` and click
   **Create**.
2. Open Project ▸ Records ▸ particles ▸ `wheat-chaff` and fill it in:

   ```json
   {"schema": "aige.particles/1",
    "rate": 12, "lifetime": [1.0, 2.0],
    "shape": "box", "radius": 0.6,
    "speed": [0.3, 0.8], "direction": -30, "spread": 25,
    "gravity": 20, "drag": 0.5,
    "size": [3, 2],
    "color": ["#e8d38a", "#c9a85a"], "alpha": [0.9, 0],
    "layer": "world", "max": 60}
   ```

3. Save the record, then click **Try** on `wheat-chaff` in the Effects tab.

Result: small pale-gold flecks drift up and to the right from a box about a cell wide, slow down and sink as they
fade. Raise `rate` for more, `size` for bigger flecks.

Questions: what fields does a particles record have? how do I make smoke rise? how do I make particles bigger? how many particles can a scene have?

## Effect records: particles, sound, light, flash, shake

An **effect** (Project ▸ Records ▸ effect, or a preset) is what gameplay triggers: it says how something looks and sounds
when it happens, so it can be restyled without touching gameplay. It is a list of `parts`, each one of:

- `particles`: a particles id, with `at` (`target`…) where it plays;
- `sound`: a sound file to play;
- `light`: a short light (`color`, `radius`, `seconds`);
- `flash`: the target tinted for a moment (`color`, `time`);
- `shake`: screen shake (a trauma amount, 0–1);
- `hitstop`: the world frozen for a moment (seconds);
- `slowmo`: slow motion.

Example, the `hit-fire` preset: sparks-fire at the target, an orange light for 0.4 s, a warm flash, shake 0.25 and a
0.05 s hit-stop. An effect with no parts, or naming particles that don't exist, is reported as a problem. The
accessibility settings can reduce shake and turn off flashes.

### Example: A heavy hammer hit with a clang and a shake

1. In Project ▸ Effects ▸ Particles & effects, click **+ New** beside **Effects**, type `hammer-hit`, click **Create**.
2. Open Project ▸ Records ▸ effect ▸ `hammer-hit` and set its parts:

   ```json
   {"schema": "aige.effect/1",
    "parts": [{"particles": "sparks", "at": "target"},
              {"sound": "assets/sounds/anvil-clang.wav"},
              {"flash": {"color": "#ffffff", "time": 0.06}, "at": "target"},
              {"shake": 0.35},
              {"hitstop": 0.08}]}
   ```

3. Save it and click **Try** on `hammer-hit`.

Result: sparks fly at the target with a clang, the target flashes white, the screen shakes and the world freezes for
a moment. Players who reduce shake or turn off flashes in the accessibility settings get a gentler version.

Questions: what can an effect contain? how do I add screen shake to a hit? how do I add a sound to an effect?

## Using particles and effects in the game

- **ParticleEmitter** component (Inspector): `particles` (the id), `playing` (emitting now), `scale` (rate and size,
  0.05–20). A campfire prop with `fire`, a chimney with `smoke`.
- **EffectCues** component: `onHit`, `onDeath`, `onAttack`, each an effect's id: played when this entity is hit, dies or
  attacks.
- **Animation frame events**: a sheet clip's `events` with `effect:<id>` play that effect at the entity on that frame;
  `footstep` plays the `footstep` effect; `sound:<path>` plays a sound.
- **Lua**: `play_effect(id, x, y)` or `play_effect(id, entity)`; `emit_particles(id, x, y, seconds)`.
- **Cutscenes**: an `effect` track's `play` key.
- **Abilities and actions** play effects by name.

The AI panel checks that a ParticleEmitter, EffectCues or Animator it sets names something that exists (a preset, a
record, or one it writes in the same answer).

### Example: A campfire with flames, and a goblin that bursts into dust when it dies

1. Select the `campfire` prop in the scene. In the Inspector, pick **ParticleEmitter** in **+ Add component**; set
   **particles** to `fire`, tick **playing** and set **scale** to `0.8` for a small fire.
2. For smoke, place a second, empty entity just above the fire and give it a ParticleEmitter with `smoke` (an entity
   has one ParticleEmitter).
3. Select the goblin and add **EffectCues**; set **onDeath** to `death` and **onHit** to `hit`.
4. Press F5.

Result: the campfire burns and smokes in Play; the goblin flashes and throws sparks when struck and bursts into blood
and dust with a small shake when it dies. From a script, `play_effect("hit-fire", goblin_id)` plays the same kind of
effect on demand. Or ask the AI panel:

> "Give the campfire in the village square a fire and some smoke."

Questions: how do I add fire to a campfire? how do I play an effect when an enemy dies? how do I play an effect from a script? how do I add footstep dust?

## Effects and generated animations

Generated animations are sprite sheets; effects are drawn by the engine over them. Common combinations:

- A torch animated in Generate ▸ Animate (a looping `idle` flicker) plus a **ParticleEmitter** with `embers`, and a
  Light2D for its glow.
- A character's attack clip with an `effect:hit` event on the striking frame (added in the sheet's file), or an
  **EffectCues** `onAttack`.
- A spell: the character's `cast` clip (Generate ▸ Animate, motion cast) and the ability's effect (fireball, heal).

The Effects tab also holds **Sheets** (animation sheets, see Animations tab and sheet editor) and **Cutscenes** (a
timeline of tracks: actor, dialogue, camera, audio, screen, logic, effect; **Save** and **▶ Play**).

### Example: A wall torch that flickers, glows and throws embers

1. Animate `wall-torch.png` in Generate ▸ Animate (a flickering flame) and click **Make prefab** in its History: the
   prefab plays a looping `idle` flicker with the `prop` animator.
2. Place the torch on the wall and select it. Add a **ParticleEmitter** with **particles** `embers`, **playing**
   ticked.
3. Add a **Light2D** for its warm glow.

Result: the drawn flame flickers, embers rise from it and the wall around it is lit. For a sword swing that throws
sparks, add `{"frame": 3, "event": "effect:hit"}` to the attack clip's `events` in the sheet's file instead.

See also: Animations tab and sheet editor, The Animator.

Questions: how do I combine an animation with particles? how do I add an effect on an attack frame?
