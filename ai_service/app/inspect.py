"""A vision model checks each step of a place's build (docs/build-fixes.md): the layout (its zones),
the plan (every thing placed, on a labelled map) and the built scene (as the game draws it, in
windows when it's big). Every check works the same way:

1. identify the elements in the image (zones for a layout; each kind of thing in a plan or build);
2. judge each one's position, in the whole place and against the elements beside it;
3. judge its visual fit, with the whole scene and with what stands near it;
4. judge its visual quality (how it looks, whether it is what the story describes).

The answer is structured, so the engine can act on it (revise the layout or plan, refuse a piece of
art and find or make another) and runs can be compared by their scores."""

from __future__ import annotations

import json
import re
from concurrent.futures import ThreadPoolExecutor

from .places import _parse
from .models import Usage
from .providers import Provider, ProviderError

SHAPE = """Answer with one JSON object:
{
  "identified": [{"type": "<the element: a zone's label or an item type from the list>", "count": <how many you see>,
                  "where": "<where in the image, in a few words>"}],
  "placement": [{"type": "<element>", "ok": true|false, "score": <0-10>, "why": "<its position in the whole place and next to the
                 elements around it: right or wrong, and why>", "fix": "<what to change, if wrong>"}],
  "fit": [{"type": "<element>", "ok": true|false, "score": <0-10>, "why": "<does it fit the place, its story and the elements beside it>"}],
  "quality": [{"type": "<element>", "ok": true|false, "score": <0-10>, "why": "<how it looks>"}],
  "missing": ["<an element that should be there and isn't>"],
  "problems": [{"severity": "error"|"warning", "target": "<element, or 'layout' / 'scene'>", "issue": "<what is wrong,
                in one sentence>", "fix": "<what to do, concretely>"}],
  "scores": {"layout": <0-10>, "placement": <0-10>, "fit": <0-10>, "quality": <0-10>},
  "summary": "<two sentences: how close this is to a hand-crafted game place, and the most important fix>"
}
Every element you identify gets a placement entry; fit and quality entries are for things you can see drawn.
Name each kind exactly as the layout's list names it (its type), one entry per kind: never group kinds in
one entry ("debris/bones"), never one entry per object. Be brief: each "why", "where" and "fix" under 25 words,
at most 12 problems (the most important first).
Score as a strict art director: 10 is a place from a shipped, hand-crafted game; 5 is passable but generic or
odd; below 4 is clearly wrong. An error is something a player would notice at once (a door in the middle of a
room, a sprite sheet standing in for a barrel, a part of the place nobody can reach); a warning is a lesser flaw."""

ROLE = ("You are the lead level designer and art director of a studio that makes hand-crafted 2D games. You judge "
        "what a procedural builder made against what a human designer would have made.")

TASKS = {
    "layout": (
        "The image is the LAYOUT of a game place: a top-down schematic map of its zones (coloured, outlined and "
        "labelled), with the ground (roads, water, rock) under them. The colours, outlines, labels and legend are the "
        "map's notation, not the game's art: judge the arrangement, never how the map is drawn. Identify every zone by its label. For each, judge its "
        "position: does it make sense for what the zone is, for the place's kind and story, and next to the zones "
        "around it (a market on the main road at the heart of a town, a graveyard at the edge, docks on the water, "
        "a dungeon's boss room deepest from its entrance, a kitchen next to the hall it serves)? Are the sizes right "
        "for each zone's purpose? Can every zone be reached (roads, corridors, doors), with no part cut off? Is "
        "anything the Brief or story asks for missing, or is a stretch of the map empty for no reason? Fit and "
        "quality entries aren't needed for a layout; score 'fit' and 'quality' by how well the zones serve the "
        "story. This step is ONLY the zones (and, inside, rooms or plots with their walls): the things in them "
        "(furniture, props, lights, decoration) are placed in the next step, so never count them as missing. A "
        "building with several floors has each floor drawn as its own area, side by side (they stack in the "
        "game, joined by stairs): that's not an error. Each planned zone's module says what it is ('is')."),
    "plan": (
        "The image is the PLAN of a game place: a top-down schematic map of everything placed in it. Each kind of "
        "thing has its own colour and a code drawn on it, listed in the legend. The colours, codes, outlines, plot "
        "lines and legend are the map's notation, not the game's art (the art comes later): judge where things are "
        "and how they're arranged, never how the map is drawn. Identify each item type you can see (by the "
        "legend) and count them. For each type, judge its placement: on its own (a bed against a wall, a door in a "
        "wall, a torch on a wall, an altar at the end of a shrine, a well in a square, crops in a field, a stall on "
        "a street) and relative to the other things (chairs around tables, stalls in a row facing the street, "
        "barrels and crates stacked together near a storeroom door, graves in rows, nothing blocking a door or a "
        "path). Say which types are placed well and which aren't, and why; say what's missing that a designer "
        "would add, and what's there too often (a unique thing repeated, the same prop scattered everywhere). "
        "Score 'fit' by whether each thing belongs in this place and room, and 'quality' by how purposeful and "
        "readable the arrangement is."),
    "build": (
        "The image is part of a game place as the game draws it (isometric pixel art). Identify every object you "
        "can see (the list of what was placed in this window is given, with positions, to help you name them; say "
        "if you see something that isn't what it was meant to be). For each type, judge: (1) its placement, in the "
        "whole scene and next to the objects around it; (2) its visual fit, with the whole scene and with its "
        "neighbours: the same art style, palette, outline, perspective and pixel size, the right scale against a "
        "door or a person, the right era and mood for the place's story; (3) its visual quality: a clean single "
        "object, not a sprite sheet or a collection of small images, not on a white or coloured box, not cut off, "
        "not blurry, and recognisably what it should be. Plain flat-coloured shapes are placeholders: list them as "
        "problems. Judge the ground too: continuous, without visible tile seams or a grid. A building's floors may "
        "be drawn as separate areas side by side (they stack in the game, joined by stairs), and walls in front of "
        "a room are left out so the room shows (as in Diablo): neither is an error."),
    "match": (
        "Three images of ONE game place, in order: (1) its LAYOUT, a top-down schematic map of its zones; (2) its PLAN, "
        "a top-down schematic map of everything placed in it (codes in the legend); (3) its FINAL BUILD as the game "
        "draws it (isometric: the schematic's top-left corner is the painting's top corner, its top-right edge runs "
        "down to the painting's right corner, its bottom-left edge to the left corner). The maps' colours, codes and "
        "labels are notation. Judge whether the final build IS the place the maps plan: each zone where the layout puts "
        "it, each building and each kind of thing where the plan puts it and about as many, nothing big that the maps "
        "don't have; and whether it tells the place's story and matches the descriptions given (its mood, its look, "
        "the zones' and things' descriptions). In 'placement' give one entry per zone and per item type: ok when the "
        "final build has it in the same part of the place, its score how close; in 'why' say where it is in each "
        "(\"layout: north-west; built: north-west\"). 'fit' entries: how each zone or kind of thing matches its "
        "description and the story. Score 'layout' by how well the final build follows the layout's zones, "
        "'placement' by how well it follows the plan's things, 'fit' by how well it matches the story and the "
        "descriptions, 'quality' by how hand-crafted it looks."),
    "audit": (
        "Two images of ONE game place, in order: (1) its PLAN, a top-down schematic map of everything placed in it "
        "(codes in the legend; plot and field edges drawn as lines); (2) its RENDER as the game draws it (isometric: "
        "the plan's top-left corner is the render's top corner, its top-right edge runs down to the render's right "
        "corner). Find EVERY concrete thing drawn wrong in the render, as a careful level designer would before it "
        "ships: fences that don't run along a plot, garden or field edge (crossing a road or path, cutting through a "
        "yard, stray short pieces, gaps where a gate isn't, the wrong way round); walls with holes or doubled; "
        "buildings on a coloured base or pad, floating, overlapping, facing nothing, the wrong kind for their zone "
        "(houses where warehouses belong); doors not on a road or path; crops or beds outside their field; trees, "
        "rocks or props on a road, in water or in a doorway; bridges not over the water; ground in visible tiles, "
        "seams, a grid or a repeated pattern; sprites at the wrong scale; anything the plan has that the render "
        "lacks, or the render has that the plan doesn't. Each is one problem: 'target' names the thing, 'issue' "
        "says what is wrong and WHERE (compass words: north is the render's top corner), 'fix' says exactly how it "
        "should look instead. List up to 20, the worst first; 'identified', 'placement', 'fit' and 'quality' may "
        "stay empty. Score 'placement' by how right the things are placed, 'quality' by how cleanly drawn."),
}

FIXES_SHAPE = """Answer with one JSON object:
{
  "fixes": [{"issue": "<the issue, as given>", "fixed": true|false, "why": "<what the second image shows there, under 25 words>"}],
  "new": [{"severity": "error"|"warning", "target": "<thing>", "issue": "<a mistake the second image has that the first didn't>"}],
  "scores": {"fixed": <0-10, the share of the issues fixed>, "kept": <0-10, how exactly the layout is kept>},
  "summary": "<two sentences>"
}
One entry in "fixes" per issue given, in order."""

APPEAL_SHAPE = """Answer with one JSON object:
{"appeal": <0-10>, "modern": <0-10>, "polish": <0-10>, "readability": <0-10>, "why": "<two sentences: what makes it attractive or not>"}"""

APPEAL_TASK = (
    "The image is a place of an isometric 2D game. Judge ONLY how visually appealing it is, as a player and an "
    "art director would at first sight, not whether it matches any plan: 'appeal' overall; 'modern': does it look "
    "like a current, high-end indie or AA release (rich lighting, depth, texture, a strong art direction) or dated "
    "and flat; 'polish': clean, consistent and finished, no artefacts, muddy areas or repeated stamps; "
    "'readability': can you tell at once what everything is (buildings, paths, water, props). Score strictly: 10 is "
    "a showcase image of a shipped game; 5 passable; below 4 unattractive.")

KINDS_SHAPE = """Answer with one JSON object:
{"crops": [{"n": <its number>, "is": "<one of the kinds listed, or 'other: <what it is>', or 'nothing'>", "sure": <0-10>}]}
One entry per numbered crop, in order."""

KINDS_TASK = (
    "The image is a contact sheet of numbered crops cut from one painting of an isometric game place. Each crop is "
    "centred on one building or thing. Say what the thing in the MIDDLE of each crop is, as a player would name "
    "it: one of the kinds listed when it clearly is one, else 'other: <what it is>' (a market stall, a tent, a "
    "barn), or 'nothing' when the middle is only ground, trees or water. Judge only the middle thing, not its "
    "neighbours.")

FIXES_TASK = (
    "Two images of ONE isometric game place: (1) the draft render, (2) its repaint. The draft had the mistakes listed. "
    "For each, look at the same spot in the repaint and say whether it is fixed. Then list mistakes the repaint "
    "made that the draft didn't have (things moved, removed or invented, buildings off the ground, new wrong "
    "fences). Judge strictly: a mistake only restyled, still in the same wrong place, is NOT fixed.")


def _text(value: object, limit: int = 6000) -> str:
    text = json.dumps(value, ensure_ascii=False) if not isinstance(value, str) else value
    return text if len(text) <= limit else text[:limit] + " …"


def _inputs(request: dict, window: dict | None = None) -> str:
    parts = []
    look = request.get("look") or {}
    if look:
        parts.append("What this kind of place should look like (the goal): " + _text(look, 2000))
    if request.get("story"):
        parts.append("The place in the game's story: " + _text(request["story"], 3000))
    if request.get("brief"):
        parts.append("Its Brief (what was asked): " + _text(request["brief"], 3000))
    expected = request.get("expected") or {}
    if expected.get("zones"):
        parts.append("The zones planned: " + _text(expected["zones"], 4000))
    if expected.get("items"):
        parts.append("The item types placed, from the place's layout (type, how many, in which zones and rooms; 'ontology': "
                     "what the game's ontology says each is, how it's drawn, where it belongs ('belongs': wall, corner or "
                     "centre; 'near': beside what), what it's designed against, and the set it's part of with that set's "
                     "placement rules). Judge each placement against these definitions: " + _text(expected["items"], 9000))
    if expected.get("legend"):
        parts.append("The map's legend (code: item type): " + _text(expected["legend"], 3000))
    if request.get("issues"):
        parts.append("The draft's mistakes: " + _text(request["issues"], 6000))
    if window:
        parts.append(f"This image shows the window {window.get('label', '')} of the place; what was placed in it: "
                     + _text(window.get("items", []), 5000))
    return "\n\n".join(parts)


def _ask(provider: Provider, kind: str, image: dict, request: dict, window: dict | None = None) -> tuple[dict | None, list[str], Usage]:
    usage = Usage()
    system = f"{ROLE}\n\n{TASKS.get(kind, '')}\n\n{SHAPE}\n\nAnswer with the JSON object only."
    if kind == "fixes":
        system = f"{ROLE}\n\n{FIXES_TASK}\n\n{FIXES_SHAPE}\n\nAnswer with the JSON object only."
    if kind == "kinds":
        system = f"{ROLE}\n\n{KINDS_TASK}\n\n{KINDS_SHAPE}\n\nAnswer with the JSON object only."
    if kind == "appeal":
        system = f"{ROLE}\n\n{APPEAL_TASK}\n\n{APPEAL_SHAPE}\n\nAnswer with the JSON object only."
    if isinstance(image, list):  # several images of one place, read together ("match", "audit", "fixes")
        names = {"audit": ["The plan:", "The render:"], "fixes": ["The draft:", "The repaint:"]}.get(
            kind, ["The layout:", "The plan:", "The final build:"])
        content = [{"type": "text", "text": _inputs(request, window)}]
        for i, one in enumerate(image):
            content += [{"type": "text", "text": names[i] if i < len(names) else "Another image:"}, one]
    else:
        content = [{"type": "text", "text": _inputs(request, window)}, {"type": "text", "text": "The image:"}, image]
    problem = ""
    for _attempt in range(2):  # an answer that isn't JSON (cut short, or chatty) is asked for once more
        messages = [{"role": "system", "content": system}, {"role": "user", "content": content}]
        try:
            try:
                reply = provider.complete(messages, max_tokens=24000)  # Large 4 reasons first: its thinking counts
            except TypeError:
                reply = provider.complete(messages)
        except ProviderError as error:
            return None, [str(error)], usage
        usage.add(reply.usage)
        # A model in JSON mode can pour out whitespace without end: collapsed before it's read.
        text = re.sub(r"\s{40,}", " ", reply.text)
        answer, problem = _parse(text)
        if answer is not None and isinstance(answer, dict):
            return answer, [], usage
        problem = f"{problem or 'not an object'} (it began: {reply.text[:200]!r}, {len(reply.text)} characters)"
    return None, [problem], usage


def _clean(answer: dict) -> dict:
    """The answer in its shape, whatever the model wrote."""
    def entries(key: str) -> list[dict]:
        value = answer.get(key)
        return [e for e in value if isinstance(e, dict)] if isinstance(value, list) else []
    scores = answer.get("scores") if isinstance(answer.get("scores"), dict) else {}
    clean_scores = {}
    for k in ("layout", "placement", "fit", "quality"):
        try:
            clean_scores[k] = max(0.0, min(10.0, float(scores.get(k))))
        except (TypeError, ValueError):
            pass
    missing = answer.get("missing")
    return {"identified": entries("identified"), "placement": entries("placement"), "fit": entries("fit"),
            "quality": entries("quality"),
            "missing": [str(m) for m in missing] if isinstance(missing, list) else [],
            "problems": [p for p in entries("problems") if p.get("issue")],
            "scores": clean_scores, "summary": str(answer.get("summary", ""))}


def _merge(parts: list[tuple[dict, dict]]) -> dict:
    """Windows of one scene, as one answer: everything listed (each with its window), scores averaged."""
    merged = {"identified": [], "placement": [], "fit": [], "quality": [], "missing": [], "problems": [], "scores": {},
              "summary": "", "windows": []}
    totals: dict[str, list[float]] = {}
    for window, answer in parts:
        label = window.get("label", "")
        for key in ("identified", "placement", "fit", "quality", "problems"):
            for e in answer[key]:
                merged[key].append({**e, "window": label})
        merged["missing"] += [m for m in answer["missing"] if m not in merged["missing"]]
        for k, v in answer["scores"].items():
            totals.setdefault(k, []).append(v)
        merged["windows"].append({"window": label, "scores": answer["scores"], "summary": answer["summary"]})
    merged["scores"] = {k: round(sum(v) / len(v), 2) for k, v in totals.items() if v}
    merged["summary"] = " ".join(w["summary"] for w in merged["windows"] if w["summary"])[:1500]
    return merged


def inspect_place(provider: Provider, kind: str, images: list[dict], request: dict, windows: list[dict] | None = None) -> dict:
    """Checks one step: `images` are image chunks (one, or one per window of a big scene, in the order of
    `windows`). Windows are asked at once, a few at a time."""
    if kind not in TASKS and kind not in ("fixes", "appeal", "kinds"):
        return {"ok": False, "errors": [f"unknown check '{kind}'"]}
    usage = Usage()
    errors: list[str] = []
    if kind in ("appeal", "kinds"):
        answer, problems, used = _ask(provider, kind, images[0], {"issues": request.get("issues")} if kind == "kinds" else {})
        usage.add(used)
        if answer is None:
            return {"ok": False, "errors": problems, "usage": usage.model_dump()}
        out = answer
    elif kind in ("match", "audit", "fixes"):
        answer, problems, used = _ask(provider, kind, images, request)
        usage.add(used)
        if answer is None:
            return {"ok": False, "errors": problems, "usage": usage.model_dump()}
        out = answer if kind == "fixes" else _clean(answer)
    elif windows and len(windows) == len(images) and len(images) > 1:
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda pair: _ask(provider, kind, pair[0], request, pair[1]), zip(images, windows)))
        parts = []
        for window, (answer, problems, used) in zip(windows, results):
            usage.add(used)
            errors += problems
            if answer is not None:
                parts.append((window, _clean(answer)))
        if not parts:
            return {"ok": False, "errors": errors, "usage": usage.model_dump()}
        out = _merge(parts)
    else:
        answer, problems, used = _ask(provider, kind, images[0], request, windows[0] if windows else None)
        usage.add(used)
        errors += problems
        if answer is None:
            return {"ok": False, "errors": errors, "usage": usage.model_dump()}
        out = _clean(answer)
    out.update({"ok": True, "kind": kind, "errors": errors, "usage": usage.model_dump()})
    return out



def check_painting(provider: Provider, root, painting: str, legend: dict) -> dict:
    """A place's painting checked against its plan's block-out (the Painted method): each building of the plan cut
    from the painting where the block-out drew it, named by the vision model and compared with the plan's kind; and
    how the painting looks (appeal). `legend` is the block-out's colour key with its boxes and size."""
    import base64
    import io
    from pathlib import Path
    from PIL import Image, ImageDraw
    image = Image.open(Path(root) / painting).convert("RGB")
    placed = [p for p in legend.get("placed", []) if p.get("layer") == "building"]
    bw, bh = legend.get("size", [image.width, image.height])
    sx, sy = image.width / bw, image.height / bh

    def chunk(im):
        copy = im.copy()
        copy.thumbnail((1600, 1600))
        out = io.BytesIO()
        copy.save(out, format="PNG")
        return {"type": "image_url", "image_url": f"data:image/png;base64,{base64.b64encode(out.getvalue()).decode('ascii')}"}

    result: dict = {"ok": True, "painting": painting}
    if placed:
        cell, cols = 256, 6
        sheet = Image.new("RGB", (cols * cell, ((len(placed) + cols - 1) // cols) * cell), (20, 20, 24))
        draw = ImageDraw.Draw(sheet)
        for i, p in enumerate(placed):
            x, y, w, h = p["box"]
            cx, cy, half = x + w / 2, y + h / 2, max(48, max(w, h) * 0.6)
            crop = image.crop((int((cx - half) * sx), int((cy - half) * sy), int((cx + half) * sx), int((cy + half) * sy))).resize((cell - 8, cell - 8))
            sheet.paste(crop, ((i % cols) * cell + 4, (i // cols) * cell + 4))
            draw.rectangle(((i % cols) * cell + 4, (i // cols) * cell + 4, (i % cols) * cell + 40, (i // cols) * cell + 30), fill=(0, 0, 0))
            draw.text(((i % cols) * cell + 9, (i // cols) * cell + 9), str(i + 1), fill=(255, 255, 0))
        kinds = sorted({p["kind"] for p in placed})
        answer = inspect_place(provider, "kinds", [chunk(sheet)], {"issues": [{"kinds": kinds}]})
        seen = {int(x.get("n", 0)): str(x.get("is", "")) for x in answer.get("crops", []) if isinstance(x, dict)}
        right = sum(seen.get(i + 1, "").strip().lower() == p["kind"] for i, p in enumerate(placed))
        result["buildings"] = [right, len(placed)]
        result["wrong"] = [f"{p['kind']} -> {seen.get(i + 1, '?')}" for i, p in enumerate(placed) if seen.get(i + 1, "").strip().lower() != p["kind"]]
    result["appeal"] = inspect_place(provider, "appeal", [chunk(image)], {})
    return result
