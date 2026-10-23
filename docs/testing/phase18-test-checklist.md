# Phase 18 — Manual Test Checklist

The user-test checkpoints from Phase 18 (`development_plan.md`, "Animation and effects"), for
one end-of-phase pass. Automated tests cover the logic: sheets, directions and mirroring,
Aseprite and grid import, animator conditions, an animator walking, attacking with its frame
event and dying, particles spawning and dying within a budget, effects with shake, hit-stop and
flash, day and night ambient and night-only lights, tweens, cutscene keys, animated corpses and
the AI's `write_anim` (`fx_test.cpp`, `test_fx.py`). This list covers how it looks. The design is
`effects-animation.md` (see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A character sheet: a picture with frames in rows (idle, walk south/east/north, attack, die),
  e.g. 4 columns of 32×32.

## 18.1–18.2 Characters

- [ ] Project ▸ **Effects** ▸ Sheets ▸ New sheet from a grid (your picture, 32×32, 4 columns, rows
      `idle, walk_s, walk_e, walk_n, attack, die`): the sheet appears, each clip playing.
- [ ] Or export an Aseprite sheet as JSON with tags (idle, walk_e…) and Import it.
- [ ] Give the player **Animator** (sheet: yours, animator: humanoid). In Play: idle standing,
      walking in each direction (west shows east mirrored), attacking on Space, a hurt flinch when
      hit, and an enemy with an Animator plays its death before it disappears.
- [ ] An `events` entry `{"frame": 1, "event": "effect:hit"}` on the attack clip shows sparks on
      that frame.

## 18.3–18.5 Tweens, particles, effects

- [ ] Effects ▸ Particles & effects: **Try** fire, smoke, sparks, explosion, rain: each plays in
      the middle of the viewport while editing.
- [ ] Try the *fireball* effect: particles, an orange light (with Lighting on), a shake and a short
      freeze.
- [ ] **Customise** *sparks*: it appears in Records ▸ particles; change its colour; Try shows it.
- [ ] A torch entity with **ParticleEmitter** (fire) burns in Play; an enemy with **EffectCues**
      (onHit: hit, onDeath: death) sparks when hit and bleeds when it dies.
- [ ] Lua `tween(self_id(), "Sprite.a", 0, 1)` fades a ghost out; `shake(0.6)` shakes the screen.

## 18.6 Lighting and weather

- [ ] Project Settings ▸ Lighting.enabled on, Time.startHour 21: the scene is dark blue; a
      **Light2D** (flicker 0.5) on a torch lights the ground around it and flickers; a window
      sprite with **emissive** stays bright; a lamp with nightOnly is off at noon (startHour 12).
- [ ] With the clock running, dusk darkens the scene gradually and dawn brightens it.
- [ ] A **Weather** entity (Rain, 0.7): rain falls around the player and the scene is greyer;
      Snow drifts.
- [ ] Lighting.vignette darkens the screen's edges.

## 18.7 The cutscene timeline

- [ ] Effects ▸ Cutscenes ▸ new "intro": add an actor track (player) with a move key, a dialogue
      line, a screen fade_in and an effect track playing *fireball*; the keys sit on the ruler by
      their time; edit a key's JSON and Apply. **▶ Play**: the game starts and the cutscene plays.

## The AI panel

- [ ] Ask the AI "animate the player with assets/hero.png, 32×32 frames, rows idle, walk south,
      walk east, walk north": a write_anim and an Animator on the player, passing the checks.
- [ ] Ask "make the campfire burn and light the area at night": a ParticleEmitter (fire) and a
      Light2D (nightOnly off, flicker), and Lighting enabled.
- [ ] Ask "a fire spell hit effect with sparks, an orange flash and a small shake": an effect record.
