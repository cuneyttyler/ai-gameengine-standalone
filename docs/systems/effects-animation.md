# Animation and effects — sprites, particles, lighting, screen effects, cutscenes

Status: **built, to be tried (2026-10-22)**; see "As built" at the end. Phase 18 in `development_plan.md`. The AI
Planner gets an **Animations** step once this exists (`ai-planner.md` §5.8); cutscene
files are written from the story earlier (§8).

---

## 1. Goal

Today every entity is one still image; the only "effects" are a red hit flash and a fade
for sleeping NPCs. A finished 2D game needs:
- **sprite animation**: clips, directions, state machines, events on frames;
- **property animation and tweens**: any field over time;
- **particles**: fire, smoke, sparks, blood, magic, weather;
- **effects** as one concept combining particles, sound, light and screen effects;
- **screen effects** ("juice"): shake, hit-stop, flash, slow motion;
- **2D lighting**, day and night, weather, post-processing;
- **cutscenes**: scripted sequences on a timeline.

## 2. What engines use

| Engine / tool | Animation | Effects | Sequences |
|---|---|---|---|
| Unity | AnimationClip + **Animator** state machines; 2D Animation (sprite skinning) | Shuriken particles; VFX Graph; URP **2D lights**; Cinemachine impulse (shake) | **Timeline** |
| Godot | **AnimatedSprite2D** (SpriteFrames); **AnimationPlayer** animates any property; **AnimationTree** state machines and blend spaces; **Tween** | GPU/CPU **Particles2D**; **Light2D** with occluders | AnimationPlayer tracks |
| Unreal | Paper2D **flipbooks**; Animation Blueprints | **Niagara**: emitters as **stacks of modules** (spawn, update, render) | **Sequencer** |
| Spine, DragonBones | skeletal 2D (bones, meshes, skins) | | |
| Aseprite | frames, **tags** (clips), per-frame durations, **slices** | | |
| Creation Kit | behaviour graphs | magic effect art, shaders | **Scenes** (actors, phases, dialogue, packages) |

Design references for juice: Jonasson & Purho, *Juice it or lose it* (2012); Nijman, *The
art of screenshake* (2013); Eiserloh, *Juicing Your Cameras With Math* (GDC 2016) for
trauma-based shake.

## 3. Sprite animation

### 3.1 Clips

An **animation clip** (`aige.clip/1`, in the asset's `.meta` or a `.anim.json`) is a list
of frames, each a region of a texture with a duration:

| Field | Meaning |
|---|---|
| `frames` | `[{texture, region, duration, anchor?}]` |
| `loop` | loop, once, ping-pong, hold last |
| `events` | `[{frame, event, args}]`: `footstep`, `hit-start`, `hit-end`, `spawn-effect`, `sound`, custom |
| `directions` | a **directional set**: one clip per direction (4 or 8), with mirroring (east mirrored for west) |

**Import:**
- **Aseprite** (`.aseprite`/`.ase`, or its JSON export): tags become clips, frame
  durations kept, slices become anchors and hit areas.
- **Sheets**: grid detection and frame segmentation from the catalog's analysis (7.3);
  rows named by direction or action are proposed as clips for confirmation.
- **The catalog** gets `animated: true` and the clips, so slot retrieval can require an
  animated asset for a character.

### 3.2 Animators

An **animator** (`aige.animator/1`, a record) is a state machine driving clips:

```json
{ "schema": "aige.animator/1", "id": "humanoid",
  "parameters": { "speed": "float", "dir": "direction", "attacking": "trigger", "hurt": "trigger", "dead": "bool" },
  "bindings": { "speed": "Velocity2D.length", "dir": "Facing", "dead": "Dead" },
  "states": [
    { "id": "idle", "clip": "idle" }, { "id": "walk", "clip": "walk" },
    { "id": "attack", "clip": "attack", "exit": "end" }, { "id": "hurt", "clip": "hurt", "exit": "end" },
    { "id": "die", "clip": "die" } ],
  "transitions": [
    { "from": "idle", "to": "walk", "if": "speed > 0.1" }, { "from": "walk", "to": "idle", "if": "speed <= 0.1" },
    { "from": "*", "to": "attack", "if": "attacking" }, { "from": "*", "to": "hurt", "if": "hurt", "priority": 2 },
    { "from": "*", "to": "die", "if": "dead", "priority": 9 } ] }
```

- **Parameters bind to components** (velocity, facing, health), so most characters need no
  script; triggers come from events (`attack_started`, `damaged`).
- **Layers:** a body layer and overlay layers (a held weapon, a hit flash tint).
- An entity's **`Animator` component** names the animator and the clip set (which clips
  fill `idle`, `walk`…), so one animator serves every humanoid.
- **Templates:** humanoid (idle, walk, run, attack, cast, hurt, die, use, sit, sleep),
  quadruped, flyer, prop (idle, open, close, break), platformer (idle, run, jump, fall,
  wall-slide).

### 3.3 Skeletal and cut-out animation (later)

Bones with sprite parts (cut-out) suit large characters and smooth motion. Spine's runtime
licence doesn't fit an engine we ship; options are the DragonBones format or our own bones.
Deferred until a genre needs it.

## 4. Property animation and tweens

- **Tracks** over any reflected field (`Sprite.a`, `WorldTransform2D.x`, a light's radius)
  with keyframes and easing curves; used by cutscenes, UI and effects.
- **Tweens** in Lua: `tween(id, "Sprite.a", 0.0, 0.5, "ease_out")` returns a handle;
  `wait_tween(h)` in a coroutine; sequences and parallels.

## 5. Particles

An **emitter** (`aige.particles/1`, a record) is a stack of modules, as in Niagara:

| Stage | Modules |
|---|---|
| spawn | rate, burst, distance-travelled (trails), on event |
| initialise | lifetime, position shape (point, circle, box, edge, sprite outline), velocity (cone, radial), size, rotation, colour, sprite or animated sheet |
| update | gravity, drag, colour over life, size over life, rotation speed, noise/turbulence, attract to point, orbit, collide with the tile grid (bounce, die), sub-emitter on death |
| render | blend (alpha, additive), sort with the y-sorted world or above it, layer, light emission |

- **CPU first** (2D needs are modest: thousands of particles per scene), pooled, with a
  per-scene budget; GPU particles later.
- **Local or world space**; emitters can follow entities.
- **Preset library:** fire, embers, smoke, sparks, blood, dust puff, footstep dust, heal
  glow, magic trail, explosion, rain, snow, falling leaves, fireflies.

## 6. Effects (cues)

An **effect** (`aige.effect/1`, a record) is the one concept gameplay triggers:

```json
{ "schema": "aige.effect/1", "id": "hit-fire",
  "parts": [ { "particles": "sparks-fire", "at": "target" }, { "sound": "audio/fx/fire_hit.wav" },
             { "light": { "color": "#ff9a3c", "radius": 3, "fade": 0.3 } },
             { "flash": { "target": "target", "color": "#ffffff", "time": 0.08 } },
             { "shake": { "trauma": 0.25 } }, { "hitstop": 0.05 } ] }
```

Triggered by events, ability cues (`magic.md` §5), animation frame events, Lua
(`play_effect(id, at)`) and cutscenes. This is Unreal's Gameplay Cue idea: gameplay says
*what happened*; the effect says *how it looks and sounds*, and can be restyled without
touching gameplay.

## 7. Screen effects, lighting, weather, post

### 7.1 Juice

| Effect | Model |
|---|---|
| screen shake | trauma (0–1) that decays; offset and rotation ∝ trauma², from smooth noise (Eiserloh) |
| hit-stop | freeze the world (or the two combatants) for 30–100 ms |
| flash | a sprite tinted white or a colour for a moment (today's HitFlash, generalised) |
| slow motion | time scale for a duration, with easing |
| camera kick, zoom punch | short camera tweens |
| damage numbers | world-space UI (`game-ui.md` §4) |
| squash and stretch | scale tweens on jump, land, hit |

Each respects the accessibility settings (reduce shake, no flashes).

### 7.2 2D lighting

- **Ambient light** per scene, with a **day–night curve** over the game clock.
- **Lights**: point and spot, colour, intensity, radius, falloff, flicker; a `Light2D`
  component; effects can add short-lived lights.
- **Emissive** sprites (windows at night, glowing runes).
- **Shadows** from occluders (walls from the tile grid) later; normal-mapped sprites later.
- **Perception** reads light at a cell (stealth, `ai-packages.md` §8).

### 7.3 Weather

Per region or scene: rain, snow, fog, sandstorm, wind, as particle layers + ambience +
lighting tint + gameplay hooks (perception range, movement), changing by schedule or chance.

### 7.4 Post-processing

Colour grading (a LUT per place or mood), vignette, bloom (light), pixel-perfect scaling,
CRT and dithering filters for retro genres, fades for transitions.

## 8. Cutscenes

A **cutscene** (`cutscenes/<id>.json`) is a timeline of tracks:

| Track | Keys |
|---|---|
| actor | move to a marker or along a path, face, play a clip or animator state, appear, disappear |
| dialogue | lines with speaker, subtitle and voice; choices end the cutscene into a dialogue |
| camera | move, zoom, follow an actor, shake, cut |
| audio | music cue, sound, ambience change |
| effect | play an effect at a marker or actor |
| screen | fade in/out, letterbox, title card |
| logic | set a variable or flag, advance a quest stage, emit an event, run Lua, wait for an input |

- Started by a quest stage, a trigger area, a dialogue effect or Lua; **skippable** (jumps
  to the end state: every logic key applied, actors at their end marks).
- Actors are aliases (`@thief`), so a cutscene works with whoever fills the role.
- **Story scenes in `cutscene` mode** (`story-ontology.md` §4.1) are written by the Planner
  as cutscene files in ⑫, with actor marks from the place's anchors.
- The timeline editor previews in the viewport.

## 9. Editor

- **Animation panel:** clip editor (frame strip from a sheet, durations, events, directions,
  onion skin), animator graph (states and transitions, live parameters while playing),
  preview on the selected entity.
- **Particle editor:** the module stack with live preview in the viewport.
- **Effect editor:** parts list with "play" at the cursor.
- **Lighting:** lights visible and editable in the viewport; a time-of-day slider.
- **Timeline editor** for cutscenes: tracks, keys, scrubbing, play in the viewport.

## 10. Files

| File | Holds |
|---|---|
| `*.anim.json` or the asset's `.meta` | clips and directional sets |
| `records/animators/*.json` | animators |
| `records/particles/*.json`, `records/effects/*.json` | emitters, effects |
| `cutscenes/<id>.json` | cutscenes |
| scene components | `Animator`, `ParticleEmitter`, `Light2D`, `Weather` (scene settings) |

## 11. The Planner's Animations step (later)

Once Phase 18 exists:
1. For each character class: the animator template and the clips it needs; the asset plan
   sources animated sprites (project, library, web), or generates frames (hard to keep
   consistent; checked by the vision model frame to frame).
2. For each ability and hit type: an effect from the library, or composed from presets in
   the ability's school colours (`magic.md` §9).
3. For each place's mood: lighting, weather and grading.
4. Cutscenes are already written from the story; this step adds their camera work and
   effects.

## 12. As built (Phase 18)

- **Sheets** are `assets/<name>.anim.json` (`aige.anim/1`): a texture, its `size`, an optional
  frame `grid`, and `clips` with `frames` (grid indices or `{region, duration}`), `fps`, `loop`
  (loop, once, pingpong, hold), `events` (`{frame, event}`: `effect:<id>`, `sound:<path>`,
  `footstep`, or a name scripts hear as `anim_event`), and `directions` (s, se, e, ne, n, nw, w,
  sw; `"mirror:e"` flips east for west; a missing one uses the nearest). Import: Aseprite's JSON
  export (tags become clips, `walk_e` the east of `walk`) and a grid with named rows.
- **Animators:** built-in humanoid, quadruped, flyer and prop, or `animator` records; transitions
  by conditions over speed, dead, health and the triggers attacking, hurt, casting, using (set by
  combat events, cutscenes and `anim_trigger`). An `Animator` component names the sheet and the
  animator; an animated character that dies stays `corpseSeconds` for its death clip.
- **Tweens** over any reflected number (Lua `tween`, cutscene `tween` keys), with linear,
  ease_in, ease_out, ease_in_out and bounce.
- **Particles** run on the CPU (a budget of 4000), drawn as sprites sorted with the world (or
  above it); `particles` records or the presets in `data/fx/particles`; a `ParticleEmitter`
  component; additive blending, collisions and sub-emitters are later work.
- **Effects** are `effect` records or presets (`data/fx/effects`): particles, sound, a short light,
  a flash, shake (trauma, Eiserloh's squared noise), hit-stop and slow motion; played by
  `EffectCues`, animation events, cutscenes (`effect` tracks) and Lua `play_effect`.
- **Lighting:** Project Settings ▸ Lighting turns on an ambient light that follows the clock
  (day and night ambients, dawn and dusk) and `Light2D` lights (flicker, night only); the
  renderer tints each sprite corner; `Sprite.emissive` glows in the dark. A `Weather` component
  brings rain, snow or sand around the player and dims the light. A vignette is the only
  post effect: shadows, normal maps, colour grading, bloom and CRT filters need render targets
  and come with the renderer's next steps.
- **Editor:** Project ▸ **Effects** tab: sheets with their clips playing, made from a grid or an
  Aseprite export; particles and effects to Try in the viewport and Customise into records; the
  cutscene timeline (tracks, keys by time, a key's JSON, Save, ▶ Play). Records edit animators,
  particles and effects.
- **The AI:** `write_anim` for sheets; the context lists sheets, particles, effects and animators;
  components naming a missing one are flagged.
- **Accessibility:** less shake and no flashes (`FxAccessibility`; the settings screen comes with
  the settings tabs).
