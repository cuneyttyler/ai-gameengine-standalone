# Generate ▸ Character: characters seen from every direction

The Character tab of the Generate panel draws a character from 4 or 8 directions and keeps it as one character sheet,
ready to become the player or an NPC and to be animated. This page covers the form, which model draws, what it makes and
what to do with the result.

## The Character tab form

Open the Generate panel ▸ **Character**. The form on the left:

- **Description**: who they are and what they look like ("an old herbalist in a green hooded cloak, carrying a basket
  of herbs…"). Clothes, colours, what they carry.
- **Name**: the character's name and folder. Left empty, it is made from the first three words of the description.
- **Directions**: **4 (south, west, east, north)** or **8 (and the diagonals)**. Pick 8 for characters that walk on
  the diamond grid: walking there is mostly diagonal, and with 4 the diagonals show the nearest straight direction.
- **Size**: the frame size, 32, 48, 64 (default), 96 or 128 px square.
- **View**: **low top-down (most 2D RPGs)**, **high top-down** or **side**.
- **Isometric** (a tick box): asks for an isometric figure.

Press **Generate character**. While it works the tab says "Drawing N directions (a minute or two)…" and the progress
strip at the top shows each step. The button is off, with the reason beside it, when AI is off, the key for the model
in use is missing ("Needs BFL_API_KEY in ai_service/.env" or "Needs PIXELLAB_API_KEY…"), or there is no description.

### Example: Mira Quill, a herbalist in a green cloak

1. Open the Generate panel and click **Character**.
2. In the description box type `Mira Quill, a herbalist in a green hooded cloak with a leather satchel of herbs and a
   wooden staff`.
3. Set **Name** to `mira-quill`, **Directions** to **8 (and the diagonals)**, **Size** to **64 px** and **View** to
   **low top-down (most 2D RPGs)**. Leave **Isometric** unticked.
4. Click **Generate character**. Beside it: "Drawing 8 directions (a minute or two)…"; the progress strip shows each
   direction as it is done.

Result: a minute or two later Mira's card appears at the top of the History with her sheet's picture, showing her
from all eight directions, ready for **Make NPC** or **Animate…**.

Questions: how do I make a character? should I use 4 or 8 directions? what size should my character be? what does View mean?

## Who draws characters: BFL or PixelLab

Generate ▸ Character draws with the project's **art model** when it is one of BFL's (Project Settings ▸ AI ▸ art, else
Preferences ▸ Art and painting ▸ Prefab art set to a BFL FLUX model), and with **PixelLab** otherwise. The note under
the button says which ("Drawn by the project's art model (flux-…)" or "Drawn by PixelLab").

Preferences' "Characters, animations" line says the same: with a BFL art model characters are drawn by BFL, else by
PixelLab, and animations are made by the Humanoid / Non-humanoid animation models.

- **PixelLab** (`PIXELLAB_API_KEY`): PixelLab's own 4- or 8-direction character generator, as a background job (up to
  4 minutes), with a single-colour black outline, basic shading, medium detail and the project's colours.
- **BFL** (`BFL_API_KEY`): the character is first painted facing the camera, whole body, on flat magenta. Each other
  direction is then an *edit* of that picture ("the same sprite design, only turned"): south-east, east, north-east and
  north for 8 directions (east and north for 4). West, south-west and north-west are the east side mirrored, so 8
  directions take 5 calls and 4 take 3. The magenta (and any shade of it, such as a painted shadow) is cleared and every
  direction is fitted at one scale on a size × size square with its feet on the same line, so they swap in place.
  BFL's moderation sometimes refuses one direction; it is asked again with up to 3 other seeds.

The prompt also carries the project's art style (Project Settings ▸ AI ▸ artStyle) and its main colours.

### Example: Drawing the town guard with BFL instead of PixelLab

The note under **Generate character** says "Drawn by PixelLab", but you want the guard painted by BFL.

1. Make sure `BFL_API_KEY` is in `ai_service/.env`.
2. Open **Project Settings ▸ AI** and set **art** to **BFL FLUX.3 Image** (or set Preferences ▸ Art and painting ▸
   **Prefab art** to a BFL model, for every project).
3. Back on Generate ▸ Character the note now reads "Drawn by the project's art model (flux-3-image): facing you
   first, each other direction an edit of it, the west side mirrored".
4. Describe `a town guard in a steel helmet and a blue tabard, holding a spear`, name it `town-guard`, pick **8**
   directions and click **Generate character**.

Result: the guard is painted facing you, then turned by 4 edits; the west side is mirrored, so 5 calls in all. If BFL
refuses one direction, it is retried with other seeds on its own.

Questions: who draws my characters? why is my character drawn by PixelLab and not BFL? how do I use BFL for characters? why was a direction refused?

## What Generate ▸ Character makes: one character sheet

A character is one asset, its **character sheet**: one picture and one sheet file beside it,
`assets/characters/<name>/<name>.character.png` and `<name>.character.anim.json`. The sheet starts with one clip,
**idle**, holding one pose for each direction. Generate ▸ Animate adds more clips (walk, attack…) to the same sheet,
each with every direction. No loose direction pictures are written.

When it's done, the result's card in the Character tab's history shows the sheet's picture and path ("every direction,
its idle; Animate adds actions") and these buttons:

- **Make player**: a player prefab from the sheet, put in the open scene where the player was (the old player goes).
- **Make NPC**: an NPC prefab, put in the middle of the view.
- **Animate…**: switches to the Animate tab with this character picked.
- **Use again** puts the request back in the form; **Generate again** sends it again.

**Clear history** forgets the list (the characters stay). Every character sheet is listed in Assets ▸ Characters, where
the same actions are on its right-click menu. See the Character sheets topic for the file format.

### Example: Finding Mira's sheet after she is drawn

1. When `mira-quill` finishes, her card in the Character tab's History reads
   `assets/characters/mira-quill/mira-quill.character.png · every direction, its idle; Animate adds actions`.
2. Open the Assets panel ▸ **Characters**: `mira-quill` is listed.
3. In the project folder you find two files in `assets/characters/mira-quill/`: `mira-quill.character.png` (all her
   poses in one picture) and `mira-quill.character.anim.json` (the sheet, with its one clip, `idle`).

Result: Mira is a single asset; right-click her in Assets ▸ Characters for the same Make player, Make NPC and
Animate… actions as on her card.

Questions: where is my generated character saved? how do I make the generated character the player? how do I add an NPC from a character? what is a character sheet?

## Make player and Make NPC from a character sheet

**Make player** and **Make NPC** (Generate ▸ Character's result, or right-click a character in Assets ▸ Characters) make
a prefab that plays the character sheet:

- the prefab is `prefabs/<name>-player.prefab` or `prefabs/<name>-npc.prefab` (a number added when the name is taken);
- it is the engine's player (moves, collides, fights) or an NPC, with a **Sprite** showing the idle pose facing the
  camera until it plays, about two cells tall (at least 48 px), its width following the frame's proportions;
- an **Animator** with the sheet and the `humanoid` animator (idle standing, walk moving, attack, cast, use, hurt, die);
- a **Facing** pointing south on screen (towards the camera).

The player goes where the old player stood (it is replaced); an NPC goes in the middle of the view and is selected. The
notice says "… is the player (F5 to play; R turns it; Save keeps it)". In the editor, **R** / **Shift+R** on a selected
character turns it an eighth of a turn; save the scene to keep it. Both buttons are off during Play.

### Example: Placing Mira at her herb stall, facing the square

1. Scroll the Scene view so the herb stall is in the middle.
2. In Assets ▸ **Characters**, right-click `mira-quill` and choose **Make NPC**.
3. `prefabs/mira-quill-npc.prefab` is made and Mira appears in the middle of the view, selected, facing the camera.
4. Drag her behind the stall's counter. Press **R** twice to turn her a quarter turn (two eighths) so she faces the
   square.
5. Save the scene.

Result: Mira stands behind her stall facing the square with her idle pose; once she has a walk, her Animator plays it
when she moves. To play as Mira yourself, use **Make player** instead and press F5.

Questions: how do I make my character the player? how do I turn a character to face another way? why does my NPC face the camera?

## Characters made before character sheets: Make character sheet

Older projects may have characters as loose pictures, one per direction (`…-south.png`, `…-east.png`, in
`assets/textures/generated/characters/<name>/`). The Character tab's history shows these as one small picture per
direction, each with **Prefab** (a still prefab of that picture) and **Animate** (that one direction in the Animate tab).

To turn them into a proper character, press **Make character sheet** on that history card. It joins the direction
pictures into one sheet (`assets/characters/<name>/…`) with an idle clip; then it shows in Assets ▸ Characters and can
be animated in every direction at once. **Make player** on such a card makes a player straight from the loose pictures.
Make character sheet needs AI on.

### Example: Updating an old "blacksmith" made from loose pictures

An older project has `assets/textures/generated/characters/blacksmith/` with `blacksmith-south.png`,
`blacksmith-east.png` and the others.

1. On Generate ▸ Character, find the blacksmith's card in the History: it shows one small picture per direction, each
   with **Prefab** and **Animate**.
2. With AI on, click **Make character sheet** on the card.
3. Open Assets ▸ **Characters**: `blacksmith` is now listed, with its sheet in `assets/characters/blacksmith/`.

Result: the blacksmith is one character with an idle clip and can be animated in every direction at once (right-click
▸ **Animate…**) instead of one picture at a time.

Questions: how do I convert old character pictures into a character sheet? my character is separate south/east pictures, what do I do?

## Animating a generated character

A character's actions are added in Generate ▸ Animate with the character picked (its **Animate…** button, or
right-click in Assets ▸ Characters ▸ **Animate…**). The action is made in every direction the character has and added
to its sheet as one clip named for what the animators play: walking → `walk`, attacking → `attack`, casting → `cast`,
a punch → `attack_punch`, anything else its own name. With AutoSprite (the default) all 8 directions come from one
render; the character is uploaded to AutoSprite once and its id kept in the sheet.

Animating the same action again replaces that clip; the old frames stay in the picture, unused. Remove a clip in the
sheet editor (Assets ▸ Characters, right-click ▸ **Edit…**, pick the clip, **Remove**, **Save**).

### Example: Giving Mira a walk, then removing a clip you don't need

1. In Assets ▸ **Characters**, right-click `mira-quill` and choose **Animate…**. The Animate tab opens with Mira
   picked.
2. With AutoSprite, pick **walk** in **Motion** and click **Animate** ("Animating all 8 directions (2 to 5
   minutes)…"). All 8 directions come back in one render.
3. Mira's sheet now has a clip named `walk`, so her `humanoid` animator plays it when she moves.
4. Later you also made a `wave` you don't want. Right-click `mira-quill` ▸ **Edit…**, click the `wave` clip tab,
   click **Remove**, then **Save**.

Result: Mira walks in every direction; the `wave` clip is gone from her sheet (its frames stay in the picture,
unused).

See also: Animate tab, Character sheets, The Animator, Troubleshooting generation.

Questions: how do I make my character walk? how do I add an attack animation to a character? how do I remove an animation from a character?
