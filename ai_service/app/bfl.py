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
import time

import httpx

from .providers import ProviderError

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
            r = client.post(f"{BASE}/{self.model}", json=body, headers=headers)
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
    return (f"SCALE: the image shows the whole level, {across} by {down} grid squares; each grid square on the floor is one "
            f"flagstone about {m:g} m across (one person stands in one), so the level is about {across * m:g} m by "
            f"{down * m:g} m and holds many rooms. Keep that scale: count the squares; a wall that runs 20 squares in the "
            f"image is {20 * m:g} m long and stays exactly that long in the painting; flagstones, torches, doors and "
            "things stay small, at that scale. ")


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


# Who paints a place whole (Project Settings ▸ AI ▸ paint), as shown.
PAINT_MODELS = {"flux-2-klein-4b": "FLUX-2-Klein-4b", "flux-2-pro": "FLUX-2-Pro", "flux-2-max": "FLUX-2-Max", "flux-3-image": "FLUX-3-Image",
                "flux-3-to-pro": "FLUX-3-Image-to-Pro", "flux-3-to-klein": "FLUX-3-Image-to-Klein"}


# Tiles (docs/places/tiling.md §6): a model image holds about TILE_SPAN cells across its diamond at the block-out's
# density, so a bigger place is painted in n × n tiles, n = ceil((W + H) / TILE_SPAN), each overlapping its neighbours
# by TILE_OVERLAP of the image, painted at full size and joined by cross-fading the overlaps.
TILE_SPAN = 60
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
                objects: bool = True, report=None) -> tuple[bytes, str]:
    """A place painted from its block-out by one of PAINT_MODELS; the painting at the block-out's own size (so its
    pixels line up with the block-out's id map) and the prompt(s). A place more than TILE_SPAN cells across its diamond
    (its legend's scale) is painted in tiles (tile_count) and joined; `report(done, total, text)` hears each tile.
    - flux-3-image: the block-out with the colour key and the counts (keeps the layout best).
    - flux-2-klein-4b: klein copies a flat input it's told about: the block-out as a rough draft, scaled up, a short key.
    - flux-2-pro / flux-2-max: the block-out with the colour key and the counts, as FLUX.3 Image (prompt upsampling off);
      they look good but can drift from the layout (Phase 25's tests).
    - flux-3-to-klein / flux-3-to-pro: FLUX.3 Image paints the plan, then klein or FLUX.2 pro repaints its look, the
      block-out beside it (pro) saying what each building is."""
    import io
    from concurrent.futures import ThreadPoolExecutor
    from PIL import Image
    src = Image.open(io.BytesIO(blockout_png)).convert("RGB")
    # The ontology's guide for its kind unless one is given; the place's own words; the key only for things shown.
    guide = paint_guide(str(place.get("type") or "")) if guide is None else guide
    scale = (legend or {}).get("scale") or {}
    n = tile_count(int(scale.get("across") or 0), int(scale.get("down") or 0)) if scale else 1
    tiled = _tile_legend(legend, n) if n > 1 else legend
    keyed = (blockout_prompt(place, story, look, tiled, notes, guide, written, objects) + " " +
             (counts_sentence(tiled) if objects else "")).strip()
    draft = draft_prompt(place, story, look, tiled, notes, guide, written, objects)
    if n > 1:
        # The counts are the whole place's: a part has only some of them.
        keyed = keyed.replace("Exactly ", "Of the whole map's ") + " " + TILE_NOTE
        draft += " " + TILE_NOTE
    # A picture given to paint after (the Build panel's "as a reference"): its look, on the plan's layout.
    refs = [reference] if reference else []
    after = (" The second image is a picture of how this place should look: paint it in that picture's look, palette and "
             "details (or as that picture, changed as the notes say), but keep the first image's layout exactly.") if reference else ""
    keyed += after
    draft += after

    def one(part):
        """One image painted from `part` (a PIL block-out, the whole or a tile): the painting (PIL) and its prompt(s)."""
        buf = io.BytesIO()
        part.save(buf, format="PNG")
        part_png = buf.getvalue()
        size = _four_mp(part.width, part.height)
        if model in ("flux-2-pro", "flux-2-max"):
            png, prompt = BflPainter(key, model).repaint(part_png, keyed, references=refs, seed=seed, **size), keyed
        elif model == "flux-2-klein-4b":
            big = part.resize((4096, round(4096 * part.height / part.width)), Image.LANCZOS)
            buf = io.BytesIO()
            big.save(buf, format="PNG")
            png, prompt = BflPainter(key, "flux-2-klein-4b").repaint(buf.getvalue(), draft, references=refs, seed=seed, **size), draft
        else:
            png, prompt = BflPainter(key, "flux-3-image").repaint(part_png, keyed, references=refs, seed=seed, **size), keyed
            if model == "flux-3-to-klein":
                png = BflPainter(key, "flux-2-klein-4b").repaint(png, draft, seed=seed, **size)
                prompt += "\n---\n" + draft
            elif model == "flux-3-to-pro":
                things = "; ".join(f"{t['colour']} = {t['kind']}" for t in (legend or {}).get("things", []) if t.get("kind"))
                refine = ("Refine this painted isometric game map into finished, hand-crafted art: richer materials, light and "
                          "detail, one consistent palette. Keep the camera, the layout and every building, road, river, fence and "
                          "tree exactly where it is. The second image is the place's colour-coded plan, aligned with the first: "
                          f"each coloured box marks one building or thing ({things}). Draw each as the kind its colour says; never "
                          "draw the second image's colours. " + counts_sentence(tiled) + " No text, no UI, no characters." +
                          (" " + TILE_NOTE if n > 1 else ""))
                png = BflPainter(key, "flux-2-pro").repaint(png, refine, references=[part_png], seed=seed, **size)
                prompt += "\n---\n" + refine
        return Image.open(io.BytesIO(png)).convert("RGB"), prompt

    boxes = tile_boxes(src.width, src.height, n)
    done = [0]

    def tile(box):
        result = one(src.crop(box))
        done[0] += 1
        if report and n > 1:
            report(done[0], len(boxes), f"Painted tile {done[0]} of {len(boxes)}")
        return result

    if report and n > 1:
        report(0, len(boxes), f"Painting in {n} × {n} tiles ({len(boxes)} images, {TILES_AT_ONCE} at a time)")
    with ThreadPoolExecutor(min(TILES_AT_ONCE, len(boxes))) as pool:
        results = list(pool.map(tile, boxes))
    painted = join_tiles([r[0] for r in results], boxes, src.width, src.height) if n > 1 else results[0][0]
    if painted.size != src.size:
        painted = painted.resize(src.size, Image.LANCZOS)
    out = io.BytesIO()
    painted.save(out, format="PNG")
    prompt = results[0][1] + (f"\n---\n(painted in {n} × {n} tiles)" if n > 1 else "")
    return out.getvalue(), prompt
