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
