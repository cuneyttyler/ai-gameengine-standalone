"""Matched art (docs/places/matched-art.md): a place's art made to fit together, by whichever art provider the user
picked: BFL (a FLUX model: a whole sheet of boxes per image, the style sheet as its reference), PixelLab or Mistral
(one piece per image, in the style sheet's look). The engine drew the sheets (MatchedArtSheets) and cuts what this
makes (CutMatchedArt)."""

from __future__ import annotations

import io
import json
import threading
from pathlib import Path

from PIL import Image

from .providers import ProviderError

KEY_HEX = "#FF00FF"
KEY_WORDS = f"a flat, pure magenta ({KEY_HEX}) background"
# The first thing every piece must have: a game sprite reads by its outline.
OUTLINE = ("MOST IMPORTANT: every asset stands completely clear of everything else, cleanly separated: magenta all round "
           "its silhouette, nothing touching or attached to it: no loose ground, no shadow, no scraps of its surroundings, no "
           "neighbouring object, so it can be cut out with a clean edge (a building's own lot is part of the building).")
# How a piece stands: a building on its own lot (as the best pieces came out), a small thing alone.
LOT = ("Each BUILDING stands on its own lot: a thin, flat plinth exactly as big as its footprint, of the ground it stands on "
       "in this place, with only what belongs to the building on it (no vehicles, no people, no other buildings), nothing "
       "beyond its edge. Each SMALL THING is one clean object, centred, with no plinth or ground under it.")


def lot_words(piece: dict) -> str:
    """How this piece stands: a building on a lot of its own ground (the layout's terrain under it), a small thing alone."""
    if piece.get("layer") != "building":
        return "one clean object, centred, with no plinth or ground under it"
    ground = str(piece.get("ground") or "").replace("-", " ")
    return (f"on its own lot: a thin, flat plinth exactly as big as its {piece.get('w')} by {piece.get('h')} cell footprint"
            + (f", of {ground}" if ground else "") + ", with only what belongs to the building on it, nothing beyond its edge")


def _png(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


class Steps:
    """A run's steps, known beforehand (each sheet and the pieces on it, the check, the fixes as they're decided),
    told to `report(done, total, step, plan)` as each starts and ends (from any thread). A made piece's path is
    `rel`/pieces/<slug>.png (relative to the project at `root`)."""

    def __init__(self, report=None, root: Path | None = None, rel: str = ""):
        self.items: list[dict] = []
        self.report = report
        self.root = root
        self.rel = rel
        self.lock = threading.Lock()

    def add(self, id: str, label: str, pieces: list[dict] | None = None, before: tuple[str, ...] = ()) -> None:
        """A step, at the end, or before the first of the steps `before` (a fix decided late goes before what follows)."""
        item = {"id": id, "label": label, "state": "waiting", "pieces": [{"name": _piece_name(p), "slug": p.get("slug", "")} for p in pieces or []]}
        with self.lock:
            at = next((k for k, i in enumerate(self.items) if i["id"] in before), len(self.items))
            self.items.insert(at, item)
        self._tell()

    def start(self, id: str, label: str | None = None) -> None:
        self._set(id, "working", label)

    def done(self, id: str, ok: bool = True, label: str | None = None) -> None:
        self._set(id, "done" if ok else "failed", label)

    def tick(self, id: str, label: str) -> None:
        """A long step going on: its words ("12 of 50") and the pieces made so far ticked."""
        self._set(id, "working", label, pieces=True)

    def _set(self, id: str, state: str, label: str | None, pieces: bool = False) -> None:
        with self.lock:
            for item in self.items:
                if item["id"] == id:
                    item["state"] = state
                    if label:
                        item["label"] = label
                    if (pieces or state in ("done", "failed")) and self.root is not None:
                        for p in item["pieces"]:
                            p["made"] = bool(p["slug"]) and (self.root / self.rel / "pieces" / f"{p['slug']}.png").exists()
                            if p["made"]:
                                p["path"] = f"{self.rel}/pieces/{p['slug']}.png"
        self._tell()

    def _tell(self) -> None:
        if self.report is None:
            return
        with self.lock:
            plan = [dict(i, pieces=[dict(p) for p in i["pieces"]]) for i in self.items]
        done = sum(1 for i in plan if i["state"] in ("done", "failed"))
        now = [i["label"] for i in plan if i["state"] == "working"]
        self.report(done, len(plan), "; ".join(now) if now else ("Done" if done == len(plan) else ""), plan)


def _piece_name(p: dict) -> str:
    name = str(p.get("words") or p.get("role") or p.get("key", "")).replace("-", " ")
    if int(p.get("variant", 1) or 1) > 1:
        name += f" (look {p.get('variant')})"
    if p.get("view") == "back":
        name += " (back)"
    return name


def _key_list(sheet: dict) -> str:
    """Each colour of the sheet's block-out and what it stands for (the ontology's words), and how it stands."""
    things = (sheet.get("legend") or {}).get("things", [])
    stands = {p.get("role"): lot_words(p) for p in sheet.get("pieces", [])}
    # The thing's own words (the role's draw), without its plot's description ("… cars filling up").
    what = lambda t: str(t.get("what", t.get("kind"))).split(" (")[0]
    return "; ".join(f"{t.get('colour')} {t.get('shape', 'box')} = {what(t)}"
                     + (f" ({stands[t.get('kind')]})" if t.get("kind") in stands else "") for t in things)[:3000]


def _look(context: dict) -> str:
    look = context.get("look") or {}
    story = context.get("story") or {}
    place = context.get("place") or {}
    mood = " ".join(str(story.get(k) or "") for k in ("mood", "purpose", "logline")).strip()
    goal = str(look.get("goals") or look.get("reference") or "").strip()
    text = f"finished isometric game art for a {place.get('type') or 'place'}"
    if goal:
        text += f" ({goal[:300]})"
    if mood:
        text += f"; its mood: {mood[:300]}"
    return text


def style_line(vision, image_png: bytes) -> str:
    """The style sheet's look in a line (outline, shading, light, detail, palette), for providers that take words."""
    if vision is None:
        return ""
    import base64
    message = [{"role": "user", "content": [
        {"type": "text", "text": "Describe the art style of these game assets in one sentence for an artist who must match it: "
                                  "the outline (weight, colour), the shading, where the light comes from, the level of detail, "
                                  "pixel art or painted, the palette (a few colour names). Answer as JSON: {\"style\": \"...\"}."},
        {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(image_png).decode('ascii')}"}]}]
    try:
        return str(json.loads(vision.complete(message).text).get("style", ""))[:500]
    except Exception:  # noqa: BLE001 - the words are a help, not a need
        return ""


# --- BFL: whole sheets ----------------------------------------------------------------------------------

def _sheet_prompt(sheet: dict, context: dict, with_style: bool) -> str:
    if sheet.get("kind") == "ground":
        text = ("Image 1 is a colour-coded block-out of ground patches for a game, each patch a diamond of one colour on "
                f"{KEY_WORDS}. Paint each patch as the ground its colour stands for: its texture seen from above at the same "
                "isometric angle, flat, filling exactly its diamond, nothing standing on it. ")
        text += "Ground kinds: " + "; ".join(g.get("words", "") for g in sheet.get("grounds", [])) + ". "
    else:
        count = len(sheet.get("pieces", []))
        text = (f"{OUTLINE} {_place_words(context)} A SPRITE SHEET of {count} separate game assets, like a game's asset pack: each asset isolated on {KEY_WORDS}. "
                "Image 1 shows where each asset goes: a coloured placeholder shape per asset. Replace each placeholder with the "
                "finished asset its colour stands for (the key below), at exactly its place, size and outline, its base on its "
                f"footprint, seen from the same isometric angle (2:1), {_style_words(context)}. "
                "EACH ASSET STANDS ALONE: nothing between or around them: no roads or paths, no extra plants, vehicles, animals "
                f"or people. {LOT} The magenta stays completely empty and flat. Use no magenta or pink in the assets themselves. ")
        text += "COLOUR KEY: " + _key_list(sheet) + ". "
    if with_style:
        text += ("Image 2 shows assets already made for this game: use it ONLY for the art style (palette, outline, shading, "
                 "light from the upper left, level of detail, pixel size); do not copy its assets or its layout. ")
    text += (f"Keep the background {KEY_HEX} magenta everywhere around them, flat: no floor, no cast shadows on it, no text, "
             "no labels.")
    return text


# Every sprite is only its object: what a model likes to add round a thing (people, a street, a lawn) is never part of it.
ISOLATED = "All assets should only consists of the object described. For example, a building shouldn't have people near it."


def _style_line(context: dict) -> str:
    """The place's look in a few plain words: its era, culture and condition, the story's mood words (never its
    logline: that lists what's in the place, which gets painted), the game it should look like."""
    place = context.get("place") or {}
    style = place.get("style") or {}
    words = [str(style.get(k) or "").strip() for k in ("era", "culture", "condition")]
    words.append(str((context.get("story") or {}).get("mood") or "").strip())
    reference = (context.get("look") or {}).get("reference")
    line = ", ".join(w for w in words if w) or "a game world"
    if isinstance(reference, str) and reference.strip():
        line += f". It looks like {reference.rsplit(':', 1)[0].strip()[:80]}"
    return line


def _place_words(context: dict) -> str:
    """The place the assets belong to, as its mood: its name and kind, its era, culture and condition (the Brief's
    style), the story's mood. Not what's in it: named things get painted as extras."""
    place = context.get("place") or {}
    story = context.get("story") or {}
    style = place.get("style") or {}
    parts = []
    era, culture, condition = (str(style.get(k) or "").strip() for k in ("era", "culture", "condition"))
    kind = str(place.get("type") or "place").replace("-", " ")
    head = f"These assets belong to {place.get('name') or 'a place'}, a {kind}"
    if era or culture:
        head += " of " + ", ".join(x for x in (era, culture) if x)
    parts.append(head)
    if condition:
        parts.append(f"everything {condition}")
    mood = " ".join(str(story.get(k) or "").strip() for k in ("mood", "purpose", "logline")).strip()
    if mood:
        parts.append(f"its mood: {mood[:300]}")
    return "; ".join(parts) + ". Paint only the assets listed; the place's mood is for how they look."


def colour_name(rgb) -> str:
    """A colour in words ("dark greyish brown"): what an image model follows better than a hex code alone."""
    import colorsys
    h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in rgb))
    h *= 360
    shade = "very dark " if v < 0.3 else "dark " if v < 0.45 else "" if v < 0.65 else "light "
    if s < 0.12:
        return shade + "grey"
    name = ("red" if h < 15 or h >= 345 else ("brown" if v < 0.6 else "orange") if h < 45 else ("olive" if v < 0.5 else "yellow") if h < 70
            else "green" if h < 170 else "teal" if h < 200 else "blue" if h < 260 else "purple" if h < 300 else "reddish purple")
    return shade + ("greyish " if s < 0.35 else "") + name


def background_colour(path: Path) -> dict | None:
    """A place's background read for the art coloured by it (relation coloured-by: a wall from the ground it stands on):
    its colour on average (the void and the darkest rock left out), in words and as hex, and a crop of its most
    painted part (512 px) to show the image model. None when there's no picture."""
    if not path.is_file():
        return None
    with Image.open(path) as im:
        im = im.convert("RGB")
    small = im.copy()
    small.thumbnail((256, 256))
    lit = [px for px in small.getdata() if sum(px) >= 30]
    if len(lit) < 50:
        return None
    rgb = tuple(round(sum(px[c] for px in lit) / len(lit)) for c in range(3))
    # The crop: of a 5 x 5 grid of squares a quarter of the picture across, the one with the most ground in it.
    side = max(16, min(im.size) // 4)
    best, at = -1, (0, 0)
    for gy in range(5):
        for gx in range(5):
            x, y = (im.width - side) * gx // 4, (im.height - side) * gy // 4
            sq = small.crop((x * small.width // im.width, y * small.height // im.height,
                             (x + side) * small.width // im.width, (y + side) * small.height // im.height))
            n = sum(1 for px in sq.getdata() if sum(px) >= 30)
            if n > best:
                best, at = n, (x, y)
    crop = im.crop((at[0], at[1], at[0] + side, at[1] + side)).resize((512, 512), Image.LANCZOS)
    return {"rgb": rgb, "hex": "#%02X%02X%02X" % rgb, "words": colour_name(rgb), "png": _png(crop)}


def _coloured_line(piece: dict, context: dict) -> str:
    """For a piece whose art takes the background's colours (its relation coloured-by, docs/places/ontology-in-use.md):
    those colours, said."""
    floor = context.get("floor")
    if piece.get("colourFrom") != "background" or not floor:
        return ""
    return f", in the colours of the ground it stands on: {floor['words']} ({floor['hex']})"


def _thing_words(piece: dict, context: dict) -> str:
    """What a piece is, in the colours it takes from the background (coloured-by)."""
    return _thing_words_alone(piece, context) + _coloured_line(piece, context)


def _thing_words_alone(piece: dict, context: dict) -> str:
    """What a piece is. The ontology's words carry their own era ("a modern car"); with an era in the Brief, the role's
    name, and the era decides how it looks. A wall part is one block that tiles, not a wall."""
    kit = piece.get("kit")
    if kit:
        wall = str(kit.get("wall") or piece.get("role") or "wall").replace("-", " ")
        run = (f"a straight {wall}, three cells long, seen from above at an angle like the box: running from the upper left "
               "down to the lower right, its base sloping one step down for every two across, flat cut ends")
        thing = str(piece.get("purpose") or piece.get("role", "door")).replace("-", " ")
        if kit.get("on"):
            return f"{run}, with {thing} mounted on its middle third"
        if not kit.get("run"):
            return f"{run}, with {thing} set in its middle third, as tall as the wall"
        return run + ", one continuous wall"
    if piece.get("part") and piece.get("layer") == "building":
        return f"one single {str(piece.get('role', 'wall block')).replace('-', ' ')}: a cube of wall, square on every side, that makes a wall when copies stand side by side"
    # What it's for in this place (the plan's purpose: "stairs leading up to the level above"), drawn and sized for it.
    if piece.get("purpose"):
        return f"{str(piece.get('role') or 'asset').replace('-', ' ')}: {piece['purpose']}"
    era = str(((context.get("place") or {}).get("style") or {}).get("era") or "").strip()
    if era:
        return str(piece.get("role") or "asset").replace("-", " ")
    return piece.get("words") or str(piece.get("role") or "asset").replace("-", " ")


def _style_words(context: dict) -> str:
    """The art style only: the build type's reference games, for how things are drawn. Never its goals (what a place
    holds: streets, cars, parks), which turn a sheet of assets into a scene."""
    look = context.get("look") or {}
    reference = look.get("reference")
    text = "finished isometric game art, clean and detailed"
    if isinstance(reference, str) and reference.strip():
        text += f", drawn like {reference.rsplit(':', 1)[0].strip()[:120]}"
    return text


def _is_key(r: int, g: int, b: int) -> bool:
    return abs(r - 255) + g + abs(b - 255) < 150 or (r - g > 90 and b - g > 90 and abs(r - b) < 90)


def _border_colour(image: Image.Image) -> tuple[int, int, int]:
    """The background a painting really has: the median of its border pixels (a model asked for pure magenta often
    paints a shaded magenta, a maroon or a dark purple instead)."""
    rgb = image.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    step = max(1, min(w, h) // 64)
    border = [px[x, y] for x in range(0, w, step) for y in (0, h - 1)] + [px[x, y] for y in range(0, h, step) for x in (0, w - 1)]
    return tuple(sorted(c[i] for c in border)[len(border) // 2] for i in range(3))


def _background(image: Image.Image, tolerance: int = 70) -> list[list[bool]]:
    """Which pixels are the background: the key colour, or (when the painting's background isn't the key) pixels close to
    its border colour that join the border, flood-filled from it, so the same colour inside the thing stays."""
    rgb = image.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    back = _border_colour(rgb)
    if _is_key(*back):
        return [[_is_key(*px[x, y]) for x in range(w)] for y in range(h)]
    near = [[_is_key(*px[x, y]) or sum(abs(px[x, y][i] - back[i]) for i in range(3)) < tolerance for x in range(w)] for y in range(h)]
    out = [[False] * w for _ in range(h)]
    stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        if out[y][x] or not near[y][x]:
            continue
        out[y][x] = True
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not out[ny][nx] and near[ny][nx]:
                stack.append((nx, ny))
    return out


def _cleared(image: Image.Image, background: list[list[bool]] | None = None) -> Image.Image:
    """The key colour made clear, and its tint taken out of every pixel near it: how much a pixel leans to the key
    (red and blue together above green) is how clear it gets and how much of that lean it loses, so a purplish haze
    or shadow on the key goes, and an edge keeps its own colour. `background`: the crop's own background mask (when the
    painting's background isn't the key: _background)."""
    image = image.convert("RGBA")
    px = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = px[x, y]
            if background is not None and background[y][x]:
                px[x, y] = (0, 0, 0, 0)
                continue
            if _is_key(r, g, b):
                px[x, y] = (0, 0, 0, 0)
                continue
            lean = min(r, b) - g
            if lean > 20 and abs(r - b) < 110:
                t = min(1.0, (lean - 20) / 110)
                cut = int(lean * (0.5 + 0.5 * t))
                px[x, y] = (r - cut, g, b - cut, int(a * (1 - 0.85 * t)))
    return image


def find_items(png: bytes) -> list[dict]:
    """The separate things on a painted sheet: each blob on the key colour (found on a quarter-size mask, near
    blobs joined: a lamp's head and its pole), cropped at full size with the key cleared. In reading order."""
    image = Image.open(io.BytesIO(png)).convert("RGB")
    k = 4
    small = image.resize((max(1, image.width // k), max(1, image.height // k)), Image.NEAREST)
    w, h = small.size
    # The background: the key, or the colour the model really painted round the things (read from the border).
    keyed = _is_key(*_border_colour(image))
    back = _background(small)
    solid = [[not back[y][x] for x in range(w)] for y in range(h)]
    seen = [[False] * w for _ in range(h)]
    blobs = []
    for y in range(h):
        for x in range(w):
            if not solid[y][x] or seen[y][x]:
                continue
            stack, box, n = [(x, y)], [x, y, x, y], 0
            seen[y][x] = True
            while stack:
                cx, cy = stack.pop()
                n += 1
                box = [min(box[0], cx), min(box[1], cy), max(box[2], cx), max(box[3], cy)]
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < h and solid[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            if n >= max(12, w * h // 4000):
                blobs.append({"box": box, "area": n})
    # Blobs a pixel or so apart are one thing (a lamp's head and its pole); farther apart, separate things.
    merged = True
    while merged:
        merged = False
        for i in range(len(blobs)):
            for j in range(i + 1, len(blobs)):
                a, b = blobs[i]["box"], blobs[j]["box"]
                if a[0] - 1 <= b[2] and b[0] - 1 <= a[2] and a[1] - 1 <= b[3] and b[1] - 1 <= a[3]:
                    blobs[i] = {"box": [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])],
                                "area": blobs[i]["area"] + blobs[j]["area"]}
                    del blobs[j]
                    merged = True
                    break
            if merged:
                break
    items = []
    for blob in blobs:
        x0, y0, x1, y1 = blob["box"]
        crop = image.crop((max(0, (x0 - 1) * k), max(0, (y0 - 1) * k), min(image.width, (x1 + 2) * k), min(image.height, (y1 + 2) * k)))
        if keyed:
            cleared = _cleared(crop)
        else:
            # Not the key: the crop's background is what joins its edge in the painting's own background colour.
            cleared = _cleared(crop, _background(crop))
        items.append({"image": cleared, "box": [x0 * k, y0 * k, (x1 + 1) * k, (y1 + 1) * k], "area": blob["area"] * k * k})
    # Reading order: rows (by their middles), then left to right.
    items.sort(key=lambda i: (i["box"][1] + i["box"][3]) / 2)
    rows: list[list[dict]] = []
    for item in items:
        middle, height = (item["box"][1] + item["box"][3]) / 2, item["box"][3] - item["box"][1]
        if rows and abs(middle - sum((r["box"][1] + r["box"][3]) / 2 for r in rows[-1]) / len(rows[-1])) < 0.5 * height:
            rows[-1].append(item)
        else:
            rows.append([item])
    return [item for row in rows for item in sorted(row, key=lambda i: i["box"][0])]


def _board(images: list, cell: int = 320, numbered: bool = False, background=(255, 0, 255)) -> Image.Image:
    from PIL import ImageDraw
    columns = min(4, max(1, len(images)))
    rows = (len(images) + columns - 1) // columns
    board = Image.new("RGB", (columns * cell, max(1, rows) * cell), background)
    draw = ImageDraw.Draw(board)
    for i, im in enumerate(images):
        thumb = im.copy()
        thumb.thumbnail((cell - 24, cell - 24))
        ox, oy = (i % columns) * cell + (cell - thumb.width) // 2, (i // columns) * cell + (cell - thumb.height) // 2
        board.paste(thumb, (ox, oy), thumb if thumb.mode == "RGBA" else None)
        if numbered:
            x, y = (i % columns) * cell, (i // columns) * cell
            draw.rectangle([x, y, x + 34, y + 26], fill=(0, 0, 0))
            draw.text((x + 6, y + 6), str(i + 1), fill=(255, 255, 255))
    return board


NAMING_ERRORS: list[str] = []  # what went wrong naming items, for the log


def name_items(vision, items: list[dict], pieces: list[dict]) -> dict[int, int]:
    """Which item shows which piece (piece index -> item index): the vision model reads a numbered board against
    the kinds; without it, the pieces in order when there are as many items."""
    if vision is not None and items:
        import base64
        board = _board([i["image"] for i in items], numbered=True, background=(128, 128, 128))
        kinds = "; ".join(f"{n + 1}) {p.get('role')}{' seen from the back' if p.get('view') == 'back' else ''}: {p.get('words', '')[:120]}"
                          for n, p in enumerate(pieces))
        message = [{"role": "user", "content": [
            {"type": "text", "text": "The picture shows numbered game assets. Match each kind below to the number of the asset that "
                                      "shows it (each number at most once; null if none fits). Kinds: " + kinds +
                                      '. Answer as JSON: {"matches": [{"kind": 1, "asset": 3}, ...]}.'},
            {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(_png(board)).decode('ascii')}"}]}]
        for _ in range(3):  # a busy or odd answer: asked again
            try:
                answer = json.loads(vision.complete(message).text)
            except Exception as error:  # noqa: BLE001 - asked again, then by order
                NAMING_ERRORS.append(str(error)[:200])
                continue
            out, used = {}, set()
            for m in answer.get("matches", []):
                try:
                    kind, asset = int(m.get("kind")), int(m.get("asset"))
                except (TypeError, ValueError):
                    continue
                if 1 <= kind <= len(pieces) and 1 <= asset <= len(items) and asset not in used:
                    out[kind - 1] = asset - 1
                    used.add(asset)
            if out:
                return out
    if len(items) == len(pieces):
        return {i: i for i in range(len(pieces))}
    return {}


def _piece_input(sheet_png: Path, piece: dict, side: int = 1024) -> bytes:
    """One piece's box alone, cut from its sheet's block-out, on the key colour, square."""
    block = Image.open(sheet_png).convert("RGB")
    x0, y0, x1, y1 = piece["box"]
    m = int(0.25 * max(x1 - x0, y1 - y0)) + 8
    crop = block.crop((max(0, x0 - m), max(0, y0 - m), min(block.width, x1 + m), min(block.height, y1 + m)))
    crop.thumbnail((side, side))
    square = Image.new("RGB", (side, side), (255, 0, 255))
    square.paste(crop, ((side - crop.width) // 2, (side - crop.height) // 2))
    return _png(square)


def _paint_sheet(painter, model: str, folder: Path, sheet: dict, context: dict, refs: list[bytes], seed: int) -> bytes:
    """A sheet's block-out painted whole: at most 4:1 (padded with the key colour, the padding cut off after), scaled
    up for klein (its V3 recipe)."""
    from .bfl import _four_mp
    block = Image.open(folder / sheet["png"]).convert("RGB")
    pad = max(0, round(block.width / 4) - block.height)
    if pad:
        padded = Image.new("RGB", (block.width, block.height + pad), (255, 0, 255))
        padded.paste(block, (0, pad // 2))
        block = padded
    image = block
    if model.startswith("flux-2-klein") and block.width < 4096:
        image = block.resize((4096, round(4096 * block.height / block.width)), Image.LANCZOS)
    png = painter.repaint(_png(image), _sheet_prompt(sheet, context, bool(refs)), references=refs, seed=seed,
                          **_four_mp(block.width, block.height))
    if pad:
        painted = Image.open(io.BytesIO(png)).convert("RGB")
        k = painted.height / block.height
        top, keep = round(pad // 2 * k), round((block.height - pad) * k)
        png = _png(painted.crop((0, top, painted.width, top + keep)))
    return png


def edit_from_example(painter, folder: Path, piece: dict, example: dict, context: dict, seed: int, why: str = "") -> bool:
    """A piece made by turning a good piece into it (image 1: the example, alone on the key colour): klein edits, so
    the new piece keeps the example's angle, framing, finish and clean separation, and with no box to follow it
    can't take a lamp for a building."""
    with Image.open(folder / "pieces" / f"{example['slug']}.png") as im:
        source = im.convert("RGBA")
    source.thumbnail((880, 880))
    canvas = Image.new("RGB", (1024, 1024), (255, 0, 255))
    canvas.paste(source, ((1024 - source.width) // 2, (1024 - source.height) // 2), source)
    what = _thing_words(piece, context)
    prompt = (f"{OUTLINE} {_place_words(context)} Turn the {example.get('role', 'asset')} in image 1 into a different game asset: {what}. Keep the same 3/4 "
              "isometric angle, the same close-up framing (filling the picture the same way), the same art style, outlines, "
              "finish and level of detail. ")
    prompt += f"It stands {lot_words(piece)}"
    prompt += ", as tall as such a building is. " if piece.get("layer") == "building" else ". "
    prompt += f"Alone on {KEY_WORDS}: no other object, nothing around it; no magenta or pink in it."
    # `why` (what was wrong last time) changes the seed only: named in a prompt, it would be painted.
    made = painter.repaint(_png(canvas), prompt, references=[], seed=seed + len(why), width=1024, height=1024)
    found = find_items(made)
    if not found:
        return False
    max(found, key=lambda i: i["area"])["image"].save(folder / "pieces" / f"{piece['slug']}.png")
    return True


def _pack(units: list[list[dict]], buildings: int, small: int) -> list[list[dict]]:
    """Units (a kind's pieces: its looks and views, kept together) packed into sheets of up to `buildings` buildings and
    `small` small things; a unit too big for one sheet split into sheet-sized runs (a front and its back together)."""
    sheets, current, nb, ns = [], [], 0, 0
    for unit in units:
        cap = buildings if unit[0].get("layer") == "building" else small
        cap -= cap % 2 if any(p.get("view") for p in unit) else 0
        for start in range(0, len(unit), max(1, cap)):
            part = unit[start:start + max(1, cap)]
            ub = sum(1 for p in part if p.get("layer") == "building")
            us = len(part) - ub
            if current and (nb + ub > buildings or ns + us > small):
                sheets.append(current)
                current, nb, ns = [], 0, 0
            current += part
            nb, ns = nb + ub, ns + us
    if current:
        sheets.append(current)
    return sheets


def _groups(pieces: list[dict], buildings: int = 4, small: int = 6) -> list[list[dict]]:
    """The pieces in sheets of up to `buildings` buildings and `small` small things (each klein call one sheet). Things
    that work together (`with`, from the ontology: a door and its wall) on sheets of their own, nothing else with them,
    first; then the rest, one of each kind first so the first sheet shows the place's variety. A kind's looks
    (variants) and views (front, back) stay on one sheet, so they're told apart there."""
    # A run (a wall, a door or a light in one) alone on its sheet: nothing like it beside it to be taken for it.
    alone = [[p] for p in pieces if p.get("kit")]
    pieces = [p for p in pieces if not p.get("kit")]
    units: dict[str, list[dict]] = {}
    for p in pieces:
        units.setdefault(p.get("variantKey") or p["key"], []).append(p)
    links = {u: {k for p in unit for k in p.get("with", [])} for u, unit in units.items()}
    sets, seen = [], set()
    for u in units:
        if u in seen or not links[u]:
            continue
        group, todo = [], [u]
        while todo:
            k = todo.pop()
            if k in seen or k not in units:
                continue
            seen.add(k)
            group.append(k)
            todo += list(links[k])
        sets.append(group)
    out = list(alone)
    for group in sorted(sets, key=len, reverse=True):
        out += _pack([units[k] for k in group], buildings, small)
    rest = [units[u] for u in units if u not in seen]
    rest = [u for u in rest if u[0].get("layer") == "building"] + [u for u in rest if u[0].get("layer") != "building"]
    out += _pack(rest, buildings, small)
    return out


def make_with_bfl(api_key: str, model: str, root: Path, rel: str, sheets: dict, context: dict, seed: int = 7,
                  vision=None, track: Steps | None = None) -> list[dict]:
    """The art in sheets of the generic prompt (docs/places/matched-art.md §2): up to 4 buildings and 6 small things a
    klein call; things that work together on sheets of their own; the first sheet sets the look and every later one
    gets it as image 2; what no sheet gave (or the check finds wrong) on one more round of sheets; then the ground
    textures. A piece already made (pieces/<slug>.png, from an earlier run) is kept: only what's missing is painted,
    in the look of the earlier style sheet. Each piece written as pieces/<slug>.png. The background is painted
    apart (docs/places/background.md), not here."""
    from .bfl import BflPainter
    folder = root / rel
    (folder / "pieces").mkdir(parents=True, exist_ok=True)
    painter = BflPainter(api_key, model)
    # The background, for the art coloured by it (coloured-by): its colour and a crop of it.
    if context.get("background"):
        context = dict(context, floor=background_colour(root / context["background"]))
    # A piece cut from another's painting (a run's end: `cutFrom`) isn't painted itself.
    every = [dict(p, _sheet=s["png"]) for s in sheets.get("sheets", []) if s.get("kind") not in ("ground", "plan")
             for p in s.get("pieces", []) if not p.get("cutFrom")]
    steps: list[dict] = []
    style_png: bytes | None = (folder / "style-sheet.png").read_bytes() if (folder / "style-sheet.png").exists() else None
    had = {p["key"] for p in every if (folder / "pieces" / f"{p['slug']}.png").exists()}
    made: set[str] = set(had)
    todo = [p for p in every if p["key"] not in had]
    # Another look of a kind (variant 2, 3…) and a back view aren't painted from boxes (a small box for "a bicycle,
    # version 2" comes back a hut): each is an edit of its accepted first piece, after the sheets (derive_pieces).
    derived = [p for p in todo if int(p.get("variant", 1)) > 1 or p.get("view") == "back"]
    todo = [p for p in todo if p not in derived]
    # A wall and what's set in it or mounted on it: blocks, the wall first and the rest edits of it (make_blocks), not
    # boxes on a sheet. Made before the sheets; kept like any piece.
    blocks = [p for p in todo if p.get("kit")]
    todo = [p for p in todo if not p.get("kit")]
    # The method (2026-10-27): "words", each piece painted alone from a description the LLM writes for it (a kind's
    # looks different in form), no sheets, at most one repaint; "sheets", the earlier style-sheet way (boxes on sheets,
    # a check, fix rounds), which cost many calls and drew plain one-cell houses.
    words = context.get("artMethod", "words") != "sheets"
    if words:
        todo += [p for p in derived if p.get("view") != "back"]  # other looks: from their own words too
        derived = [p for p in derived if p.get("view") == "back"]
    groups = [] if words else _groups(todo)
    # The steps, known now (the fixes are added when the check has said what's wrong).
    track = track or Steps()
    ground = next((s for s in sheets.get("sheets", []) if s.get("kind") == "ground"), None)
    if blocks:
        track.add("blocks", "Wall blocks: the wall, then what's set in it or on it", blocks)
    first_is_style = bool(groups) and style_png is None
    if words and todo:
        track.add("describe", "Describing each kind and its looks (the LLM)")
        track.add("paint", f"Painting {len(todo)} pieces from their descriptions", todo)
    for n, group in enumerate(groups):
        if n == 0 and first_is_style:
            track.add("style", "Style sheet: sets the look of every other sheet", group)
        else:
            k = n if first_is_style else n + 1
            track.add(f"add-{k}", f"Sheet {k + (1 if first_is_style else 0)} of {len(groups)}", group)
    if vision is not None:
        track.add("check", "Checking the pieces (the vision model)")
    if derived:
        track.add("derived", "Back views (edits of their front)" if words else "Other looks and back views (edits of their first look)", derived)
    if vision is not None:
        track.add("facing", "Turning pieces to face the right way")
    if ground and not (folder / "ground-painted.png").exists():
        track.add("ground", "Ground textures")
    later = ("derived", "facing", "ground")
    if blocks:
        # Every block of the kit, the kept wall among them: a door or a light missing is an edit of the wall there is.
        track.start("blocks")
        kit = [p for p in every if p.get("kit")]
        steps += make_blocks(painter, folder, kit, context, style_png, seed + 500, make={p["key"] for p in blocks})
        made |= {p["key"] for p in blocks if (folder / "pieces" / f"{p['slug']}.png").exists()}
        track.done("blocks")
    if had:
        steps.append({"kept": sorted(had)})
    checked_path = folder / "checked.json"
    try:
        checked = set(json.loads(checked_path.read_text(encoding="utf-8"))) if checked_path.exists() else set()
    except ValueError:
        checked = set()
    if words:
        result = paint_from_words(painter, folder, todo, every, context, seed, writer=vision, track=track)
        steps.append(result["step"])
        made |= result["made"]
        new = [p for p in todo if p["key"] in result["made"]]
        again = [p for p in todo if p["key"] not in result["made"]]
        wrong, unchecked, repainted = set(), set(), set(result["made"])
    else:
        from concurrent.futures import ThreadPoolExecutor
        if first_is_style:
            # The style sheet first: every other sheet is painted in its look.
            track.start("style")
            result = style_sheet(painter, model, folder, sheets, context, vision, seed, pieces=groups[0], name="style")
            style_png = result["png"]
            made |= set(result["named"])
            track.done("style")
            steps.append({"sheet": "style", "pieces": [p["key"] for p in groups[0]], "items": result["items"], "named": list(result["named"]),
                          "prompt": result["prompt"]})
            groups = groups[1:]

        def sheet(numbered):
            # Each later sheet needs only the style sheet: four painted at a time.
            n, group = numbered
            track.start(f"add-{n + 1}")
            result = style_sheet(painter, model, folder, sheets, context, vision, seed + n + 1, pieces=group,
                                 refs=[style_png] if style_png else None, name=f"add-{n + 1}")
            track.done(f"add-{n + 1}")
            return f"add-{n + 1}", group, result
        with ThreadPoolExecutor(4) as pool:
            for name, group, result in pool.map(sheet, enumerate(groups)):
                made |= set(result["named"])
                steps.append({"sheet": name, "pieces": [p["key"] for p in group], "items": result["items"], "named": list(result["named"]),
                              "prompt": result["prompt"]})
        # The fix: what no sheet gave, and what the check finds wrong, on one more round of sheets. Every piece not yet
        # checked is (checked.json: the pieces that passed, whichever run made them), not only this run's.
        checked_path = folder / "checked.json"
        try:
            checked = set(json.loads(checked_path.read_text(encoding="utf-8"))) if checked_path.exists() else set()
        except ValueError:
            checked = set()
        # Blocks have their own check (make_blocks: the box's outline) and are never painted on a sheet.
        new = [p for p in every if p["key"] in made and p["slug"] not in checked and not p.get("kit")]
        if vision is not None:
            track.start("check", f"Checking the pieces (the vision model): {len(new)} to check")
        wrong, unchecked, said = set(), set(), {}
        for start in range(0, len(new), 16):
            chunk = new[start:start + 16]
            found = check_pieces(vision, folder, chunk)
            if found is None:
                unchecked |= {p["key"] for p in chunk}
                continue
            wrong |= {chunk[i]["key"] for i in found}
            said.update({chunk[i]["slug"]: why for i, why in found.items()})
        # The shape test (two things found as one, or half of one) for what this run painted only: an earlier piece the
        # vision model passes stays (a building is taller than its flat box, which this test can't tell from a merge).
        painted_now = {p["key"] for p in todo}
        wrong |= {p["key"] for p in new if p["key"] in painted_now and not p.get("kit") and not _fits_its_box(folder, p)}
        # A building painted with another footprint than its box's (a square house for a long lot): painted again.
        wrong |= {p["key"] for p in new if p["key"] in painted_now and p.get("layer") == "building" and not p.get("kit") and not _foot_fits(folder, p)}
        steps.append({"checked": len(new) - len(unchecked), "unchecked": len(unchecked), "wrong": sorted(wrong), "said": said})
        if vision is not None:
            missing = sum(1 for p in every if not p.get("kit") and p["key"] not in made)
            track.done("check", label=f"Checked {len(new) - len(unchecked)} pieces: {len(wrong)} wrong" + (f", {missing} not on their sheet" if missing else ""))
        # A piece made before is kept beside its repaint: if the repaint fails too, the earlier one comes back.
        kept_before = {}
        for p in every:
            if p["key"] in wrong and p["key"] not in painted_now and (folder / "pieces" / f"{p['slug']}.png").exists():
                kept_before[p["slug"]] = (folder / "pieces" / f"{p['slug']}.png").read_bytes()
        again = [p for p in every if not p.get("kit") and (p["key"] not in made or p["key"] in wrong)]
        # Up to three rounds: what is still not made (or found wrong) is painted again, each time anew.
        fixed, repainted = 0, set()
        for round_ in range(3):
            if not again:
                break

            def fix(numbered, round_=round_):
                n, group = numbered
                track.start(f"fix-{fixed + n + 1}")
                result = style_sheet(painter, model, folder, sheets, context, vision, seed + 50 + 17 * round_ + n, pieces=group,
                                     refs=[style_png] if style_png else None, name=f"fix-{fixed + n + 1}")
                track.done(f"fix-{fixed + n + 1}")
                return n, group, result
            groups = _groups(again)
            for n, group in enumerate(groups):
                track.add(f"fix-{fixed + n + 1}", f"Fix round {round_ + 1} of up to 3: sheet {n + 1} of {len(groups)} (what was missing or wrong)",
                          group, before=later)
            with ThreadPoolExecutor(4) as pool:
                for n, group, result in pool.map(fix, enumerate(groups)):
                    steps.append({"sheet": f"fix-{fixed + n + 1}", "pieces": [p["key"] for p in group], "items": result["items"],
                                  "named": list(result["named"])})
            fixed += len(groups)
            made_now = {p["key"] for p in again if (folder / "pieces" / f"{p['slug']}.png").exists()}
            repainted |= made_now
            if vision is not None and kept_before:
                # A repaint of an earlier piece that's wrong too: the earlier one back (it was at least one thing).
                redone = [p for p in again if p["slug"] in kept_before and p["key"] in made_now]
                for i in check_pieces(vision, folder, redone) or {}:
                    (folder / "pieces" / f"{redone[i]['slug']}.png").write_bytes(kept_before[redone[i]["slug"]])
            again = [p for p in again if p["key"] not in made_now]
    if derived:
        track.start("derived")
        steps.append({"derived": derive_pieces(painter, folder, derived, every, context, seed, writer=vision)})
        new += [p for p in derived if (folder / "pieces" / f"{p['slug']}.png").exists()]
        track.done("derived")
    # The interiors (Phase 27): each building one can go into whose exterior is made, its interior an edit of it.
    rooms = [p for p in every if p.get("interior") and p.get("view") != "back" and (folder / "pieces" / f"{p['slug']}.png").exists()
             and not (folder / "pieces" / f"{p['slug']}-interior.png").exists()]
    if rooms:
        track.add("interiors", f"Interiors: {len(rooms)} buildings cut away to show their rooms", rooms, before=("facing", "ground"))
        track.start("interiors")
        made_rooms = make_interiors(painter, folder, rooms, context, seed, vision=vision)
        steps.append({"interiors": made_rooms})
        track.done("interiors", label=f"Interiors: {len(made_rooms)} of {len(rooms)} made")
    # Things with a front face the lower left (their back view: their back): turned round where they don't.
    if vision is not None:
        track.start("facing")
    turned = face_pieces(vision, folder, [p for p in new + again if p.get("view")])
    if turned:
        steps.append({"turned": turned})
    if vision is not None:
        track.done("facing", label=f"Turned {len(turned)} piece(s) to face the right way" if turned else "Every piece faces the right way")
    # What passed (the check, or its fix) isn't checked again.
    if vision is not None:
        failing = {p["key"] for p in again}
        passed = {p["slug"] for p in new if p["key"] not in wrong and p["key"] not in unchecked} | \
                 {p["slug"] for p in every if p["key"] in repainted and p["key"] not in failing}
        checked_path.write_text(json.dumps(sorted(checked | passed)), encoding="utf-8")
    # The ground's textures (each terrain's).
    if ground and not (folder / "ground-painted.png").exists():
        track.start("ground")
        (folder / "ground-painted.png").write_bytes(_paint_sheet(painter, model, folder, ground, context, [style_png] if style_png else [], seed))
        steps.append({"sheet": "ground"})
        track.done("ground")
    return steps


VARIANT_TASK = """You write image prompts for a game's art. Given one thing of a place, write {n} descriptions of other
things of the same kind for the same place, each one clearly different in FORM, not only in colour: a different number
of storeys, footprint shape (square, long, L-shaped, round), roof form (gabled, hipped, thatched, flat, steep), size, and
features (a porch, a balcony, an outside stair, a chimney, a lean-to, a small tower, a shop front, a fenced yard); for a
small thing, another build, silhouette and parts. For example, for a house: "a one-storey cottage with one small window on
each side and a low thatched roof" and "a two-storey timber house with a balcony over the door and a steep gabled roof"
are as different as they should be. Keep each the same kind of thing, the same era, culture and style as the place, and
about the size such a thing has (it stands on the same footprint). Each one line, concrete and visual (what it's made of,
its shape, its parts), no names, no story, no background. Answer JSON: {{"variants": ["...", "..."]}}"""


def variant_words(writer, kind: str, first: str, place: str, n: int) -> list[str]:
    """`n` descriptions of other things of the kind, each different in form (VARIANT_TASK); [] without an answer."""
    import json as _json
    if writer is None or n <= 0:
        return []
    try:
        answer = _json.loads(writer.complete([{"role": "system", "content": VARIANT_TASK.format(n=n)},
                                              {"role": "user", "content": f"The place: {place}\nThe kind: {kind}\nThe first one: {first}"}]).text)
    except Exception:  # noqa: BLE001 - the edit way is used instead
        return []
    return [str(v).strip() for v in answer.get("variants", []) if str(v).strip()][:n]


LOOKS_TASK = """You write image prompts for a game's art. For one kind of thing in a place, write one description for
each numbered item (its footprint is given): every one clearly different in FORM from all the others, not only in
colour, and shaped to its footprint (a long footprint a long building, a square one a compact one). A building
gets a real design: how many storeys (one to three), its footprint shape (square, long, L-shaped, round), its roof (form
and material: thatched, slate, shingle, tiled; gabled, hipped, steep, with dormers), its walls (timber frame, stone,
plaster, logs), and features that fit what it is for (a porch, a balcony, an outside stair, chimneys, a lean-to, a shop
front with an awning, a sign, dormers, a bay window); never a plain box. Nothing on the ground around it (no yard,
fence, paving, cart or plinth): only the thing itself. A small thing gets its build,
silhouette, material and parts. Keep every one the same kind of thing, the same era, culture and style as the place, and
the size such a thing has on its footprint. Each one line, concrete and visual, no names, no story, no background.
Answer JSON: {{"looks": ["...", "..."]}} (one per item, in order)."""


def look_words(writer, kind: str, purpose: str, place: str, footprints: list[tuple[int, int]]) -> list[str]:
    """One description per item of a family (LOOKS_TASK: every house of the place, every workshop), each its footprint,
    all different in form from each other."""
    import json as _json
    if writer is None or not footprints:
        return []
    items = "\n".join(f"{i + 1}. footprint {w} by {h} cells" for i, (w, h) in enumerate(footprints))
    try:
        answer = _json.loads(writer.complete([{"role": "system", "content": LOOKS_TASK},
                                              {"role": "user", "content": f"The place: {place}\nThe kind: {kind}" + (f" ({purpose})" if purpose else "") +
                                               " (a cell is about a person's stride; one storey about two cells tall)\nThe items:\n" + items}]).text)
    except Exception:  # noqa: BLE001 - the plain description is used instead
        return []
    return [str(v).strip() for v in answer.get("looks", []) if str(v).strip()][:len(footprints)]


def paint_from_words(painter, folder: Path, pieces: list[dict], every: list[dict], context: dict, seed: int, writer=None,
                     track: Steps | None = None) -> dict:
    """Every piece painted alone from its own words (builder_tests/variant_test): one LLM call per kind writes the main
    look and its other looks, each different in form (LOOKS_TASK); each piece painted in the pieces' framing with the
    place's style line, no sheet, no reference image; cut from its picture (find_items reads the real background). A
    piece the cut can't find, or finds as more than one thing, is painted once more; then left for a placeholder. The
    vision model checks the lot once; what it finds wrong is painted once more with its words and why. At most one
    repaint each: no fix rounds. Returns {made, step}."""
    import json as _json
    from concurrent.futures import ThreadPoolExecutor
    track = track or Steps()
    place = ", ".join(w for w in (_place_words(context), _style_line(context)) if w)
    style = f"Style: {_style_words(context)}; {_style_line(context)}."
    # A family: every piece of one role and concepts, whatever its footprint (each house kind of the place, each look):
    # described together, so no two houses come out alike.
    kinds: dict[str, list[dict]] = {}
    for p in pieces:
        kinds.setdefault(f"{p.get('role', '')}|{' '.join(str(c) for c in p.get('concepts', []) if c)}", []).append(p)
    for looks in kinds.values():
        looks.sort(key=lambda q: (q.get("variantKey") or q["key"], int(q.get("variant", 1))))
    # 1. The words: one call a family, its items in order (at most 24 a call).
    track.start("describe")
    words: dict[str, str] = {}

    def describe(family: str) -> None:
        looks = kinds[family]
        first = looks[0]
        kind = str(first.get("role") or "object").replace("-", " ")
        concepts = " ".join(str(c) for c in first.get("concepts", []) if c)
        what = f"{concepts} {kind}".strip() if concepts and concepts != kind else kind
        for start in range(0, len(looks), 24):
            part = looks[start:start + 24]
            said = look_words(writer, what, str(first.get("purpose") or ""), place, [(int(q.get("w", 1)), int(q.get("h", 1))) for q in part])
            for i, p in enumerate(part):
                words[p["key"]] = said[i] if i < len(said) else f"{what}" + (f", {first['purpose']}" if first.get("purpose") else "")
    with ThreadPoolExecutor(4) as pool:
        list(pool.map(describe, list(kinds)))
    track.done("describe", label=f"Described {len(pieces)} pieces in {len(kinds)} families")
    try:
        (folder / "piece-prompts.json").write_text(_json.dumps(words, indent=2), encoding="utf-8")
    except OSError:
        pass

    # 2. The pictures: four at a time, each cut from its own.
    def prompt_of(p: dict, why: str = "") -> str:
        foot = f"it stands on a {p.get('w', 1)} by {p.get('h', 1)} cell footprint"
        if p.get("layer") == "building":
            foot += " (one storey is about two cells tall): the whole building, its roof and all its walls the camera sees"
        return (f"Game asset. Isometric view. Flat magenta background (#FF00FF).\nOne {words[p['key']]}.\n{style} Seen from the "
                f"isometric game camera (2:1), its front facing the lower left; {foot}.\n" + (f"Not like the last try: {why}\n" if why else "") +
                "Only this one object, alone, centred, nothing under it: no ground, no shadow, no people, no text. Keep the magenta empty.")

    def paint(p: dict, attempt: int = 0, why: str = "") -> bool:
        try:
            png = painter.create(prompt_of(p, why), 1024, 1024, seed=seed + 31 * int(p.get("variant", 1)) + 7 * attempt + len(p["key"]))
        except Exception:  # noqa: BLE001 - another try, then a placeholder
            return False
        found = find_items(png)
        if not found:
            return False
        biggest = max(found, key=lambda i: i["area"])
        # Two big things where one was asked (a house and its shed apart): not this one.
        if len([i for i in found if i["area"] > biggest["area"] * 0.35]) > 1:
            return False
        biggest["image"].save(folder / "pieces" / f"{p['slug']}.png")
        return True

    track.start("paint", f"Painting the pieces: 0 of {len(pieces)}")
    made: set[str] = set()
    tries: dict[str, int] = {}
    finished = [0]
    count_lock = __import__("threading").Lock()

    def paint_counted(p: dict) -> bool:
        ok = paint(p)
        with count_lock:
            finished[0] += 1
            n = finished[0]
        track.tick("paint", f"Painting the pieces: {n} of {len(pieces)}")
        return ok
    with ThreadPoolExecutor(4) as pool:
        first = list(pool.map(paint_counted, pieces))
    again = [p for p, ok in zip(pieces, first) if not ok]
    made |= {p["key"] for p, ok in zip(pieces, first) if ok}
    if again:
        with ThreadPoolExecutor(4) as pool:
            second = list(pool.map(lambda p: paint(p, 1), again))
        made |= {p["key"] for p, ok in zip(again, second) if ok}
        tries.update({p["key"]: 2 for p in again})
    track.done("paint", label=f"Painted {len(made)} of {len(pieces)} pieces" + (f" ({len(again)} painted twice)" if again else ""))
    # 3. One look by the vision model; what's wrong painted once more (if it wasn't already), told why.
    redo: dict[str, str] = {}
    have = [p for p in pieces if p["key"] in made]
    for start in range(0, len(have), 16):
        chunk = have[start:start + 16]
        for i, why in (check_pieces(writer, folder, chunk) or {}).items():
            if tries.get(chunk[i]["key"], 1) < 2:
                redo[chunk[i]["key"]] = why
    fixed = 0
    if redo:
        by_key = {p["key"]: p for p in pieces}
        with ThreadPoolExecutor(4) as pool:
            fixed = sum(pool.map(lambda k: paint(by_key[k], 2, redo[k]), list(redo)))
    return {"made": made, "step": {"painted": sorted(made), "missing": sorted(p["key"] for p in pieces if p["key"] not in made),
                                   "repainted": sorted(redo), "fixed": fixed, "words": words}}


STOREYS_TASK = """How many storeys (floors one walks on, the ground floor counted) does this building have? An attic under
the roof counts only if it has windows or dormers of its own. Answer as JSON: {"storeys": <1-4>, "why": "<a few words>"}."""

_ORDINAL = {1: "ground", 2: "second", 3: "third", 4: "fourth"}


def floor_prompt(k: int, n: int, purpose: str, style: str) -> str:
    """Floor `k` of `n` (docs/places/interiors.md §2, builder_tests/interior_test): the building cut open above it, its
    camera-facing walls gone on that floor, the floors below closed under it."""
    above = "the roof and everything else above it" if k == n else f"every storey above it ({n - k} more) and the roof"
    lines = [
        "Game asset. Isometric view. Flat magenta background (#FF00FF).",
        f"Image 1 is a building with {n} storey{'s' if n > 1 else ''}. Paint the same building, from the same camera, at the same "
        f"place and size in the picture, cut open to show its {_ORDINAL[k]} floor from above, as a game shows a room the player walks in:",
        f"- {above} is gone, cut away level with the top of this floor's walls: no roof, no gables, no beams across;",
        "- this floor's two walls that face the camera (front-left and front-right) are gone ENTIRELY, with their windows, doors, "
        "shutters and timber frames, down to the level of the floor: the floor's front edges are open, nothing stands on them;",
        "- this floor's two back walls stay, one storey high, with their windows and the hearth, shelves and beds against them;",
    ]
    if k > 1:
        lines.append(f"- the {k - 1} storey{'s' if k > 2 else ''} below stay{'' if k > 2 else 's'} closed, as seen from outside in "
                     "image 1: the solid base this floor stands on;")
    stairs = "" if n == 1 else ", a staircase going up in a back corner" if k < n else ", the top of the staircase in a back corner"
    lines += [
        f"- the floor fully visible: boards or flagstones, its rooms with low inner partitions only, the furniture and things "
        f"{purpose}{stairs}, lit warmly;",
        f"Every wall keeps exactly the building's footprint, materials and colours. {style}",
        "Nothing stands between the camera and the floor. Only this building: no ground round it, no shadow, no people, no text. "
        "Keep the magenta empty.",
    ]
    return "\n".join(lines)


def count_storeys(vision, exterior: Image.Image) -> int:
    """How many floors the building in the picture has (a vision model's answer, 1 to 4); 1 without one."""
    if vision is None:
        return 1
    import base64
    url = "data:image/png;base64," + base64.b64encode(_png(exterior.convert("RGB"))).decode()
    try:
        answer = vision.complete([{"role": "user", "content": [{"type": "text", "text": STOREYS_TASK}, {"type": "image_url", "image_url": url}]}])
        text = answer.text if hasattr(answer, "text") else str(answer)
        return max(1, min(4, int(json.loads(text[text.index("{"): text.rindex("}") + 1]).get("storeys", 1))))
    except Exception:  # noqa: BLE001 - unread: one floor
        return 1


def footing(picture: Image.Image) -> tuple[int, int, int, int, float]:
    """Where a building stands (docs/places/interiors.md §2): (its left and right wall corners' x, its front corner's x
    and y, the slope its base rises at from the front corner). The base is the lowest opaque pixel of each column: a V
    falling to the front corner; the walls end where it rises well above that line and stays (eaves, the gap past them);
    what hangs below it (a bush, a step) is passed. The slope is 1/2 in true isometric; a model may paint from higher up.
    Only solid pixels count (a building's ground shadow is half see-through); a stretch above the line as wide as a
    thirtieth of the picture (between stilts, under a deck) is passed too."""
    import numpy as np
    a = np.array(picture.convert("RGBA"))[:, :, 3] >= 230
    w = a.shape[1]
    low = np.full(w, -1)
    for x in range(w):
        ys = np.nonzero(a[:, x])[0]
        if ys.size:
            low[x] = ys.max()
    if low.max() < 0:
        return 0, w - 1, w // 2, picture.height - 1, 0.5
    front = int(np.argmax(low))
    fy = int(low[front])
    rises = [(fy - low[x]) / abs(x - front) for x in range(max(0, front - w // 4), min(w, front + w // 4))
             if abs(x - front) >= max(1, w // 20) and low[x] >= 0]
    slope = float(np.median(rises)) if rises else 0.5

    def walk(step: int) -> int:
        x, best, above = front, front, 0
        while 0 <= x + step < w:
            x += step
            if low[x] < 0 or low[x] < fy - abs(x - front) * slope - 18:
                above += 1
                if above >= max(6, w // 30):
                    break
                continue
            above, best = 0, x
        return best
    return walk(-1), walk(1), front, fy, slope


def fit_floor(inside: Image.Image, exterior: Image.Image) -> tuple[Image.Image, float, float]:
    """`inside` laid on the building's footprint: its wall corners on the exterior's, its front corner on its front
    corner, brought to its angle when painted from higher up (a vertical scale). Only what's outside the footprint's
    columns or below its base goes (the outline isn't a cut: back walls and stairs stand above the roofline's dip).
    Returns it, the width scale and the angle's (1: as painted)."""
    import numpy as np
    eL, eR, eF, eY, eS = footing(exterior)
    iL, iR, iF, iY, iS = footing(inside)
    sx = (eR - eL) / max(1, iR - iL)
    angle = eS / max(0.05, iS)
    sy = sx * angle
    big = inside.resize((max(1, round(inside.width * sx)), max(1, round(inside.height * sy))), Image.LANCZOS)
    out = Image.new("RGBA", exterior.size, (0, 0, 0, 0))
    out.paste(big, (round(eF - iF * sx), round(eY - iY * sy)), big)
    px = np.array(out)
    for x in range(px.shape[1]):
        if x < eL or x > eR:
            px[:, x, 3] = 0
        else:
            px[int(min(px.shape[0], eY - abs(eF - x) * eS + 3)):, x, 3] = 0
    return Image.fromarray(px), sx, angle


def cutaway(painter, exterior: Image.Image, purpose: str, style: str, seed: int, prompt: str | None = None, clip: bool = True) -> Image.Image | None:
    """`exterior` (RGBA, transparent round the building) edited on a magenta canvas where it stands as it is, then cut
    back to exactly its size, its background cleared (and, `clip`, all outside the building's outline). None when the
    edit fails."""
    side = 1024
    k = min(1.0, side * 0.78 / max(exterior.width, exterior.height))
    shown = exterior.resize((max(1, round(exterior.width * k)), max(1, round(exterior.height * k))), Image.LANCZOS) if k < 1 else exterior
    ox, oy = (side - shown.width) // 2, (side - shown.height) // 2
    canvas = Image.new("RGB", (side, side), (255, 0, 255))
    canvas.paste(shown, (ox, oy), shown)
    try:
        png = painter.repaint(_png(canvas), prompt or floor_prompt(1, 1, purpose, style), references=[], seed=seed, width=side, height=side)
    except Exception:  # noqa: BLE001 - the building stays closed this run; asked again next run
        return None
    painted = Image.open(io.BytesIO(png)).convert("RGB")
    if painted.size != (side, side):
        painted = painted.resize((side, side), Image.LANCZOS)
    part = painted.crop((ox, oy, ox + shown.width, oy + shown.height))
    if part.size != exterior.size:
        part = part.resize(exterior.size, Image.LANCZOS)
    back = _background(part)
    inside = part.convert("RGBA")
    px, ex = inside.load(), exterior.load()
    for y in range(inside.height):
        for x in range(inside.width):
            if back[y][x] or (clip and ex[x, y][3] < 128):
                px[x, y] = (0, 0, 0, 0)
    return inside


def make_floors(painter, exterior: Image.Image, purpose: str, style: str, seed: int, storeys: int) -> list[Image.Image | None]:
    """A building's interiors, one a floor (docs/places/interiors.md §2): floor k is the building cut open above it, its
    front walls gone, the floors below closed, each laid on its footprint (`fit_floor`). A floor painted far off (its
    angle or its size: a model paints a ground floor from higher up, or smaller) is painted once more, then fitted."""
    from concurrent.futures import ThreadPoolExecutor
    import numpy as np
    # Its footing read wrong (narrower than half its solid width: stilts, a deck, a yard round it): every floor as painted.
    eL, eR = footing(exterior)[:2]
    solid = np.nonzero((np.array(exterior.convert("RGBA"))[:, :, 3] >= 230).any(axis=0))[0]
    trusted = solid.size > 0 and (eR - eL) >= 0.5 * (solid.max() - solid.min())

    def one(k: int) -> Image.Image | None:
        best = None
        for attempt in range(2):
            raw = cutaway(painter, exterior, purpose, style, seed + 977 + 31 * k + 101 * attempt,
                          prompt=floor_prompt(k, storeys, purpose, style), clip=False)
            if raw is None:
                continue
            fitted, sx, angle = fit_floor(raw, exterior)
            far = abs(sx - 1) + abs(angle - 1)
            if best is None or far < best[1]:
                best = (fitted, far, raw)
            if 0.75 <= sx <= 1.35 and angle >= 0.65:
                break
        if best is None:
            return None
        if trusted and best[1] <= 0.7:
            return best[0]
        # Its footing misread (a fit that far off is wrong, not the picture): as painted, in the exterior's frame, nothing
        # past its outline.
        raw, ex = best[2], exterior.convert("RGBA")
        px, ep = raw.load(), ex.load()
        for y in range(raw.height):
            for x in range(raw.width):
                if ep[x, y][3] < 128:
                    px[x, y] = (0, 0, 0, 0)
        return raw
    with ThreadPoolExecutor(min(4, storeys)) as pool:
        return list(pool.map(one, range(1, storeys + 1)))


def floor_name(stem: str, k: int) -> str:
    """Floor k's picture beside a building's: <stem>-interior.png (the ground floor), <stem>-interior-<k>.png above."""
    return f"{stem}-interior.png" if k == 1 else f"{stem}-interior-{k}.png"


def _purpose(p: dict) -> str:
    return f"of {p['purpose']}" if p.get("purpose") else f"a {str(p.get('role') or 'building').replace('-', ' ')} has"


def make_interiors(painter, folder: Path, pieces: list[dict], context: dict, seed: int, vision=None) -> list[str]:
    """Each building's interiors (Assembled): its storeys counted (`vision`), one floor each (`make_floors`) from its
    exterior picture, written as pieces/<slug>-interior.png, -interior-2.png…, exactly its size, so the cutter
    (CutMatchedArt) frames them as it does the exterior. Always BFL (an edit needs the picture). Returns the slugs made."""
    from concurrent.futures import ThreadPoolExecutor
    style = f"Style: {_style_words(context)}; {_style_line(context)}."

    def make(p: dict) -> str | None:
        exterior_path = folder / "pieces" / f"{p['slug']}.png"
        if not exterior_path.exists():
            return None
        with Image.open(exterior_path) as im:
            exterior = im.convert("RGBA")
        floors = make_floors(painter, exterior, _purpose(p), style, seed + len(p["key"]), count_storeys(vision, exterior))
        if not floors or floors[0] is None:
            return None
        for k, floor in enumerate(floors, 1):
            if floor is None:
                break  # the floors above a missing one aren't reached
            floor.save(folder / "pieces" / floor_name(p["slug"], k))
        return p["slug"]
    with ThreadPoolExecutor(2) as pool:
        return [s for s in pool.map(make, pieces) if s]


def paint_interiors(painter, folder: Path, items: list[dict], context: dict, seed: int, painting: str = "painting.png", vision=None) -> list[str]:
    """Each painted building's interiors (Paint, docs/places/interiors.md §7): its occluder's region of the painting
    (`box` [x, y, w, h], `mask` an alpha PNG that size, both from marks.json), one floor each (`make_floors`), written as
    marks/<id>-interior.png, -interior-2.png…, the box's size, so they lie exactly over the painted building. Returns
    the ids made."""
    from concurrent.futures import ThreadPoolExecutor
    style = f"Style: {_style_words(context)}; {_style_line(context)}."
    with Image.open(folder / painting) as im:
        whole = im.convert("RGBA")

    def make(item: dict) -> str | None:
        x, y, w, h = (int(v) for v in item["box"][:4])
        mask_path = folder / item.get("mask", f"marks/{item['id']}.png")
        if w <= 0 or h <= 0 or not mask_path.exists():
            return None
        with Image.open(mask_path) as m:
            mask = m.convert("RGBA").split()[3]
        if mask.size != (w, h):
            mask = mask.resize((w, h), Image.NEAREST)
        exterior = whole.crop((x, y, x + w, y + h))
        exterior.putalpha(mask)
        floors = make_floors(painter, exterior, _purpose(item), style, seed + len(item["id"]), count_storeys(vision, exterior))
        if not floors or floors[0] is None:
            return None
        (folder / "marks").mkdir(exist_ok=True)
        for k, floor in enumerate(floors, 1):
            if floor is None:
                break
            floor.save(folder / "marks" / floor_name(item["id"], k))
        return item["id"]
    with ThreadPoolExecutor(2) as pool:
        return [i for i in pool.map(make, items) if i]


def derive_pieces(painter, folder: Path, pieces: list[dict], every: list[dict], context: dict, seed: int, writer=None) -> list[str]:
    """Other looks and back views made from an accepted piece. Another look (variant 2, 3…) is painted from its own words
    alone: the LLM (`writer`) writes, for each kind, a description per look that differs in form (storeys, footprint,
    roof, features: VARIANT_TASK), and each is painted with the place's style line and no reference image
    (builder_tests/variant_test: an edit of the first look only recoloured it, a reference kept its shape). Without an
    answer, a look is painted on an empty canvas with the first look as image 2. A back view is an edit of that look's
    front, "the same thing, seen from behind". It starts from the right thing, so it stays that thing, in the same style and size. Fronts
    first, then their backs. Returns the slugs made."""
    from concurrent.futures import ThreadPoolExecutor
    by_key = {p["key"]: p for p in every}

    def source(p: dict) -> dict | None:
        base = p.get("variantKey") or p["key"]
        if p.get("view") == "back":
            front = p["key"][: -len("#back")] if p["key"].endswith("#back") else base
            return by_key.get(front)
        return by_key.get(base)

    # Each kind's other looks in words, one call a kind (their descriptions kept with the art).
    words: dict[str, str] = {}
    looks: dict[str, list[dict]] = {}
    for p in pieces:
        if p.get("view") != "back":
            looks.setdefault(p.get("variantKey") or p["key"], []).append(p)
    place = ", ".join(w for w in (_place_words(context), _style_line(context)) if w)

    def describe(base: str) -> None:
        first = by_key.get(base) or {}
        kind = str(first.get("role") or "object").replace("-", " ")
        concepts = " ".join(str(c) for c in first.get("concepts", []) if c)
        what = f"{concepts} {kind}".strip() if concepts and concepts != kind else kind
        said = f"a {what}" + (f": {first['purpose']}" if first.get("purpose") else "") + f" ({first.get('w', 1)}x{first.get('h', 1)} cells)"
        for p, text in zip(looks[base], variant_words(writer, what, said, place, len(looks[base]))):
            words[p["key"]] = text
    if writer is not None and looks:
        from concurrent.futures import ThreadPoolExecutor as _Pool
        with _Pool(4) as pool:
            list(pool.map(describe, list(looks)))
        try:
            (folder / "variant-prompts.json").write_text(__import__("json").dumps(words, indent=2), encoding="utf-8")
        except OSError:
            pass

    def make(p: dict) -> str | None:
        src = source(p)
        path = folder / "pieces" / f"{src['slug']}.png" if src else None
        if not path or not path.exists():
            return None
        with Image.open(path) as im:
            thing = im.convert("RGBA")
        side = 1024
        thing.thumbnail((int(side * 0.62), int(side * 0.62)))
        canvas = Image.new("RGB", (side, side), (255, 0, 255))
        canvas.paste(thing, ((side - thing.width) // 2, (side - thing.height) // 2), thing)
        what = str(p.get("role") or "object").replace("-", " ")
        seed_of = seed + int(p.get("variant", 1)) * 13 + (7 if p.get("view") == "back" else 0)
        if p.get("view") == "back":
            # A back view: an edit of the front, the same model turned around.
            change = (f"The same {what}, seen from behind: turn it around so its back faces the lower left. Same model, colours, "
                      "size, angle and style.")
            prompt = (f"Game asset. Isometric view. Flat magenta background (#FF00FF).\n{change}\n"
                      "Only this one object, alone, nothing under it: no ground, no shadow, no people, no text. Keep the magenta empty.")
            made = painter.repaint(_png(canvas), prompt, references=[], seed=seed_of, width=side, height=side)
        elif p["key"] in words and hasattr(painter, "create"):
            # Another look from its own words: no reference image (which would keep the first look's shape).
            prompt = (f"Game asset. Isometric view. Flat magenta background (#FF00FF).\n"
                      f"One {words[p['key']]}.\n"
                      f"Style: {_style_words(context)}; {_style_line(context)}. Seen from the isometric game camera (2:1), its front "
                      f"facing the lower left; it stands on a {p.get('w', 1)} by {p.get('h', 1)} cell footprint.\n"
                      "Only this one object, alone, centred, nothing under it: no ground, no shadow, no people, no text. Keep the magenta empty.")
            made = painter.create(prompt, side, side, seed=seed_of)
        else:
            # Without its words: a new design on an empty canvas, the first look beside it as the reference for the style,
            # scale and angle alone.
            kind = " ".join(str(c) for c in p.get("concepts", []) if c)
            kind = f"{kind} " if kind and kind != what else ""
            purpose = f" ({p['purpose']})" if p.get("purpose") else ""
            if p.get("layer") == "building":
                differ = (f"a different building from the one in image 2: its own footprint shape, roof form, storeys, facade, "
                          f"doors, windows, chimneys and porch. It must not look like image 2 recoloured: two different {what}s of "
                          "one town.")
            else:
                differ = (f"a different {what} from the one in image 2: another shape, silhouette, build and details, not only "
                          "other colours.")
            prompt = (f"Game asset. Isometric view. Flat magenta background (#FF00FF).\n"
                      f"Image 1 is empty: paint in its middle one {kind}{what}{purpose}, {differ}\n"
                      "Same art style, palette family, outline, light direction, camera angle and size as image 2 (as big in the "
                      "picture as image 2's).\n"
                      "Only this one object, alone, nothing under it: no ground, no shadow, no people, no text. Keep the magenta empty.")
            empty = Image.new("RGB", (side, side), (255, 0, 255))
            made = painter.repaint(_png(empty), prompt, references=[_png(canvas)], seed=seed_of, width=side, height=side)
        found = find_items(made)
        if not found:
            return None
        max(found, key=lambda i: i["area"])["image"].save(folder / "pieces" / f"{p['slug']}.png")
        return p["slug"]
    out = []
    fronts = [p for p in pieces if p.get("view") != "back"]
    backs = [p for p in pieces if p.get("view") == "back"]
    for batch in (fronts, backs):
        with ThreadPoolExecutor(4) as pool:
            out += [s for s in pool.map(make, batch) if s]
    return out


def face_pieces(vision, folder: Path, pieces: list[dict]) -> list[str]:
    """Pieces with a front (`view` front or back) made to face the way the scene expects (docs/places/matched-art.md
    §2b): a front view's front to the lower left, a back view's front to the upper right. The vision model says which
    way each one's front points; a piece on the wrong side is mirrored, a front and back painted the other way round
    swapped. Returns the slugs changed."""
    have = [p for p in pieces if (folder / "pieces" / f"{p['slug']}.png").exists()]
    if vision is None or not have:
        return []
    import base64
    ways: dict[str, str] = {}
    for start in range(0, len(have), 16):
        chunk = have[start:start + 16]
        images = []
        for p in chunk:
            with Image.open(folder / "pieces" / f"{p['slug']}.png") as im:
                images.append(im.convert("RGBA"))
        board = _board(images, numbered=True, background=(128, 128, 128))
        kinds = "; ".join(f"{i + 1}) {str(p.get('role')).replace('-', ' ')}" for i, p in enumerate(chunk))
        message = [{"role": "user", "content": [
            {"type": "text", "text": "Each numbered picture shows a game object at an angle. For each, say which way its FRONT points "
                                      "(a car's headlights, a chair's seat, a chest's lock, a statue's face): lower-left, lower-right, "
                                      "upper-left or upper-right. Objects: " + kinds +
                                      '. Answer as JSON: {"front": {"1": "lower-left", "2": "upper-right"}}.'},
            {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(_png(board)).decode('ascii')}"}]}]
        try:
            answer = json.loads(vision.complete(message).text).get("front", {})
        except Exception:  # noqa: BLE001 - unchecked, left as painted
            continue
        for i, p in enumerate(chunk):
            way = str(answer.get(str(i + 1), "")).lower()
            if way in ("lower-left", "lower-right", "upper-left", "upper-right"):
                ways[p["slug"]] = way
    changed = []
    by_slug = {p["slug"]: p for p in have}
    # A front painted from behind and its back painted from the front: swapped.
    for p in have:
        back = p["slug"] + "-back"
        if p.get("view") != "front" or back not in by_slug:
            continue
        if ways.get(p["slug"], "").startswith("upper") and ways.get(back, "").startswith("lower"):
            a, b = folder / "pieces" / f"{p['slug']}.png", folder / "pieces" / f"{back}.png"
            tmp = a.with_suffix(".swap")
            a.rename(tmp)
            b.rename(a)
            tmp.rename(b)
            ways[p["slug"]], ways[back] = ways[back], ways[p["slug"]]
            changed += [p["slug"], back]
    for slug, way in ways.items():
        want = "left" if by_slug[slug].get("view") == "front" else "right"
        if not way.endswith(want):
            path = folder / "pieces" / f"{slug}.png"
            with Image.open(path) as im:
                flipped = im.transpose(Image.FLIP_LEFT_RIGHT)
            flipped.save(path)
            changed.append(slug)
    return changed


def _references(folder: Path, piece: dict) -> list[bytes]:
    """A piece made alone is styled by one or two finished pieces of its own kind of thing (a building by
    buildings, a small thing by small things), not by the whole board (many buildings invite a whole scene)."""
    names = (folder / "style-refs.json")
    refs = json.loads(names.read_text(encoding="utf-8")) if names.exists() else {}
    paths = refs.get("building" if piece.get("layer") == "building" else "prop", [])[:2]
    images = []
    for path in paths:
        if (folder / "pieces" / path).exists():
            with Image.open(folder / "pieces" / path) as im:
                images.append(im.convert("RGBA"))
    return [_png(_board(images, cell=512))] if images else []


def make_alone(painter, folder: Path, sheet: dict, piece: dict, context: dict, seed: int, why: str = "") -> bool:
    """One piece by itself: its box as image 1, one or two pieces of its kind as image 2; a single object."""
    what = _thing_words(piece, context)
    alone = (f"{OUTLINE} {_place_words(context)} Exactly ONE game asset, isolated like a product shot: {what}. Image 1 shows its placeholder box: paint it as that "
             f"single thing at the box's size and angle (isometric 2:1), {_style_words(context)}, on {KEY_WORDS}, {lot_words(piece)}. "
             "Nothing else: no scene, no streets or paths, no other buildings, no vehicles, animals or people around it; "
             "no magenta or pink in the asset itself.")
    refs = _references(folder, piece)
    if refs:
        alone += " Image 2 shows other assets of this game: match their art style exactly; don't copy them."
    made = painter.repaint(_piece_input(folder / sheet["png"], piece), alone, references=refs, seed=seed + len(why), width=1024, height=1024)
    found = find_items(made)
    if not found:
        return False
    max(found, key=lambda i: i["area"])["image"].save(folder / "pieces" / f"{piece['slug']}.png")
    return True


def _fits_its_box(folder: Path, piece: dict) -> bool:
    """Whether a piece is about the shape of its box: much wider or narrower for its height means two things found as
    one, or half of one."""
    path = folder / "pieces" / f"{piece['slug']}.png"
    if not path.exists():
        return False
    with Image.open(path) as im:
        box = im.convert("RGBA").getchannel("A").getbbox()
    if not box:
        return False
    x0, y0, x1, y1 = piece["box"]
    want = (x1 - x0) / max(1, y1 - y0)
    have = (box[2] - box[0]) / max(1, box[3] - box[1])
    return 0.55 < have / max(want, 1e-6) < 1.8


def _foot_fits(folder: Path, piece: dict) -> bool:
    """Whether a building's painted foot has its box's proportions: its front corner (its lowest point) about w / (w + h)
    of the way across, as the plan's footprint has it. A square building painted for a 4 x 7 lot fails."""
    path = folder / "pieces" / f"{piece['slug']}.png"
    if not path.exists():
        return False
    with Image.open(path) as im:
        alpha = im.convert("RGBA").getchannel("A")
    box = alpha.getbbox()
    if not box:
        return False
    a = alpha.crop(box)
    w, h = a.size
    px = a.load()
    foot = []
    for x in range(w):
        y = h - 1
        while y >= 0 and px[x, y] <= 128:
            y -= 1
        foot.append(y)
    inner = range(w // 20, w - w // 20)
    front = max(inner, key=lambda x: foot[x]) if len(inner) else w // 2
    want = int(piece.get("w", 1)) / max(1, int(piece.get("w", 1)) + int(piece.get("h", 1)))
    return abs(front / max(1, w) - want) <= 0.1


def check_pieces(vision, folder: Path, pieces: list[dict]) -> dict[int, str] | None:
    """The pieces that aren't one isolated thing of their kind (index -> what's wrong), by the vision model; None when
    the check couldn't be made (they're unchecked, not right)."""
    have = [(n, p) for n, p in enumerate(pieces) if (folder / "pieces" / f"{p['slug']}.png").exists()]
    if vision is None or not have:
        return {}
    import base64
    images = []
    for _, p in have:
        with Image.open(folder / "pieces" / f"{p['slug']}.png") as im:
            images.append(im.convert("RGBA"))
    board = _board(images, numbered=True, background=(128, 128, 128))
    kinds = "; ".join(f"{i + 1}) {p.get('role')}" for i, (_, p) in enumerate(have))
    message = [{"role": "user", "content": [
        {"type": "text", "text": "Each numbered picture should show exactly ONE game asset of the kind listed, alone (a building "
                                  "may stand on its own small lot). List the numbers that show something else: TWO OR MORE objects in one picture "
                                  "(two buildings side by side), a scene, a street or "
                                  "path, a group of buildings, other objects, vehicles or people beside it, "
                                  "a messy or broken object, something not cleanly separated (ground, a shadow or other things attached to it), or the wrong kind of thing. Kinds: " + kinds +
                                  '. Answer as JSON: {"wrong": [{"number": 2, "why": "a scene with other things around it"}]} ({"wrong": []} if all are right).'},
        {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(_png(board)).decode('ascii')}"}]}]
    answer = None
    for _ in range(2):  # a busy or odd answer: asked again
        try:
            answer = json.loads(vision.complete(message).text)
            break
        except Exception as error:  # noqa: BLE001 - asked again, then unchecked
            NAMING_ERRORS.append(f"check: {str(error)[:200]}")
    if not isinstance(answer, dict):
        return None  # not checked (not "all right")
    out = {}
    for w in answer.get("wrong", []):
        number = w.get("number")
        if isinstance(number, int) and 1 <= number <= len(have):
            out[have[number - 1][0]] = str(w.get("why", "not a single asset"))[:160]
    return out


def _strip(folder: Path, sheets: dict, chunk: list[dict], large: int = 0, centres: list | None = None) -> tuple[bytes, list[dict]]:
    """The chunk's boxes side by side on the key colour (each cut from its own sheet's block-out), at one scale; or,
    with `large`, each as big as that (a small thing drawn close up, for its detail: the grid fits its size after)."""
    crops = []
    for piece in chunk:
        block = Image.open(folder / piece["_sheet"]).convert("RGB")
        ids_path = folder / piece["_sheet"].replace(".png", "-ids.png")
        if ids_path.exists():
            # Its own pixels in the key colour (the block-out's palette has magenta too): grey, shaded as drawn.
            ids = Image.open(ids_path).convert("RGB").load()
            px = block.load()
            bx0, by0, bx1, by1 = piece["box"]
            for y in range(by0, by1):
                for x in range(bx0, bx1):
                    r, g, b = ids[x, y]
                    if r | (g << 8) | (b << 16) == piece["id"] and _is_key(*px[x, y]):
                        shade = (px[x, y][0] + px[x, y][2]) // 4 + 40
                        px[x, y] = (shade, shade, shade + 8)
        x0, y0, x1, y1 = piece["box"]
        m = int(0.15 * max(x1 - x0, y1 - y0)) + 8
        crop = block.crop((max(0, x0 - m), max(0, y0 - m), min(block.width, x1 + m), min(block.height, y1 + m)))
        if large:
            k = large / max(crop.width, crop.height)
            crop = crop.resize((max(1, round(crop.width * k)), max(1, round(crop.height * k))), Image.LANCZOS)
        crops.append(crop)
    # Far apart: a painting spreads a thing past its box, and touching things are found as one; small things, drawn
    # close up, farthest (a lamp beside a bench is painted as one).
    gap = max(260, max(c.width for c in crops)) if large else max(160, int(0.6 * max(c.width for c in crops)))
    width = sum(c.width for c in crops) + gap * (len(crops) + 1)
    height = max(c.height for c in crops) + 2 * gap
    strip = Image.new("RGB", (width, max(height, width // 4)), (255, 0, 255))
    x = gap
    for c in crops:
        strip.paste(c, (x, (strip.height - c.height) // 2))
        if centres is not None:
            centres.append((x + c.width / 2, strip.height / 2))
        x += c.width + gap
    return _png(strip), chunk


def _style_pieces(sheets: dict, buildings: int = 4, small: int = 6) -> list[dict]:
    """What shows a place's look: its most common kinds of building (one footprint each) and its most used small things."""
    seen, chosen_b, chosen_s = set(), [], []
    for sheet in sheets.get("sheets", []):
        for p in sheet.get("pieces", []):
            role = p.get("role")
            if role in seen:
                continue
            if p.get("layer") == "building" and len(chosen_b) < buildings:
                chosen_b.append(dict(p, _sheet=sheet["png"])); seen.add(role)
            elif p.get("layer") != "building" and len(chosen_s) < small:
                chosen_s.append(dict(p, _sheet=sheet["png"])); seen.add(role)
    return chosen_b + chosen_s


def _style_layout(folder: Path, pieces: list[dict], centres: list | None = None) -> bytes:
    """The style sheet's picture: buildings in the top row at one scale (true to each other), small things in the
    bottom row each drawn close up at one size, far apart, on the key colour."""
    buildings = [p for p in pieces if p.get("layer") == "building"]
    small = [p for p in pieces if p.get("layer") != "building"]
    top_at, bottom_at = [], []
    top = Image.open(io.BytesIO(_strip(folder, {}, buildings, centres=top_at)[0])) if buildings else None
    bottom = Image.open(io.BytesIO(_strip(folder, {}, small, large=420, centres=bottom_at)[0])) if small else None
    rows = [(r, at) for r, at in ((top, top_at), (bottom, bottom_at)) if r is not None]
    width = max(r.width for r, _ in rows)
    sheet = Image.new("RGB", (width, sum(r.height for r, _ in rows)), (255, 0, 255))
    y = 0
    where = []
    for r, at in rows:
        dx = (width - r.width) // 2
        sheet.paste(r, (dx, y))
        where += [(cx + dx, cy + y) for cx, cy in at]
        y += r.height
    if centres is not None:
        centres.extend(where)
    return _png(sheet)


def style_sheet(painter, model: str, folder: Path, sheets: dict, context: dict, vision, seed: int = 7, pieces: list[dict] | None = None,
                refs: list[bytes] | None = None, name: str = "style") -> dict:
    """A sheet in the generic prompt (docs/places/matched-art.md): up to 4 buildings in the top row, up to 6 small things
    in the bottom row; the first ("style") sets the place's look, every later one gets it as image 2 (`refs`). The
    items found, named and written as pieces/<slug>.png. One image, one naming."""
    from .bfl import _four_mp
    pieces = pieces or _style_pieces(sheets)
    centres: list = []
    layout = _style_layout(folder, pieces, centres)
    (folder / f"{name}-layout.png").write_bytes(layout)
    block = Image.open(io.BytesIO(layout))
    image = block.resize((4096, round(4096 * block.height / block.width)), Image.LANCZOS) if model.startswith("flux-2-klein") and block.width < 4096 else block
    buildings = [p for p in pieces if p.get("layer") == "building"]
    small = [p for p in pieces if p.get("layer") != "building"]
    grounds = sorted({str(p.get("ground") or "").replace("-", " ") for p in buildings} - {""})
    # Short, plain sentences: the model follows them better than long ones.
    # Things on this sheet that work together, said plainly: they must match.
    keys = {p["key"]: p for p in pieces}
    pairs = []
    for p in pieces:
        for k in p.get("with", []):
            if k in keys and (k, p["key"]) not in pairs:
                pairs.append((p["key"], k))
    together = ""
    if pairs:
        sets = {}
        for a, b in pairs:
            sets.setdefault(a, {a}).add(b)
        names = [", ".join(_thing_words(keys[k], context) for k in sorted(v)) for v in {frozenset(v) for v in sets.values()}]
        together = ("These objects go together in the game and must match: " + "; ".join(names) + ". Use the same materials, "
                    "colours and detail for them. A door or gate fits in its wall: the same height as the wall, in the same "
                    "stone or wood. Something that hangs on a wall is made for that wall.\n")
    # A kind's looks (variants) and views (front, back) said by their numbers: different looks, one thing seen two ways.
    number = {id(p): ("building", n + 1) for n, p in enumerate(buildings)}
    number.update({id(p): ("small object", n + 1) for n, p in enumerate(small)})

    def numbers(ps: list[dict]) -> str:
        ns = [str(number[id(q)][1]) for q in ps]
        return f"{number[id(ps[0])][0]}s {', '.join(ns[:-1])} and {ns[-1]}" if len(ns) > 1 else f"{number[id(ps[0])][0]} {ns[0]}"
    kinds: dict[str, list[dict]] = {}
    for p in buildings + small:
        kinds.setdefault(p.get("variantKey") or p["key"], []).append(p)
    looks = ""
    for of in kinds.values():
        fronts = [q for q in of if q.get("view") != "back"]
        if len(fronts) > 1:
            looks += f"{numbers(fronts).capitalize()} are different versions of {_thing_words(fronts[0], context)}: each with its own colours, shape and details.\n"
        for f in fronts:
            back = next((q for q in of if q.get("view") == "back" and q.get("variant") == f.get("variant")), None)
            if back:
                looks += (f"{numbers([f, back]).capitalize()} are the same {str(f.get('role', 'object')).replace('-', ' ')}: "
                          f"{number[id(f)][1]} seen from the front, {number[id(back)][1]} seen from the back.\n")
    if any(p.get("view") for p in pieces):
        looks += "Seen from the front, an object faces the lower left. Seen from the back, it shows its back to the lower left.\n"
    # Made with another piece (made-with: stairs with the walls they stand against): its picture shown, matched.
    slug_of = {q["key"]: q["slug"] for s in sheets.get("sheets", []) for q in s.get("pieces", [])}
    shown = {}
    for p in pieces:
        for key in dict.fromkeys(p.get("madeWith") or []):
            path = folder / "pieces" / f"{slug_of.get(key, '')}.png"
            if key in slug_of and path.exists():
                shown.setdefault(key, (path.read_bytes(), []))[1].append(p)
    styled = bool(refs)  # image 2: the style sheet
    refs = list(refs or [])
    for key, (png_bytes, with_it) in shown.items():
        refs.append(png_bytes)
        names = ", ".join(dict.fromkeys(str(p.get("role", "object")).replace("-", " ") for p in with_it))
        looks += (f"Image {len(refs) + 1} shows the {str(key).split('|')[-1].replace('-', ' ')} of this place: paint the {names} "
                  "in the same stone, colours and style, so they belong together.\n")
    prompt = ("Game asset sheet. Isometric view. Flat magenta background (#FF00FF).\n"
              f"{ISOLATED}\n"
              f"Style: {_style_line(context)}. All objects in the same style.\n"
              + ("Image 2 shows other assets of this game. Paint in exactly the same style.\n" if styled else "") + together + looks +
              "Image 1 shows boxes. Paint each box as the object listed below. Keep its place and size.\n"
              f"There are exactly {len(buildings)} buildings and {len(small)} small objects. Paint nothing else.\n"
              f"Top row, buildings: {'; '.join(f'{n + 1}. {_thing_words(p, context)}' for n, p in enumerate(buildings))}. "
              "Each building stands directly on the magenta: no base, no sidewalk, no ground around it.\n"
              f"Bottom row, small objects: {'; '.join(f'{n + 1}. {_thing_words(p, context)}' for n, p in enumerate(small))}. "
              "Draw each one alone. Nothing under it: no grass, no ground, no shadow.\n"
              "Every object is separate, with magenta all around it. Keep the magenta empty. "
              "No people. No fog or smoke. No text. Do not use magenta or pink in the objects.")
    png = painter.repaint(_png(image), prompt, references=refs or [], seed=seed, **_four_mp(block.width, block.height))
    (folder / f"{name}-sheet.png").write_bytes(png)
    items = find_items(png)
    named = name_items(vision, items, pieces)
    # What the vision model left: each by where its box was (the nearest item no one has, near enough).
    painted = Image.open(io.BytesIO(png))
    kx, ky = painted.width / block.width, painted.height / block.height
    taken = set(named.values())
    for p in range(len(pieces)):
        if p in named or p >= len(centres):
            continue
        ex, ey = centres[p][0] * kx, centres[p][1] * ky
        best, far = None, 0.2 * painted.width
        for i, item in enumerate(items):
            if i in taken:
                continue
            ix, iy = (item["box"][0] + item["box"][2]) / 2, (item["box"][1] + item["box"][3]) / 2
            d = ((ix - ex) ** 2 + (iy - ey) ** 2) ** 0.5
            if d < far:
                best, far = i, d
        if best is not None:
            named[p] = best
            taken.add(best)
    for p, i in named.items():
        items[i]["image"].save(folder / "pieces" / f"{pieces[p]['slug']}.png")
    _board([i["image"] for i in items], numbered=True, background=(128, 128, 128)).save(folder / f"{name}-items.png")
    return {"prompt": prompt, "pieces": pieces, "items": len(items), "named": {pieces[p]["key"]: i + 1 for p, i in named.items()},
            "png": png}


def remake(api_key: str, model: str, root: Path, rel: str, sheets: dict, context: dict, vision, keys: list[str],
           notes: dict | None = None, exemplars: dict | None = None, seed: int = 31) -> list[dict]:
    """The pieces `keys` made again, in batches: their boxes side by side on one new sheet (buildings and small
    things apart, up to 6 a sheet), image 2 the good pieces they should look like (`exemplars`: {"building":
    [slug], "prop": [slug]}, else the style references), what was wrong with each (`notes`) in the prompt; the
    items named and written over the old pieces. One image and one naming per batch."""
    from .bfl import BflPainter, _four_mp
    folder = root / rel
    painter = BflPainter(api_key, model)
    notes = notes or {}
    wanted = []
    for sheet in sheets.get("sheets", []):
        for piece in sheet.get("pieces", []):
            if piece["key"] in keys:
                wanted.append(dict(piece, _sheet=sheet["png"]))
    if exemplars:
        refs_file = folder / "style-refs.json"
        refs = json.loads(refs_file.read_text(encoding="utf-8")) if refs_file.exists() else {}
        for kind, slugs in exemplars.items():
            refs[kind] = [f"{slug}.png" for slug in slugs] + [r for r in refs.get(kind, []) if r[:-4] not in slugs]
        refs_file.write_text(json.dumps(refs), encoding="utf-8")
    out = []
    for kind in ("building", "prop"):
        group = [p for p in wanted if (p.get("layer") == "building") == (kind == "building")]
        size = 6 if kind == "building" else 4  # small things: few to a sheet, each with room
        for start in range(0, len(group), size):
            chunk = group[start:start + size]
            strip, chunk = _strip(folder, sheets, chunk, large=0 if kind == "building" else 640)
            block = Image.open(io.BytesIO(strip))
            image = block.resize((4096, round(4096 * block.height / block.width)), Image.LANCZOS) if block.width < 4096 else block
            rule = LOT if kind == "building" else "Each is ONE clean object, centred in its place, with no plinth, ground, road or scene under or around it."
            # What each should be, never what went wrong before (named, it would be painted).
            listing = "; ".join(f"{n + 1} from the left: {_thing_words(p, context)}" + (f", {lot_words(p)}" if kind == "building" else "")
                                for n, p in enumerate(chunk))
            prompt = (f"{OUTLINE} {_place_words(context)} A SPRITE SHEET of {len(chunk)} separate game assets, each isolated on {KEY_WORDS}. Image 1 shows a placeholder "
                      f"box for each, left to right: replace each box with the finished asset, at its place, size and isometric angle "
                      f"(2:1), {_style_words(context)}. The assets: {listing}. {rule} Nothing between them; the magenta stays flat "
                      "and empty; no magenta or pink in the assets.")
            if kind == "prop":
                prompt += (" Each is drawn CLOSE UP and filling its box: finely detailed, crisp dark outlines, cel shading with clear "
                           "materials, seen from the same 3/4 isometric angle, no ground, no shadow, no plinth under it.")
            refs = _references(folder, {"layer": kind})
            if refs:
                prompt += (" Image 2 shows assets of this game done right: make every one exactly as good: as cleanly separated, the same "
                           "style, finish, detail and how it stands; don't copy them.")
            png = painter.repaint(_png(image), prompt, references=refs, seed=seed + start, **_four_mp(block.width, block.height))
            (folder / f"remake-{kind}-{start // size + 1}.png").write_bytes(png)
            items = find_items(png)
            named = name_items(vision, items, chunk)
            for p, i in named.items():
                items[i]["image"].save(folder / "pieces" / f"{chunk[p]['slug']}.png")
            out.append({"sheet": f"remake-{kind}-{start // size + 1}", "pieces": [p["key"] for p in chunk], "items": len(items),
                        "named": [chunk[p]["key"] for p in named]})
    return out


def rename_from_sheets(root: Path, rel: str, sheets: dict, vision) -> list[dict]:
    """The sheets already painted, their items named again (no new image): every piece found on its sheet is taken
    from there, over one made alone."""
    folder = root / rel
    out = []
    for sheet in sheets.get("sheets", []):
        painted = folder / f"{sheet['name']}-sheet.png"
        if sheet.get("kind") == "ground" or not painted.exists():
            continue
        items = find_items(painted.read_bytes())
        named = name_items(vision, items, sheet.get("pieces", []))
        for p, i in named.items():
            items[i]["image"].save(folder / "pieces" / f"{sheet['pieces'][p]['slug']}.png")
        out.append({"sheet": sheet["name"], "items": len(items), "named": len(named)})
    return out


# --- PixelLab and Mistral: one piece at a time ------------------------------------------------------------

def _piece_prompt(piece: dict, context: dict, style: str, words_for_box: bool) -> str:
    text = f"One {piece.get('words') or piece.get('role')}, a single game asset, {_look(context)}, seen from an isometric angle (2:1)."
    if words_for_box and piece.get("layer") == "building":
        text += f" Its footprint is {piece.get('w')} by {piece.get('h')} cells; the whole building in view, nothing cut off."
    if style:
        text += f" Style: {style}"
    return text


def make_one_by_one(generator, provider: str, root: Path, rel: str, sheets: dict, context: dict, vision=None,
                    track: Steps | None = None) -> list[dict]:
    """Each piece made alone: the style sheet's first (they become the style), then the rest in their look (PixelLab:
    as its style images; Mistral: in words). Written as pieces/<slug>.png."""
    folder = root / rel
    (folder / "pieces").mkdir(parents=True, exist_ok=True)
    steps: list[dict] = []
    style_paths: list[str] = []
    style = ""
    ordered = sorted(sheets.get("sheets", []), key=lambda s: 0 if s.get("kind") == "style" else 1)
    track = track or Steps()
    for sheet in ordered:
        for piece in sheet.get("pieces", []):
            track.add(piece["slug"], ("Style piece: " if sheet.get("kind") == "style" else "") + _piece_name(piece), [piece])
        if sheet.get("grounds"):
            track.add(f"grounds-{len(track.items)}", "Ground textures")
    for sheet in ordered:
        if sheet.get("kind") != "style" and not style and vision is not None and style_paths:
            # The style sheet's pieces side by side, their look in words.
            board = Image.new("RGB", (256 * len(style_paths), 256), (128, 128, 128))
            for i, p in enumerate(style_paths):
                with Image.open(p) as im:
                    im = im.convert("RGBA")
                    im.thumbnail((240, 240))
                    board.paste(im, (256 * i + (256 - im.width) // 2, 256 - im.height), im)
            style = style_line(vision, _png(board))
            steps.append({"style": style})
        for piece in sheet.get("pieces", []):
            track.start(piece["slug"])
            prompt = _piece_prompt(piece, context, style, True)
            if provider == "pixellab":
                height = 256 if piece.get("layer") == "building" else 96
                options = {"kind": "object", "height": height, "isometric": True}
                if style_paths and sheet.get("kind") != "style":
                    options["references"] = style_paths[:4]
                image = generator.generate(prompt, options)
            else:
                image = generator.generate(prompt + f" Alone on {KEY_WORDS}, nothing else in the picture, no ground under it.")
            path = folder / "pieces" / f"{piece['slug']}.png"
            path.write_bytes(image.data)
            steps.append({"piece": piece["key"], "model": image.model, "prompt": image.prompt})
            track.done(piece["slug"])
            if sheet.get("kind") == "style":
                style_paths.append(str(path))
        grounds_step = next((i["id"] for i in track.items if i["id"].startswith("grounds-") and i["state"] == "waiting"), None)
        if sheet.get("grounds") and grounds_step:
            track.start(grounds_step)
        for ground in sheet.get("grounds", []):
            prompt = f"A seamless ground texture of {ground.get('words')}, seen from above, {_look(context)}, filling the whole square."
            if style:
                prompt += f" Style: {style}"
            image = generator.generate(prompt, {"kind": "tile", "height": 128}) if provider == "pixellab" else generator.generate(prompt)
            (folder / "pieces" / f"{ground['slug']}.png").write_bytes(image.data)
            steps.append({"ground": ground["terrain"], "model": image.model, "prompt": image.prompt})
        if sheet.get("grounds") and grounds_step:
            track.done(grounds_step)
    return steps


def make(art: str, root: Path, rel: str, sheets: dict, context: dict, generator=None, vision=None, keys: dict | None = None,
         report=None) -> dict:
    """The art for the sheets in `rel`, by `art`: "bfl:<model>", "pixellab" or "mistral" (generator: its image generator).
    `report(done, total, step, plan)`, if given, hears the run's steps (Steps) as they go."""
    keys = keys or {}
    track = Steps(report, root, rel)
    try:
        if art.startswith("bfl"):
            model = art.split(":", 1)[1] if ":" in art else "flux-2-klein-4b"
            if not keys.get("bfl"):
                return {"ok": False, "errors": ["No key for BFL: set BFL_API_KEY in ai_service/.env"]}
            steps = make_with_bfl(keys["bfl"], model, root, rel, sheets, context, vision=vision, track=track)
            return {"ok": True, "provider": f"BFL {model}", "steps": steps}
        if generator is None:
            return {"ok": False, "errors": [f"No image generator for {art}"]}
        steps = make_one_by_one(generator, "pixellab" if art == "pixellab" else "mistral", root, rel, sheets, context, vision, track)
        return {"ok": True, "provider": getattr(generator, "name", art), "steps": steps}
    except ProviderError as error:
        return {"ok": False, "errors": [str(error)]}


# --- The background: the place's ground painted from its plan ------------------------------------------------------


def _solid(terrain: str) -> bool:
    """Ground nothing stands on: the void round a building, solid rock or wall (black in the palette, left black)."""
    t = terrain.lower()
    return t in ("void", "", "nothing") or "wall" in t or t in ("rock", "solid-rock")


def _terrain_looks() -> dict:
    """Each terrain's tone and other words (data/places/vocabularies.json, terrain-kind)."""
    path = Path(__file__).resolve().parents[2] / "data" / "places" / "vocabularies.json"
    try:
        kinds = json.loads(path.read_text(encoding="utf-8")).get("terrain-kind", [])
    except (OSError, ValueError):
        return {}
    return {k["id"]: {"tone": (k.get("look") or {}).get("tone"), "alt": k.get("alt", [])} for k in kinds if isinstance(k, dict) and "id" in k}


def background_palette(terrain: dict, cells: tuple[int, int], cell_px: int = 32) -> tuple[Image.Image, list[dict], list[list[str]]]:
    """The plan's ground as a top-down colour map: each cell a square of its terrain's colour (its own tone, so the
    colour means what it is: asphalt dark grey, lawn green), two alike tones pushed apart so each area stays its own;
    solid rock and the void black. Returns the picture, its key ([{terrain, words, rgb}]) and the grid of names."""
    w, h = cells
    names = terrain.get("palette", [])
    grid = [["void"] * w for _ in range(h)]
    for y, row in enumerate(terrain.get("rows", [])[:h]):
        x = 0
        for n, i in row:
            for _ in range(n):
                if x < w:
                    grid[y][x] = names[i] if 0 <= i < len(names) else "void"
                x += 1
    looks = _terrain_looks()
    counts: dict[str, int] = {}
    for row in grid:
        for t in row:
            if not _solid(t):
                counts[t] = counts.get(t, 0) + 1
    key, chosen = [], []
    for t in sorted(counts, key=lambda t: -counts[t]):  # the commonest keeps its tone; the rarer move if they must
        tone = (looks.get(t) or {}).get("tone") or [128, 128, 128]
        rgb = [int(c) for c in tone]
        for step in range(12):
            if all(sum((a - b) ** 2 for a, b in zip(rgb, c)) ** 0.5 >= 42 for c in chosen):
                break
            k = 26 * ((step // 2) + 1) * (1 if step % 2 == 0 else -1)  # lighter, then darker, further each time
            rgb = [max(8, min(247, c + k)) for c in tone]
        chosen.append(rgb)
        alt = (looks.get(t) or {}).get("alt") or []
        words = t.replace("-", " ") + (f" ({alt[0]})" if alt else "")
        key.append({"terrain": t, "words": words, "rgb": tuple(rgb)})
    rgb_of = {k["terrain"]: k["rgb"] for k in key}
    picture = Image.new("RGB", (w * cell_px, h * cell_px), (0, 0, 0))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(picture)
    for y in range(h):
        for x in range(w):
            if grid[y][x] in rgb_of:
                draw.rectangle([x * cell_px, y * cell_px, (x + 1) * cell_px - 1, (y + 1) * cell_px - 1], fill=rgb_of[grid[y][x]])
    return picture, key, grid


def _hex(rgb) -> str:
    return "#%02X%02X%02X" % tuple(rgb)


# --- Tiles joined along the plan: seams through quiet ground (any place, any size) -------------------------------

def seam_costs(grid: list[list[str]]) -> list[list[float]]:
    """How much a join through each cell would show, from the plan's ground alone: most where two grounds meet, a lot in
    a thin strip of one ground (a road, a path, a river, a sidewalk), little inside a wide even area, nothing in solid
    rock or the void."""
    h, w = len(grid), len(grid[0])

    def run(x, y, dx, dy):
        n, t = 0, grid[y][x]
        while 0 <= x < w and 0 <= y < h and grid[y][x] == t and n < 12:
            n += 1
            x, y = x + dx, y + dy
        return n
    cost = [[0.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            t = grid[y][x]
            if _solid(t):
                continue
            edge = any(0 <= x + dx < w and 0 <= y + dy < h and grid[y + dy][x + dx] != t
                       for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            across = min(run(x, y, 1, 0) + run(x, y, -1, 0), run(x, y, 0, 1) + run(x, y, 0, -1)) - 1
            cost[y][x] = (20.0 if edge else 0.0) + (12.0 if across <= 6 else 0.0) + 1.0
    return cost


def plan_tiles(grid: list[list[str]], max_cells: int, overlap: int = 10) -> list[tuple[int, int, int, int]]:
    """Where a place's background is split into tiles (each at most `max_cells` across, one call each), chosen from the
    plan: each split line goes where a join would show least (the cheapest column or row of seam_costs: through block
    middles, fields, rock; never along a road where there's a choice), the tiles overlapping `overlap` cells about it.
    Returns (x0, y0, x1, y1) in cells, in reading order."""
    h, w = len(grid), len(grid[0])
    cost = seam_costs(grid)
    col = [sum(cost[y][x] for y in range(h)) for x in range(w)]
    row = [sum(cost[y][x] for x in range(w)) for y in range(h)]

    def splits(n: int, line: list[float]) -> list[int]:
        """Split lines along one axis: as few as keep every tile within max_cells, each at the cheapest line near its
        even share."""
        parts = max(1, -(-(n - overlap) // max(1, max_cells - overlap)))
        while parts > 1 and (n + (parts - 1) * overlap) / parts > max_cells:
            parts += 1
        out, start = [], 0
        for i in range(1, parts):
            target = n * i // parts
            reach = max(2, (max_cells - overlap) // 4)
            lo, hi = max(start + overlap + 1, target - reach), min(n - overlap - 1, target + reach)
            best = min(range(lo, max(lo + 1, hi)), key=lambda c: (sum(line[max(0, c - 1):c + 2]), abs(c - target)))
            out.append(best)
            start = best
        return out
    xs, ys = splits(w, col), splits(h, row)
    xb, yb = [0] + xs + [w], [0] + ys + [h]
    half = overlap // 2
    tiles = []
    for j in range(len(yb) - 1):
        for i in range(len(xb) - 1):
            tiles.append((max(0, xb[i] - half), max(0, yb[j] - half), min(w, xb[i + 1] + half), min(h, yb[j + 1] + half)))
    return tiles


def blend_tiles(tiles: list, size: tuple[int, int], cell_px: int, overlap: int, band: int = 6) -> Image.Image:
    """Tiles painted from one shared picture of the whole place ((x0, y0) in cells, picture covering its outer border),
    in reading order, laid into one: each tile is used inside its inner border (half the overlap in from each joined
    edge) and fades into what's already there across a `band`-cell strip centred on it. The tiles already agree (they
    started from the same picture), so a soft blend is enough."""
    from PIL import ImageChops
    w, h = size
    out = Image.new("RGB", (w * cell_px, h * cell_px), (0, 0, 0))
    half, b = overlap // 2 * cell_px, band * cell_px

    def ramp(n: int, start: int) -> list[int]:
        # 0 before the band, rising across it, 255 after; no band when the tile starts at the place's edge.
        if start == 0:
            return [255] * n
        lo = half - b // 2
        return [max(0, min(255, round(255 * (i - lo) / b))) for i in range(n)]
    for x0, y0, picture in tiles:
        across = Image.new("L", (picture.width, 1))
        across.putdata(ramp(picture.width, x0))
        down = Image.new("L", (1, picture.height))
        down.putdata(ramp(picture.height, y0))
        mask = ImageChops.multiply(across.resize(picture.size), down.resize(picture.size))
        out.paste(picture, (x0 * cell_px, y0 * cell_px), mask)
    return out


def background_words(description: str, key: list[dict]) -> tuple[list[str], dict[str, str]]:
    """The background description (the planner's, editable: docs/places/background.md §2) read as its general lines and
    its ground lines: a line "<ground>: <words>" names how a ground looks (its terrain, or the terrain's words, before
    the colon); every other line is said as it is."""
    names = {}
    for k in key:
        names[k["terrain"].lower()] = k["terrain"]
        names[k["terrain"].replace("-", " ").lower()] = k["terrain"]
        names[k["words"].lower()] = k["terrain"]
    general, grounds = [], {}
    for line in (description or "").splitlines():
        line = line.strip().lstrip("-").strip()
        if not line:
            continue
        head, sep, words = line.partition(":")
        if sep and head.strip().lower() in names and words.strip():
            grounds[names[head.strip().lower()]] = words.strip()
        else:
            general.append(line)
    return general, grounds


def background_prompt(description: str, key: list[dict], solid: bool) -> str:
    """The whole picture's prompt (docs/places/background.md §2): the description's general lines, then the colour map
    and one line per colour in plain words, black kept black, only the ground."""
    general, grounds = background_words(description, key)
    lines = general or ["A background for a game."]
    lines.append("Seen from directly above (top-down).")
    lines.append("Image 1 is a colour map of the ground. Paint each colour as its ground. Keep every area and every edge exactly where it is.")
    lines.append("Colours:")
    lines += [f"- {_hex(k['rgb'])} = {grounds.get(k['terrain'], k['words'])}" for k in key]
    if solid:
        lines.append("Black is solid rock or nothing: keep it black.")
    if not any(line.lower().startswith("only the ground") for line in general):
        lines.append("Only the ground: no buildings, no people, no objects, no text.")
    return "\n".join(lines)


def paint_background(painter, terrain: dict, cells: tuple[int, int], description: str, folder: Path, seed: int = 7,
                     target_px: int = 64, max_tiles: int = 9, overlap: int = 10, check: float = 35.0,
                     retries: int = 2, report=None) -> dict:
    """A place's background painted from its plan (docs/places/background.md): its ground as a top-down colour map;
    the whole painted in one call (every look decided once); if that's coarser than `target_px` a cell, tiles planned
    from the plan (plan_tiles, at most `max_tiles`), each painted again from its part of the whole picture, enlarged,
    and checked against its neighbours (a tile that disagrees with every one by more than `check` repainted, up to
    `retries` times); blended at their inner borders (blend_tiles); solid ground set back to black. Written to
    `folder` as background-topdown.png (and its steps). Returns {path, cellPx, cells, calls, tiles, prompt}.
    `report(done, total, step)`, if given, hears each step: the whole, then each tile (1 + tiles steps)."""
    from concurrent.futures import ThreadPoolExecutor
    from PIL import ImageChops, ImageDraw, ImageStat
    W, H = cells
    folder.mkdir(parents=True, exist_ok=True)
    palette, key, grid = background_palette(terrain, (W, H), cell_px=32)
    solid = any(_solid(t) for row in grid for t in row)
    prompt = background_prompt(description, key, solid)
    (folder / "background-prompt.txt").write_text(prompt, encoding="utf-8")
    palette.save(folder / "background-blockout-topdown.png")
    calls = 0

    def size_for(w: int, h: int) -> tuple[int, int]:
        k = 2048 / max(w, h)
        return max(16, round(w * k) // 16 * 16), max(16, round(h * k) // 16 * 16)
    report = report or (lambda done=None, total=None, step=None: None)
    # 2 (planned first, so the steps are known). Tiles, from the plan; as many as reach the target, at most max_tiles.
    tiles = []
    if 2048 / max(W, H) < target_px:
        max_cells = max(overlap + 4, int(2048 / target_px))
        tiles = plan_tiles(grid, max_cells, overlap)
        while len(tiles) > max_tiles:
            max_cells += 2
            tiles = plan_tiles(grid, max_cells, overlap)
    total = 1 + len(tiles)
    # 1. The whole place, one call.
    report(0, total, "Painting the whole place")
    size = size_for(W, H)
    whole = Image.open(io.BytesIO(painter.repaint(_png(palette.resize(size, Image.NEAREST)), prompt, references=[], seed=seed,
                                                  width=size[0], height=size[1]))).convert("RGB")
    calls += 1
    whole.save(folder / "background-whole.png")
    if not tiles:
        cell = min(128, int(2048 / max(W, H)))
        top = whole.resize((W * cell, H * cell), Image.LANCZOS)
        report(1, total, "Painted the whole place")
    else:
        report(1, total, f"Painting the detail windows: 0 of {len(tiles)}")
        painted = [0]
        count = threading.Lock()
        cell = max(24, min(128, int(2048 / max(max(x1 - x0, y1 - y0) for x0, y0, x1, y1 in tiles))))
        first = prompt.splitlines()[0]
        detail = (f"{first}\nImage 1 is one part of it. Make it sharp and detailed at high resolution.\n"
                  "Keep everything exactly as it is: every area, edge, line and colour stays where it is.\n"
                  "Only the ground: no buildings, no people, no objects, no text.")
        (folder / "background-tile-prompt.txt").write_text(detail, encoding="utf-8")
        sx, sy = whole.width / W, whole.height / H

        def paint(t, attempt=0):
            x0, y0, x1, y1 = t
            s = size_for(x1 - x0, y1 - y0)
            part = whole.crop((round(x0 * sx), round(y0 * sy), round(x1 * sx), round(y1 * sy))).resize(s, Image.BICUBIC)
            png = painter.repaint(_png(part), detail, references=[], seed=seed + 101 * attempt + x0 * 7 + y0, width=s[0], height=s[1])
            with count:
                painted[0] += 1
                report(1 + painted[0], None, f"Painting the detail windows: {painted[0]} of {total - 1}"
                       + (" (repainting the ones that didn't match)" if attempt else ""))
            return Image.open(io.BytesIO(png)).convert("RGB").resize(((x1 - x0) * cell, (y1 - y0) * cell), Image.LANCZOS)
        with ThreadPoolExecutor(4) as pool:
            pics = dict(zip(tiles, pool.map(paint, tiles)))
        calls += len(tiles)

        def disagreement(a, b) -> float | None:
            ox0, oy0, ox1, oy1 = max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])
            if ox1 <= ox0 or oy1 <= oy0:
                return None
            ca = pics[a].crop(((ox0 - a[0]) * cell, (oy0 - a[1]) * cell, (ox1 - a[0]) * cell, (oy1 - a[1]) * cell))
            cb = pics[b].crop(((ox0 - b[0]) * cell, (oy0 - b[1]) * cell, (ox1 - b[0]) * cell, (oy1 - b[1]) * cell))
            return sum(ImageStat.Stat(ImageChops.difference(ca, cb)).mean) / 3
        # 3. The check: a tile redrawn (shifted, a new layout) disagrees with every neighbour; painted again.
        for attempt in range(1, retries + 1):
            bad = []
            for t in tiles:
                scores = [d for u in tiles if u != t for d in [disagreement(t, u)] if d is not None]
                if scores and min(scores) > check:
                    bad.append(t)
            if not bad:
                break
            total += len(bad)
            report(None, total, f"Checking the windows: {len(bad)} to paint again")
            for t in bad:
                pics[t] = paint(t, attempt)
                calls += 1
        report(total, total, "Blending the windows together")
        top = blend_tiles([(t[0], t[1], pics[t]) for t in tiles], (W, H), cell, overlap, band=max(2, min(6, overlap - 2)))
    # 4. Cut to the plan: solid ground black.
    if solid:
        rock = Image.new("L", top.size, 0)
        draw = ImageDraw.Draw(rock)
        for y in range(H):
            for x in range(W):
                if _solid(grid[y][x]):
                    draw.rectangle([x * cell, y * cell, (x + 1) * cell - 1, (y + 1) * cell - 1], fill=255)
        top.paste((0, 0, 0), (0, 0), rock)
    path = folder / "background-topdown.png"
    top.save(path)
    return {"path": path, "cellPx": cell, "cells": [W, H], "calls": calls, "tiles": [list(t) for t in tiles], "prompt": prompt}


# --- Blocks: a wall and what's set in it or mounted on it (docs/places/matched-art.md §2b) ---------------------------

BLOCK_TW = 256            # a cell's diamond in a block's picture, pixels across (2:1)
BLOCK_WALL = int(BLOCK_TW * 0.95)


def _block_box(height: int = BLOCK_WALL) -> Image.Image:
    """A 1 x 1 cell box on the key colour, at the grid's angle: what a wall block is painted from."""
    from PIL import ImageDraw
    tw, th, gap = BLOCK_TW, BLOCK_TW // 2, BLOCK_TW
    img = Image.new("RGB", (tw + 2 * gap, height + th + 2 * gap), (255, 0, 255))
    d = ImageDraw.Draw(img)
    x, y = gap + tw // 2, gap + height + th
    front, left, right, back = (x, y), (x - tw // 2, y - th // 2), (x + tw // 2, y - th // 2), (x, y - th)
    up = lambda p: (p[0], p[1] - height)  # noqa: E731
    d.polygon([up(back), up(right), up(front), up(left)], fill=(200, 200, 205))
    d.polygon([left, front, up(front), up(left)], fill=(150, 150, 158))
    d.polygon([front, right, up(right), up(front)], fill=(110, 110, 118))
    return img


def block_outline_fit(made: bytes) -> tuple[float, float]:
    """How well a painted block fills a box's outline (a 1-cell box's hexagon, fitted to the painting: as wide as it,
    standing on its foot, its top a 2:1 diamond): the share of its paint outside it (a bump, a stone on a corner) and the
    share of the outline left empty (a stepped or chipped corner)."""
    from PIL import ImageChops, ImageDraw, ImageFilter
    found = find_items(made)
    if not found:
        return 1.0, 1.0
    paint = max(found, key=lambda it: it["area"])["image"].convert("RGBA").getchannel("A").point(lambda v: 255 if v > 128 else 0)
    paint = paint.crop(paint.getbbox())
    w, h = paint.size
    d = w / 2
    want = Image.new("L", paint.size, 0)
    ImageDraw.Draw(want).polygon([(w / 2, 0), (w, d / 2), (w, h - d / 2), (w / 2, h), (0, h - d / 2), (0, d / 2)], fill=255)
    grace = max(3, int(w * 0.015)) | 1
    loose, tight = want.filter(ImageFilter.MaxFilter(grace)), want.filter(ImageFilter.MinFilter(grace))
    outside = ImageChops.subtract(paint, loose).histogram()[255] / max(1, paint.histogram()[255])
    missing = ImageChops.subtract(tight, paint).histogram()[255] / max(1, tight.histogram()[255])
    return outside, missing


def block_to_grid(im: Image.Image, wall_height: float | None = None) -> tuple[Image.Image, float]:
    """A painted block set on the grid. Its foot's corners (the front: its lowest point; the left and right: walked out
    to along the foot while it stays one edge, so a torch or a door beside the block doesn't count) and its top's (the
    back: its highest point; the left and right edges, fitted from the middle columns, so a flame above doesn't count)
    are found, each edge a straight line. Each side of the front corner is scaled so the foot is half a cell across, and
    each column stretched between where its foot and its top should be: both foot edges and both top edges slope
    exactly 1 in 2 (the grid's angle, south and north alike); vertical lines stay vertical. One cell (BLOCK_TW) wide by
    its foot, whatever sticks out; the front corner at the picture's middle and bottom. With `wall_height` (the block
    it was made from: a door's wall), that height, so the two join level. Returns the picture and its wall height."""
    tw = BLOCK_TW
    w, h = im.size
    alpha = im.getchannel("A").load()
    foot, top = [], []
    for x in range(w):
        y = h - 1
        while y >= 0 and alpha[x, y] <= 128:
            y -= 1
        foot.append(y)
        y = 0
        while y < h and alpha[x, y] <= 128:
            y += 1
        top.append(y if y < h else -1)
    xf = max(range(w), key=lambda x: foot[x])
    jump = max(3, int(0.02 * h))
    xl = xf
    while xl > 0 and foot[xl - 1] >= 0 and abs(foot[xl - 1] - foot[xl]) <= jump:
        xl -= 1
    xr = xf
    while xr < w - 1 and foot[xr + 1] >= 0 and abs(foot[xr + 1] - foot[xr]) <= jump:
        xr += 1
    if xr - xl < w // 2:  # the lowest point was something small beside the block: take the block's whole width
        painted = [x for x in range(w) if foot[x] >= 0]
        xl, xr = painted[0], painted[-1]
        xf = max(range(xl, xr + 1), key=lambda x: foot[x])
    span = max(8, xr - xl)
    xb = min(range(xl + span // 4, xr - span // 4 + 1), key=lambda x: top[x] if top[x] >= 0 else h)

    def fit(lo: int, hi: int, values: list[int], x0: int) -> tuple[float, float]:
        pts = [(abs(x - x0), values[x]) for x in range(lo, hi) if values[x] >= 0]
        if len(pts) < 2:
            return float(values[x0]), 0.5
        mx, my = sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)
        sxx = sum((p[0] - mx) ** 2 for p in pts) or 1
        k = sum((p[0] - mx) * (p[1] - my) for p in pts) / sxx
        return my - k * mx, k
    fl = fit(xl + (xf - xl) // 10, xf - (xf - xl) // 10, foot, xf)
    fr = fit(xf + (xr - xf) // 10, xr - (xr - xf) // 10, foot, xf)
    tl = fit(xl + (xb - xl) // 5, xb - (xb - xl) // 10, top, xb)
    tr = fit(xb + (xr - xb) // 10, xr - (xr - xb) // 5, top, xb)
    foot_src = lambda x: (fl if x < xf else fr)[0] + (fl if x < xf else fr)[1] * abs(x - xf)  # noqa: E731
    top_src = lambda x: (tl if x < xb else tr)[0] + (tl if x < xb else tr)[1] * abs(x - xb)  # noqa: E731
    y_tl, y_tr, y_tb = top_src(xl), top_src(xr), top_src(xb)
    y_ft = y_tl + y_tr - y_tb  # the front-top corner, above the front corner (the top is a parallelogram)

    def edge_src(x: float) -> float:  # the top's front edge: its left corner, to the front-top corner, to its right corner
        if x < xf:
            return y_tl + (y_ft - y_tl) * (x - xl) / max(1, xf - xl)
        return y_ft + (y_tr - y_ft) * (x - xf) / max(1, xr - xf)
    a_left, a_right = (tw / 2) / max(1, xf - xl), (tw / 2) / max(1, xr - xf)
    s = tw / span
    wall = wall_height if wall_height else max(tw * 0.3, (foot_src(xf) - top_src(xf)) * s - tw / 2)
    pad = max(round(xl * a_left), round((w - 1 - xr) * a_right), 0) + 2
    out_w = tw + 2 * pad
    above = round(tw * 0.35)
    yf = above + round(tw / 2 + wall)
    out = Image.new("RGBA", (out_w, yf + 2), (0, 0, 0, 0))
    src, dst = im.load(), out.load()
    for X in range(out_w):
        dx = X - (pad + tw / 2)
        x_src = xf + dx / (a_left if dx < 0 else a_right)
        xi = int(round(x_src))
        if xi < 0 or xi >= w:
            continue
        # Three lines a column crosses, each put where the grid has it: the top's outline (its back edges), the top's
        # front edge (where it meets the wall's face) and the foot; the column stretched between them in two parts.
        f_out, e_out, t_out = yf - 0.5 * abs(dx), yf - wall - 0.5 * abs(dx), yf - tw / 2 - wall + 0.5 * abs(dx)
        f_in, e_in, t_in = foot_src(x_src), edge_src(x_src), top_src(x_src)
        # The painting's own top where it's a little below the line (a rounded corner: stretched up to it), inside the block.
        if xl <= xi <= xr and top[xi] >= 0 and t_in < top[xi] < t_in + 0.08 * h:
            t_in = min(float(top[xi]), e_in - 2)
        e_in = min(max(e_in, t_in + 1), f_in - 1)
        k_low = (f_in - e_in) / max(1.0, f_out - e_out)  # the column's plain scale: above the top (a flame) too
        for Y in range(yf + 2):
            if Y < t_out:
                yi = int(round(t_in + (Y - t_out) * k_low))
            elif Y <= e_out:
                yi = int(round(t_in + (Y - t_out) * (e_in - t_in) / max(1.0, e_out - t_out)))
            else:
                yi = int(round(e_in + (Y - e_out) * k_low))
            if 0 <= yi < h:
                dst[X, Y] = src[xi, yi]
    return out, wall


def ReadJson(path: Path) -> dict:
    """A small JSON file's object; {} when it's missing or isn't one."""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return value if isinstance(value, dict) else {}


def make_blocks(painter, folder: Path, pieces: list[dict], context: dict, style_png: bytes | None, seed: int,
                make: set[str] | None = None) -> list[dict]:
    """A wall and what's set in it or mounted on it (a run: `kit`), as blocks (docs/places/matched-art.md §2b): the wall
    block painted from a 1-cell box, alone, checked against the box's outline (a stepped or bumpy block painted again,
    up to three tries); each thing set in it or on it (a door, a wall light: its relation says it depends on the wall)
    an edit of the accepted wall block, so it's the same block; each set on the grid (block_to_grid), the things on the
    wall at the wall's height. Written as pieces/<slug>.png with pieces/<slug>.json ({"cellPx", "wall"}). Only the
    pieces in `make` (all, without it); a wall kept from before is the one the others are edits of. Returns the steps."""
    steps: list[dict] = []
    base = next((p for p in pieces if (p.get("kit") or {}).get("run")), None)
    if base is None:
        return steps
    wall_words = str(base.get("role") or "wall").replace("-", " ")
    style = f"Style: {_style_line(context)}.\n"
    # Coloured by the background (its relation coloured-by): its colour said, and the ground shown as the last image.
    floor = context.get("floor") if base.get("colourFrom") == "background" else None
    refs = ([style_png] if style_png else []) + ([floor["png"]] if floor else [])
    colour = ""
    if floor:
        colour = (f"Its stone is the colour of the ground it stands on: {floor['words']} ({floor['hex']}), a little lighter. "
                  f"Image {len(refs) + 1} shows that ground: the same colours and light.\n")
    prompt = ("Game asset. Isometric view, 2:1: every edge on the ground rises one step for two across. Flat magenta background (#FF00FF).\n"
              + style + colour +
              f"Image 1 shows a box. Paint it as one block of {wall_words} that fills exactly the box: its size, place and angle. "
              "Its sides cut flat and straight; its top flat and level; its corners sharp and square. Nothing sticks out of it: "
              "no extra stones on its top or at its corners, no rounded or bevelled edges. Identical blocks standing side by "
              "side make one continuous wall with no gaps.\n"
              "Only the block: no floor, no ground, no shadow, no people, no text. Do not use magenta or pink in it."
              + ("\nImage 2 shows other assets of this game: paint in exactly the same style." if style_png else ""))
    make = {p["key"] for p in pieces} if make is None else make
    kept = folder / "pieces" / f"{base['slug']}.png"
    from .bfl import _four_mp
    box = _block_box()
    size = _four_mp(box.width, box.height)
    best = None
    for attempt in range(3 if base["key"] in make or not kept.exists() else 0):
        made = painter.repaint(_png(box.resize((size["width"], size["height"]), Image.NEAREST)), prompt,
                               references=refs, seed=seed + 37 * attempt, **size)
        (folder / f"block-{attempt + 1}.png").write_bytes(made)
        outside, missing = block_outline_fit(made)
        steps.append({"block": base["slug"], "try": attempt + 1, "outside": round(outside, 3), "missing": round(missing, 3)})
        if best is None or outside + missing < best[0]:
            best = (outside + missing, made)
        if outside <= 0.015 and missing <= 0.05:  # a little rounding at the edges is fine; a stepped corner is not
            break

    def biggest(made: bytes) -> Image.Image | None:
        found = find_items(made)
        if not found:
            return None
        im = max(found, key=lambda it: it["area"])["image"].convert("RGBA")
        return im.crop(im.getchannel("A").getbbox())
    if best is None:
        # The wall kept from before: already on the grid; its height as it was written (else measured again).
        with Image.open(kept) as im:
            wall = im.convert("RGBA")
        wall = wall.crop(wall.getchannel("A").getbbox())
        saved = ReadJson(kept.with_suffix(".json"))
        height = saved.get("wall") or block_to_grid(wall)[1]
    else:
        wall = biggest(best[1])
        if wall is None:
            return steps
        placed, height = block_to_grid(wall)
        placed.save(kept)
        kept.with_suffix(".json").write_text(json.dumps({"cellPx": BLOCK_TW, "wall": height}), encoding="utf-8")
    # What's set in the wall or mounted on it: an edit of the wall block (it starts from the wall, so it is that wall).
    canvas = Image.new("RGB", (1024, 1024), (255, 0, 255))
    w = wall.copy()
    w.thumbnail((620, 620))
    canvas.paste(w, ((1024 - w.width) // 2, (1024 - w.height) // 2), w)
    for p in pieces:
        if p is base or p["key"] not in make or (p.get("kit") or {}).get("sameAs"):
            continue
        # What it is (its role's words: "a burning torch in an iron wall bracket"), then what it's for: a purpose alone
        # ("illuminating dark areas") names no thing to paint.
        thing = str(p.get("words") or str(p.get("role") or "door").replace("-", " "))
        if p.get("purpose"):
            thing += f" (for {p['purpose']})"
        how = "mounted on" if (p.get("kit") or {}).get("on") else "set into"
        change = (f"Image 1 is a block of {wall_words}. Paint the same block with {thing} {how} its face towards the lower left"
                  + (", as tall as two thirds of the block" if how == "set into" else "") +
                  ". Keep everything else exactly as it is: the same block, stone, size, shape and angle. "
                  "Flat magenta background (#FF00FF). No floor, no shadow, no text.")
        made = painter.repaint(_png(canvas), change, references=[], seed=seed + 101, width=1024, height=1024)
        (folder / f"edit-{p['slug']}.png").write_bytes(made)
        im = biggest(made)
        if im is None:
            continue
        placed, _ = block_to_grid(im, height)
        placed.save(folder / "pieces" / f"{p['slug']}.png")
        (folder / "pieces" / f"{p['slug']}.json").write_text(json.dumps({"cellPx": BLOCK_TW, "wall": height}), encoding="utf-8")
        steps.append({"block": p["slug"], "from": base["slug"]})
    # A kind of the same family (a locked door, a light the plan put on the wall): the art of the one made (sameAs).
    slugs = {p["key"]: p["slug"] for p in pieces}
    for p in pieces:
        same = (p.get("kit") or {}).get("sameAs")
        source = folder / "pieces" / f"{slugs.get(same, '')}.png"
        if p["key"] not in make or not same or not source.exists():
            continue
        (folder / "pieces" / f"{p['slug']}.png").write_bytes(source.read_bytes())
        (folder / "pieces" / f"{p['slug']}.json").write_text(json.dumps({"cellPx": BLOCK_TW, "wall": height}), encoding="utf-8")
        steps.append({"block": p["slug"], "sameAs": slugs[same]})
    return steps
