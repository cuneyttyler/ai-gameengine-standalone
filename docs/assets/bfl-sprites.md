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

## Animation by FLUX 3 video (the default)

Preferences ▸ AI ▸ Animate (`animateProvider`, for now only `bfl:flux-3-video`) picks what animates; the BFL strip above
is the fallback without it. `ai_service/app/bfl_video.py`, tested in `builder_tests/bfl_video` and
`builder_tests/bfl_sprites/sheet_test.py`.

1. The pose (a picture, or a character's idle facing that way) on a 1024 square of flat magenta, about two thirds of its
   height, its feet low.
2. `POST /v1/flux-3-video`, `mode: i2v`, the pose as **both keyframes** (the first and the last frame: the clip loops),
   5 seconds, `hd`, 1:1, no audio; the prompt asks for big, clear motion in place (a walk that reads at sprite size:
   knees lifting, clothes swinging, the body bobbing), the camera still, the magenta plain.
3. Its frames read with ffmpeg (`imageio-ffmpeg`'s own); **one cycle** found where the pose comes back closest to a
   frame past the start's settling (0.5 to 2.4 s on); 8 frames taken from it evenly, cut out of the magenta and fitted.
4. A character: each painted direction so (5 calls for 8 directions, the west side mirrored), added to its sheet as one
   clip of 8 frames a direction.

What the tests showed: the character stays exactly itself in every frame and direction (the image strip's weakness); a
gentle walk under a long robe barely reads, so the prompt asks for a brisk, exaggerated one; BFL caps a low-credit
account at one task at a time (429), so a busy answer is waited for and asked again.
