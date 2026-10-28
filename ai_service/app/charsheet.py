"""Character sheets (docs/assets/character-sheets.md): one character, one picture and one aige.anim/1 sheet with
"kind": "character", whose clips (idle, walk, attack…) each hold every direction. Frames are packed into the picture's
square slots, 8 to a row; a clip added later takes new rows at the bottom, so the slots there keep their numbers."""
from __future__ import annotations

import io
import json
import math
from pathlib import Path

from PIL import Image

DIRECTIONS = ["south", "south-east", "east", "north-east", "north", "north-west", "west", "south-west"]
SHORT = {"south": "s", "south-east": "se", "east": "e", "north-east": "ne", "north": "n", "north-west": "nw", "west": "w", "south-west": "sw"}
COLUMNS = 8
# What the animators play (engine/gameplay/Animation.cpp): an action asked for in words, as their clip.
_CLIPS = (("run", "walk"), ("walk", "walk"), ("attack", "attack"), ("strik", "attack"), ("swing", "attack"), ("cast", "cast"),
          ("spell", "cast"), ("hurt", "hurt"), ("hit", "hurt"), ("die", "die"), ("death", "die"), ("idle", "idle"), ("breath", "idle"),
          ("use", "use"))


def clip_for(action: str) -> str:
    a = action.lower()
    for word, clip in _CLIPS:
        if word in a:
            return clip
    return "".join(c if c.isalnum() else "_" for c in a).strip("_")[:24] or "action"


def _image(data: bytes | Image.Image) -> Image.Image:
    return data.convert("RGBA") if isinstance(data, Image.Image) else Image.open(io.BytesIO(data)).convert("RGBA")


def _fit(image: Image.Image, size: int) -> Image.Image:
    """Into a size × size slot: as it is when it's that size, else scaled to fit, centred, its feet at the bottom."""
    if image.size == (size, size):
        return image
    k = min(size / image.width, size / image.height)
    part = image.resize((max(1, round(image.width * k)), max(1, round(image.height * k))), Image.LANCZOS)
    slot = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    slot.paste(part, ((size - part.width) // 2, size - part.height), part)
    return slot


def _append(atlas: Image.Image | None, size: int, frames: list[Image.Image]) -> tuple[Image.Image, int]:
    """`frames` in new rows at the bottom of `atlas`; it, and the first new slot's number."""
    rows = 0 if atlas is None else atlas.height // size
    first = rows * COLUMNS
    grown = Image.new("RGBA", (COLUMNS * size, (rows + math.ceil(len(frames) / COLUMNS)) * size), (0, 0, 0, 0))
    if atlas is not None:
        grown.paste(atlas, (0, 0))
    for k, frame in enumerate(frames):
        slot = first + k
        grown.paste(frame, ((slot % COLUMNS) * size, (slot // COLUMNS) * size), frame)
    return grown, first


def add_clip(sheet: dict, atlas: Image.Image | None, clip: str, by_direction: dict[str, list], fps: float = 8.0,
             loop: str = "loop") -> tuple[dict, Image.Image]:
    """`clip` with each direction's frames ({"south": [png…], …}, the full or short names) added to the sheet: its frames
    in new rows of the picture, the clip naming their slots."""
    size = sheet["grid"]["width"]
    order = [d for d in DIRECTIONS if d in by_direction or SHORT[d] in by_direction]
    frames, directions = [], {}
    for d in order:
        list_ = by_direction.get(d, by_direction.get(SHORT[d], []))
        directions[SHORT[d]] = list(range(len(frames), len(frames) + len(list_)))
        frames += [_fit(_image(f), size) for f in list_]
    atlas, first = _append(atlas, size, frames)
    sheet["clips"][clip] = {"loop": loop, "fps": fps, "directions": {d: [first + i for i in slots] for d, slots in directions.items()}}
    sheet["size"] = [atlas.width, atlas.height]
    return sheet, atlas


def new_sheet(texture: str, size: int, character: dict) -> dict:
    return {"schema": "aige.anim/1", "kind": "character", "character": character, "texture": texture,
            "grid": {"width": size, "height": size}, "size": [0, 0], "fps": 8, "clips": {}}


def write(root: Path, folder: str, name: str, sheet: dict, atlas: Image.Image) -> tuple[str, str]:
    """The picture and the sheet, under `folder` (project-relative): their paths."""
    base = root / folder
    base.mkdir(parents=True, exist_ok=True)
    atlas.save(base / f"{name}.character.png")
    sheet["texture"] = f"{folder}/{name}.character.png"
    (base / f"{name}.character.anim.json").write_text(json.dumps(sheet, indent=2) + "\n", encoding="utf-8")
    return f"{folder}/{name}.character.anim.json", sheet["texture"]


def read(root: Path, path: str) -> tuple[dict, Image.Image]:
    sheet = json.loads((root / path).read_text(encoding="utf-8"))
    with Image.open(root / sheet["texture"]) as im:
        return sheet, im.convert("RGBA")


def directions_of(sheet: dict) -> list[str]:
    """The directions its idle (else first) clip faces, as full names."""
    clips = sheet.get("clips", {})
    clip = clips.get("idle") or next(iter(clips.values()), {})
    names = {v: k for k, v in SHORT.items()}
    return [names[d] for d in clip.get("directions", {}) if d in names]


def frame(sheet: dict, atlas: Image.Image, clip: str, direction: str) -> Image.Image | None:
    """The first frame of `clip` facing `direction` (a full name)."""
    slots = sheet.get("clips", {}).get(clip, {}).get("directions", {}).get(SHORT[direction])
    if not isinstance(slots, list) or not slots:
        return None
    size, slot = sheet["grid"]["width"], slots[0]
    columns = max(1, atlas.width // size)
    x, y = (slot % columns) * size, (slot // columns) * size
    return atlas.crop((x, y, x + size, y + size))
