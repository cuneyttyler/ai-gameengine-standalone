# Animation by AutoSprite

Preferences ▸ AI ▸ Animate: **AutoSprite** (`autosprite`, the default) or PixelLab Animate with text v3
(`pixellab:animate-v3`). The key is `AUTO_SPRITE_API_KEY` (or `AUTOSPRITE_API_KEY`) in `ai_service/.env`; API access
needs AutoSprite's Pro plan. `ai_service/app/autosprite.py`, tested in `builder_tests/autosprite`.

## What it does

- **A picture** (Generate ▸ Animate, an image): uploaded as an AutoSprite character (free), then
  `POST /characters/{id}/spritesheets` with one animation, turbo tier (5 credits). AutoSprite animates the picture
  **in its own view only, one direction**: what's drawn facing east moves facing east. 12 frames, cut from the sheet,
  fitted to the picture's size, written as before (frames + a spritesheet in
  `assets/textures/generated/animations/<name>/`).
- **A character** (a character sheet): its south idle pose uploaded once (the AutoSprite character id is kept in the
  sheet's `character.autosprite`), then **one isometric pack**, `POST /characters/{id}/packs`: **all 8 directions in
  one render** (15 credits). The pack's sheets are named `iso_<action>_<direction>` (up, northeast, right, southeast,
  down, southwest, left, northwest → north … north-west). Every direction the character has gets its own frames (no
  mirroring, no "north-east plays north"); the figure is brought to the idle pose's height and the clip added to the
  sheet at 12 frames a second.

Single directions (`iso_walk_<direction>`, east side only, a tier's price each) and the side-view kinds are one
direction each, so characters always use the pack: 8 directions for the price of 3 single ones.

## Humanoid or not

AutoSprite rigs a character as a person unless told `isHumanoid: false` (a chest, a torch, a wolf). It's decided from
what the thing is (the Animate form's "What it is", the picture's `.gen.json` description, its name, a character's
description): people words (herbalist, knight, goblin, villager…) or thing / animal words (chest, door, torch, wolf,
slime…); when it says neither or both, the LLM is asked; failing that, a character is humanoid and a picture isn't.
The choice is kept (`humanoid` in the `.gen.json` and the character sheet) and returned to the editor.

## Motions

With AutoSprite, the Animate panel offers its motions (autosprite.io/docs/reference-animation-types) by group —
basic (idle, walk, run, attack, jump), combat, magic and skills, emotes, movement — or **custom**, said in words ("the
lid swings open, gold glints inside"). Words typed elsewhere ("walking", "dying") are mapped to a motion when they
name one, else sent as custom.

## Costs (Pro, $29 a month, 1,500 credits ≈ $0.019 each)

| | Credits | ≈ $ |
|---|---|---|
| A character's action, all 8 directions (pack) | 15 | 0.29 |
| A picture's action (turbo, 2 s) | 5 | 0.10 |
| Uploading a picture | free | |

Failed jobs are refunded. Jobs are polled every 5 s (a picture takes about a minute, a pack 2 to 5); a 429 waits for
its `Retry-After`.
