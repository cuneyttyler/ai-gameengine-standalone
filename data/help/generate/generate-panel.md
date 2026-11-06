# The Generate panel

The Generate panel is where the editor makes new things with AI: levels, art, characters and animations. This page
covers its tabs, what they need to work, where their results go and how to follow their progress.

## The Generate panel and its four tabs

The Generate panel has four tabs along its top, each a different generator:

- **Level**: generate a dungeon or a village into the open scene from parameters.
- **Art**: draw sprites and ground tiles from a description (a barrel, a rug, a cobblestone tile). See the Art tab topic.
- **Character**: draw a character seen from 4 or 8 directions, kept as one character sheet. See the Character tab topic.
- **Animate**: make a picture or a character move (walking, attacking, a chest opening). See the Animate tab topic.

The tab you were on is remembered while the editor runs. Other parts of the editor open a tab for you: a character's
**Animate…** (Generate ▸ Character, or right-click in Assets ▸ Characters) switches to the Animate tab with that
character picked, and a direction picture's **Animate** button picks that picture.

Every generator runs one request at a time: while one is drawing, its button is off and says "Still drawing the last
one…". The three AI tabs (Art, Character, Animate) each keep a **History** of the requests made there, saved with the
project, so it is still there after switching tabs or restarting.

Questions: where do I generate art? what are the Generate panel's tabs? how do I make a character with AI? where is Animate?

## What the Generate tabs need: AI on and the right keys

The Art, Character and Animate tabs need the AI service to be running (Preferences ▸ AI ▸ **Use AI** ticked). When it is off, the
generate button is greyed and its tooltip and the text beside it say why: "Turn AI on (…)", "Write a description",
"Name it", "Pick a character or an image on the left", "Say the action", or "Needs <KEY> in ai_service/.env".

The keys live in the file `ai_service/.env` (never sent anywhere but the service they belong to):

| Key | Used for |
|---|---|
| `MISTRAL_API_KEY` | art when the art model is Mistral or "Follow the provider" with Mistral |
| `PIXELLAB_API_KEY` | art by PixelLab, characters when the art model isn't BFL's, non-humanoid animation, PixelLab Animate v3 |
| `BFL_API_KEY` | art by a BFL FLUX model, characters when the art model is BFL's, backgrounds, paint, interiors, FLUX video |
| `AUTO_SPRITE_API_KEY` (or `AUTOSPRITE_API_KEY`) | humanoid animation by AutoSprite (the default); needs AutoSprite's Pro plan |

In Preferences the model lists mark a choice whose key is missing with "(no key)". If a key you added still shows
"(no key)", restart the AI service (untick and tick Use AI, or restart the editor).

Questions: why is the Generate button greyed out? which API key do I need for characters? where do I put my PixelLab key? what does "(no key)" mean?

## Where generated files go

Everything the Generate tabs make is written into the open project, next to a small `.gen.json` file that records how
it was made (the description, the prompt, the model, the seed when the provider gives one, the time it took):

| Made by | Written to |
|---|---|
| Generate ▸ Art | `assets/textures/generated/<name>.png` (one per variant: `<name>-<variant>.png`) |
| Generate ▸ Character | `assets/characters/<name>/<name>.character.png` and `<name>.character.anim.json` |
| Generate ▸ Animate, a picture | `assets/textures/generated/animations/<name>/<name>-1.png …` and `<name>-sheet.png` |
| Generate ▸ Animate, a character | a new clip added to the character's own sheet and picture |
| Make animation (Animate's history) | `<name>-sheet.anim.json` beside the spritesheet |
| Animate a part, Build ▸ Animate | crops in `assets/textures/generated/parts/`, frames under `generated/animations/` |

New files show in the Assets panel at once. Characters are listed in Assets ▸ Characters, animation sheets in Assets ▸
Animations.

Questions: where are generated sprites saved? what is the .gen.json file? where do characters go? where is the animation I made?

## Following progress: the progress strip and Notifications

While a Generate request runs, a progress strip appears across the top of its tab (Art, Character or Animate). It
names the call ("Generating art", "Generating a character", "Generating animations"), what it is doing now (for
example "Animating knight: queued, position 3" while PixelLab's queue waits, or "Animating herbalist: south done"),
how many steps of how many are done, and the seconds or minutes it has taken.

The same calls also appear in **Notifications**, the bell at the right of the status bar. The bell shows a spinning
mark while anything runs and a count of what finished while you weren't looking. Click it to open the Notifications
panel: each AI call (art, characters, animations, backgrounds, a place's steps), a part being animated, an interior's
things, and the editor's notices ("Animation … made: in Assets ▸ Animations", "No player: …"). **Clear** removes what
has finished. Animate a part and Animate the interior show a small floating box while their window is closed; **Hide**
hides that box (it stays in Notifications).

How long things take: art about 10 seconds an image; a character a minute or two; an animation of a picture about a
minute with AutoSprite; a character's action in all 8 directions 2 to 5 minutes with AutoSprite; PixelLab's queue can
add minutes per call. The editor waits up to 5 minutes for art and 15 minutes for a character or an animation.

Questions: how do I know if generation is still running? where are notifications? how long does a character take? why is it "queued"?

## Stopping generation

The Art, Character and Animate tabs have no stop button: a request, once sent, runs until the provider answers (or the
editor's wait runs out: 5 minutes for art, 15 for characters and animations). Their buttons stay off until it is done.
Failed AutoSprite jobs are refunded by AutoSprite.

The longer batch tools can be stopped between items:

- **Build ▸ Animate**: **Stop after this one** finishes the target being animated, then stops.
- **Animate the interior…**: **Stop** puts the parts still waiting back to "to animate"; the one in flight finishes.
- **Animate a part…** and **Animate the interior…** keep working when their window is closed ("Close (it goes on)");
  opening the window again shows where the work has got to.

Questions: how do I stop a generation? can I cancel an animation? does closing the window stop it?

## The AI panel can generate too

Besides the Generate panel, the AI panel can make art, characters and animations as part of an answer. When a change
needs art the project doesn't have, the answer lists it and the service draws it first with the same Art pipeline
(written into `assets/textures/generated/`); characters and animations it asks for are made the same way as the
Character and Animate tabs, using the same models (Project Settings ▸ AI, else Preferences). It looks at the project's
assets first and reuses a character already drawn, or a clip its sheet already has, unless you ask for a new one. It
can also open **Animate the interior…** on a building for you. See the AI panel topics for how answers are reviewed.

See also: Art tab, Character tab, Animate tab, Models and providers, Troubleshooting generation.

Questions: can the AI panel make a character? does the AI panel draw art? will the AI reuse my characters?
