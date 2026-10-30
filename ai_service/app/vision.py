"""Looking at art (development_plan.md 6.2): a vision model describes each
image of a project's assets — what it is, its kind, tags, direction, a rough
size in grid cells, whether it blocks the way, and a better file name — so
the AI chooses by meaning instead of file names like "Ground 37".

Catalog v2 (7.3) adds what the builder needs: the ontology role each image can
fill, its perspective, named points on it (a door, a gate) and whether it can be
entered. Objects cut from a sheet are described one by one (`items` with a
`rect`), and small pixel art is enlarged before the model sees it."""

from __future__ import annotations

import base64
import io
import json
import re
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Literal

from PIL import Image
from pydantic import BaseModel, ValidationError, field_validator

from .providers import Provider, ProviderError

KINDS = ["ground", "building", "nature", "decoration", "item", "character", "effect", "other"]
PERSPECTIVES = ["top-down", "three-quarter", "isometric", "unknown"]
BATCH = 6          # images per request
PREVIEW = 256      # longest side sent to the model, in pixels

PROMPT = """You catalogue art for a 2D game made of grid cells. A cell is about one metre: a person
stands in one, a cart takes about two, a small house two to three. For EACH numbered image, return:
- "index": its number
- "name": a short kebab-case file name for what it shows, e.g. "stone-well", "dirt-road-corner",
  "fence-horizontal" (lowercase letters, digits and dashes; no numbering, no pack name)
- "kind": one of ground (floor/terrain tiles that fill a cell), building, nature (trees, bushes, rocks),
  decoration (banners, flags, barrels, carts, campfires), item (things picked up), character, effect, other
- "description": one plain sentence about what it is and what it's for
- "tags": 2-6 lowercase words a level designer would search for (e.g. "well", "water", "village", "stone")
- "direction": "horizontal" if it runs left-right on screen (a fence piece, a bridge), "vertical" if
  it runs up-down, else "none"
- "sizeCells": how many cells WIDE it should be in the game, judged from what it is (not from its pixel
  size): ground tiles are exactly 1; a barrel 0.5; a well 1-1.5; a house 2-3; a castle 3-5; a tree 1-2
- "blocks": true if characters can't walk through it (walls, buildings, wells, trees, rocks), else false
- "perspective": "top-down" (seen from straight above: floor tiles, rugs, roads, crops seen flat),
  "three-quarter" (seen from the front and a little above: most houses, trees and characters in RPGs),
  "isometric" (drawn on a diamond grid: its ground lines run at about 30 degrees), or "unknown"
- "anchors": named points on the image, each [x, y] from 0 to 1 across the image (0,0 top-left), for
  the ones that are there: "door" (the bottom middle of a door), "gate", "seat", "counter"; else {}
- "accessible": true if a character could go inside it (it has a door), else false
- "role": %ROLE%
The file name and pixel size are given as hints only; names are often meaningless ("Ground 37").
Answer as JSON: {"assets": [{...}, ...]} with one entry per image."""

ROLE_FREE = 'one or two kebab-case words for what it is used as in a place, e.g. "house", "tree", "fence-piece-set"'
ROLE_LIST = ('the one role from this list it best fills in a place (a piece of a fence, wall or hedge that '
             'joins others is the "...-piece-set" role), or "" if none fits: ')


def prompt_for(roles: list[str] | None, wanted: dict | None = None) -> str:
    text = PROMPT.replace("%ROLE%", ROLE_LIST + ", ".join(roles) if roles else ROLE_FREE)
    if wanted:
        text += ("\n\nThese images were made or found for ONE piece of a game place. What was wanted:\n"
                 f"{json.dumps(wanted, ensure_ascii=False)}\n"
                 "Judge each image against it strictly, as an art director would, and add to its entry:\n"
                 "- \"fits\": true only if it shows exactly what was wanted, as ONE whole thing of the kind asked "
                 "(a whole building, not its sign, door, gate or a part of it; one object, not several), in the view "
                 "asked, and it would look right in that room and place (its materials, era, condition and mood);\n"
                 "- \"problems\": if not, each reason in a few plain words an artist can act on (\"it shows the inn's "
                 "hanging sign, not the inn building\", \"seen from the front, not isometric\", \"a clean modern street "
                 "lamp in a damp medieval cellar\", \"two barrels, not one\"); else [];\n"
                 "- \"sheet\": true if the image is a sprite sheet, tileset or collection of many separate things.")
    return text


class AssetDescription(BaseModel):
    id: str = ""
    path: str
    name: str
    kind: str
    description: str
    tags: list[str]
    direction: Literal["none", "horizontal", "vertical"] = "none"
    sizeCells: float
    blocks: bool
    perspective: str = "unknown"
    anchors: dict[str, list[float]] = {}
    accessible: bool = False
    role: str = ""
    # The verdict when what was wanted is given: does it show that, and if not, why.
    fits: bool | None = None
    problems: list[str] = []
    sheet: bool = False

    @field_validator("name")
    @classmethod
    def _slug(cls, value: str) -> str:
        slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")[:48]
        return slug or "asset"

    @field_validator("kind")
    @classmethod
    def _kind(cls, value: str) -> str:
        value = value.strip().lower()
        return value if value in KINDS else "other"

    @field_validator("description")
    @classmethod
    def _one_line(cls, value: str) -> str:
        return " ".join(value.split())[:300]

    @field_validator("tags")
    @classmethod
    def _tags(cls, value: list[str]) -> list[str]:
        tags = [re.sub(r"[^a-z0-9-]+", "-", t.lower()).strip("-") for t in value]
        return [t for t in dict.fromkeys(tags) if t][:8]

    @field_validator("sizeCells")
    @classmethod
    def _size(cls, value: float) -> float:
        return round(min(max(float(value), 0.1), 20.0), 2)

    @field_validator("perspective", mode="before")
    @classmethod
    def _perspective(cls, value) -> str:
        value = str(value).strip().lower().replace(" ", "-").replace("3/4", "three-quarter")
        return value if value in PERSPECTIVES else "unknown"

    @field_validator("anchors", mode="before")
    @classmethod
    def _anchors(cls, value) -> dict:
        if not isinstance(value, dict):
            return {}
        out = {}
        for key, point in value.items():
            if isinstance(point, (list, tuple)) and len(point) == 2 and all(
                    isinstance(v, (int, float)) and not isinstance(v, bool) for v in point):
                name = re.sub(r"[^a-z0-9-]+", "-", str(key).lower()).strip("-")
                if name:
                    out[name] = [round(min(max(float(v), 0.0), 1.0), 3) for v in point]
        return out

    @field_validator("role", mode="before")
    @classmethod
    def _role(cls, value) -> str:
        return re.sub(r"[^a-z0-9-]+", "-", str(value or "").lower()).strip("-")[:48]


def image_chunk(path: Path, rect: list[int] | None = None, preview: int = 0,
                sent: list | None = None) -> tuple[dict, tuple[int, int]]:
    """The image (or `rect` of it: x, y, w, h) as a PNG data URI chunk, at about
    PREVIEW pixels (or `preview`) on its longest side, and its real size. Small pixel art is
    enlarged by a whole factor, pixels kept sharp, so the model sees its shapes. `sent` gets the size sent."""
    PREVIEW = preview or globals()["PREVIEW"]
    with Image.open(path) as source:
        image = source.convert("RGBA")
    if rect:
        x, y, w, h = rect
        image = image.crop((x, y, x + w, y + h))
    size = image.size
    longest = max(size)
    if 0 < longest * 2 <= PREVIEW:
        factor = PREVIEW // longest
        image = image.resize((size[0] * factor, size[1] * factor), Image.NEAREST)
    image.thumbnail((PREVIEW, PREVIEW))
    # Transparent parts on a light checker, so the model sees the outline.
    backdrop = Image.new("RGBA", image.size, (235, 235, 235, 255))
    for y in range(0, image.size[1], 16):
        for x in range(0, image.size[0], 16):
            if (x // 16 + y // 16) % 2:
                backdrop.paste((205, 205, 205, 255), (x, y, x + 16, y + 16))
    backdrop.alpha_composite(image)
    out = io.BytesIO()
    backdrop.convert("RGB").save(out, format="PNG")
    data = base64.b64encode(out.getvalue()).decode("ascii")
    if sent is not None:
        sent[:] = list(image.size)
    return {"type": "image_url", "image_url": f"data:image/png;base64,{data}"}, size


def map_chunk(path: Path, longest: int = 1400) -> dict:
    """A whole map (a solved layout's PNG) as a data URI chunk, at most `longest` pixels on a side."""
    with Image.open(path) as source:
        image = source.convert("RGB")
    image.thumbnail((longest, longest))
    out = io.BytesIO()
    image.save(out, format="PNG")
    return {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(out.getvalue()).decode('ascii')}"}


def _describe_batch(provider: Provider, root: Path, items: list[dict],
                    roles: list[str] | None = None, wanted: dict | None = None) -> tuple[list[dict], list[str]]:
    content: list[dict] = [{"type": "text", "text": prompt_for(roles, wanted)}]
    ids = [item["id"] for item in items]
    for index, item in enumerate(items, start=1):
        file = (root / item["path"]).resolve()
        if root.resolve() not in file.parents:  # only the project's own images are shown to a model
            raise ValueError(f"{item['path']} is outside the project")
        chunk, (w, h) = image_chunk(file, item.get("rect"))
        part = " (one object cut from a sheet)" if item.get("rect") else ""
        content.append({"type": "text", "text": f"Image {index}: file \"{Path(item['path']).name}\"{part}, {w}x{h} pixels"})
        content.append(chunk)
    try:
        completion = provider.complete([{"role": "user", "content": content}])
    except ProviderError as error:
        return [], [f"{', '.join(ids)}: {error}"]
    try:
        answer = json.loads(completion.text)
        entries = answer.get("assets", []) if isinstance(answer, dict) else []
    except json.JSONDecodeError:
        return [], [f"{', '.join(ids)}: the model's answer wasn't JSON"]
    results, errors = [], []
    seen: set[int] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        index = entry.get("index")
        if not isinstance(index, int) or not 1 <= index <= len(items) or index in seen:
            continue
        seen.add(index)
        try:
            fields = {k: v for k, v in entry.items() if k not in ("index", "id", "path")}
            described = AssetDescription(id=ids[index - 1], path=items[index - 1]["path"], **fields).model_dump()
            if roles and described["role"] not in roles:
                described["role"] = ""
            results.append(described)
        except (ValidationError, TypeError) as error:
            errors.append(f"{ids[index - 1]}: {str(error).splitlines()[0]}")
    for index, item_id in enumerate(ids, start=1):
        if index not in seen:
            errors.append(f"{item_id}: not described")
    return results, errors


def describe_assets(provider: Provider, root: Path, paths: list[str], workers: int = 3,
                    items: list[dict] | None = None, roles: list[str] | None = None, judge: dict | None = None) -> dict:
    """Describes every image in `paths` (project-relative), and every item of
    `items` ({"id", "path", "rect": [x, y, w, h]}: an object cut from a sheet),
    several per request and a few requests at once. Unreadable or missing files
    are reported, the rest still described. `roles` (the ontology's) are the
    roles the model picks from."""
    wanted = [{"id": rel, "path": rel} for rel in paths] + [
        {"id": str(item.get("id") or item.get("path", "")), "path": str(item.get("path", "")), "rect": item.get("rect")}
        for item in (items or [])]
    images, errors = [], []
    for item in wanted:
        file = (root / item["path"]).resolve()
        rect = item.get("rect")
        if root.resolve() not in file.parents or not file.is_file():
            errors.append(f"{item['id']}: not a file in the project")
        elif file.suffix.lower() not in (".png", ".jpg", ".jpeg", ".bmp", ".tga"):
            errors.append(f"{item['id']}: not an image")
        elif rect is not None and not (isinstance(rect, list) and len(rect) == 4
                                       and all(isinstance(v, int) for v in rect) and rect[2] > 0 and rect[3] > 0):
            errors.append(f"{item['id']}: a rect is [x, y, w, h]")
        else:
            images.append(item)
    batches = [images[i:i + BATCH] for i in range(0, len(images), BATCH)]
    results: list[dict] = []
    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        for found, problems in pool.map(lambda batch: _describe_batch(provider, root, batch, roles, judge), batches):
            results.extend(found)
            errors.extend(problems)
    order = {item["id"]: i for i, item in enumerate(wanted)}
    results.sort(key=lambda r: order.get(r["id"], 0))
    return {"ok": bool(results) or not wanted, "assets": results, "errors": errors}


FIND_PARTS = """You look at one picture from a 2D game (a building's art, or part of a painted scene) and find where its moving
parts are: {kinds}. A building can have SEVERAL doors (a front door and a side door, a big double door and a small one,
doors on both of its faces): give EVERY one at ground level, each on its own, the main entrance first; not a loft or
upper-floor door, not a window or a shutter. For each: its box in the picture's pixels (x and y of its top-left corner,
its width and height: the whole door, from its threshold to its top, an arched one to the top of its arch, frame edge to
frame edge, not the wall around it), what it is, how sure you are, "leaves" (1, or 2 for a double door: two halves
meeting in the middle), "shape" (rect or arched) and "open" (true for an open archway or doorway with no door in it).
None: an empty list. Answer as JSON:
{{"parts": [{{"kind": "door", "box": [x, y, w, h], "label": "the front door", "confidence": 0.9, "leaves": 1, "shape": "rect", "open": false}}]}}"""


STAIRS = """This is one floor of a building from an isometric 2D game, drawn cut away (the walls in front and the floors
above removed) so its rooms show. Where is the STAIRCASE (or ladder) going up to the next floor? Say which part of the
building it's in, as the building is drawn: "top" (the back corner, furthest from the viewer), "right" (the right-hand
corner), "left" (the left-hand corner), "front" (the front corner, nearest the viewer) or "middle". None drawn: "none".
Answer as JSON: {"stairs": "top", "why": "<a few words>"}"""


def find_stairs(provider: Provider, root: Path, floors: list[str]) -> dict:
    """Where each floor's stairs up are (docs/places/interiors.md §5): one of the building's corners (top, right, left,
    front) or its middle, by the vision model, for each floor's picture but the top's. Choosing among places is what it's
    good at; it misplaces exact boxes."""
    from concurrent.futures import ThreadPoolExecutor

    def one(path: str) -> str:
        file = (root / path).resolve()
        if root.resolve() not in file.parents or not file.is_file():
            return "none"
        chunk, _ = image_chunk(file, None, preview=768)
        try:
            answer = json.loads(provider.complete([{"role": "user", "content": [{"type": "text", "text": STAIRS}, chunk]}]).text)
        except (ProviderError, json.JSONDecodeError, ValueError):
            return "none"
        where = str(answer.get("stairs", "none")).lower() if isinstance(answer, dict) else "none"
        return where if where in ("top", "right", "left", "front", "middle") else "none"
    with ThreadPoolExecutor(4) as pool:
        return {"ok": True, "stairs": list(pool.map(one, floors))}


LOCATE_DOORS = """The picture is a building in a 2D isometric game, seen from outside. Find each of its doors one walks in by (on
the ground floor's walls; not windows, not shutters). Give the smallest box round each as [x0, y0, x1, y1] in thousandths
of the picture (0-1000; x from the left edge, y from the top edge). Answer as JSON exactly:
{"doors": [{"box_2d": [x0, y0, x1, y1]}, ...]}, or {"doors": []} when it has none."""


def _boxes(answer, w: int, h: int) -> list[list[float]]:
    """Boxes [x0, y0, x1, y1] in the shown picture's pixels from a vision answer in thousandths of it (Mistral's grounding
    box_2d [x0, y0, x1, y1], or named edges), alone or in a list."""
    items = answer if isinstance(answer, list) else answer.get("doors", [answer]) if isinstance(answer, dict) else []
    out = []
    for item in items if isinstance(items, list) else []:
        if not isinstance(item, dict):
            continue
        # A list of four anywhere in it (box_2d, or packed under another key): Mistral's thousandths.
        packed = next((v for v in item.values() if isinstance(v, list) and len(v) == 4), None)
        try:
            if packed is not None:
                x0, y0, x1, y1 = (float(v) / 1000 * s for v, s in zip(packed, (w, h) * 2))
            else:
                x0, y0, x1, y1 = (float(item[key]) / 1000 * s for key, s in zip(("left", "top", "right", "bottom"), (w, h) * 2))
        except (KeyError, TypeError, ValueError):
            continue
        if x1 - x0 >= 2 and y1 - y0 >= 2:
            out.append([x0, y0, x1, y1])
    return out


def locate_doors(provider: Provider, picture: Image.Image) -> list[list[int]]:
    """The boxes round a building picture's doors, [x, y, w, h] each in its own pixels (docs/places/interiors.md §2: the
    ground floor is painted with them marked, so its entrance is where the door is); [] when none can be read."""
    shown = picture.convert("RGBA")
    k = min(1.0, 1024 / max(shown.width, shown.height))
    if k < 1:
        shown = shown.resize((round(shown.width * k), round(shown.height * k)), Image.LANCZOS)
    flat = Image.new("RGB", shown.size, (255, 255, 255))
    flat.paste(shown, (0, 0), shown)
    buffer = io.BytesIO()
    flat.save(buffer, "PNG")
    url = "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()
    content = [{"type": "text", "text": LOCATE_DOORS}, {"type": "image_url", "image_url": url}]
    try:
        text = provider.complete([{"role": "user", "content": content}]).text
        answer = json.loads(text[min(i for i in (text.find("{"), text.find("[")) if i >= 0):])
    except (ProviderError, json.JSONDecodeError, ValueError, AttributeError):
        return []
    return [[round(x0 / k), round(y0 / k), round((x1 - x0) / k), round((y1 - y0) / k)] for x0, y0, x1, y1 in _boxes(answer, shown.width, shown.height)]


def find_parts(provider: Provider, root: Path, path: str, kinds: list[str], rect: list[int] | None = None) -> dict:
    """The moving parts (doors, gates, shutters…) in a project picture, or in `rect` of it: their boxes in the
    picture's own pixels (the rect's offset added back)."""
    file = (root / path).resolve()
    if root.resolve() not in file.parents or not file.is_file():
        return {"ok": False, "errors": [f"No picture {path}"]}
    # Sharp enough to place a door (not the 256 px a description needs), its boxes asked in the pixels it's shown
    # and scaled back: told the real size of a picture shown smaller, the model guessed the scale and missed.
    seen: list = []
    chunk, (w, h) = image_chunk(file, rect, preview=1024, sent=seen)
    content = [{"type": "text", "text": FIND_PARTS.format(kinds=", ".join(kinds) or "doors")},
               {"type": "text", "text": f"The picture: {seen[0]}x{seen[1]} pixels."}, chunk]
    try:
        completion = provider.complete([{"role": "user", "content": content}])
        answer = json.loads(completion.text)
    except (ProviderError, json.JSONDecodeError) as error:
        return {"ok": False, "errors": [f"The parts couldn't be found: {error}"]}
    # Its boxes are in the pixels it was told (the picture's, or its rect's): the rect's offset added back.
    sx, sy = w / max(1, seen[0]), h / max(1, seen[1])
    ox, oy = (rect[0], rect[1]) if rect else (0, 0)
    parts = []
    for p in answer.get("parts", []) if isinstance(answer, dict) else []:
        box = p.get("box") if isinstance(p, dict) else None
        if not (isinstance(box, list) and len(box) == 4 and all(isinstance(v, (int, float)) for v in box)):
            continue
        x, y, bw, bh = box
        if bw < 2 or bh < 2:
            continue
        parts.append({"kind": str(p.get("kind", "door")), "label": str(p.get("label", "")),
                      "leaves": 2 if p.get("leaves") == 2 else 1, "shape": "arched" if p.get("shape") == "arched" else "rect",
                      "open": bool(p.get("open", False)),
                      "confidence": float(p.get("confidence", 0.5) or 0.5),
                      "box": [round(ox + x * sx), round(oy + y * sy), round(bw * sx), round(bh * sy)]})
    return {"ok": True, "parts": parts}
