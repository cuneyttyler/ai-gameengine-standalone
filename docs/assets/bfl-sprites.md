# Characters and animations by BFL

Generate ▸ Character and Generate ▸ Animate draw with the project's art model when it's BFL's (Project Settings ▸ AI ▸
Art), else with PixelLab. `ai_service/app/bfl_sprites.py`; tested in `builder_tests/bfl_sprites`.

## A character

1. **Facing the camera**: painted from the description (the model's `create`), on magenta, the whole body.
2. **The other directions**: each an *edit* of that picture, "the same sprite design, only turned": south-east, east,
   north-east and north for 8 directions (east and north for 4). West, south-west and north-west are the east side
   mirrored: 5 calls for 8 directions, 3 for 4, both sides the same.
3. **Cut and fitted**: the magenta and any shade of it (a shadow painted in it) cleared; every direction at one scale on
   a `size` × `size` canvas, its feet on the same line, so they swap in place.

What the tests showed:

| Finding | Answer |
|---|---|
| "The same face, exactly" | refused by BFL's moderation ("Protected Content": a real person's likeness): the prompt asks for the same *sprite design* |
| A harmless edit refused at one seed, passed at another | moderation judges each picture: a refused direction is asked again with up to 3 seeds |
| Game names in the style ("like Baldur's Gate") | not the cause, but left out: the style is the project's palette in words |
| A dark magenta ground shadow left under each sprite | shades of the key colour cleared too |

## An animation

The picture is placed in the first of 4 cells of one strip, and the model edits the strip into all 4 frames at once
("the same sprite design, holding the same things as in the first frame; only the pose changes": a walk cycle, an
attack, a spell, breathing…). Painted together, the frames stay the same character. Frame lines it draws anyway (a row
or column of a frame mostly light) are cleared, each frame brought to the frames' middle height (but a jump), and fitted
like a character's directions. The frames and a spritesheet are written as PixelLab's were.

## Animation by FLUX 3 video

Preferences ▸ AI ▸ Animate (`animateProvider`) choice `bfl:flux-3-video`; the BFL strip above is the fallback without any. `ai_service/app/bfl_video.py`, tested in `builder_tests/bfl_video` and
`builder_tests/bfl_sprites/sheet_test.py`.

1. The pose (a picture, or a character's idle facing that way) on a 1024 square of flat magenta, about two thirds of its
   height, its feet low.
2. `POST /v1/flux-3-video`, `mode: i2v`, the pose as **both keyframes** (the first and the last frame: the clip loops),
   5 seconds, `hd`, 1:1, no audio; the prompt asks for big, clear motion in place (a walk that reads at sprite size:
   knees lifting, clothes swinging, the body bobbing), the camera still, the magenta plain.
3. Its frames read with ffmpeg (`imageio-ffmpeg`'s own); **one cycle** found where the pose comes back closest to a
   frame past the start's settling (0.5 to 2.4 s on); 8 frames taken from it evenly, cut out of the magenta and fitted.
4. **As many frames as play the cycle at its real speed** at 12 a second (6 to 24): the clip's `fps` is 12.
5. **A character: 3 calls an action** (south, east, north; one clip each, one after another, since a low-credit BFL account
   runs one task at a time). West is east mirrored; the diagonals are left out of the action's clip, so the animator
   plays the nearest direction it has (walking north-east shows the east walk); standing, the diagonals keep their own
   idle poses. HD, 1:1, 5 seconds: the least BFL charges.

What the tests showed (`builder_tests/bfl_video`): every direction in ONE call (five figures side by side on one 16:9
clip, `onecall.py`) costs a fifth but moves badly, so it isn't used; 8 frames played at 8 a second looked choppy, the
cycle is 1-2 seconds of a 24-frames-a-second clip, hence frames sized to the cycle at 12 a second (`ingame.py` shows it
at its in-game size on FirstGame's ground). The character stays exactly itself in every frame and direction (the image strip's weakness); a
gentle walk under a long robe barely reads, so the prompt asks for a brisk, exaggerated one; BFL caps a low-credit
account at one task at a time (429), so a busy answer is waited for and asked again.

## Animation by PixelLab

Preferences ▸ AI ▸ Animate choice `pixellab:animate-v3`: too slow to use (its queue takes minutes a call). `pixellab.animate_v3`, tested in `builder_tests/pixellab_v3` on FirstGame's
a-male-herbalist (BFL-painted): the pose fitted into a square of up to 256 px as both the first and the last frame (it
loops), the action in words, 8 frames, no background (`POST /v2/animate-with-text-v3`, a background job polled). A
character: south, east and north, one call each, west mirrored, the diagonals left to the nearest direction; the clip
at 10 frames a second.

Tried and not used with BFL's image models (cheap, one call a direction): a walk cycle drawn as stick figures or grey
mannequins, one a square of a grid, for the model to paint the character over. FLUX.3 Image klein copied the character
and ignored the poses; FLUX.2 klein 4b kept the poses but painted mannequins; changing the pose of the character copied
into each square to its mannequin's worked better in profile (its colours brought back by matching them to the original)
but left mannequin heads facing the camera.

## Animation by editing copies

No longer offered in Preferences (AutoSprite is the default, docs/assets/autosprite.md); `bfl:image`, FLUX.2 klein 4b, `bfl_sprites.animate_copies`,
tested in `builder_tests/bfl_strip` (`COPIES=1`) on FirstGame's a-male-herbalist. **One call a direction, 8 frames.**

1. The pose copied into all 8 squares of a 4 x 2 grid (1024 x 512, 256 px squares) on flat magenta, about 85 % of a
   square's height, the feet on one ground line.
2. The model is asked to change ONLY each copy's legs, feet, arms and body height to its phase, keeping everything else
   (the facing as drawn, outfit, colours, face, size, place). An edit keeps the facing; drawn into empty squares, a
   front-facing walk turned to profile.
3. Phases by direction: south, knees lifting toward the camera in turn (no "near"/"far" leg, which turns it); north the
   same, away from the camera; the sides and diagonals contact / down / passing / up, then the other leg. Run adds
   longer strides; idle, attack and cast have their own 8 moments; any other action, 8 even moments of it.
4. Frame lines it draws anyway are cleared; the squares cut from one shared box (the bob and the stride kept), fitted
   to the sheet's size, their colours matched to the original (each channel's mean and spread).
5. **A character: 5 calls an action** (south, south-east, east, north-east, north; 3 with 4 directions), one after
   another; the west side is the east side mirrored, so every one of the 8 directions plays its own walk. 10 frames a
   second. Moderation refusals try other seeds; a busy account (429) waits 10 s and asks again.
