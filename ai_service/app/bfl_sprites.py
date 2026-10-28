"""Characters and animations drawn by BFL (docs/assets/bfl-sprites.md), for a project whose art model is BFL's: a
character painted facing the camera, each other direction an edit of it (the same character turned), the west side the
east side mirrored; an animation an edit of a picture into a strip of frames on one canvas (painted together, they stay
the same character), cut into its frames. Same results as PixelLab's (pixellab.py): one PNG a direction, or the frames."""
from __future__ import annotations

import io
from concurrent.futures import ThreadPoolExecutor

from PIL import Image

from .matched import _background, _png

DIRECTIONS = ["south", "south-east", "east", "north-east", "north", "north-west", "west", "south-west"]
_FACING = {  # how each direction looks from the game's camera
    "south": "facing the camera (we see their front)",
    "south-east": "turned three-quarters toward the camera and to the viewer's right (front and right side seen)",
    "east": "in profile, facing the viewer's right",
    "north-east": "turned three-quarters away from the camera, to the viewer's right (back and right side seen)",
    "north": "facing away from the camera (we see their back)",
}
# Painted ones; the others are their mirror (west = east flipped).
_PAINTED = {8: ["south", "south-east", "east", "north-east", "north"], 4: ["south", "east", "north"]}
_MIRROR = {"west": "east", "south-west": "south-east", "north-west": "north-east"}


def _view_words(view: str, isometric: bool) -> str:
    if isometric:
        return "seen from above at an isometric angle (2:1), as in an isometric RPG"
    return {"side": "seen from the side, as in a side-scrolling game",
            "high top-down": "seen from high above, steeply top-down"}.get(view, "seen from slightly above (low top-down), as in most 2D RPGs")


def _cut(png: bytes) -> Image.Image:
    """The sprite out of its magenta background (transparent round it)."""
    picture = Image.open(io.BytesIO(png)).convert("RGB")
    back = _background(picture)
    out = picture.convert("RGBA")
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, _ = px[x, y]
            # The background, and any shade of its key colour (a shadow painted magenta, a fringe).
            if back[y][x] or (r - g > 35 and b - g > 25):
                px[x, y] = (0, 0, 0, 0)
    return out


def _fit(sprites: dict[str, Image.Image], size: int) -> dict[str, Image.Image]:
    """Each on a size × size canvas at one scale (the tallest fits), its feet on the same line: they swap in place."""
    boxes = {k: im.getbbox() for k, im in sprites.items() if im.getbbox()}
    if not boxes:
        return {}
    tallest = max(b[3] - b[1] for b in boxes.values())
    widest = max(b[2] - b[0] for b in boxes.values())
    k = min((size * 0.94) / tallest, (size * 0.94) / widest)
    out = {}
    for name, box in boxes.items():
        part = sprites[name].crop(box)
        part = part.resize((max(1, round(part.width * k)), max(1, round(part.height * k))), Image.LANCZOS)
        canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        canvas.paste(part, ((size - part.width) // 2, size - round(size * 0.03) - part.height), part)
        out[name] = canvas
    return out


def character(painter, description: str, directions: int = 4, size: int = 64, view: str = "low top-down", isometric: bool = False,
              style: str = "", seed: int = 7) -> dict[str, bytes]:
    """A character from `directions` (4 or 8) directions: {direction: PNG}, each `size` px square."""
    see = _view_words(view, isometric)
    front = painter.create(
        "Game sprite, full body. A single character: " + description.strip() + f". Standing, {_FACING['south']}, {see}. "
        "The whole body shown, centred, with space around it. Flat magenta background (#FF00FF): no ground, no shadow, no "
        f"frame, no text, nothing else. {style}".strip(), width=768, height=768, seed=seed)
    painted = {"south": _cut(front)}

    def turn(direction: str) -> tuple[str, Image.Image | None]:
        # The sprite's design, not a likeness ("the same face" reads as a real person's: BFL refuses it).
        prompt = ("Image 1 is a game sprite. Paint the same sprite design: the same outfit, colours, proportions, size and "
                  "standing pose, the same camera and the same place in the picture, only turned: "
                  f"{_FACING[direction]}, {see}. Flat magenta background (#FF00FF), nothing else.")
        # BFL's moderation judges each picture (a harmless one refused at one seed passes at another): up to 3 seeds.
        for attempt in range(3):
            try:
                return direction, _cut(painter.repaint(front, prompt, references=[], seed=seed + 11 + 37 * attempt, width=768, height=768))
            except Exception as error:  # noqa: BLE001 - that direction is missing; the others stand
                failed[direction] = str(error)[:200]
        return direction, None
    failed: dict[str, str] = {}
    with ThreadPoolExecutor(2) as pool:
        for direction, sprite in pool.map(turn, [d for d in _PAINTED[8 if directions >= 8 else 4] if d != "south"]):
            if sprite is not None:
                painted[direction] = sprite
                failed.pop(direction, None)
    if failed:
        print("bfl_sprites: directions not painted:", failed, flush=True)
    for west, east in _MIRROR.items():
        if east in painted and (directions >= 8 or west == "west"):
            painted[west] = painted[east].transpose(Image.FLIP_LEFT_RIGHT)
    return {d: _png(im) for d, im in _fit(painted, size).items() if d in DIRECTIONS}


_POSES = {
    "walk": "a walk cycle: 1 the left foot forward, its heel down; 2 passing, the legs together, the body a little higher; "
            "3 the right foot forward, its heel down; 4 passing again",
    "run": "a run cycle: 1 the left leg reaching forward; 2 both feet off the ground; 3 the right leg reaching forward; 4 both off the ground",
    "idle": "breathing at rest: the chest and shoulders rising a little in 2 and 3, settling in 4",
    "attack": "an attack: 1 the wind-up; 2 the swing starting; 3 the strike, at full reach; 4 the follow-through",
    "cast": "casting a spell: 1 the hands raised; 2 light gathering between them; 3 the spell released; 4 recovering",
    "jump": "a jump: 1 crouching; 2 springing up; 3 at the top; 4 landing",
}


def _pose(action: str) -> str:
    a = action.lower()
    for key, words in (("run", "run"), ("walk", "walk"), ("attack", "attack"), ("strik", "attack"), ("cast", "cast"),
                       ("spell", "cast"), ("jump", "jump"), ("idle", "idle"), ("breath", "idle")):
        if key in a:
            return _POSES[words]
    return f"{action}: four steps of it, one after another, looping back to the first"


def animate(painter, picture: bytes, action: str, description: str = "", direction: str = "south", view: str = "low top-down",
            frames: int = 4, size: int = 64, seed: int = 7) -> list[bytes]:
    """`picture` animated: `frames` PNGs (`size` px square), painted together as one strip of frames, cut apart."""
    source = Image.open(io.BytesIO(picture)).convert("RGBA")
    cell = 256
    canvas = Image.new("RGB", (cell * frames, cell), (255, 0, 255))
    box = source.getbbox() or (0, 0, source.width, source.height)
    part = source.crop(box)
    k = min(cell * 0.82 / part.width, cell * 0.82 / part.height)
    part = part.resize((max(1, round(part.width * k)), max(1, round(part.height * k))), Image.LANCZOS)
    canvas.paste(part, ((cell - part.width) // 2, cell - round(cell * 0.06) - part.height), part)
    what = description.strip() or "the character or thing in the first frame"
    facing = _FACING.get(direction, direction)
    prompt = (f"Image 1 is a strip of {frames} animation frames of a game sprite, side by side, each in its own square; the first "
              f"is drawn. Paint all {frames} frames: {what}, {facing}, {_view_words(view, False)}, doing {_pose(action)}. Every frame "
              "is the SAME sprite design (the same colours, proportions, outfit and size, holding the same things as in the first "
              "frame), in the same place in its square, its "
              "feet on the same line; only the pose changes from frame to frame. Flat magenta background (#FF00FF) everywhere, "
              "no lines or borders between the frames, no text.")
    strip = painter.repaint(_png(canvas), prompt, references=[], seed=seed, width=cell * frames, height=cell)
    painted = Image.open(io.BytesIO(strip)).convert("RGB").resize((cell * frames, cell), Image.LANCZOS)
    # Frame lines the model drew anyway: a row or column of a frame mostly light is a line, background again.
    import numpy as np
    rgb = np.array(painted)
    light = rgb.min(axis=2) > 170
    for i in range(frames):
        x0, x1 = i * cell, (i + 1) * cell
        block = light[:, x0:x1]
        for x in np.nonzero(block.mean(axis=0) > 0.55)[0]:
            rgb[block[:, x], x0 + x] = (255, 0, 255)
        for y in np.nonzero(block.mean(axis=1) > 0.55)[0]:
            rgb[y, x0:x1][block[y]] = (255, 0, 255)
    painted = Image.fromarray(rgb)
    cut = {str(i): _cut(_png(painted.crop((i * cell, 0, (i + 1) * cell, cell)))) for i in range(frames)}
    # A walking (breathing, striking) sprite stays as tall from frame to frame: each brought to the frames' middle height
    # (a jump keeps its own).
    if "jump" not in action.lower():
        heights = sorted((im.getbbox()[3] - im.getbbox()[1]) for im in cut.values() if im.getbbox())
        middle = heights[len(heights) // 2] if heights else 0
        for key, im in list(cut.items()):
            box = im.getbbox()
            if box and middle and 0.75 < middle / (box[3] - box[1]) < 1.35:
                k = middle / (box[3] - box[1])
                part = im.crop(box).resize((max(1, round((box[2] - box[0]) * k)), middle), Image.LANCZOS)
                canvas = Image.new("RGBA", im.size, (0, 0, 0, 0))
                canvas.paste(part, ((im.width - part.width) // 2, box[3] - middle), part)
                cut[key] = canvas
    fitted = _fit(cut, size)
    return [_png(fitted[str(i)]) for i in range(frames) if str(i) in fitted]
