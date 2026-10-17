"""Looking at art (development_plan.md 6.2): a vision model describes each
image of a project's assets — what it is, its kind, tags, direction, a rough
size in grid cells, whether it blocks the way, and a better file name — so
the AI chooses by meaning instead of file names like "Ground 37"."""

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
The file name and pixel size are given as hints only; names are often meaningless ("Ground 37").
Answer as JSON: {"assets": [{...}, ...]} with one entry per image."""


class AssetDescription(BaseModel):
    path: str
    name: str
    kind: str
    description: str
    tags: list[str]
    direction: Literal["none", "horizontal", "vertical"] = "none"
    sizeCells: float
    blocks: bool

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


def image_chunk(path: Path) -> tuple[dict, tuple[int, int]]:
    """A downsized PNG of the image as a data URI chunk, and its real size."""
    with Image.open(path) as source:
        size = source.size
        image = source.convert("RGBA")
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


def _describe_batch(provider: Provider, root: Path, paths: list[str]) -> tuple[list[dict], list[str]]:
    content: list[dict] = [{"type": "text", "text": PROMPT}]
    for index, rel in enumerate(paths, start=1):
        chunk, (w, h) = image_chunk(root / rel)
        content.append({"type": "text", "text": f"Image {index}: file \"{Path(rel).name}\", {w}x{h} pixels"})
        content.append(chunk)
    try:
        completion = provider.complete([{"role": "user", "content": content}])
    except ProviderError as error:
        return [], [f"{', '.join(paths)}: {error}"]
    try:
        answer = json.loads(completion.text)
        entries = answer.get("assets", []) if isinstance(answer, dict) else []
    except json.JSONDecodeError:
        return [], [f"{', '.join(paths)}: the model's answer wasn't JSON"]
    results, errors = [], []
    seen: set[int] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        index = entry.get("index")
        if not isinstance(index, int) or not 1 <= index <= len(paths) or index in seen:
            continue
        seen.add(index)
        try:
            results.append(AssetDescription(path=paths[index - 1], **{k: v for k, v in entry.items() if k != "index"})
                           .model_dump())
        except (ValidationError, TypeError) as error:
            errors.append(f"{paths[index - 1]}: {str(error).splitlines()[0]}")
    for index, rel in enumerate(paths, start=1):
        if index not in seen:
            errors.append(f"{rel}: not described")
    return results, errors


def describe_assets(provider: Provider, root: Path, paths: list[str], workers: int = 3) -> dict:
    """Describes every image in `paths` (project-relative), several per
    request and a few requests at once. Unreadable or missing files are
    reported, the rest still described."""
    images, errors = [], []
    for rel in paths:
        file = (root / rel).resolve()
        if root.resolve() not in file.parents or not file.is_file():
            errors.append(f"{rel}: not a file in the project")
        elif file.suffix.lower() not in (".png", ".jpg", ".jpeg", ".bmp", ".tga"):
            errors.append(f"{rel}: not an image")
        else:
            images.append(rel)
    batches = [images[i:i + BATCH] for i in range(0, len(images), BATCH)]
    results: list[dict] = []
    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        for found, problems in pool.map(lambda batch: _describe_batch(provider, root, batch), batches):
            results.extend(found)
            errors.extend(problems)
    order = {rel: i for i, rel in enumerate(paths)}
    results.sort(key=lambda r: order.get(r["path"], 0))
    return {"ok": bool(results) or not paths, "assets": results, "errors": errors}
