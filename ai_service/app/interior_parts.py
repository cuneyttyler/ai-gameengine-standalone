"""The moving things in a building's interior (docs/places/interior-objects.md): each floor's picture shown to the vision
model with the interior objects the ontology says such a building holds (data/places/roles.json: found_in, animate),
which ones it sees and where; then the LLM writes, for each, how it moves: the motion the animator is given, its clip,
and what plays it. The editor animates each one (/animations/generate) and puts it on its floor (an AnimatedPart)."""

from __future__ import annotations

import json
import re
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .providers import Provider, ProviderError
from .vision import image_chunk

MAX_PER_FLOOR = 12

FIND = """This picture is floor {floor} of a building from an isometric 2D game: {building}. It is drawn cut away (the walls
in front and the floors above removed) so its rooms and their furniture show.

Find the things in it that could move when the game animates them. Only these kinds (use the id before the colon as
"kind"):
{kinds}

Give each one you can clearly see, each on its own (three candles: three; a candle on a table: the candle). Its box in the
picture's pixels (x and y of its top-left corner, its width and height), tight round the thing with what moves in it (a
fireplace: the whole fireplace with its fire; a ceiling fan: all its blades). "label": what it looks like here, in a few
words ("a stone fireplace with a pot hanging over the fire"). "confidence": how sure you are, 0 to 1. Leave out what is
too small to see or only guessed. None: an empty list. Answer as JSON:
{{"parts": [{{"kind": "fireplace", "label": "a stone fireplace with a lit fire", "box": [x, y, w, h], "confidence": 0.8}}]}}"""

MOTIONS = """A building in a 2D isometric game: {building}{description}. These things were found in its interior pictures, to
be animated where they stand (each is cut out of the picture as drawn, animated, and put back over it):
{parts}

For each one, write how it moves, choosing the suggested animation that fits what is drawn (or a better one when none
fits what the label says):
- "motion": one or two sentences for a picture animator: what moves and how, in this thing's own terms (its label),
  and that the rest stays as drawn; nothing new appears, no people or hands. A motion that plays on and on loops
  smoothly. Keep it small: things move in place.
- "clip": one lower-case word naming the motion (burn, flicker, spin, open).
- "opens": "always" (plays on and on: a fire, a clock, a fan, a fish tank), "use" (the player uses it: a cupboard, a
  stove to cook on, a piano) or "auto" (as someone comes near).
- "prompt": for "use", what the player is shown, two or three words ("Cook", "Play the piano").
- "duration": for "use", how many seconds it plays before it stops by itself (0: until used again).
- "animate": false when nothing in it would visibly move as drawn (an unlit candle, a switched-off screen).
Answer as JSON: {{"parts": [{{"n": 1, "motion": "...", "clip": "burn", "opens": "always", "prompt": "", "duration": 0,
"animate": true}}]}}"""


def _kinds_text(candidates: list[dict]) -> str:
    lines = []
    for c in candidates:
        what = c.get("draw") or c.get("label") or c.get("id")
        lines.append(f"- {c.get('id')}: {c.get('label', c.get('id'))} ({what})")
    return "\n".join(lines)


def _overlap(a: list[int], b: list[int]) -> float:
    ix = max(0, min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0]))
    iy = max(0, min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1]))
    return ix * iy / max(1, min(a[2] * a[3], b[2] * b[3]))


def find_objects(provider: Provider, root: Path, path: str, floor: int, building: str, candidates: list[dict],
                 rect: list[int] | None = None) -> dict:
    """The interior objects seen on one floor's picture (or `rect` of it): kind (a candidate's id), label, box in the
    picture's own pixels, confidence."""
    file = (root / path).resolve()
    if root.resolve() not in file.parents or not file.is_file():
        return {"ok": False, "errors": [f"No picture {path}"]}
    known = {str(c.get("id")) for c in candidates}
    seen: list = []
    chunk, (w, h) = image_chunk(file, rect, preview=1024, sent=seen)
    content = [{"type": "text", "text": FIND.format(floor=floor, building=building or "a building", kinds=_kinds_text(candidates))},
               {"type": "text", "text": f"The picture: {seen[0]}x{seen[1]} pixels."}, chunk]
    try:
        answer = json.loads(provider.complete([{"role": "user", "content": content}]).text)
    except (ProviderError, json.JSONDecodeError, ValueError) as error:
        return {"ok": False, "errors": [f"Floor {floor}'s things couldn't be found: {error}"]}
    sx, sy = w / max(1, seen[0]), h / max(1, seen[1])
    ox, oy = (rect[0], rect[1]) if rect else (0, 0)
    parts: list[dict] = []
    for p in answer.get("parts", []) if isinstance(answer, dict) else []:
        if not isinstance(p, dict) or str(p.get("kind", "")) not in known:
            continue
        box = p.get("box")
        if not (isinstance(box, list) and len(box) == 4 and all(isinstance(v, (int, float)) for v in box)):
            continue
        x, y, bw, bh = box
        if bw < 3 or bh < 3:
            continue
        try:
            confidence = float(p.get("confidence", 0.5) or 0.5)
        except (TypeError, ValueError):
            confidence = 0.5
        box = [round(ox + x * sx), round(oy + y * sy), round(bw * sx), round(bh * sy)]
        # Inside the picture, and the same thing seen twice kept once.
        box[0], box[1] = max(ox, min(box[0], w + ox - 2)), max(oy, min(box[1], h + oy - 2))
        box[2], box[3] = max(2, min(box[2], w + ox - box[0])), max(2, min(box[3], h + oy - box[1]))
        if any(q["kind"] == p["kind"] and _overlap(q["box"], box) > 0.6 for q in parts):
            continue
        parts.append({"floor": floor, "kind": str(p["kind"]), "label": str(p.get("label", ""))[:120], "box": box,
                      "confidence": confidence})
    parts.sort(key=lambda q: -q["confidence"])
    return {"ok": True, "parts": parts[:MAX_PER_FLOOR]}


def _default_motion(candidate: dict | None) -> dict:
    animations = (candidate or {}).get("animate") or []
    first = animations[0] if animations and isinstance(animations[0], dict) else {}
    return {"motion": first.get("motion", "it moves gently in place"), "clip": first.get("clip", "idle"),
            "opens": first.get("opens", "always"), "prompt": first.get("prompt", ""),
            "duration": float(first.get("duration", 0) or 0), "animate": True}


def write_motions(provider: Provider | None, building: str, description: str, parts: list[dict],
                  candidates: dict[str, dict]) -> list[dict]:
    """Each part with how it moves: the LLM's choice from its kind's animations, fitted to what is drawn (its label);
    its kind's first animation when the LLM can't answer."""
    out = [{**p, **_default_motion(candidates.get(p["kind"])), "source": "ontology"} for p in parts]
    if provider is None or not parts:
        return out
    listed = []
    for n, p in enumerate(parts, 1):
        suggested = [{k: a.get(k) for k in ("clip", "motion", "opens", "prompt", "duration") if a.get(k) not in (None, "")}
                     for a in (candidates.get(p["kind"], {}).get("animate") or []) if isinstance(a, dict)]
        listed.append(f"{n}. {p['kind']} on floor {p['floor']}: {p.get('label') or p['kind']}; suggested: {json.dumps(suggested)}")
    text = MOTIONS.format(building=building or "a building", description=f" ({description})" if description else "",
                          parts="\n".join(listed))
    try:
        answer = json.loads(provider.complete([{"role": "user", "content": text}]).text)
    except (ProviderError, json.JSONDecodeError, ValueError, AttributeError):
        return out
    for item in answer.get("parts", []) if isinstance(answer, dict) else []:
        if not isinstance(item, dict):
            continue
        try:
            n = int(item.get("n", 0))
        except (TypeError, ValueError):
            continue
        if not 1 <= n <= len(out):
            continue
        part = out[n - 1]
        motion = str(item.get("motion", "")).strip()
        if motion:
            part["motion"] = motion[:400]
            part["source"] = "llm"
        clip = re.sub(r"[^a-z]", "", str(item.get("clip", "")).lower())
        if clip:
            part["clip"] = clip[:20]
        if item.get("opens") in ("always", "use", "auto"):
            part["opens"] = item["opens"]
        part["prompt"] = str(item.get("prompt", part["prompt"]) or "")[:40] if part["opens"] == "use" else ""
        try:
            part["duration"] = max(0.0, min(30.0, float(item.get("duration", part["duration"]) or 0))) if part["opens"] == "use" else 0.0
        except (TypeError, ValueError):
            pass
        if item.get("animate") is False:
            part["animate"] = False
    return out


def interior_parts(vision: Provider, text: Provider | None, root: Path, floors: list[str], candidates: list[dict],
                   building: str = "", description: str = "", rects: list | None = None) -> dict:
    """Every floor looked at (in parallel), then one LLM call for all the things found: [{floor, kind, label, box,
    confidence, motion, clip, opens, prompt, duration, animate, source}]."""
    usable = [c for c in candidates if isinstance(c, dict) and c.get("id")]
    if not usable:
        return {"ok": False, "errors": ["The ontology names no moving things for this kind of building"]}
    rects = rects or []

    def one(k: int) -> dict:
        rect = rects[k] if k < len(rects) and isinstance(rects[k], list) and len(rects[k]) == 4 else None
        return find_objects(vision, root, floors[k], k + 1, building, usable, rect)
    with ThreadPoolExecutor(4) as pool:
        found = list(pool.map(one, range(len(floors[:4]))))
    errors = [e for f in found if not f.get("ok") for e in f.get("errors", [])]
    parts = [p for f in found if f.get("ok") for p in f["parts"]]
    if not parts and errors:
        return {"ok": False, "errors": errors}
    by_id = {str(c["id"]): c for c in usable}
    return {"ok": True, "parts": write_motions(text, building, description, parts, by_id), "warnings": errors}
