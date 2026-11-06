# Generate ▸ Art: sprites and tiles from a description

The Art tab of the Generate panel draws single pieces of game art (an object, a character picture, a ground tile) from
words, cuts them out and scales them to size. This page covers its form, its gallery, what happens behind the scenes and
how to turn a result into a prefab.

## The Art tab form: description, name, kind, height, style, variants

Open the Generate panel and pick the **Art** tab. The form on the left:

- **Description** (the big box): what to draw, in plain words. "a wooden barrel with iron bands", "an old herbalist
  with a basket". One subject per picture.
- **Name**: the file name. Left empty, it is made from the first three words of the description (lowercase, joined by
  dashes; shown greyed as the placeholder). Names may use lowercase letters, digits, `-` and `_`, up to 48 characters.
- **Kind**: **object** (a single thing seen in 3/4 isometric view, whole and centred), **character** (one figure, full
  body, standing, facing the viewer) or **ground tile (top-down)** (a seamless texture seen from straight above, filling
  the square edge to edge, for painting the ground).
- **Height**: the finished sprite's height in pixels, 8 to 512 (48 by default). The width follows the picture's
  proportions; a ground tile is made square, height × height.
- **Style**: **pixel art** (hard edges, a reduced palette) or **smooth** (clean hand-painted look, kept as drawn).
- **Variants** (optional): a comma-separated list such as "mossy, broken, painted red". Each variant is drawn as its own
  picture of the same thing, at most 6 at a time; the button then reads "Generate 3 variants".

Press **Generate**. Beside it the tab says which model draws ("By …", with "(set by this project)" when the project has
its own art model) and, while it works, "Drawing N images (~10 s each)…". A failure shows in orange under the button.

### Example: Drawing a stone well in three conditions

1. Open the Generate panel and click **Art**.
2. In the description box type `a round stone well with a wooden roof and a bucket on a rope`.
3. Set **Name** to `well`, **Kind** to **object**, **Height** to `64`, **Style** to **pixel art**.
4. In **Variants** type `clean, mossy, ruined`. The button now reads **Generate 3 variants**.
5. Click it. Beside the button: "Drawing 3 images (~10 s each)…".

Result: about half a minute later, `well-clean.png`, `well-mossy.png` and `well-ruined.png` appear at the top of
**Files**, each 64 px tall, cut out on a checkerboard.

Questions: how do I generate a sprite? what does Kind mean? how big should the height be? how do I make variants? what names are allowed?

## Which model draws the Art tab's pictures

The Art tab draws with the project's **art model**: Project Settings ▸ AI ▸ **art** when the project sets one, else
Preferences ▸ Art and painting ▸ **Prefab art**. The tab shows the model in use next to the Generate button ("By …";
hover for where it comes from). The choices:

| Choice | Draws with | Key |
|---|---|---|
| Follow the provider (`auto`, the default) | Mistral's image tool if `MISTRAL_API_KEY` is set, else PixelLab | `MISTRAL_API_KEY` or `PIXELLAB_API_KEY` |
| Mistral | FLUX through Mistral's image generation | `MISTRAL_API_KEY` |
| PixelLab | PixelLab's pixel-art generator, in the project's palette and matched to its own pieces | `PIXELLAB_API_KEY` |
| BFL FLUX.2 klein / FLUX.3 Image / FLUX.2 pro | Black Forest Labs' FLUX models | `BFL_API_KEY` |

Claude draws no pictures, so with Claude as the language model "Follow the provider" uses Mistral if its key is there,
else PixelLab. With PixelLab, each new piece is drawn to match up to four of the project's own pieces of the same kind
(its style references) and keeps to the project's colours.

The same art model also decides who draws characters in Generate ▸ Character (BFL when it is a BFL model, else
PixelLab). See Models and providers.

### Example: Making this project draw its props with BFL FLUX.2 klein

Your project "Harbor Town" should draw with BFL while your other projects keep Mistral.

1. Make sure `BFL_API_KEY` is in `ai_service/.env` (the choice otherwise shows "(no key)").
2. Open **Project Settings ▸ AI** and set **art** to **BFL FLUX.2 klein**.
3. Go back to Generate ▸ Art. Beside the Generate button it now says "By … (set by this project)"; hover it to see that
   the choice comes from Project Settings ▸ AI.

Result: the Art tab, and Generate ▸ Character, draw with BFL in this project only. Clear the project's art setting to
follow Preferences again.

Questions: which AI draws my art? how do I switch the art model to BFL? why does it say "set by this project"? does Claude draw images?

## How a generated sprite is made: prompt, cut-out, crop and scale

Behind the Art tab, the AI service does this for each picture:

1. **The prompt**: built from the kind ("one game sprite of …, isometric 3/4 view, centered, whole object visible"),
   the variant, the look (the project's art style from Project Settings ▸ AI ▸ artStyle when one is set, else the Style
   choice: "pixel art, clean outlines, limited palette" or "clean hand-painted game art") and a backdrop line asking for
   one isolated object on plain pure white, with no ground, base, shadow, text, frame or white sticker outline. A tile
   instead asks for no border, frame, text or perspective.
2. **The cut-out** (objects and characters): the background colour is read from the picture's border and cleared by
   filling inward from the edges, so a white highlight inside the object survives. The anti-aliased halo is faded, a
   light rim touching the cleared area is peeled off (up to three pixels deep), and the backdrop's colour is taken out
   of the edge's half-transparent pixels so no light fringe shows on dark ground.
3. **Crop** to the object, keeping one transparent pixel round it.
4. **Scale** to the chosen height (box filtering for pixel art, smooth filtering otherwise).
5. **Pixel art clean-up**: with the pixel style, and when the project's art style is a pixel style or unset, edges are
   made hard (fully on or off) and the colours reduced to 32. A painted or flat-vector art style keeps its colours.

A ground tile is not cut out: its centre square is taken, scaled to height × height and kept fully opaque.

### Example: A white sheep that keeps its white wool

1. On Generate ▸ Art, describe `a fluffy white sheep standing`, **Kind** **object**, **Height** `40`, **Style**
   **pixel art**, and click **Generate**.
2. The image model draws the sheep on pure white. The cut-out clears the white by filling inward from the picture's
   edges, so the backdrop goes but the white wool inside the sheep's outline stays.
3. The light halo round the sheep is peeled off, the picture is cropped with one clear pixel round it and scaled to
   40 px, then its edges made hard and its colours reduced to 32.

Result: `fluffy-white-sheep.png`, a white sheep with no white fringe, that shows cleanly on dark grass. A
`cobblestone` drawn as **ground tile (top-down)** instead comes out square and fully opaque, with nothing cut out.

Questions: how does the background get removed? why are my sprites pixelated? what prompt is sent to the image model? why is the tile not transparent?

## The .gen.json file next to each generated picture

Every picture the Art tab writes, `assets/textures/generated/<name>.png`, gets a sidecar `<name>.png.gen.json` saying
exactly where it came from:

- `description`, `variant`, `family` (the name it was asked under) and `kind`;
- `prompt` (as built) and `providerPrompt` (as the image model received it);
- `provider` and `model`, and `seed` (null when the provider doesn't expose one);
- `processing`: the source size, the cropped size, the final `size`, the style, the palette size, the cut-out tolerance,
  whether it is a tile;
- `seconds` it took and `created` (the time).

The gallery reads it: a card's title shows the prompt on hover, its lines show the description, variant, model and
seed, and **Regenerate** uses it to ask again. Generate ▸ Animate also reads a picture's description from it to decide
whether it is a person or a thing. Deleting the `.gen.json` doesn't break the picture, but the gallery then says
"unknown model" and Regenerate has nothing to go on.

### Example: Checking which prompt drew the mossy well

1. On Generate ▸ Art, **Files**, hover the title `well-mossy.png`: the tooltip shows the prompt that was built.
2. For the full record, open `assets/textures/generated/well-mossy.png.gen.json` from the project folder. Trimmed, it
   looks like this:

   ```json
   {
     "description": "a round stone well with a wooden roof and a bucket on a rope",
     "variant": "mossy",
     "family": "well",
     "kind": "object",
     "provider": "mistral",
     "seed": null,
     "processing": { "size": [52, 64], "style": "pixel" },
     "seconds": 9.6,
     "created": "2026-11-06T10:14:03+00:00"
   }
   ```

Result: you can see the variant, the provider and its size; `"seed": null` means the provider doesn't expose one, so
the card says "seed not exposed".

Questions: what is in the .gen.json? how do I see the prompt that was used? where is the seed?

## The Art gallery: Files and History

The right side of the Art tab has two views, switched by the buttons at its top:

**Files (N)** lists every PNG directly in `assets/textures/generated/`, newest first (this includes art the AI panel
had drawn). Each card shows the picture on a checkerboard (any size of art is fitted inside its 92-pixel box, small
art scaled up, big art scaled down), the file name (hover: the prompt), the description and variant, and the model and
seed. Its buttons:

- **Regenerate**: draws it again with the same description, kind, height, style and variant. The new picture replaces
  the file of the same name.
- **Make prefab**: a placeable prefab with this sprite (see the next section).
- **lies on the ground** (a tick box, not on ground tiles): for a field, a rug or a path piece; set it before Make prefab.
- **Discard**: deletes the picture with its `.gen.json` and `.meta`, after asking. Only files in the generated folder
  can be discarded here.

**History (N)** lists every request made in the Art tab, kept or not: when, the kind, height, style and variants, its
state (Drawing…, failed with the reason, or done with small thumbnails), and the buttons **Use again** (puts that
request back in the form) and **Generate again** (sends it again). A result later deleted shows "discarded"; one drawn
over by a later request shows "redrawn". **Clear history** forgets the list; the files stay.

### Example: Redoing yesterday's lantern request in a smooth style

1. On Generate ▸ Art, click **History (N)** at the top of the right side.
2. Find the entry "a brass lantern with a candle inside" (object · 32 px · pixel) and click **Use again**. The form on
   the left fills with that request.
3. Change **Style** to **smooth** and click **Generate**.
4. The old entry's thumbnail now says "redrawn", since the new lantern was written over `lantern.png`.
5. If you don't like it either, click **Files (N)**, then **Discard** on the `lantern.png` card and **Discard** in
   the dialog: the History marks the result "discarded".

Result: a smooth `lantern.png` replaces the pixel one; the History still lists both requests.

Questions: how do I delete generated art? where is the history of my requests? how do I redo the same request? why does it say "redrawn"?

## Make prefab from generated art, and "lies on the ground"

In the Art gallery, **Make prefab** turns a picture into a prefab you can place in scenes, named after the file:

- **An object or character picture**: a normal standing prefab, sorted with the world so characters walk in front of or
  behind it. It is not solid; give it a collider in the Inspector if it should block.
- **A ground tile**: a flat tile prefab on the grid, to paint ground with.
- **With "lies on the ground" ticked**: for things lying flat on the ground (a field, a rug, a path piece). The picture
  is fitted onto the grid: its top face's corners are found and the picture warped so they lie along the grid's lines,
  as many cells each way as it spans, saved beside it as `<name>-ground.png`. It is placed with its bottom edge on the
  cell's front corner and drawn under characters, so they walk over it.

Tick the box before pressing Make prefab; the tick is per picture. Prefabs appear in the Assets panel's prefab list.

See also: Character tab (for a character that walks and turns, use Generate ▸ Character, not the Art tab's character
kind), Troubleshooting generation.

### Example: A well that stands, and a rug that lies on the ground

1. On Generate ▸ Art, **Files**, find `well-mossy.png`. Leave **lies on the ground** unticked and click **Make
   prefab**. A prefab `well-mossy` appears in the Assets panel's prefab list.
2. Place it in the scene: it stands up, and the villagers walk in front of it or behind it. Since it isn't solid,
   select it and add a collider in the Inspector so people can't walk through it.
3. Now draw `a round woven rug, red and gold` (Kind **object**) and, on its card, tick **lies on the ground** before
   clicking **Make prefab**.

Result: the rug's picture is fitted to the grid (saved as `round-woven-rug-ground.png`) and its prefab lies flat with
its bottom edge on the cell's front corner, drawn under characters, so the innkeeper walks over it.

Questions: how do I place generated art in my scene? what does "lies on the ground" do? how do I make a rug that characters walk over? how do I make a tile prefab?

## Tips for good results in the Art tab

- **One subject per request**: "a stone well" draws better than "a village square with a well and a cart".
- **Say materials and condition**: "a weathered oak chest with rusted iron bands" is better than "a chest".
- **Ground under the subject**: the Art tab's note warns that sometimes a little ground is drawn under the subject even
  though the prompt asks for none. Regenerate when it happens.
- **Pick the height you will use**: art is scaled down to the height given; 48 px suits a prop a cell tall, larger
  buildings need more. Re-scaling later in the engine blurs pixel art.
- **Variants for families**: one description with variants ("closed, open, broken") keeps the pieces alike.
- **A project art style** (Project Settings ▸ AI ▸ artStyle: pixel-16, pixel-32, painted, flat-vector) is added to every
  art prompt, so pieces made at different times match. With painted or flat-vector, the pixel clean-up is skipped.
- **A walking character** is better made in Generate ▸ Character: the Art tab's character kind is a single still picture
  facing the viewer.

### Example: Turning a vague chest into a matching set

The first try, `a chest`, came out plain and with a patch of grass under it.

1. Open **Project Settings ▸ AI** and set **artStyle** to **pixel-32**, so every prop drawn from now on shares it.
2. On Generate ▸ Art, describe `a weathered oak chest with rusted iron bands`, **Name** `chest`, **Height** `40`.
3. In **Variants** type `closed, open, broken`, then click **Generate 3 variants**.
4. If one of them still has ground drawn under it, click **Regenerate** on that card only.

Result: three chests that look alike, in the project's style, at the height they will be used.

Questions: how do I get better art? why is there grass under my object? how do I keep a consistent style?
