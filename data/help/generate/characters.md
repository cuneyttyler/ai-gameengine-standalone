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

Questions: how do I make my character the player? how do I turn a character to face another way? why does my NPC face the camera?

## Characters made before character sheets: Make character sheet

Older projects may have characters as loose pictures, one per direction (`…-south.png`, `…-east.png`, in
`assets/textures/generated/characters/<name>/`). The Character tab's history shows these as one small picture per
direction, each with **Prefab** (a still prefab of that picture) and **Animate** (that one direction in the Animate tab).

To turn them into a proper character, press **Make character sheet** on that history card. It joins the direction
pictures into one sheet (`assets/characters/<name>/…`) with an idle clip; then it shows in Assets ▸ Characters and can
be animated in every direction at once. **Make player** on such a card makes a player straight from the loose pictures.
Make character sheet needs AI on.

Questions: how do I convert old character pictures into a character sheet? my character is separate south/east pictures, what do I do?

## Animating a generated character

A character's actions are added in Generate ▸ Animate with the character picked (its **Animate…** button, or
right-click in Assets ▸ Characters ▸ **Animate…**). The action is made in every direction the character has and added
to its sheet as one clip named for what the animators play: walking → `walk`, attacking → `attack`, casting → `cast`,
a punch → `attack_punch`, anything else its own name. With AutoSprite (the default) all 8 directions come from one
render; the character is uploaded to AutoSprite once and its id kept in the sheet.

Animating the same action again replaces that clip; the old frames stay in the picture, unused. Remove a clip in the
sheet editor (Assets ▸ Characters, right-click ▸ **Edit…**, pick the clip, **Remove**, **Save**).

See also: Animate tab, Character sheets, The Animator, Troubleshooting generation.

Questions: how do I make my character walk? how do I add an attack animation to a character? how do I remove an animation from a character?
