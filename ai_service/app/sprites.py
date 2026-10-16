"""Turns a generated picture into a game sprite (development_plan.md 3.6):
background removed, cropped to the object, scaled to the size the game wants,
optionally reduced to a small palette for a crisp pixel-art look.

Image models draw on an opaque background (here: plain white, as asked), so
the background is found by flood-filling inward from the borders at the
border colour — a white patch inside the object (a highlight, a label) isn't
connected to the border and survives. A light halo left by anti-aliasing at
the edge is then faded out."""

from __future__ import annotations

import io
from collections import deque
from dataclasses import dataclass

from PIL import Image


@dataclass
class SpriteOptions:
    height: int = 48           # target sprite height in pixels (width follows the aspect)
    style: str = "pixel"       # "pixel": reduced palette, hard edges; "smooth": as drawn
    colors: int = 32           # palette size for "pixel"
    tolerance: int = 40        # how far from the background colour still counts as background (0-255 per channel)
    padding: int = 1           # transparent pixels kept around the object
    tile: bool = False         # a top-down ground tile (5.2): kept opaque and square, height x height


def _distance(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def background_color(image: Image.Image) -> tuple[int, int, int]:
    """The most common colour along the border (quantized), i.e. the backdrop."""
    rgb = image.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    counts: dict[tuple[int, int, int], int] = {}
    for x in range(w):
        for y in (0, h - 1):
            c = tuple(v // 8 * 8 for v in px[x, y])
            counts[c] = counts.get(c, 0) + 1
    for y in range(h):
        for x in (0, w - 1):
            c = tuple(v // 8 * 8 for v in px[x, y])
            counts[c] = counts.get(c, 0) + 1
    return max(counts, key=counts.get)  # type: ignore[arg-type]


def remove_background(image: Image.Image, tolerance: int = 40) -> Image.Image:
    """RGBA with the border-connected background made transparent."""
    rgba = image.convert("RGBA")
    w, h = rgba.size
    px = rgba.load()
    bg = background_color(image)
    seen = bytearray(w * h)
    queue: deque[tuple[int, int]] = deque()
    for x in range(w):
        queue.append((x, 0))
        queue.append((x, h - 1))
    for y in range(h):
        queue.append((0, y))
        queue.append((w - 1, y))
    while queue:
        x, y = queue.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y * w + x]:
            continue
        seen[y * w + x] = 1
        r, g, b, _ = px[x, y]
        if _distance((r, g, b), bg) > tolerance:
            continue
        px[x, y] = (r, g, b, 0)
        queue.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    # Defringe: opaque pixels next to the cleared background that are still
    # close to its colour are the anti-aliased halo; fade them by closeness.
    halo = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            touches = any(0 <= nx < w and 0 <= ny < h and px[nx, ny][3] == 0
                          for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
            d = _distance((r, g, b), bg)
            if touches and d <= tolerance * 3:
                halo.append((x, y, int(255 * d / (tolerance * 3))))
    for x, y, alpha in halo:
        r, g, b, _ = px[x, y]
        px[x, y] = (r, g, b, alpha)
    return rgba


def crop_to_content(image: Image.Image, padding: int = 1) -> Image.Image:
    alpha = image.getchannel("A").point(lambda a: 255 if a > 24 else 0)
    box = alpha.getbbox()
    if box is None:
        return image
    x0, y0, x1, y1 = box
    w, h = image.size
    return image.crop((max(0, x0 - padding), max(0, y0 - padding), min(w, x1 + padding), min(h, y1 + padding)))


def to_size(image: Image.Image, height: int, style: str) -> Image.Image:
    w, h = image.size
    width = max(1, round(w * height / h))
    # Resized premultiplied ("RGBa"), so the cleared background's colour doesn't
    # bleed into the edge as a light halo.
    resample = Image.Resampling.BOX if style == "pixel" else Image.Resampling.LANCZOS
    return image.convert("RGBa").resize((width, height), resample=resample).convert("RGBA")


def reduce_palette(image: Image.Image, colors: int) -> Image.Image:
    """Hard edges and at most `colors` colours: alpha snapped to on/off, RGB quantized."""
    alpha = image.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    rgb = image.convert("RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    return out


def make_sprite(data: bytes, options: SpriteOptions) -> tuple[bytes, dict]:
    """Generated picture (any format Pillow reads) -> PNG bytes, and what was done."""
    source = Image.open(io.BytesIO(data))
    source.load()
    if options.tile:
        # Ground fills its cell: no background to remove; the centre square.
        w, h = source.size
        side = min(w, h)
        left, top = (w - side) // 2, (h - side) // 2
        sprite = source.convert("RGBA").crop((left, top, left + side, top + side))
        cropped = sprite.size
        resample = Image.Resampling.BOX if options.style == "pixel" else Image.Resampling.LANCZOS
        sprite = sprite.resize((options.height, options.height), resample=resample)
        sprite.putalpha(255)
    else:
        sprite = remove_background(source, options.tolerance)
        sprite = crop_to_content(sprite, options.padding)
        cropped = sprite.size
        sprite = to_size(sprite, options.height, options.style)
    if options.style == "pixel":
        sprite = reduce_palette(sprite, options.colors)
    out = io.BytesIO()
    sprite.save(out, format="PNG")
    info = {
        "sourceSize": list(source.size),
        "croppedSize": list(cropped),
        "size": list(sprite.size),
        "style": options.style,
        "colors": options.colors if options.style == "pixel" else None,
        "tolerance": options.tolerance,
        "tile": options.tile,
    }
    return out.getvalue(), info
