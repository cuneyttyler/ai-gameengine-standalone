"""Black Forest Labs (https://api.bfl.ai): image models that repaint an image from words and keep its layout,
so a place built by code (every building, road and river where the plan puts it) can be repainted as one
hand-crafted picture without anything moving.

Models (POST /v1/<model>, then the polling URL):
- flux-kontext-pro / flux-kontext-max: about 1 MP; `input_image`, `aspect_ratio`.
- flux-2-klein-4b / flux-2-klein-9b / flux-2-pro / flux-2-flex / flux-2-max: up to 4 MP; `input_image`,
  `input_image_2`… (references), exact `width` and `height`.
- flux-3-image-klein: up to 16.8 MP (`resolution` "4k"); `images` (the first is the one edited).
- flux-3-image: up to 4 MP with references."""

from __future__ import annotations

import base64
import json
import random
import time

import httpx
import numpy as np

from .providers import ProviderError
# Tiling pipeline removed from production; painting uses a placeholder.
from .style_refs import generate_style_refs

BASE = "https://api.bfl.ai/v1"


def _b64(png: bytes) -> str:
    return base64.b64encode(png).decode("ascii")


class BflPainter:
    def __init__(self, api_key: str, model: str = "flux-kontext-pro", timeout_s: float = 300.0):
        self.api_key = api_key
        self.model = model
        self.timeout_s = timeout_s

    def body(self, image_png: bytes, prompt: str, references: list[bytes] | None = None, width: int | None = None,
             height: int | None = None, aspect_ratio: str | None = None, resolution: str | None = None,
             seed: int | None = None) -> dict:
        refs = references or []
        body: dict = {"prompt": prompt, "output_format": "png"}
        if self.model.startswith("flux-3"):
            body["images"] = [_b64(image_png)] + [_b64(r) for r in refs]
            if width and height:
                body["width"], body["height"] = width, height
            elif resolution:
                body["resolution"] = resolution
            if self.model == "flux-3-image":
                body["grounding"] = False  # a game's place, not the web's
        else:
            body["input_image"] = _b64(image_png)
            for i, r in enumerate(refs[:7]):
                body[f"input_image_{i + 2}"] = _b64(r)
            if self.model.startswith("flux-kontext"):
                if aspect_ratio:
                    body["aspect_ratio"] = aspect_ratio
            elif width and height:
                body["width"], body["height"] = width // 16 * 16, height // 16 * 16
        if self.model in ("flux-2-pro", "flux-2-max", "flux-2-pro-preview"):
            body["disable_pup"] = True  # its prompt upsampling rewrites the prompt: things get invented
        if seed is not None and self.model != "flux-3-image":
            body["seed"] = seed
        if self.model == "flux-3-image":
            body.pop("output_format", None)  # it takes neither (422)
        body["safety_tolerance"] = 2
        return body

    def repaint(self, image_png: bytes, prompt: str, **options) -> bytes:
        """The image repainted as the prompt says, its composition kept; PNG bytes."""
        return self._run(self.body(image_png, prompt, **options))

    def fill(self, image_png: bytes, mask_png: bytes, prompt: str, seed: int | None = None) -> bytes:
        """The image with only its masked part (white in `mask_png`, the same size) painted again as the prompt says
        (FLUX.1 Fill: model flux-pro-1.0-fill); PNG bytes."""
        body: dict = {"image": _b64(image_png), "mask": _b64(mask_png), "prompt": prompt, "output_format": "png",
                      "safety_tolerance": 2}
        if seed is not None:
            body["seed"] = seed
        return self._run(body)

    def create(self, prompt: str, width: int = 1024, height: int = 1024, seed: int | None = None) -> bytes:
        """An image from words alone; PNG bytes."""
        body: dict = {"prompt": prompt, "width": width // 16 * 16, "height": height // 16 * 16, "safety_tolerance": 2}
        if self.model == "flux-3-image":
            body["grounding"] = False
        else:
            body["output_format"] = "png"
            if seed is not None:
                body["seed"] = seed
        if self.model in ("flux-2-pro", "flux-2-max", "flux-2-pro-preview"):
            body["disable_pup"] = True
        return self._run(body)

    def _run(self, body: dict) -> bytes:
        headers = {"x-key": self.api_key, "accept": "application/json"}
        with httpx.Client(timeout=120) as client:
            # Busy, asked again after a wait, up to ~10 min: 429 (too many tasks at once, or low credit capping them at one),
            # 503 (the model over capacity, "shedding requests: retry shortly"), 502/504 and a dropped connection (its
            # gateway). The waits grow (5, 10, 20, 30 s...) with a little jitter, so tiles waiting together don't all ask
            # again at the same moment.
            waited, attempt = 0.0, 0
            while True:
                try:
                    r = client.post(f"{BASE}/{self.model}", json=body, headers=headers)
                    busy = r.status_code in (429, 502, 503, 504)
                except httpx.TransportError:
                    r, busy = None, True
                if not busy or waited >= 600:
                    break
                pause = min(30.0, 5.0 * (2 ** attempt)) + random.uniform(0, 3)
                time.sleep(pause)
                waited += pause
                attempt += 1
            if r is None:
                raise ProviderError(f"BFL {self.model}: no connection after {int(waited)} s")
            if r.status_code != 200:
                raise ProviderError(f"BFL {self.model}: {r.status_code} {r.text[:300]}")
            task = r.json()
            polling = task.get("polling_url") or f"{BASE}/get_result?id={task.get('id')}"
            deadline = time.monotonic() + self.timeout_s
            while time.monotonic() < deadline:
                time.sleep(1.5)
                try:
                    p = client.get(polling, headers=headers)
                except httpx.TransportError:
                    continue  # a dropped connection while it works: asked again
                if p.status_code != 200:
                    continue
                answer = p.json()
                status = answer.get("status")
                if status == "Ready":
                    url = (answer.get("result") or {}).get("sample")
                    if not url:
                        raise ProviderError("BFL: ready without an image")
                    return client.get(url).content
                if status in ("Error", "Failed", "Content Moderated", "Request Moderated"):
                    raise ProviderError(f"BFL: {status} {answer.get('details', '')}")
            raise ProviderError("BFL: no image in time")


# The models the Art choice offers (Preferences ▸ AI ▸ Art: "bfl:<model>").
BFL_ART_MODELS = ("flux-2-klein-4b", "flux-3-image", "flux-2-pro")


class BflImageGenerator:
    """BFL as the Art tool: one asset from words, on a flat magenta it then clears (FLUX has no transparent
    output), so a piece comes back as a sprite like PixelLab's."""

    takes_options = True

    def __init__(self, api_key: str, model: str = "flux-2-klein-4b"):
        self.name = f"bfl {model}"
        self._painter = BflPainter(api_key, model)

    def generate(self, prompt: str, options: dict | None = None):
        from PIL import Image
        import io
        from .images import GeneratedImage
        o = options or {}
        tile = o.get("kind") == "tile"
        full = prompt if tile else prompt + " Alone, centred, on a flat, pure magenta (#FF00FF) background; nothing else in the picture."
        png = self._painter.create(full, 1024, 1024, seed=7)
        if not tile:
            image = Image.open(io.BytesIO(png)).convert("RGBA")
            px = image.load()
            for y in range(image.height):
                for x in range(image.width):
                    r, g, b, _ = px[x, y]
                    if (abs(r - 255) + g + abs(b - 255) < 150) or (r - g > 90 and b - g > 90 and abs(r - b) < 90):
                        px[x, y] = (0, 0, 0, 0)
            out = io.BytesIO()
            image.save(out, format="PNG")
            png = out.getvalue()
        return GeneratedImage(png, self.name, full)


def _four_mp(w: int, h: int) -> dict:
    k = (4_190_000 / (w * h)) ** 0.5
    return {"width": int(w * k) // 16 * 16, "height": int(h * k) // 16 * 16}


# A place painted whole (docs/places/painted-places.md §2): its plan's colour-coded block-out (PlaceBlockout)
# repainted by FLUX.3 Image with the colour key, which keeps the layout (Phase 25's tests: layout 9, fit 10).

def paint_guide(kind: str) -> str:
    """The ontology's paint prompt for a kind of place (data/places/buildtypes.json "paint"): its build type by id,
    name or one of its words; "" when none."""
    import json
    from pathlib import Path
    file = Path(__file__).resolve().parents[2] / "data" / "places" / "buildtypes.json"
    try:
        types = json.loads(file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return ""
    k = (kind or "").strip().lower()
    for t in types:
        if k in (t.get("id", ""), str(t.get("name", "")).lower()) or k in [w.lower() for w in t.get("words", [])]:
            return str(t.get("paint", ""))
    return ""


# Black in a block-out (the background, and rock or void the plan leaves empty): nothing there, in every painting.
EMPTY_RULE = ("PURE BLACK IS EMPTY: every black area of the image (around the map and between its parts) is nothing, "
              "outside the place. Leave every black pixel pure black: paint no ground, floor, rock, wall top, corridor, "
              "room, door, light or anything else in it, and don't extend any part into it.")

# Characters are placed separately (their own sprites): none in any painting.
NO_FIGURES = "NO CREATURES OR HUMANS: paint no people, monsters, animals or any other figures anywhere in the image."


def _relief_key(legend: dict | None) -> str:
    """The block-out's relief (its legend's): the raised rock is mountains, painted as high land, not as walls."""
    r = (legend or {}).get("relief") or {}
    if not r:
        return ""
    return (f"RELIEF: the {r.get('shape')} are {r.get('kind')}. Paint them as real mountains (or hills where they're low) "
            "rising as the steps rise, with ridges, cliffs, scree and shadowed slopes; never as walls, blocks or buildings.")


def _lines_key(legend: dict | None) -> str:
    """The walls and fences the block-out draws, as key lines; a wall drawn in a marking colour (a dungeon's slate blue)
    said to be only a marking, painted in the place's own stone."""
    parts = []
    for l in (legend or {}).get("lines", []):
        part = f"{l.get('shape')}" + (f" ({l['hex']})" if l.get("hex") else "") + f" = {l.get('kind')}"
        if l.get("colour"):
            part += (f"; the {l['colour']} only marks where the walls stand: paint them as real grey stone walls, "
                     f"with no {l['colour']} or any blue tint left on them")
        parts.append(part)
    return "; ".join(parts)


def _look_words(look: dict) -> str:
    """The look's goals as words (a list joined, not its Python form), cut at a sentence."""
    goal = look.get("goals") or look.get("style") or ""
    text = "; ".join(str(g).strip().rstrip(".") for g in goal) if isinstance(goal, list) else str(goal).strip()
    if len(text) > 400:
        text = text[:400].rsplit(";", 1)[0] if ";" in text[:400] else text[:400].rsplit(" ", 1)[0]
    return text


def _has_terrain(legend: dict | None, *words: str) -> bool:
    terrains = " ".join(str(g.get("terrain", "")) for g in (legend or {}).get("ground", [])).lower()
    things = " ".join(str(t.get("kind", "")) for t in (legend or {}).get("things", [])).lower()
    return any(w in terrains or w in things for w in words)


def blockout_prompt(place: dict, story: dict, look: dict, legend: dict | None = None, notes: str = "",
                    guide: str = "", written: str = "", objects: bool = True) -> str:
    """The prompt that paints a place from its block-out: what the block-out is and that black is empty, the
    ontology's guide for its kind (`guide`), the place's own words (`written`, by the LLM from the request), the colour
    key (each colour the kind it stands for, in the ontology's words: only with `objects`, a block-out that shows its
    things), the ground, the story's mood and the look; the layout kept exactly."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    if objects:
        layout = ("KEEP THE LAYOUT EXACTLY: the same camera and framing, the same map, and every part and thing at the same "
                  "position, size and orientation; add nothing large, remove nothing. Replace every coloured shape by the "
                  "real thing its colour stands for, drawn in full detail; the key's colours themselves must not show.")
    else:
        layout = ("KEEP THE LAYOUT EXACTLY: the same camera and framing, the same map, every area at the same position, size "
                  "and shape; add no large parts and remove none. The coloured areas are its ground (the key below): paint "
                  "each as the real ground it stands for, in full detail; the flat colours themselves must not show. Small "
                  "things of your own that suit the place may be added on its ground.")
    text = (f"Paint this isometric game map of a {kind} ({name}) as finished, hand-crafted art, as in classic isometric "
            "RPGs. The image is a colour-coded BLOCK-OUT of the place's plan, not art: each colour stands for one kind of "
            f"thing (the colour key below), and its shape says how it stands. {layout} {EMPTY_RULE} {NO_FIGURES}")
    if guide.strip():
        text += f" THIS KIND OF PLACE: {guide.strip()}"
    if written.strip():
        text += f" THIS PLACE: {written.strip()[:900]}"
    text += zones_sentence(place.get("zones") or [], False)
    things = (legend or {}).get("things", []) if objects else []
    if things:
        text += " COLOUR KEY: " + "; ".join(f"{t.get('colour')} {t.get('shape')} = {t.get('what')}" for t in things)[:2600] + "."
    if _lines_key(legend):
        text += " " + _lines_key(legend) + "."
    if _relief_key(legend):
        text += " " + _relief_key(legend)
    if scale_sentence(legend):
        text += " " + scale_sentence(legend).strip()
    # The ground: black cells (rock, void) are left out: they're empty (EMPTY_RULE).
    grounds = [g for g in (legend or {}).get("ground", []) if not g.get("empty")]
    if grounds:
        water = _has_terrain(legend, "water", "river", "lake")
        text += (" The ground: " + ", ".join(f"{g.get('hex')} cells = {g.get('terrain')}" for g in grounds) +
                 (" (brown cells across water are a bridge)." if water else "."))
    mood = " ".join(str(story.get(k) or "") for k in ("mood", "purpose", "logline")).strip()
    if mood:
        text += f" The story of this place: {mood[:500]} Paint its mood in the palette, light, weathering and details."
    goal = _look_words(look)
    if goal:
        text += f" Look: {goal}."
    from .style import art_style_words
    if art_style_words():
        text += f" Art style: {art_style_words()}."
    if notes.strip():
        text += f" Also: {notes.strip()[:600]}"
    # What the outdoors has, only where the place has it.
    details = []
    if _has_terrain(legend, "road", "street", "track", "path"):
        details.append("roads worn with ruts")
    if _has_terrain(legend, "water", "river", "lake", "pond", "sea"):
        details.append("water with banks and ripples")
    if _has_terrain(legend, "forest", "wood", "tree"):
        details.append("woods as dense masses of overlapping crowns")
    text += (" " + ", ".join(details).capitalize() + "." if details else "")
    text += (" Ground with natural variation and no visible cells or grid; one light from the upper left; one palette "
             "everywhere. No text, no labels, no UI, no characters.")
    return text


def scale_sentence(legend: dict | None) -> str:
    """The block-out's measure (its legend's scale): how big the whole place is, so a model paints it at its size (klein
    otherwise reads a whole dungeon level as one room)."""
    s = (legend or {}).get("scale") or {}
    across, down, m = int(s.get("across") or 0), int(s.get("down") or 0), float(s.get("metresPerCell") or 2)
    if not s.get("grid") or across <= 0 or down <= 0:
        return ""
    return (f"SCALE: the image shows the whole place, {across} by {down} grid squares; each grid square is about {m:g} m across "
            f"(one person stands in one), so the place is about {across * m:g} m by {down * m:g} m. Keep that scale: a typical "
            f"building occupies about 3 to 5 grid squares per side (6 to {5 * m:.0f} m); a person is about 1 grid square tall. "
            f"Count the squares; an element that spans 20 squares in the image is {20 * m:g} m long and stays exactly that long "
            f"in the painting. Things stay small at that scale. ")


def counts_sentence(legend: dict | None) -> str:
    """The plan's buildings and small things counted, so the model adds and swaps nothing."""
    things = (legend or {}).get("things", [])
    buildings = [t for t in things if "roof" in t.get("shape", "")]
    props = [t for t in things if t.get("shape") == "small box"]
    if not buildings:
        return ""

    def one(t):
        return f"{t.get('count', 1)} {t['kind']}{'s' if t.get('count', 1) != 1 else ''} ({t['colour']})"
    text = "Exactly " + ", ".join(one(t) for t in buildings) + ", each where the draft has a box with a roof; no other buildings anywhere."
    if props:
        text += " Small things: " + ", ".join(one(t) for t in props) + "; no others."
    return text + " Where the draft has trees, woods, grass or water, there are no buildings."


def draft_prompt(place: dict, story: dict, look: dict, legend: dict | None = None, notes: str = "",
                 guide: str = "", written: str = "", objects: bool = True) -> str:
    """A short prompt that makes an editing model (FLUX.2 klein) repaint the block-out completely: the image a rough
    draft to replace, black left empty, the kind's guide and the place's words, the mood, the colour key a brief line
    per kind (klein copies a long key's colours; none without `objects`)."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    mood = str(story.get("mood") or story.get("purpose") or "")[:300]
    text = f"Repaint this image as a finished, hand-crafted isometric game map of a {kind} ({name}). "
    if mood:
        text += f"Mood: {mood} "
    text += ("The image is only a rough colour-coded draft: replace all of its flat colours and simple shapes with real, "
             "detailed things, materials, light and shadow; none of the draft's colours may remain. ")
    text += ("Pure black is empty: leave every black area pure black, with nothing painted in it, and paint only inside "
             "the draft's coloured areas, keeping their exact shapes. Keep the exact framing: the whole map as the image "
             "shows it, from as far above, every part as small as it is in the image and the black all round it; do not "
             "zoom in, crop or enlarge any part. ")
    text += NO_FIGURES + " "
    if guide.strip():
        text += guide.strip() + " "
    if written.strip():
        text += written.strip()[:500] + " "
    grounds = [g for g in (legend or {}).get("ground", []) if not g.get("empty")]
    if grounds and not objects:
        text += "In the draft: " + ", ".join(f"{g.get('hex')} = {g.get('terrain')}" for g in grounds) + ". "
    # Walls and fences drawn in it (a dungeon's slate-blue walls): what they are; and its measure.
    if _lines_key(legend):
        text += "In the draft: " + _lines_key(legend) + ". "
    if _relief_key(legend):
        text += _relief_key(legend) + " "
    text += scale_sentence(legend)
    things = [t for t in (legend or {}).get("things", []) if t.get("kind")] if objects else []
    if things:
        text += "In the draft: " + ", ".join(f"{t['colour']} = {t['kind']}" for t in things[:14]) + ". "
    lines = (legend or {}).get("lines", [])
    if lines:
        text += "Thin strips are " + " and ".join(str(l.get("kind", "fence")) + "s" for l in lines) + ". "
    if notes.strip():
        text += f"Also: {notes.strip()[:400]} "
    return text + ("Keep every part of the place where it is, with the same camera and the same map. No text, no UI, "
                   "no characters.")


PLACE_WORDS_TASK = """You write the art direction for painting one place of an isometric 2D game, as a short paragraph
(3 to 6 sentences, under 120 words) that the painting model reads after the rules for its kind of place. From the user's
request and the kind's rules below, say what THIS place looks like: its materials, its age and state of repair, its light
and colours, and what its parts are used for (by their names), in concrete visual words. Keep to the kind's rules (never
contradict them), never ask for anything in empty or black areas, never change the layout, never name coordinates, and
add no doors unless the rules allow them. Answer as JSON exactly: {"words": "<the paragraph>"}"""


def write_place_words(provider, request: str, guide: str, place: dict, story: dict, look: dict, legend: dict | None = None) -> str:
    """The place's own words for its painting (the LLM step): from the user's request and the ontology's guide for
    its kind, its parts and story; "" without a provider or an answer."""
    if provider is None:
        return ""
    parts = sorted({str(t.get("what") or t.get("kind") or "").split(":")[0] for t in (legend or {}).get("things", []) if t.get("kind")})
    grounds = sorted({str(g.get("terrain", "")) for g in (legend or {}).get("ground", []) if not g.get("empty")})
    brief = {
        "request": request, "kind": place.get("type"), "name": place.get("name"),
        "story": {k: story.get(k) for k in ("mood", "purpose", "logline") if story.get(k)},
        "look": _look_words(look), "ground": grounds, "things in it": parts[:30],
    }
    import json
    try:
        answer = provider.complete([{"role": "system", "content": PLACE_WORDS_TASK + "\n\nRULES FOR ITS KIND:\n" + guide},
                                    {"role": "user", "content": json.dumps(brief, ensure_ascii=False)}]).text
    except Exception:  # noqa: BLE001 - painted without them
        return ""
    # JSON (providers answer in JSON mode), else the text as it came.
    try:
        start, end = answer.find("{"), answer.rfind("}")
        parsed = json.loads(answer[start:end + 1]) if start >= 0 else None
        if isinstance(parsed, dict):
            answer = str(parsed.get("words") or parsed.get("paragraph") or "")
    except ValueError:
        pass
    return " ".join(answer.strip().strip('"').split())[:900]


# --- Painted freely (docs/places/painted-places.md §2a) --------------------------------------------------------------
# The block-out is the land alone (ground, roads, water, woods, fields); the painter composes the place on it from what
# it's to hold (its kind, size, each kind of building and how many) and its design, decorating as it likes. One call
# composes it on the whole land; then tiles of FREE_TILE (overlapping FREE_OVERLAP) repaint it at the painting's full size
# (48 px a cell: the editor's PlanFreePaint, which shows the count), each tile with some land in it.
FREE_TILE = (2864, 1432)
FREE_OVERLAP = 160
FIELDS_EMPTY = ("FARM FIELDS STAY EMPTY: where the land has fields (brown tilled soil), paint them as bare, freshly "
                "ploughed soil in furrows with nothing growing on them: no crops, plants, scarecrows or things inside "
                "them (they're for the game's characters to plant).")


def free_grid(width: int, height: int) -> list[tuple[int, int, int, int]]:
    """The free painting's tiles over a width × height painting, row by row (the editor's PlanFreePaint grid)."""
    tw, th = FREE_TILE

    def starts(size: int, tile: int) -> list[int]:
        if size <= tile:
            return [0]
        n = 1 + -(-(size - tile) // (tile - FREE_OVERLAP))
        return [min(i * (tile - FREE_OVERLAP), size - tile) for i in range(n)]

    return [(x, y, min(width, x + tw), min(height, y + th)) for y in starts(height, th) for x in starts(width, tw)]


def zones_sentence(zones: list, free: bool) -> str:
    """The plan's parts for the painter: each zone, where it is in the picture, and its buildings by kind and count."""
    parts = []
    for z in zones or []:
        if not isinstance(z, dict) or not z.get("name"):
            continue
        built = ", ".join(f"{int(b.get('count') or 0)} {str(b.get('kind'))}{'s' if int(b.get('count') or 0) != 1 else ''}"
                          for b in z.get("buildings") or [] if isinstance(b, dict) and b.get("kind") and b.get("count"))
        where = f" ({z['where']} of the image)" if z.get("where") else ""
        parts.append(f"{str(z['name']).lower()}{where}: {built or 'no buildings'}")
    if not parts:
        return ""
    if free:
        return ("Lay the place out in these parts, each where it is said to be in the image, with exactly these buildings in "
                "it: " + "; ".join(parts) + ". ")
    return " THE PLACE'S PARTS (where each is in the image, and the buildings its coloured shapes stand for): " + "; ".join(parts) + "."


def free_prompt(place: dict, scale: str, contents: list, design: str, request: str, guide: str, notes: str = "") -> str:
    """The composing call's prompt: the land kept, the place built on it (its kind, size, buildings by kind and count, its
    design), decorated as the painter likes, fields empty, black empty."""
    kind = str(place.get("type") or "place")
    name = place.get("name") or "the place"
    size = f" a {scale.replace('-', ' ')}" if scale else ""
    layout = place.get("layoutParams") or {}
    layout_line = ""
    if isinstance(layout, dict) and layout:
        layout_line = " Composition/plan parameters to follow: " + ", ".join(
            f"{k}={v}" for k, v in layout.items() if v not in (None, "", [], {})
        ) + "."
    def building(kind: str) -> str:  # the plan's kind as a building ("house plot" is a house)
        for tail in (" plot", " yard", " lot", " site"):
            if kind.endswith(tail):
                return kind[: -len(tail)]
        return kind

    built = ", ".join(f"{c.get('count')} {building(str(c.get('kind')))}{'s' if int(c.get('count') or 0) != 1 else ''}" for c in contents
                      if c.get("kind") and c.get("count"))
    total = sum(int(c.get("count") or 0) for c in contents)
    text = ("Paint this isometric game map as finished, hand-crafted art, as in classic isometric RPGs. The image is a block-out "
            "of the LAND only: green is grass, brown strips are dirt roads, dark red-brown strips across water are wooden bridges (paint them as bridges), blue is water, green areas are forests/woods, brown "
            "patches are farm fields, grey is a paved square. STAY TRUE TO THE BLOCK-OUT'S LAND: every road, river, lake, "
            "coast and the sea exactly where it is in the block-out, the same course, width and shape; the woods and fields "
            "where they are. Don't move, add or remove a road or any water; build the place around them. "
            "Green areas are where trees and forests belong; you may also place occasional trees on grass elsewhere. ")
    text += f"Build {name},{size} {kind}, on it"
    text += f": about {total} buildings: {built}. " if built else ". "
    text += zones_sentence(place.get("zones") or [], True)
    text += ("Place them as a real place of its kind grows: along and around the roads, densest at its heart, thinning at its "
             "edges, with its own streets, lanes, yards, gardens and squares between them. ")
    if request.strip():
        text += f"What it is: {request.strip()[:400]}. "
    if design.strip():
        text += f"Its design: {design.strip()[:300]}; every building, material and detail fits it. "
    if guide.strip():
        text += guide.strip()[:600] + " "
    text += ("Decorate it as you like: carts, barrels, crates, wells, fences, hedges, flowers, signs, wash lines, paths. "
             + FIELDS_EMPTY + " " + EMPTY_RULE + " " + NO_FIGURES + " No text.")
    if notes.strip():
        text += f" Also: {notes.strip()[:300]}"
    return text


def zoned_prompt(place: dict, scale: str, design: str, request: str, guide: str, notes: str = "") -> str:
    """The composing call's prompt for Paint (Free) (painted-places.md §2b): the land block-out with each zone's ground in
    a flat colour, and what each colour is: the zone, what it holds (its kind of place, its buildings by kind and count),
    built freely inside its colour and nowhere else. No colour may remain."""
    kind = str(place.get("type") or "place")
    name = place.get("name") or "the place"
    size = f" a {scale.replace('-', ' ')}" if scale else ""
    zones = [z for z in place.get("zones") or [] if isinstance(z, dict) and z.get("colour")]
    names = sorted({str(z["colour"]).lower() for z in zones})
    colours = names[0] if len(names) == 1 else ", ".join(names[:-1]) + " or " + names[-1]
    layout = place.get("layoutParams") or {}
    layout_line = ""
    if isinstance(layout, dict) and layout:
        layout_line = "Composition/plan parameters to follow: " + ", ".join(
            f"{k}={v}" for k, v in layout.items() if v not in (None, "", [], {})
        ) + ". "
    text = ("Paint this isometric game map as finished, hand-crafted art, as in classic isometric RPGs. The image is a "
            f"colour-coded plan of {name},{size} {kind}. THE COLOURS ARE AREAS, NOT MATERIALS: none of the flat "
            f"{colours} may remain in the painting. " + layout_line)
    for z in zones:
        built = ", ".join(f"{int(b.get('count') or 0)} {str(b.get('kind'))}{'s' if int(b.get('count') or 0) != 1 else ''}"
                          for b in z.get("buildings") or [] if isinstance(b, dict) and b.get("kind") and b.get("count"))
        # What it is: its module's description, its first sentence (the rest may be advice for planning it).
        what = str(z.get("description") or "").strip().split(". ")[0].strip().rstrip(".")
        text += f"{str(z['colour']).upper()} area = the {str(z.get('name') or 'zone').lower()}"
        text += f" ({what})" if what else ""
        if built:
            text += f": fill all of it with about {built}, with their lanes, yards, gardens and fences. "
        else:
            text += ": fill all of it as such a place is; no houses in it unless it has them. "
    text += ("Brown strips are dirt roads, blue is water, dark red-brown strips across water are wooden bridges, grey is a "
             "paved square: keep every road, river, lake and coast exactly where it is, the same course, width and shape. "
             "Arrange buildings, lots, blocks and open spaces inside each zone according to the composition/plan parameters (lot-layout, block-fill, block-use, frontage, building-cover, building-shape, coverage, open-space, arrangement) when given. "
             "Green is grass and green areas are forests/woods: keep the woods and the meadow outside the coloured areas where "
             "they are, as natural woodland and meadow. You may also place occasional trees on grass elsewhere. Every part stays exactly where its colour is: don't build outside "
             "the coloured areas. ")
    if request.strip():
        text += f"What it is: {request.strip()[:400]}. "
    if design.strip():
        text += f"Its design: {design.strip()[:300]}; every building, material and detail fits it. "
    if guide.strip():
        text += guide.strip()[:600] + " "
    text += FIELDS_EMPTY + " " + EMPTY_RULE + " " + NO_FIGURES + " No text."
    if notes.strip():
        text += f" Also: {notes.strip()[:300]}"
    return text


FREE_DETAIL = ("This image is one part of a painted isometric game map, enlarged: repaint it as finished, detailed, hand-crafted "
               "art. Keep everything exactly where it is: every building, road, lane, river, lake, the coast, bridges, woods, fields and the black "
               "outside the map; keep each building's size, shape and roof. Sharpen it into crisp detail: roof tiles and thatch, "
               "walls, windows, doors, chimneys, fences, cobbles, barrels, flowers, grass and leaves. "
               + FIELDS_EMPTY + " No people, animals or creatures, no text. ")  # (and TILE_NOTE, at its use)


KLEIN_GROUND = (
    "Refine this painted isometric game map. Keep everything exactly where it is: every building, road, river, lake, the sea, "
    "mountains, trees, fences, fields and gardens, and the black outside the map; nothing moves, nothing is added or removed. "
    "Only the plain flat green areas change: paint them as real ground, in the same hand-crafted style: lush grass with natural "
    "light and darker patches, wildflowers, tall grass tufts, small bushes, clover, scattered stones and worn footpaths. Green "
    "stays ground: no water, river, pond, stream or marsh anywhere on it. No people, animals or text.")


# Zone tint colours (the editor's kTints). Used to decide which zones actually appear in each tile's crop.
_ZONE_RGB = {
    "orange": (232, 146, 76), "yellow": (236, 212, 58), "purple": (146, 96, 196),
    "red": (200, 60, 60), "white": (240, 240, 240), "pink": (235, 140, 190),
}


def _zones_in_tile(crop, zones: list) -> list:
    """Only the zones whose tint colour actually appears in this tile's flat block-out crop."""
    arr = np.asarray(crop).astype(int)
    present = []
    for z in zones:
        if not isinstance(z, dict):
            continue
        colours = []
        if z.get("rgb"):
            colours.append(tuple(z["rgb"]))
        name = str(z.get("colour") or "").strip().lower()
        if name in _ZONE_RGB:
            colours.append(_ZONE_RGB[name])
        if any(np.any(np.abs(arr - np.array(c)).sum(2) < 30) for c in colours):
            present.append(z)
    return present


# Terrain colours from the editor's blockout palette. A terrain key in the per-tile prompt gives forest/meadow/water
# the same prominence as zones; the model otherwise only sees the zone colours in the key and misplaces them.
_TERRAIN_KEY = [
    ((58, 84, 48), "- dark green patches = DENSE OLD-GROWTH FOREST: cover them with closely packed tall trees, no flat green ground"),
    ((96, 130, 70), "- light green = open meadow: lush grass, wildflowers and stones, NO buildings"),
    ((52, 96, 150), "- blue = real water: keep its exact course and banks; no blue in the block-out means no water"),
    ((150, 118, 78), "- brown strips = dirt roads and paths"),
]


def _terrain_in_tile(crop) -> list[str]:
    arr = np.asarray(crop).astype(int)
    return [text for rgb, text in _TERRAIN_KEY if np.any(np.abs(arr - np.array(rgb)).sum(2) < 30)]


def paint_free(key: str, blockout_png: bytes, place: dict, scale: str, contents: list, design: str, request: str,
               guide: str = "", notes: str = "", seed: int = 7, report=None, tiles_dir=None,
               model: str = "flux-3-image", cols: int = 0, rows: int = 0,
               variety: str = "", debug_dir=None) -> tuple[bytes, str]:
    """A place painted freely on its colour-coded land/district block-out, in congruent tiles (docs/places/tiling.md
    §6): each tile painted directly from its flat block-out crop, one after another, each taking an 8% context band
    from the tile before it, the bands restored exactly and the tiles joined on an exact grid. No whole-scene compose
    step (which made buildings the same size in big scenes). Open-world sections use the same path.
    Returns (painting PNG bytes at the block-out's size, the prompt used)."""
    # PLACEHOLDER: tiling pipeline disabled in production.
    return blockout_png, "placeholder (tiling disabled)"

PAINT_MODELS = {"flux-2-klein-4b": "FLUX-2-Klein-4b", "flux-3-image": "FLUX-3-Image"}


# Tiles (docs/places/tiling.md §6): a model image holds about TILE_SPAN cells across its diamond at the block-out's
# density, so a bigger place is painted in n × n tiles, n = ceil((W + H) / TILE_SPAN), each overlapping its neighbours
# by TILE_OVERLAP of the image, painted at full size and joined by cross-fading the overlaps.
TILE_SPAN = 60
TILE_PX = TILE_SPAN * 40  # the block-out's width a tile holds (TILE_SPAN cells at its 80 px a cell): by pixels, any density
TILE_OVERLAP = 0.12
TILES_AT_ONCE = 4
TILE_NOTE = ("THIS IMAGE IS ONE PART of a bigger map, cut out of it: paint it right up to its cut edges, the parts cut "
             "off by an edge just continuing past it; no frame, border, vignette or new edge of the map at a cut.")


def tile_count(across: int, down: int) -> int:
    """Tiles per side for a place of `across` × `down` cells (1: one image)."""
    import math
    return max(1, math.ceil((max(0, across) + max(0, down)) / TILE_SPAN))


def tile_boxes(width: int, height: int, n: int, overlap: float = TILE_OVERLAP) -> list[tuple[int, int, int, int]]:
    """The n × n tiles of a width × height image, row by row: (x0, y0, x1, y1), each its share of the image plus half
    the overlap past each inner edge."""
    if n <= 1:
        return [(0, 0, width, height)]
    ox, oy = round(width * overlap / 2), round(height * overlap / 2)
    xs = [round(width * i / n) for i in range(n + 1)]
    ys = [round(height * i / n) for i in range(n + 1)]
    return [(max(0, xs[c] - ox), max(0, ys[r] - oy), min(width, xs[c + 1] + ox), min(height, ys[r + 1] + oy))
            for r in range(n) for c in range(n)]


def _ramp(size: int, before: int, after: int):
    """Weights along one side of a tile: 0 → 1 over the `before` pixels it shares with the tile before it, 1 inside,
    1 → 0 over the `after` pixels it shares with the one after."""
    import numpy as np
    w = np.ones(size)
    if before > 0:
        w[:before] = np.linspace(0, 1, before + 2)[1:-1]
    if after > 0:
        w[size - after:] = np.minimum(w[size - after:], np.linspace(1, 0, after + 2)[1:-1])
    return w


def join_tiles(tiles: list, boxes: list[tuple[int, int, int, int]], width: int, height: int):
    """The tiles (PIL images of any size: each resized to its box) as one width × height image, the parts neighbouring
    tiles share cross-faded."""
    import numpy as np
    from PIL import Image
    acc = np.zeros((height, width, 3))
    wsum = np.zeros((height, width, 1))
    for (x0, y0, x1, y1), im in zip(boxes, tiles):
        w, h = x1 - x0, y1 - y0
        im = im.convert("RGB")
        if im.size != (w, h):
            im = im.resize((w, h), Image.LANCZOS)
        # How far the tiles beside it reach into it (on its rows / its columns).
        rows = [b for b in boxes if b[1] < y1 and b[3] > y0 and b != (x0, y0, x1, y1)]
        cols = [b for b in boxes if b[0] < x1 and b[2] > x0 and b != (x0, y0, x1, y1)]
        left = max([b[2] - x0 for b in rows if b[0] < x0 < b[2] <= x1] or [0])
        right = max([x1 - b[0] for b in rows if x0 <= b[0] < x1 < b[2]] or [0])
        top = max([b[3] - y0 for b in cols if b[1] < y0 < b[3] <= y1] or [0])
        bottom = max([y1 - b[1] for b in cols if y0 <= b[1] < y1 < b[3]] or [0])
        weight = (_ramp(h, top, bottom)[:, None] * _ramp(w, left, right)[None, :])[:, :, None]
        acc[y0:y1, x0:x1] += np.asarray(im, dtype=float) * weight
        wsum[y0:y1, x0:x1] += weight
    return Image.fromarray(np.clip(acc / np.maximum(wsum, 1e-9), 0, 255).round().astype(np.uint8))


def _tile_legend(legend: dict | None, n: int) -> dict:
    """The legend as one of n × n tiles sees it: its scale a tile's share of the place's."""
    out = json.loads(json.dumps(legend or {}))
    s = out.get("scale")
    if isinstance(s, dict) and n > 1:
        share = 1 / n + TILE_OVERLAP
        s["across"] = max(1, round((s.get("across") or 0) * share))
        s["down"] = max(1, round((s.get("down") or 0) * share))
    return out


def paint_place(key: str, model: str, blockout_png: bytes, legend: dict, place: dict, story: dict, look: dict,
                notes: str = "", seed: int = 7, reference: bytes | None = None, guide: str | None = None, written: str = "",
                objects: bool = True, report=None, tiles_dir=None, cols: int = 0, rows: int = 0,
                variety: str = "", debug_dir=None) -> tuple[bytes, str]:
    """A place painted from its colour-coded block-out by one of PAINT_MODELS, in congruent tiles (docs/places/tiling.md
    §6). Each tile is painted directly from its flat block-out crop — no composed whole-scene image — with an 8%
    context band from the previously painted tile, restored exactly, then joined on an exact grid. The building/terrain
    colour key and counts come from the legend; art direction from the Build panel. A reference picture's look is
    described in the prompt. Works for any number of tiles and place types (and open-world sections)."""
    # PLACEHOLDER: tiling pipeline disabled in production.
    return blockout_png, "placeholder (tiling disabled)"
