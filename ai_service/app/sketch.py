"""The place sketched first, as a concept artist would (docs/build-fixes.md): one painting of the whole
place (or of one level of it), drawn by Mistral's image model (FLUX, up to about 1024 pixels) from a
description the text model writes out of the plan: its zones and rooms, every kind of thing in it in
the ontology's words, the place's look and the story's mood. The painting decides how things look,
not where they stand (the plan does that), so no reference image is given to it. The vision model then
finds one clear example of each kind of thing in the painting; cropped, it is the reference that
thing's own sprite is drawn from by the project's art provider (PixelLab, or whichever is chosen).
Every piece of the place so comes from one painting: one palette, one light, one way of drawing."""

from __future__ import annotations

import base64
import io
import json
import re
from pathlib import Path

from PIL import Image

from .providers import Provider, ProviderError

PROMPT_TASK = """You write the prompt an image model paints ONE picture from: concept art of a whole place of an ISOMETRIC 2D
game, seen in the game's own view: a true isometric view from above at about 30 degrees (2:1, the ground a diamond grid,
buildings and things showing two sides, facing down-left and down-right), as a game's map would show it. Begin the
prompt with "Isometric" and say it is for an isometric game.

In one paragraph of 150 to 230 words, concrete and visual: the place and its mood (from "place", "story" and
"look": the games it should look like); its parts and where they lie ("zones"); every kind of thing in it
("items": what it is, how it's drawn, how many), named plainly and each shown clearly at least once, not hidden
behind others; the ground, walls and paths, their materials and colours; the light (indoors by torches and
candles: dark with warm pools; outdoors one sun from the upper left); the palette (name colours). It is one
coherent hand-crafted scene in detailed pixel art, every object in one art style; no text, no UI, no frame.
Answer with JSON only: {"prompt": "<the paragraph>"}"""

LOCATE_TASK = """The image is concept art of a game place. Find in it one clear, whole example of each kind of thing in the
list (by its key), the most complete and least hidden one. Give its box in the image's pixels: x and y of its top
left corner, w and h. The box holds the WHOLE object: from the top of its roof, chimney, flame or leaves to the
bottom of its base and its shadow on the ground, and from its leftmost to its rightmost part; never cut any of it
off (a box a little too big is right, one too small is wrong). A ground (a key starting "terrain|") is the other
way round: the largest box of open ground of that kind ONLY, with nothing standing on it and no other ground in it.
Skip a kind you can't see. Answer with JSON only:
{"found": [{"key": "<key from the list>", "x": <int>, "y": <int>, "w": <int>, "h": <int>, "seen": "<what it looks like there, in a few words>"}]}"""


def _chunk(image: Image.Image, longest: int = 1400) -> tuple[dict, tuple[int, int]]:
    copy = image.convert("RGB")
    copy.thumbnail((longest, longest))
    out = io.BytesIO()
    copy.save(out, format="PNG")
    return {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(out.getvalue()).decode('ascii')}"}, copy.size


def sketch_prompt(provider: Provider | None, context: dict) -> str:
    fallback = ("Concept art of a game place in detailed pixel art, one coherent hand-crafted scene: "
                + "; ".join(f"{i.get('count', 1)} {i.get('type', '')}" for i in context.get("items", [])[:24]))
    if provider is None:
        return fallback
    try:
        reply = provider.complete([{"role": "system", "content": PROMPT_TASK},
                                   {"role": "user", "content": json.dumps(context, ensure_ascii=False)[:14000]}])
        data = json.loads(re.sub(r"\s{40,}", " ", reply.text))
        prompt = str(data.get("prompt", "")).strip() if isinstance(data, dict) else ""
        return prompt if len(prompt) > 60 else fallback
    except (ProviderError, ValueError, TypeError):
        return fallback


def locate(vision: Provider, image: Image.Image, items: list[dict]) -> list[dict]:
    """Each kind's box in the painting (the vision model's), in the painting's pixels."""
    chunk, (w, h) = _chunk(image)
    listing = [{"key": i.get("key", ""), "what": i.get("type", ""), "drawn": i.get("drawn", "")} for i in items]
    messages = [{"role": "system", "content": LOCATE_TASK},
                {"role": "user", "content": [{"type": "text", "text": json.dumps(listing, ensure_ascii=False)},
                                             {"type": "text", "text": f"The image ({w} x {h} pixels):"}, chunk]}]
    try:
        try:
            reply = vision.complete(messages, max_tokens=24000)  # Large 4 thinks first: its thinking counts
        except TypeError:
            reply = vision.complete(messages)
        data = json.loads(re.sub(r"\s{40,}", " ", reply.text))
    except (ProviderError, ValueError, TypeError):
        return []
    sx, sy = image.width / w, image.height / h  # back to the painting's own pixels
    found = []
    for f in data.get("found", []) if isinstance(data, dict) else []:
        try:
            found.append({"key": str(f["key"]), "x": float(f["x"]) * sx, "y": float(f["y"]) * sy, "w": float(f["w"]) * sx,
                          "h": float(f["h"]) * sy, "seen": str(f.get("seen", ""))})
        except (KeyError, TypeError, ValueError):
            continue
    return found


def sketch_place(painter, provider: Provider | None, vision: Provider | None, root: Path, context: dict, out_dir: str,
                 name: str) -> dict:
    """Paints the place (`painter`: an image generator taking a prompt), finds each item of
    `context["items"]` ({key, type, drawn, count}) in it and crops it. Writes <out_dir>/<name>.png and
    one crop per key found."""
    prompt = sketch_prompt(provider, context)
    try:
        painted = painter.generate(prompt)
        image = Image.open(io.BytesIO(painted.data)).convert("RGB")
    except (ProviderError, OSError, ValueError) as error:
        return {"ok": False, "errors": [f"the sketch wasn't painted: {error}"], "prompt": prompt}
    folder = root / out_dir
    folder.mkdir(parents=True, exist_ok=True)
    image.save(folder / f"{name}.png")
    items = [i for i in context.get("items", []) if i.get("key")]
    boxes = locate(vision, image, items) if vision is not None and items else []
    crops, seen = {}, {}
    keys = {i.get("key") for i in items}
    for box in boxes:
        key = box["key"]
        if key in crops or key not in keys:
            continue
        slug = re.sub(r"[^a-z0-9]+", "-", key.lower()).strip("-")[:40]
        crop_rel = f"{out_dir}/{name}-{slug}.png"
        if key.startswith("terrain|"):
            # A patch of the ground: inside its box (no neighbour), stretched back from the isometric
            # view's 2:1 squash so the painter can lay it flat again, square.
            inset_x, inset_y = box["w"] * 0.1, box["h"] * 0.1
            left, top = int(box["x"] + inset_x), int(box["y"] + inset_y)
            right, bottom = int(box["x"] + box["w"] - inset_x), int(box["y"] + box["h"] - inset_y)
            left, top, right, bottom = max(0, left), max(0, top), min(image.width, right), min(image.height, bottom)
            if right - left < 24 or bottom - top < 12:
                continue
            patch = image.crop((left, top, right, bottom))
            patch = patch.resize((patch.width, patch.height * 2), Image.LANCZOS)
            side = min(patch.width, patch.height)
            patch = patch.crop(((patch.width - side) // 2, (patch.height - side) // 2, (patch.width + side) // 2, (patch.height + side) // 2))
            patch.resize((256, 256), Image.LANCZOS).save(root / crop_rel)
            crops[key] = crop_rel
            seen[key] = box["seen"]
            continue
        # Generous: a vision box is often tight, and a crop that cuts the object off is useless as a
        # reference (the art model draws only what it sees); the neighbours it takes in are told apart.
        pad_x, pad_y = max(box["w"] * 0.3, 12), max(box["h"] * 0.3, 12)
        cx, cy = box["x"] + box["w"] / 2, box["y"] + box["h"] / 2
        half_w, half_h = max(box["w"] / 2 + pad_x, 32), max(box["h"] / 2 + pad_y, 32)
        left, top = max(0, int(cx - half_w)), max(0, int(cy - half_h))
        right, bottom = min(image.width, int(cx + half_w)), min(image.height, int(cy + half_h))
        if right - left < 12 or bottom - top < 12:
            continue
        image.crop((left, top, right, bottom)).save(root / crop_rel)
        crops[key] = crop_rel
        seen[key] = box["seen"]
    return {"ok": True, "sketch": f"{out_dir}/{name}.png", "crops": crops, "seen": seen, "prompt": prompt,
            "size": [image.width, image.height], "boxes": boxes}
