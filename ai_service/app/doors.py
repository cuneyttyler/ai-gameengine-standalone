"""A building's door opened from its own pixels (docs/assets/animations.md §4.1): no model draws it, so it keeps the
building's art exactly and costs nothing.

From the box the vision model found (vision.find_parts) the doorway is fitted to the picture: its sides the door frame's
strongest vertical edges, its top and bottom along the wall's slope (an isometric wall runs half a pixel down or up per
pixel; which one, the picture says). The panel then swings inward on its hinge, darkening as it turns from the light,
over a dark doorway; everything is cut to the doorway (behind the wall, seen only through it) and to the door's own
pixels there, so a post or a railing in front of it stays drawn. A door on the other face of the house is the same
done on the mirrored picture."""

from __future__ import annotations

import io
import math

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps

SLOPE = 0.5      # a 2:1 isometric wall: half a pixel down (or up) per pixel along it
SWING = 95       # degrees the panel turns, open
DOORWAY = (26, 18, 13, 255)


def _edges(picture: Image.Image):
    """How strongly the picture changes across (gx: vertical edges) and down (gy: horizontal ones), either way."""
    grey = ImageOps.grayscale(Image.alpha_composite(Image.new("RGBA", picture.size, (128, 128, 128, 255)), picture))

    def both(kernel):
        return ImageChops.add(grey.filter(ImageFilter.Kernel((3, 3), kernel, 1)),
                              grey.filter(ImageFilter.Kernel((3, 3), [-v for v in kernel], 1))).load()
    return both([-1, 0, 1, -2, 0, 2, -1, 0, 1]), both([-1, -2, -1, 0, 0, 0, 1, 2, 1])


def fit_doorway(picture: Image.Image, box: list[int], slope: float) -> tuple[int, int, float, float] | None:
    """(left, right, top at the left, bottom at the left) of the doorway near `box` (x, y, w, h), its top and bottom
    running `slope` px down per px; None when it's off the picture."""
    W, H = picture.size
    x, y, w, h = (int(round(v)) for v in box)
    if w < 6 or h < 10 or x < 0 or y < 0 or x + w > W or y + h > H:
        return None
    gx, gy = _edges(picture)

    def column(c: int) -> float:
        if not 1 <= c < W - 1:
            return 0.0
        rows = range(max(1, y + h // 4), min(H - 1, y + h))
        return sum(gx[c, r] for r in rows) / max(1, len(rows))

    # The frame's sides: the strongest vertical edges near the box's (it can be off by a third of a door).
    left = max(range(x - w // 3, x + w // 4 + 1), key=column)
    right = max(range(x + w - w // 4, x + w + w // 3 + 1), key=column)
    if right - left < 6:
        return None

    def along(t: float, g) -> float:
        cols = range(left + 3, right - 2)
        total = 0.0
        for c in cols:
            r = int(t + slope * (c - left))
            if 1 <= r < H - 1:
                total += g[c, r]
        return total / max(1, len(cols))

    # Its top: where its planks start. A door's rows are full of vertical edges (its boards); the beam over it, its
    # grain across, and the shadow under the beam aren't: the first row from which ten in a row are as busy as the
    # door's middle (60% of it) is the door's top. (The beam's edges are stronger than the door's own top.)
    middle = sorted(along(t, gx) for t in range(y + h // 3, y + 2 * h // 3))
    busy = 0.6 * middle[len(middle) // 2]
    top = next((t for t in range(y - h // 10, y + h // 3) if all(along(t + k, gx) >= busy for k in range(0, 10, 2))), y)
    # Its bottom: where its planks stop (a step or a tread in front of it is grain across), else its sill (the strongest
    # edge across near the box's foot).
    sill = max(range(y + h - h // 4, y + h + h // 6), key=lambda t: along(t, gy))
    bottom = next((t for t in range(top + h // 2, sill) if all(along(t + k, gx) < busy for k in range(0, 8, 2))), sill)
    if bottom - top < 10:
        return None
    return left, right, float(top), float(bottom)


def _slope_of(picture: Image.Image, box: list[int]) -> float:
    """Which way the door's wall runs: down to the right (+) or up (-), by which gives the stronger top and sill."""
    best, best_score = SLOPE, -1.0
    gx, gy = _edges(picture)
    x, y, w, h = (int(round(v)) for v in box)
    for slope in (SLOPE, -SLOPE):
        score = 0.0
        for t in range(y - h // 4, y + h + h // 4):
            s = 0.0
            for c in range(x, x + w):
                r = int(t + slope * (c - x))
                if 1 <= r < picture.height - 1 and 1 <= c < picture.width - 1:
                    s += gy[c, r]
            score = max(score, s)
        if score > best_score:
            best, best_score = slope, score
    return best


def door_frames(picture: Image.Image, box: list[int], frames: int = 24, exact: bool = False) -> tuple[list[Image.Image], list[int]] | None:
    """The door in `box` (x, y, w, h: roughly, as found) opening, `frames` frames, each the doorway's size; and the
    doorway's box in the picture (where the frames go). None when no doorway is found there. `exact`: the box was drawn
    by hand round the door, so the doorway is the slanted shape inside it as drawn (its sides the box's, its top at the
    box's top on one side, its bottom at the box's bottom on the other), not fitted to the picture's edges."""
    picture = picture.convert("RGBA")
    slope = _slope_of(picture, box)
    mirrored = slope < 0
    if mirrored:  # the other face: worked out on the mirror image, mirrored back
        picture = ImageOps.mirror(picture)
        box = [picture.width - box[0] - box[2], box[1], box[2], box[3]]
        slope = -slope
    if exact:
        x, y, w, h = (int(round(v)) for v in box)
        x, y = max(0, x), max(0, y)
        w, h = min(w, picture.width - x), min(h, picture.height - y)
        fitted = (x, x + w, float(y), float(y + h) - slope * w) if w >= 4 and h - slope * w >= 6 else None
    else:
        fitted = fit_doorway(picture, box, slope)
    if fitted is None:
        return None
    left, right, top, bottom = fitted
    X0, X1 = left, right
    W = X1 - X0
    Y0 = int(top)
    Y1 = min(picture.height, int(bottom + slope * W) + 1)
    H = Y1 - Y0
    shape = Image.new("L", (W, H), 0)
    ImageDraw.Draw(shape).polygon([(0, top - Y0), (W, top - Y0 + slope * W), (W, bottom - Y0 + slope * W), (0, bottom - Y0)], fill=255)
    door = picture.crop((X0, Y0, X1, Y1))
    # The door's own pixels: the colours of its middle (what stands in front of it is left out of the doorway).
    px = door.load()
    middle = sorted((px[i, j][:3] for i in range(W // 4, 3 * W // 4) for j in range(H // 4, 3 * H // 4)
                     if px[i, j][3] and shape.getpixel((i, j))), key=sum)
    if not middle:
        return None
    ref = middle[len(middle) // 2]
    spread = max(30, sum(abs(a - b) for a, b in zip(middle[len(middle) // 10], middle[9 * len(middle) // 10])) // 2)
    wood = Image.new("L", (W, H), 0)
    m = wood.load()
    for i in range(W):
        for j in range(H):
            r, g, b, a = px[i, j]
            if a and abs(r - ref[0]) + abs(g - ref[1]) + abs(b - ref[2]) <= spread:
                m[i, j] = 255
    wood = wood.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    mask = ImageChops.multiply(shape, wood)
    out = []
    for k in range(frames):
        a = math.radians(SWING * k / max(1, frames - 1))
        cx = math.cos(a) + math.sin(a)                    # its run across the screen (along the wall, then into it)
        cy = slope * (math.cos(a) - math.sin(a))          # and its slope
        width = max(1, math.ceil(W * cx))
        turn = (1 / cx, 0, 0, (slope - cy) / cx, 1, 0)    # screen (X, Y) <- door (u, v): X = u cx, Y = v + u (cy - slope)
        panel = door.transform((width, H), Image.AFFINE, turn, Image.BICUBIC)
        panel.putalpha(ImageChops.multiply(panel.getchannel("A"), shape.transform((width, H), Image.AFFINE, turn)))
        panel = Image.composite(Image.blend(panel, Image.new("RGBA", panel.size, (12, 8, 6, 255)), 0.55 * math.sin(a)), panel,
                                panel.getchannel("A"))
        f = Image.new("RGBA", (W, H), DOORWAY)
        f.alpha_composite(panel.crop((0, 0, min(width, W), H)))
        f.putalpha(mask)
        out.append(ImageOps.mirror(f) if mirrored else f)
    x = picture.width - X1 if mirrored else X0
    return out, [x, Y0, W, H]


def png(image: Image.Image) -> bytes:
    buf = io.BytesIO()
    image.save(buf, "PNG")
    return buf.getvalue()
