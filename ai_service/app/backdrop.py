"""Painted places (docs/build-fixes.md, the "Painted" build method): a place, or one level of it, is
one painting, a backdrop, as in Baldur's Gate or Pillars of Eternity, and the game's plan supplies what
the player walks on, its doors and the story's anchors.

1. Describe: the text model writes the image prompt from the plan: an isometric view of the place,
   every zone and building and kind of thing with where it lies (compass words and neighbours, from
   the plan's coordinates), the look, the story's mood.
2. Paint: Mistral's image model (FLUX) paints it from those words alone; any provider that can only
   paint big pictures from words will do.
3. Read: the vision model maps the painting: each building, wall, water, wood, rock and big thing
   with its box and the ground it stands on (its footprint), what can be walked on, the doors, the way
   in, and where each of the story's anchors is.

The engine (PlacePaint) turns the map into the scene: the painting at the back, an invisible walk
layer, each tall thing cut out as an occluder (characters go behind it), markers at the anchors."""

from __future__ import annotations

import base64
import io
import json
import re
from pathlib import Path

from PIL import Image

from .providers import Provider, ProviderError

DESCRIBE_TASK = """You write the prompt an image model paints ONE picture from: a whole place of an ISOMETRIC 2D game, seen
in the game's own view (a true isometric view from above at about 30 degrees, 2:1: the ground a diamond grid, every
building and thing showing two sides facing down-left and down-right). The picture IS the place in the game, so the
layout matters: the place's diamond fills the picture; north is the top corner, east the right, south the bottom,
west the left.

Write one paragraph of 160 to 230 words, starting with "Isometric". The image model follows the start of a prompt
best and loses long lists, so write it in this order:
1. The layout, first and most plainly (half the words): the ground's big lines ("ground": the river, the roads, the
   bridge: where each runs, in the compass words given), then each zone where it lies with what is in it ("zones",
   and the buildings and things of "items" with their counts and places). Say each placement as a picture: "a
   river runs from the north corner down to the south corner; a road crosses it on a wooden bridge in the middle".
2. Then the look in a few words: the materials, the light (indoors by torches and candles: dark with warm pools;
   outdoors one sun from the upper left), three or four colours by name (never hex codes), the mood of the story.
3. End with: one coherent hand-crafted scene in detailed pixel art, every object in one style; no text, no UI, no
   frame, no characters.
Name things plainly (a house, a warehouse, a well); never use the plan's ids or labels ("outskirts-ward").
Answer with JSON only: {"prompt": "<the paragraph>"}"""

READ_TASK = """The image is a place of an isometric 2D game, painted as one picture (a backdrop). Map it for the game, in the
image's pixels:
- "elements": every building, wall, water, wood (a group of trees), single big tree, rock, cliff, and big piece of
  furniture or prop (a stall, a well, a table, a sarcophagus): {"kind": "building"|"wall"|"water"|"wood"|"tree"|
  "rock"|"cliff"|"prop", "label": "<what it is, in a few words>", "box": [x, y, w, h] (the whole of it as drawn),
  "footprint": [x, y, w, h] (the ground it stands on: for a building its base, not its roof), "walkable": false}.
  Paths, roads, squares, floors and open ground are not elements.
- "doors": each door or way into a building or room: {"label": "...", "at": [x, y]} (on the ground in front of it).
- "entrance": [x, y], where a traveller arrives (a road at the edge, a gate, a stair in).
- "anchors": for each anchor in the list given (by its id), the spot on open ground where it is: {"<id>": [x, y]}.
- "seen": each kind of thing in the list given (by its key) you can see, and how many: {"<key>": <count>}.
Answer with JSON only: {"elements": [...], "doors": [...], "entrance": [x, y], "anchors": {...}, "seen": {...}}"""


def _chunk(image: Image.Image, longest: int = 1400) -> tuple[dict, tuple[int, int]]:
    copy = image.convert("RGB")
    copy.thumbnail((longest, longest))
    out = io.BytesIO()
    copy.save(out, format="PNG")
    return {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(out.getvalue()).decode('ascii')}"}, copy.size


def _json(text: str) -> dict | None:
    try:
        data = json.loads(re.sub(r"\s{40,}", " ", text))
    except (ValueError, TypeError):
        return None
    return data if isinstance(data, dict) else None


def describe(provider: Provider | None, context: dict) -> str:
    fallback = ("Isometric pixel-art game place, one coherent hand-crafted scene: "
                + "; ".join(f"{i.get('count', 1)} {i.get('type', '')} {i.get('where', '')}" for i in context.get("items", [])[:30]))
    if provider is None:
        return fallback
    try:
        reply = provider.complete([{"role": "system", "content": DESCRIBE_TASK},
                                   {"role": "user", "content": json.dumps(context, ensure_ascii=False)[:16000]}])
    except ProviderError:
        return fallback
    data = _json(reply.text)
    prompt = str(data.get("prompt", "")).strip() if data else ""
    return prompt if len(prompt) > 80 else fallback


def read(vision: Provider, image: Image.Image, context: dict) -> dict:
    """The painting's map, in its own pixels."""
    chunk, (w, h) = _chunk(image)
    asked = {"anchors": context.get("anchors", []), "items": [{"key": i.get("key"), "what": i.get("type")} for i in context.get("items", [])]}
    messages = [{"role": "system", "content": READ_TASK},
                {"role": "user", "content": [{"type": "text", "text": json.dumps(asked, ensure_ascii=False)},
                                             {"type": "text", "text": f"The image ({w} x {h} pixels):"}, chunk]}]
    try:
        try:
            reply = vision.complete(messages, max_tokens=24000)  # Large 4 thinks first: its thinking counts
        except TypeError:
            reply = vision.complete(messages)
    except ProviderError as error:
        return {"error": str(error)}
    data = _json(reply.text) or {}
    s = image.width / w  # back to the painting's own pixels

    def box(v):
        try:
            return [float(v[0]) * s, float(v[1]) * s, float(v[2]) * s, float(v[3]) * s]
        except (TypeError, ValueError, IndexError):
            return None

    def point(v):
        try:
            return [float(v[0]) * s, float(v[1]) * s]
        except (TypeError, ValueError, IndexError):
            return None

    elements = []
    for e in data.get("elements", []) if isinstance(data.get("elements"), list) else []:
        if not isinstance(e, dict) or box(e.get("box")) is None:
            continue
        elements.append({"kind": str(e.get("kind", "prop")), "label": str(e.get("label", "")), "box": box(e.get("box")),
                         "footprint": box(e.get("footprint")) or box(e.get("box")), "walkable": bool(e.get("walkable", False))})
    doors = [{"label": str(d.get("label", "")), "at": point(d.get("at"))} for d in data.get("doors", []) if isinstance(d, dict) and point(d.get("at"))]
    anchors = {}
    for k, v in (data.get("anchors") or {}).items() if isinstance(data.get("anchors"), dict) else []:
        if point(v):
            anchors[str(k)] = point(v)
    seen = {str(k): v for k, v in (data.get("seen") or {}).items()} if isinstance(data.get("seen"), dict) else {}
    return {"elements": elements, "doors": doors, "entrance": point(data.get("entrance")), "anchors": anchors, "seen": seen}


def paint_backdrop(painter, provider: Provider | None, vision: Provider | None, root: Path, context: dict, out: str) -> dict:
    """Describes, paints and reads one backdrop; writes <out> (a PNG, relative to the project) and
    <out>.json (its map)."""
    prompt = describe(provider, context)
    try:
        painted = painter.generate(prompt)
        image = Image.open(io.BytesIO(painted.data)).convert("RGB")
    except (ProviderError, OSError, ValueError) as error:
        return {"ok": False, "errors": [f"the backdrop wasn't painted: {error}"], "prompt": prompt}
    target = (root / out).resolve()
    if root.resolve() not in target.parents:
        return {"ok": False, "errors": [f"{out} is outside the project"]}
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target)
    reading = read(vision, image, context) if vision is not None else {"error": "no vision model"}
    result = {"ok": True, "backdrop": out, "size": [image.width, image.height], "prompt": prompt, "map": reading}
    target.with_suffix(".json").write_text(json.dumps(result, indent=1, ensure_ascii=False), encoding="utf-8")
    return result
