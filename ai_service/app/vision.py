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


def prompt_for(roles: list[str] | None) -> str:
    return PROMPT.replace("%ROLE%", ROLE_LIST + ", ".join(roles) if roles else ROLE_FREE)


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


def image_chunk(path: Path, rect: list[int] | None = None) -> tuple[dict, tuple[int, int]]:
    """The image (or `rect` of it: x, y, w, h) as a PNG data URI chunk, at about
    PREVIEW pixels on its longest side, and its real size. Small pixel art is
    enlarged by a whole factor, pixels kept sharp, so the model sees its shapes."""
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
                    roles: list[str] | None = None) -> tuple[list[dict], list[str]]:
    content: list[dict] = [{"type": "text", "text": prompt_for(roles)}]
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
                    items: list[dict] | None = None, roles: list[str] | None = None) -> dict:
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
        for found, problems in pool.map(lambda batch: _describe_batch(provider, root, batch, roles), batches):
            results.extend(found)
            errors.extend(problems)
    order = {item["id"]: i for i, item in enumerate(wanted)}
    results.sort(key=lambda r: order.get(r["id"], 0))
    return {"ok": bool(results) or not wanted, "assets": results, "errors": errors}
