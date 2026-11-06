# Art and animation models: Preferences, Project Settings and keys

Which AI model draws, paints and animates, and where to choose it: Preferences ▸ Art and painting (the app's choices),
Project Settings ▸ AI (the project's own, each empty following Preferences), a build's own art and background model,
and the API keys each needs.

## Where the art and animation models are chosen

There are three levels, each overriding the one before:

1. **Preferences ▸ Art and painting** (File ▸ Preferences; yours, on this computer, for every project): **Prefab art**,
   **Background**, **Humanoid animation**, **Non-humanoid animation** and **Paint model**. Some editor messages call it
   "Preferences ▸ AI ▸ …" from when these lived on the AI page.
2. **Project Settings ▸ AI** (saved with the project in `settings.aige`): **art**, **background**, **paint**, **animate**,
   **objectAnimate** and **artStyle**. Each left **empty** uses the app's choice; set, it wins for this project only, so
   one project can be drawn by PixelLab and another by BFL.
3. **A build's own** (Build panel ▸ Assets): "made by" for its art and "Background painted by", each defaulting to "the
   project's (…)".

When a project has its own choice, Preferences says so under that setting ("This project uses its own: … (Project
Settings ▸ AI), so the choice above doesn't apply to it") with a button **Use this choice in the project**, which clears
the project's setting so it follows Preferences from then on. The Art tab shows the model in use beside Generate ("By …
(set by this project)"). File ▸ New Project also offers Prefab art, Background and Paint model for the new project.

### Example: Choosing the project's own animate model, then going back to the app's

Your "Ashfall Keep" project should animate people with PixelLab, while your other projects keep AutoSprite.

1. Open **Project Settings ▸ AI** and set **animate** to **PixelLab Animate with text v3**. Save.
2. Open **File ▸ Preferences ▸ Art and painting**. Under **Humanoid animation** it now says "This project uses its
   own: … (Project Settings ▸ AI), so the choice above doesn't apply to it".
3. Generate ▸ Animate in this project shows PixelLab's form (**Action**, **Facing**, **View**) instead of
   AutoSprite's **Motion**.
4. Weeks later you want AutoSprite back: in Preferences, under Humanoid animation, click **Use this choice in the
   project**.

Result: the project's **animate** setting is cleared and Ashfall Keep follows Preferences (AutoSprite) again.

Questions: where do I change the art model? why doesn't my Preferences art choice apply? how do I make a project use a different model? what does "set by this project" mean?

## Prefab art model (Art tab, prefabs, characters)

**Preferences ▸ Art and painting ▸ Prefab art** / **Project Settings ▸ AI ▸ art**: who draws the prefabs' art — every
piece of an Assembled build in one style, and the Art tab's single pieces.

| Choice | Key | Notes |
|---|---|---|
| Follow the provider (`auto`, default) | `MISTRAL_API_KEY`, else `PIXELLAB_API_KEY` | Mistral's images with a Mistral key; Claude draws nothing, so with Claude: Mistral if its key is there, else PixelLab |
| Mistral | `MISTRAL_API_KEY` | FLUX through Mistral's image tool |
| PixelLab | `PIXELLAB_API_KEY` | pixel art for games, kept to the project's palette and matched to its own pieces |
| BFL FLUX.2 klein (`bfl:flux-2-klein-4b`) | `BFL_API_KEY` | fast |
| BFL FLUX.3 Image (`bfl:flux-3-image`) | `BFL_API_KEY` | |
| BFL FLUX.2 pro (`bfl:flux-2-pro`) | `BFL_API_KEY` | |

It also decides who draws **Generate ▸ Character**: a BFL art model draws characters with BFL (facing you first, each
other direction an edit of it); any other choice uses PixelLab. If the key for the chosen model is missing, Preferences
shows an error under the setting ("No key for …: set … in ai_service/.env").

### Example: Pixel art matched to the project, by PixelLab

You use Claude as the language model and have only a PixelLab key; you want every prop in "Ashfall Keep" to share
the same palette.

1. Open **File ▸ Preferences ▸ Art and painting** and set **Prefab art** to **PixelLab** (with "Follow the provider"
   and no Mistral key it would be PixelLab anyway, since Claude draws nothing).
2. On Generate ▸ Art, the text beside the Generate button ("By …") now names PixelLab.
3. Draw `an iron brazier with glowing coals`.

Result: PixelLab draws it in the project's colours, matched to up to four of the project's own objects. Generate ▸
Character also draws with PixelLab, since the art model isn't BFL's.

Questions: which art model should I use? what does Follow the provider mean? why are my characters drawn by PixelLab? which key does BFL need?

## Humanoid and non-humanoid animation models

**Humanoid animation** (Preferences ▸ Art and painting; Project Settings ▸ AI ▸ **animate**): what makes a person or a
creature move — Generate ▸ Animate, a character in every direction, and the AI panel's walks.

| Choice | Key | What it does |
|---|---|---|
| AutoSprite (`autosprite`, default) | `AUTO_SPRITE_API_KEY` (or `AUTOSPRITE_API_KEY`) | its motions or your own; a character's 8 directions in one isometric pack (15 credits); a picture in its own view (5 credits). API access needs AutoSprite's Pro plan |
| PixelLab Animate with text v3 (`pixellab:animate-v3`) | `PIXELLAB_API_KEY` | 8 frames up to 256 px; a character in five directions mirrored to eight; slow (its queue takes minutes a call) |

**Non-humanoid animation** (Project Settings ▸ AI ▸ **objectAnimate**): what makes a thing move — a door (Build ▸
Animate, Animate a part), a chest, a lever, a flag, interior parts, and a picture judged to be a thing in Generate ▸
Animate. AutoSprite is a character animator: given a thing, it adds a person.

| Choice | Key | What it does |
|---|---|---|
| PixelLab Animate with text v3 (`pixellab`, default) | `PIXELLAB_API_KEY` | the thing kept as drawn; 8 frames |
| BFL FLUX video (`bfl:flux-video`) | `BFL_API_KEY` | a 5 second clip, 12 frames taken from it |

Tried and no longer offered, but still used when named in a setting: `bfl:flux-3-video` (FLUX 3 video for characters:
good motion, about $0.75 a direction) and `bfl:image` (FLUX.2 klein editing copies of the pose).

### Example: Animating the mill's waterwheel with FLUX video

PixelLab's wheel looked stiff; you want to try BFL for things in this project.

1. Make sure `BFL_API_KEY` is in `ai_service/.env`.
2. Open **Project Settings ▸ AI** and set **objectAnimate** to **BFL FLUX video**.
3. On Generate ▸ Animate, pick `waterwheel.png`, type `a wooden waterwheel` in **What it is** (a thing), describe the
   motion `the wheel turns steadily, water splashing from its paddles`, and click **Animate**.

Result: BFL makes a 5 second clip and 12 frames are taken from it. People are unaffected: they are still animated by
the **animate** model (AutoSprite by default).

Questions: what is the default animation model? AutoSprite or PixelLab? which model animates doors? how much does AutoSprite cost? do I need AutoSprite Pro?

## Background and paint models

These paint places rather than single pieces, but they are BFL models chosen the same way:

- **Background** (Preferences ▸ Art and painting ▸ Background; Project Settings ▸ AI ▸ background; a build's
  "Background painted by"; Edit ▸ Background Image…'s **Model**): who paints a place's ground, roads and floors as one
  picture from its plan. **BFL FLUX.3 Image (keeps the map best)**, the default; **BFL FLUX.2 max (as good, slower)**;
  **BFL FLUX.2 pro**; **BFL FLUX.2 klein (fast, loose)**. All need `BFL_API_KEY`.
- **Paint model** (Preferences; Project Settings ▸ AI ▸ paint): who paints a place whole for the Paint build method.
  FLUX-3-Image (default, keeps the layout best), FLUX-2-Klein-4b (fast, a rough draft), FLUX-2-Pro, FLUX-2-Max (rich
  looks, may drift from the layout), FLUX-3-Image-to-Pro and FLUX-3-Image-to-Klein (FLUX-3-Image paints the plan, then
  FLUX.2 pro or klein repaints its look).
- **Interiors** are always painted by BFL (an edit of the outside picture), whatever the art model.

**Edit ▸ Background Image…** repaints a build's background from its plan and words, or (with Build "None: the open
scene") the open scene's ground from words alone, behind everything; **Use a picture file…** takes your own instead.

### Example: A quick draft background, then the final one

1. Open **Edit ▸ Background Image…** for the village build and set **Model** to **BFL FLUX.2 klein (fast, loose)**
   to see a rough draft of the ground and roads quickly.
2. Happy with the layout, open it again, set **Model** to **BFL FLUX.3 Image (keeps the map best)** and repaint.

Result: the final background follows the plan's roads and plots closely. To make FLUX.3 Image the default for every
background in this project, set **Project Settings ▸ AI ▸ background** to it (or leave it empty to follow
Preferences, where it is already the default).

Questions: which model paints backgrounds? how do I repaint the background? what is the paint model? why do interiors need BFL?

## Art style: one look for every prompt

**Project Settings ▸ AI ▸ artStyle** sets the art's look for every art prompt — prefabs, backgrounds, paint and
characters:

| Value | Words added to prompts |
|---|---|
| (empty) | from the project: each kind of art its own look, matched to the art already made |
| `pixel-16` | 16-bit 1990s pixel art: crisp square pixels, a limited palette, clean dark outlines |
| `pixel-32` | detailed 32-bit era pixel art: crisp square pixels, rich shading, clean outlines |
| `painted` | hand-painted digital game art: soft painterly shading and brush texture, not pixel art |
| `flat-vector` | flat vector game art: clean geometric shapes, flat colours, simple shading, not pixel art |

The planner's art style sets it. With `painted` or `flat-vector`, Generate ▸ Art skips its pixel clean-up (no palette
reduction), keeping the colours as drawn; with a pixel style or none, the pixel style option reduces to 32 colours.
The project also keeps style references (a few pieces of each place built, with their palettes, in
`places/style-references.json`): new art is briefed to match them and PixelLab draws in their look.

### Example: Moving a project from pixel art to a painted look

1. Open **Project Settings ▸ AI** and set **artStyle** to **painted**.
2. On Generate ▸ Art, draw `a market stall with a striped awning`.
3. Generate ▸ Character, draw `a fishmonger in a rubber apron`.

Result: both prompts carry "hand-painted digital game art: soft painterly shading and brush texture, not pixel art",
and the Art tab keeps the stall's colours as drawn (no reduction to 32 colours). Art made before the change keeps its
old look; regenerate it if it should match.

Questions: how do I make all my art pixel art? how do I change the art style? why is my art not pixelated?

## API keys for generation

All keys go in `ai_service/.env` (or the environment) on your computer; they never leave it except to that service.
Preferences ▸ AI ▸ Keys lists the main ones.

| Key | Needed for |
|---|---|
| `MISTRAL_API_KEY` | Mistral as art model, or "Follow the provider" with Mistral |
| `PIXELLAB_API_KEY` | PixelLab art; characters when the art model isn't BFL's; PixelLab animation (non-humanoid default, door animation) |
| `BFL_API_KEY` | BFL art models; characters with a BFL art model; backgrounds; paint; interiors; FLUX video |
| `AUTO_SPRITE_API_KEY` / `AUTOSPRITE_API_KEY` | AutoSprite humanoid animation (the default); Pro plan for API access |

Model menus mark a choice without its key "(no key)". The Generate tabs name the missing key beside their button. If a
key added to `.env` still shows "(no key)", restart the AI service (untick and tick Preferences ▸ AI ▸ Use AI, or
restart the editor).

### Example: Setting up keys for characters drawn by BFL and animated by AutoSprite

1. Open `ai_service/.env` and add both keys:

   ```
   BFL_API_KEY=your_bfl_key
   AUTO_SPRITE_API_KEY=your_autosprite_key
   ```

2. Save, then untick and tick **Preferences ▸ AI ▸ Use AI** so the service reads them.
3. In **Preferences ▸ Art and painting**, set **Prefab art** to a BFL model; **Humanoid animation** stays
   **AutoSprite**. Neither shows "(no key)" any more.

Result: Generate ▸ Character draws with BFL and Generate ▸ Animate animates characters with AutoSprite (its API needs
AutoSprite's Pro plan). Things like doors still need `PIXELLAB_API_KEY` for the default non-humanoid model; without
it they go to the humanoid model.

See also: Generate panel, Art tab, Animate tab, Troubleshooting generation.

Questions: where do I put API keys? which keys do I need to generate characters and animations? I added a key but it still says no key?
