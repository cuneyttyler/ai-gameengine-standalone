# Generate ▸ Animate: making pictures and characters move

The Animate tab of the Generate panel makes a picture of the project (a torch, a chest, a knight) or a whole character
sheet move: walking, attacking, a lid opening. This page covers the form, the animation models (AutoSprite, PixelLab,
FLUX video), people versus things, directions, and what to do with the result.

## The Animate tab: picking what to animate

Open the Generate panel ▸ **Animate**. The left column lists what can be animated, with a search box on top ("Find an
image: knight, torch, characters…"):

- **Characters: every direction at once**: the project's character sheets. Picking one animates every direction it
  faces and adds the result to its sheet as a new clip.
- **Images**: the project's PNG pictures, newest first (up to 120 shown; spritesheets, character sheet pictures and
  placeholders are left out). Picking one animates that picture in its own view.

The **Image** row on the right shows what is picked ("knight.character.anim.json (every direction)" for a character).
Other places pick for you: a character's **Animate…** (Generate ▸ Character, Assets ▸ Characters right-click) and a
loose direction picture's **Animate** button. A picture whose name ends in a direction (`-east.png`) sets the facing.

Questions: how do I animate an image? how do I animate a character in all directions? where is the Animate tab?

## The Animate form with AutoSprite (the default)

When humanoid animation is by **AutoSprite** (Project Settings ▸ AI ▸ animate, else Preferences ▸ Art and painting ▸
Humanoid animation), the form shows:

- **Motion**: AutoSprite's motions, by group. Basic: idle, walk, run, attack, jump. Combat: punch, kick, block, hurt,
  hit react, dodge, dodge lean, death, shoot. Magic and skills: cast, pickup, push, pull, throw. Emotes: wave, sit,
  kneel, point, sleep, victory, dance, talk. Movement: dash, fall, climb, swim, crouch, roll, fly, slide. Or
  **custom: say it…**, which shows **Describe it**: the motion in words ("the lid swings open, gold glints inside").
- **Weapon** (a character only): **holds a weapon** and a weapon **class** (sword, axe, spear…). See the section on
  weapons below.
- **What it is** (optional): "a knight in plate armour". Helps tell a person from a thing.
- **Name** (optional): "knight-walk"; else made from the picture's name and the action.

Press **Animate**. It says "Animating (about a minute)…" for a picture, "Animating all 8 directions (2 to 5 minutes)…"
for a character. The button is off while AI is off, without `AUTO_SPRITE_API_KEY` in `ai_service/.env`, without a
picture or character picked, or (custom) without a description.

Questions: what motions can AutoSprite make? how do I make a custom animation? what is "What it is" for?

## The Animate form with PixelLab Animate v3

When humanoid animation is **PixelLab Animate with text v3**, the form instead has:

- **Action**: the action in words, with suggestions (walking, running, idle breathing, attacking with a sword, casting a
  spell, jumping, flickering flame).
- **What it is** (optional), **Facing** (the direction the picture faces: south, south-east, east… for a single
  picture), **View** (low top-down, high top-down, side) and **Name**.

PixelLab makes 8 looping frames from the picture, fitted into a square of up to 256 px (the pose used as both the first
and the last frame, so the clip loops). It needs `PIXELLAB_API_KEY`. It works, but PixelLab's queue can take minutes a
call: the progress strip shows "queued, position N", then "drawing". For a character it animates five directions
(south, south-east, east, north-east, north, all at once) and mirrors them for the west side, so all 8 directions play
their own frames; the clip plays at 10 frames a second. The text under the button says "PixelLab (Animate with text v3)
makes 8 looping frames from the picture, up to 256 px".

Questions: how do I animate with PixelLab? why is PixelLab so slow? what does queued position mean?

## Person or thing: humanoid and non-humanoid animation

People and things are animated by different models, because a character animator given a thing adds a person to it:
AutoSprite given a door drew a person in every frame and redrew the door, while PixelLab kept the door as drawn and
opened it.

- **Humanoid animation** (people and creatures): Project Settings ▸ AI ▸ **animate**, else Preferences ▸ Art and
  painting ▸ **Humanoid animation**: AutoSprite (default) or PixelLab Animate with text v3.
- **Non-humanoid animation** (things: a door, a chest, a lever, a flag, a torch): Project Settings ▸ AI ▸
  **objectAnimate**, else Preferences ▸ Art and painting ▸ **Non-humanoid animation**: PixelLab Animate with text v3
  (default, the thing kept as drawn, 8 frames) or BFL FLUX video (a 5 second clip, 12 frames taken from it).

For a single picture, the service decides which it is from what it is: the form's **What it is**, the description in the
picture's `.gen.json`, and its file name. People words (herbalist, knight, goblin, villager…) mean a person; thing and
animal words (chest, door, torch, flag, wolf, slime…) mean not. When the words say neither or both, the language model
is asked; failing that, a picture counts as a thing and a character as a person. The choice is kept as `humanoid` in
the `.gen.json` (and in a character's sheet). A thing is animated by the non-humanoid model (its picture shrunk to at
most 384 px a side first); a person by the humanoid model. Without the non-humanoid model's key, everything goes to the
humanoid model. A character sheet is always animated by the humanoid model;
AutoSprite is still told whether it is a person (a wolf character is rigged as an animal).

Questions: why did AutoSprite add a person to my door? how do I animate a chest? what is non-humanoid animation? how does it know if it's a person?

## Directions: how each model covers 8 directions

- **AutoSprite, a character**: one **isometric pack** renders all 8 directions in one go (15 AutoSprite credits). Every
  direction the character has gets its own frames: no mirroring, no "north-east plays north". The figure is brought
  to the idle pose's height. The clip plays at AutoSprite's own speed (24 frames a second × its speed multiplier, about
  10 for a 12-frame walk).
- **AutoSprite, a picture**: animated in its own view only, one direction: what is drawn facing east moves facing east
  (5 credits, 12 frames, fitted to the picture's size).
- **PixelLab Animate v3, a character**: five calls (south, south-east, east, north-east, north), the west side mirrored.
- **BFL FLUX 3 video** (only when asked for by name, `bfl:flux-3-video`, not offered in the menus): three calls per
  action (south, east, north), west mirrored, the diagonals left to the nearest direction.

A character drawn with **4 directions** has only south, west, east and north to animate; in the game the diagonals use
the nearest direction it has. Draw characters with 8 directions to get true diagonals.

Questions: why does my character only have four directions? does AutoSprite do 8 directions? why does the walk look the same going north-east and north?

## Weapons: holds a weapon and weapon class

With AutoSprite and a character picked, the **Weapon** row has **holds a weapon** and a **class** box:

- **holds a weapon**: the motion is asked for with the character holding a plain straight bright magenta rod in the
  right hand (the motion becomes a custom one said with it). In each frame the rod is found by its colour: its grip end
  becomes the `hand` point, its direction the point's angle, and a rod partly hidden means the hand is behind the body.
  Then the rod is removed. The result is a clean animation with a `hand` track, so any weapon in the character's
  Equipment is drawn in its hand, moving with the swing. Fix any frame with the sheet editor's point tool.
- **class** (sword, axe, spear…): the clip is named `attack_<class>` (`attack_sword`), and is chosen in the game when a
  weapon of that class is wielded. Attacks are made per weapon class, not per weapon: a dagger stab and a greatsword
  swing move differently, but every sword can share one swing.

See The Animator and attachments topic for how equipment is drawn on the hand.

Questions: how do I animate a character holding a sword? how do weapons show in animations? what is weapon class? how do I make a different attack for axes?

## The Animate tab's history: Make animation, Make prefab, Edit

Each request is listed under the form in **History (N)**: its name or action, when, the picture, and its state
(Drawing…, failed with the reason, or done). Its buttons:

- **A character's animation**: "Added to <sheet> as a clip, every direction: see it in Assets ▸ Characters", with
  **Edit…** (opens the sheet in the sheet editor on Project ▸ Effects).
- **A picture's animation**: the frames play in a small preview, with the spritesheet's path, and:
  - **Make animation**: writes an animation sheet beside the spritesheet (`<name>-sheet.anim.json`, Assets ▸
    Animations) with one clip named by the action: walking or running → `walk`, an attack, strike or swing → `attack`,
    a spell → `cast`, hurt or hit → `hurt`, dying → `die`, anything else `idle` (a looping motion, like a flame). An
    `open` clip also gets a `close` clip, the same frames backwards, both played once and held.
  - **Make prefab**: the sheet and a prefab that plays it: with the `humanoid` animator when it has walk, attack or cast
    clips, else the `prop` animator (its idle looping).
- **Use again** and **Generate again** on every entry. **Clear history** forgets the list (the animations stay).

Questions: how do I use the animation I generated? how do I make a prefab of a torch animation? where is Make animation? how do I make a chest open and close?

## What a picture's animation writes

Animating a single picture writes its frames and a spritesheet into
`assets/textures/generated/animations/<name>/`: `<name>-1.png`, `<name>-2.png`… and `<name>-sheet.png` (the frames side
by side in one row), with `<name>-sheet.png.gen.json` recording the description, the action, the direction, the source
picture, the frame count, the provider and model, and whether it was judged a person (`humanoid`).

AutoSprite's frames are cleaned up before they are written: AutoSprite draws a dark outline round its figures and its
frames come out darker than the picture, so the outline is peeled off (near-black pixels on the silhouette's edge) and
the colours are matched to the original pose (one shift for all frames, so they don't flicker). The frames are then cut
from one shared box and brought to the original's height.

A thing animated by PixelLab is centred in a 256 px square at 90% for PixelLab and each frame put back at the picture's
own size and place. A motion that opens, closes or breaks plays once and ends where the motion ends; any other loops.

See also: Character sheets, Animations tab and sheet editor, Models and providers, Troubleshooting generation.

Questions: where are animation frames saved? what is the -sheet.png? why are AutoSprite frames recoloured?
