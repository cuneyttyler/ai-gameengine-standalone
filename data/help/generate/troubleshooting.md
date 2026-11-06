# Troubleshooting generation

Common problems with generated art, characters and animations, and their fixes: white borders, art too big, stretched
characters, only four directions, a walk that doesn't play, slow generation, stopping, notifications and missing keys.

## White borders or a light fringe round generated art

Generated art used to come out with a white outline (the image model drew a "sticker" border, or the cut-out left a light
rim). The Art pipeline now prevents it in two ways: the prompt asks for one isolated object on plain white with no white
outline or sticker border, and the cut-out peels off a light rim touching the cleared background (up to three pixels
deep) and takes the backdrop's colour out of the edge's half-transparent pixels, so no light fringe shows on dark ground.

If a picture still has a border or a patch of ground under it:

- **Regenerate** it in Generate ▸ Art (the Art tab's note says a little ground is sometimes drawn under the subject).
- Pictures made before the fix keep their border: regenerate them.
- A white part *inside* the object (a highlight, a label) is kept on purpose: only background connected to the picture's
  edge is cleared.

### Example: A milk churn with a white rim from last year

`milk-churn.png`, made long ago, shows a thin white outline on the dark farmyard ground.

1. On Generate ▸ Art, **Files**, find `milk-churn.png` and click **Regenerate**.
2. The same description, kind, height and style are sent again, and the new picture replaces the file.
3. If the new churn has a patch of grass drawn under it, click **Regenerate** once more.

Result: the churn is cut out cleanly, with no rim on dark ground. Its white enamel band stays, since only background
touching the picture's edge is cleared.

Questions: why does my sprite have a white outline? how do I remove the white border? why is there ground under my object?

## Art too big in the gallery, or the buttons covered

In Generate ▸ Art's Files view, every picture is fitted inside its 92-pixel box: small art is scaled up, big art scaled
down, and nothing spills over the card. Earlier, big art was drawn larger than its box and covered the card's buttons
(so "lies on the ground" couldn't be ticked); that is fixed. The thumbnail size says nothing about the real size: the
file is the Height you asked for (see the card's `.gen.json`, `processing.size`).

If art is too big **in the scene**, it was generated at a large Height; generate it again with a smaller Height rather
than shrinking it (scaling pixel art blurs it), or set the Sprite's size in the Inspector.

### Example: A haystack twice as tall as the barn

The haystack prefab towers over everything in the scene.

1. On Generate ▸ Art, **Files**, open the haystack's `haystack.png.gen.json`: `processing.size` is `[180, 160]`, so
   it was made with **Height** 160.
2. Click **History (N)**, find the haystack request and click **Use again**.
3. Change **Height** to `48` and click **Generate**. The new picture replaces `haystack.png`.

Result: the prefab, which uses the same file, now shows a haystack about a cell tall, still crisp because it was drawn
at that size rather than shrunk.

Questions: my generated art is huge in the gallery; I can't click the buttons on a card; how do I make art smaller?

## Stretched or squashed characters after animating

Adding a clip to a character sheet grows its picture (new rows at the bottom). The viewport once drew a grown sheet by
its old size, so a character looked stretched or showed the wrong frames until the editor was restarted. The editor now
reads sheets again when they change on disk, on Play and when the asset list refreshes, so a new clip shows at once.

If a character still looks stretched:

- Press Play and stop, or let the Assets panel refresh, so the sheet is read again.
- Check the Sprite's width and height in the Inspector: Make player / Make NPC set the height to about two cells (at
  least 48 px) and the width from the frame's proportions; a size typed by hand can distort it.
- For a picture animated on its own, use **Make animation** / **Make prefab** in Generate ▸ Animate's history rather
  than putting the spritesheet (all frames in a row) on a sprite directly.

### Example: Mira looks stretched after her new attack

You added an `attack` to Mira Quill and, in the scene, she suddenly looks tall and thin.

1. Press **F5** to play, then stop. The sheet is read again with its new rows; usually that is all it takes.
2. If she is still stretched, select her and look at the **Sprite** in the Inspector. Her width and height read
   `30` × `96`: someone typed the height by hand.
3. Set them back to what Make NPC gives: about two cells tall (at least 48 px), the width from her 64 × 64 frame, so
   square, for example `64` × `64`.

Result: Mira stands at her normal proportions and plays her new attack correctly.

Questions: why is my character stretched after adding an animation? my character shows the whole sheet; my sprite looks squashed?

## Only four directions, or diagonals look wrong

- **The character was drawn with 4 directions** (Generate ▸ Character ▸ Directions: 4). It has only south, west, east
  and north; walking diagonally shows the nearest of those. Draw it again with **8 (and the diagonals)**.
- **The animation model covers fewer directions**: AutoSprite's pack draws all 8; PixelLab Animate v3 animates five and
  mirrors the west side (it once made only four); FLUX 3 video makes south, east and north only, the diagonals using the
  nearest direction.
- **A single picture** animated in Generate ▸ Animate moves in its own view only, one direction. For every direction,
  animate a character sheet instead.
- **A sheet made by hand** may lack directions: open it in the sheet editor and use **add direction** or set a row to
  **mirror of** another.

### Example: The scarecrow faces right while walking west

The `scarecrow` sheet was imported by hand with walk frames for `s`, `e`, `ne` and `n` only, so walking west it
shows a neighbouring direction's frames.

1. In Assets ▸ **Animations**, right-click `scarecrow` ▸ **Edit…** and click the `walk` clip tab.
2. Under **add direction** pick `w`, then set its row's menu to **mirror of e**.
3. Add `nw` the same way and set it to **mirror of ne**.
4. Click **Save**.

Result: walking west and north-west, the scarecrow faces left, using its east and north-east frames flipped. A
direction still missing (`sw` here) uses the nearest one it has.

Questions: why does my character only have four directions? why does north-east play the north walk? how do I get 8 directions?

## The walk (or another clip) doesn't play

- **The sheet has no `walk` clip**: animate the character with the walk motion. The humanoid animator plays `walk` when
  speed is over 0.1 and `idle` otherwise.
- **A clip named otherwise**: `run`, a custom name or a `punch` play only when asked for by name, unless the sheet
  editor's **plays on** ties them to a state (`plays on: attack`).
- **The wrong animator**: a character needs `humanoid` (or `quadruped`); `prop` only plays idle, open, close and break.
- **No Animator or no Sprite** on the entity, or the Animator's `sheet` path wrong: check the Inspector.
- **A character moved by an AI package or a cutscene** walks by how far it moves; one teleported or dragged doesn't.
- **A part inside a building** plays only while its floor shows.

### Example: The night watchman slides along his patrol without walking

1. Select the watchman and check the Inspector: he has a **Sprite** and an **Animator**, but **animator** reads
   `prop`. Change it to `humanoid`.
2. Press F5. He still slides: in Assets ▸ **Characters**, his card shows only `idle` and `jog` buttons.
3. The humanoid animator plays `walk`, not `jog`. Either right-click him ▸ **Edit…**, click the `jog` tab and type
   `walk` in **plays on**, then **Save**; or animate a walk (right-click ▸ **Animate…**, Motion **walk**).

Result: as his patrol package moves him, his speed goes over 0.1 and his walk plays; when he stops at each post, he
goes back to idle.

Questions: my character slides without walking; my animation doesn't play; why does my NPC not animate?

## Generation is slow

Typical times: art about 10 seconds an image; a character a minute or two (PixelLab up to 4 minutes); a picture's
animation about a minute with AutoSprite; a character's action in all 8 directions 2 to 5 minutes with AutoSprite;
PixelLab Animate v3 can take minutes a call because of its queue ("queued, position N" in the progress strip). Build ▸
Animate and Animate the interior work one item at a time, a few minutes each.

To speed up: use AutoSprite for people instead of PixelLab v3; ask for fewer variants; draw characters at the size you
need. The editor waits up to 5 minutes for art and 15 for a character or animation before giving up.

### Example: A knight's walk stuck at "queued, position 5"

Your project animates people with PixelLab Animate v3 and the Animate tab's strip has said "queued, position 5" for
several minutes.

1. Let it run: PixelLab's queue moves on its own, then the strip says "drawing". The editor waits up to 15 minutes.
2. For the next characters, open **Project Settings ▸ AI** and clear **animate** (or set it to **AutoSprite**), with
   `AUTO_SPRITE_API_KEY` in `ai_service/.env`.
3. Animate the next character's walk: "Animating all 8 directions (2 to 5 minutes)…".

Result: AutoSprite renders all 8 directions in one go, without PixelLab's queue.

Questions: why is animation so slow? how long does a character take? why is PixelLab stuck in the queue?

## Stop buttons and closing windows

- **Generate ▸ Art, Character, Animate**: no stop button. A request runs until it is answered or the wait runs out; the
  button stays off ("Still drawing the last one…") meanwhile.
- **Build ▸ Animate**: **Stop after this one** finishes the current target and stops.
- **Animate the interior…**: **Stop** puts the waiting parts back; the one in flight finishes.
- **Animate a part…** and **Animate the interior…**: closing the window doesn't stop the work ("Close (it goes on)");
  open it again to see the progress.

### Example: You asked for the wrong character and want to start again

You clicked **Generate character** with the description of the blacksmith but the name `baker`.

1. There is no stop button: wait for it to finish (a minute or two). The button reads "Still drawing the last one…"
   meanwhile; you can work elsewhere in the editor.
2. When it's done, right-click `baker` in Assets ▸ **Characters** ▸ **Delete…** and confirm.
3. On the Character tab, click **Use again** on that History entry, change **Name** to `blacksmith` and click
   **Generate character**.

Result: the blacksmith is drawn under the right name, and the wrong one is gone.

Questions: how do I stop generating? how do I cancel Build ▸ Animate? does closing the window cancel it?

## Notifications: where results and errors show

Results and errors show in three places: under the tab's button (in orange for a failure), in the tab's History (a
failed entry with its reason), and in **Notifications**, the bell at the right of the status bar. The bell spins while
anything runs and counts what finished unseen; click it to see each AI call (art, characters, animations, backgrounds,
a place's steps), parts being animated and the editor's notices ("Animation … made: in Assets ▸ Animations", "No player:
stop Play first"). **Clear** removes finished items; **Hide** hides a floating box (it stays in the list).

### Example: Finding out why the lantern animation failed

You started animating `lantern.png`, switched to the scene, and the bell now shows a count of 1.

1. Click the bell at the right of the status bar. The Notifications panel lists the animation call as failed, with
   its reason.
2. Open Generate ▸ **Animate**: under the Animate button the error shows in orange, and the History entry is marked
   failed with the same reason (for example a missing key).
3. Fix the cause, then click **Generate again** on that entry.
4. Back in Notifications, click **Clear** to remove the finished items.

Result: the lantern is animated on the second try, and Notifications names where it went ("Animation … made: in
Assets ▸ Animations") once you click Make animation.

Questions: where do I see if generation failed? what does the bell do? where did my animation go?

## Missing keys and "Needs … in ai_service/.env"

A Generate button that says "Needs PIXELLAB_API_KEY in ai_service/.env" (or `BFL_API_KEY`, `AUTO_SPRITE_API_KEY`) needs
that key in `ai_service/.env`. Which one depends on the model in use: characters need BFL's key with a BFL art model,
else PixelLab's; Animate needs AutoSprite's key with AutoSprite, else PixelLab's; things and doors need PixelLab's (or
BFL's for FLUX video); interiors always need BFL's. Model menus mark choices without a key "(no key)". AutoSprite's API
needs its Pro plan. Other messages: "No project or image generator available" (no art model with a key), "Animations
are made by AutoSprite … or PixelLab" (neither key), "AI is off" (Preferences ▸ AI ▸ Use AI).

### Example: "Needs AUTO_SPRITE_API_KEY in ai_service/.env" on the Animate tab

1. Decide which model you want for people. If you have AutoSprite's Pro plan, add the key to `ai_service/.env`:

   ```
   AUTO_SPRITE_API_KEY=your_autosprite_key
   ```

   and untick and tick **Preferences ▸ AI ▸ Use AI**.
2. If you don't, set **Preferences ▸ Art and painting ▸ Humanoid animation** to **PixelLab Animate with text v3**
   (needs `PIXELLAB_API_KEY`). Check Project Settings ▸ AI ▸ **animate** too: a project value there wins.
3. Return to Generate ▸ Animate: the message is gone and the **Animate** button is on.

Result: characters animate with the model whose key you have; things still use the non-humanoid model.

See also: Models and providers, Generate panel.

Questions: it says needs PIXELLAB_API_KEY; which key do I need; why does it say no image generator available?
