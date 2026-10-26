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
        headers = {"x-key": self.api_key, "accept": "application/json"}
        with httpx.Client(timeout=120) as client:
            r = client.post(f"{BASE}/{self.model}", json=self.body(image_png, prompt, **options), headers=headers)
            if r.status_code != 200:
                raise ProviderError(f"BFL {self.model}: {r.status_code} {r.text[:300]}")
            task = r.json()
            polling = task.get("polling_url") or f"{BASE}/get_result?id={task.get('id')}"
            deadline = time.monotonic() + self.timeout_s
            while time.monotonic() < deadline:
                time.sleep(1.5)
                p = client.get(polling, headers=headers)
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


KontextPainter = BflPainter  # the first name it had


def repaint_prompt(place: dict, story: dict, look: dict, fixes: list[dict] | None = None, with_plan: bool = False) -> str:
    """The prompt that repaints a code-built place's render (a block-out whose layout is the plan) as finished
    art: the layout kept exactly, only the drawing changed; the mood from the story, the look from the build
    type's goal."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    mood = " ".join(str(story.get(k) or "") for k in ("mood", "purpose")).strip()
    goal = str(look.get("goals") or look.get("reference") or "").strip()
    text = (
        f"Repaint this image. It is an isometric map of a game {kind} ({name}), rendered as a rough draft. "
        "KEEP THE COMPOSITION EXACTLY: the same camera and framing, the same diamond-shaped map on the same dark "
        "background, and every building, road, path, river, bridge, wall, tree, rock and prop at the same position, "
        "size, shape and orientation. Add nothing large and remove nothing. Every building and thing stands on the "
        "map's own ground, inside the diamond: nothing floats on the background or on an island of its own. "
        "CHANGE ONLY THE RENDERING: finished, hand-crafted isometric pixel art as in classic isometric RPGs and "
        "strategy games: detailed roofs with tiles or thatch, walls with timber, stone and windows, worn roads with "
        "ruts and edges, water with banks and ripples, woods as masses of overlapping crowns with shadow beneath, "
        "ground with natural variation in colour and texture and no visible tiles, seams or grid. One light from the "
        "upper left, soft shadows, one consistent palette and pixel size everywhere.")
    if mood:
        text += f" Mood and story: {mood[:400]} The palette, light and weathering show that mood."
    if goal:
        text += f" Look: {goal[:300]}"
    if with_plan:
        text += (" The second image is the PLAN of the same place, a top-down map: plot, garden and field edges are its "
                 "lines. Fences, hedges, beds and yards go where the plan shows them, along those edges, even where the "
                 "draft has them wrong.")
    if fixes:
        # The draft's mistakes (the audit's): mended, not copied. Big things stay; these change.
        listed = "; ".join(f"{f.get('target', '')}: {f.get('issue', '')} -> {f.get('fix', '')}" for f in fixes[:20])
        text += (" The draft has mistakes: DO NOT COPY THEM, draw each one right: " + listed[:3000] + ". Buildings, roads, "
                 "the river and the woods stay where they are while these are mended.")
    text += " No text, no labels, no UI, no characters."
    return text


def blockout_prompt(place: dict, story: dict, look: dict, zones: list[dict] | None = None, legend: dict | None = None) -> str:
    """The prompt that paints a place from its plan's colour-coded isometric block-out (PlaceBlockout): the
    colour key (each colour the kind of building or thing it stands for, in the ontology's words), the
    zones (the ontology's descriptions), the story's mood; the layout kept exactly."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    text = (
        f"Paint this isometric game map of a {kind} ({name}) as finished, hand-crafted pixel art, as in classic "
        "isometric RPGs and strategy games. The image is a colour-coded BLOCK-OUT of the place's plan, not art: "
        "each colour stands for one kind of thing (the colour key below), and its shape says how it stands. "
        "KEEP THE LAYOUT EXACTLY: the same camera and framing, the same diamond-shaped map, and every building, "
        "road, river, bridge, fence, tree and thing at the same position, size and orientation; add nothing large, "
        "remove nothing. Replace every coloured shape by the real thing its colour stands for, drawn in full "
        "detail; the key's colours themselves must not show in the painting.")
    things = (legend or {}).get("things", [])
    if things:
        key = [f"{t.get('colour')} {t.get('shape')} = {t.get('what')}" for t in things]
        text += " COLOUR KEY: " + "; ".join(key)[:2600] + "."
    lines = (legend or {}).get("lines", [])
    if lines:
        text += " " + "; ".join(f"{l.get('shape')} = {l.get('kind')}" for l in lines) + "."
    grounds = (legend or {}).get("ground", [])
    if grounds:
        text += " The ground: " + ", ".join(f"{g.get('hex')} cells = {g.get('terrain')}" for g in grounds) + " (brown cells across water are a bridge)."
    described = [f"{z.get('id', '')}: {z.get('is')}"[:180] for z in zones or [] if z.get("is")]
    if described:
        text += " The place's parts: " + "; ".join(described[:8]) + "."
    mood = " ".join(str(story.get(k) or "") for k in ("mood", "purpose", "logline")).strip()
    if mood:
        text += f" The story of this place: {mood[:500]} Paint its mood in the palette, light, weathering and details."
    goal = str(look.get("goals") or "").strip()
    if goal:
        text += f" Look: {goal[:300]}"
    text += (" Roads worn with ruts, water with banks and ripples, woods as dense masses of overlapping crowns, ground "
             "with natural variation and no visible cells or grid; one light from the upper left; one palette and "
             "pixel size everywhere. No text, no labels, no UI, no characters.")
    return text


def repaint_blockout_prompt(place: dict, story: dict, look: dict, legend: dict | None = None) -> str:
    """A short prompt that makes even an editing model (FLUX.2 klein) repaint the block-out completely: it
    says the image is a rough draft to be replaced, puts the mood first, and gives the colour key as one
    brief line per kind (the kinds' plain names, not their long descriptions), so the prompt stays short."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    mood = " ".join(str(story.get(k) or "") for k in ("mood",)).strip() or str(story.get("purpose") or "")[:200]
    text = (f"Repaint this image as a finished, hand-crafted isometric pixel-art game map of a {kind} ({name}). "
            f"Mood: {mood[:300]} " if mood else
            f"Repaint this image as a finished, hand-crafted isometric pixel-art game map of a {kind} ({name}). ")
    text += ("The image is only a rough colour-coded draft: replace all of its flat colours and simple shapes with "
             "real, detailed things, materials, light and shadow; none of the draft's colours may remain. ")
    things = [t for t in (legend or {}).get("things", []) if t.get("kind")]
    if things:
        text += "In the draft: " + ", ".join(f"{t['colour']} = {t['kind']}" for t in things[:14]) + ". "
    lines = (legend or {}).get("lines", [])
    if lines:
        text += "Thin strips are " + " and ".join(l.get("kind", "fence") + "s" for l in lines) + "; blue is water, brown cells roads. "
    text += ("Keep every building, road, river, bridge, fence and tree where it is, with the same camera and the "
             "diamond-shaped map. No text, no UI, no characters.")
    return text


# How each model paints a place from its plan's block-out best (tests/quality/bfl_results/settlement/README.md):
# "prompt" is which prompt ("repaint": repaint_prompt, the block-out treated as a rough draft, no colour key;
# "blockout": blockout_prompt, with the colour key); "input_width" scales the block-out up before it's sent
# (an editing model redraws a bigger input instead of copying it pixel for pixel); "notes" what was seen.
MODEL_RECIPES: dict[str, dict] = {
    "flux-2-klein-4b": {"prompt": "repaint", "input_width": 4096,
                        "notes": "an editing model: with the colour key and an input at its own size it copies the "
                                 "block-out; as a rough draft, scaled up, it repaints (layout 9, placement 8, fit 8, quality 8)"},
    "flux-3-image": {"prompt": "blockout", "input_width": None,
                     "notes": "repaints from the colour key and keeps the layout (layout 9, placement 8, fit 10, quality 9)"},
    "flux-3-image-klein": {"prompt": "repaint", "input_width": 4096,
                           "notes": "an editing model like FLUX.2 klein; with the colour key it kept the key colours"},
    "flux-2-pro": {"prompt": "blockout", "input_width": None,
                   "notes": "looks good but drifts from the layout and swaps kinds (stalls for houses)"},
    "flux-2-max": {"prompt": "blockout", "input_width": None,
                   "notes": "looks good but drifts from the layout and swaps kinds (stalls, market buildings for houses)"},
}


def refine_prompt(place: dict, story: dict, look: dict) -> str:
    """The second pass: a painting whose layout and contents are right (FLUX.3 Image's, from the plan) given
    richer rendering by a model that renders better (FLUX.2 max/pro, klein): nothing moves, nothing changes
    kind; only the drawing gains detail, materials, light and depth."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    mood = " ".join(str(story.get(k) or "") for k in ("mood", "purpose")).strip()
    text = (f"Refine this isometric game map of a {kind} ({name}) into lavish, hand-crafted, high-detail isometric "
            "pixel art, as in the best classic isometric RPGs. EVERYTHING STAYS: every building, road, path, river, "
            "bridge, fence, field, tree, rock and prop keeps its exact position, size, shape, orientation and kind "
            "(a house stays a house, a warehouse a warehouse, a stall a stall); nothing is added, removed or turned "
            "into something else; the same camera, framing and diamond-shaped map. ONLY THE RENDERING IMPROVES: "
            "richer textures (roof tiles and thatch, timber, stone, planks), weathering and grime, small details on "
            "and around buildings, depth with soft shadows and ambient occlusion, water with reflections and ripples, "
            "foliage with layered leaves, ground with mud, puddles, stones and grass tufts.")
    if mood:
        text += f" Mood: {mood[:300]}"
    goal = str(look.get("goals") or "").strip()
    if goal:
        text += f" Look: {goal[:200]}"
    return text + " No text, no UI, no characters."



def counts_sentence(legend: dict | None, with_colours: bool = False) -> str:
    """The plan's buildings and big things counted, so a model adds and swaps nothing: "Exactly 8 houses and 8
    warehouses, each where a box stands; no other buildings." (with the key colours only for models that
    read a key without keeping its colours)."""
    things = (legend or {}).get("things", [])
    buildings = [t for t in things if "roof" in t.get("shape", "")]
    props = [t for t in things if t.get("shape") == "small box"]
    if not buildings:
        return ""
    def one(t):
        name = t["kind"] + ("s" if t.get("count", 1) != 1 else "")
        return f"{t.get('count', 1)} {name}" + (f" ({t['colour']})" if with_colours else "")
    text = "Exactly " + ", ".join(one(t) for t in buildings) + ", each where the draft has a box with a roof; no other buildings anywhere."
    if props:
        text += " Small things: " + ", ".join(one(t) for t in props) + "; no others (no stalls, carts or tents the draft doesn't have)."
    return text + " Where the draft has trees, woods, grass or water, there are no buildings."


def fill(api_key: str, image_png: bytes, mask_png: bytes, prompt: str, timeout_s: float = 300.0) -> bytes:
    """Inpainting (FLUX.1 Fill [pro]): the white of `mask_png` repainted as the prompt says, the rest kept."""
    headers = {"x-key": api_key, "accept": "application/json"}
    body = {"image": _b64(image_png), "mask": _b64(mask_png), "prompt": prompt, "output_format": "png", "safety_tolerance": 2}
    with httpx.Client(timeout=120) as client:
        r = client.post(f"{BASE}/flux-pro-1.0-fill", json=body, headers=headers)
        if r.status_code != 200:
            raise ProviderError(f"BFL fill: {r.status_code} {r.text[:300]}")
        task = r.json()
        polling = task.get("polling_url") or f"{BASE}/get_result?id={task.get('id')}"
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            time.sleep(1.5)
            p = client.get(polling, headers=headers)
            if p.status_code != 200:
                continue
            answer = p.json()
            if answer.get("status") == "Ready":
                return client.get((answer.get("result") or {}).get("sample")).content
            if answer.get("status") in ("Error", "Failed", "Content Moderated", "Request Moderated"):
                raise ProviderError(f"BFL fill: {answer.get('status')}")
        raise ProviderError("BFL fill: no image in time")


def where_words(placed: list[dict], size: list[int]) -> str:
    """Where each kind of building and thing stands in the picture, in plain words ("8 warehouses: 5 in the lower
    right, 3 at the bottom"), from the block-out's boxes, so a model knows which building is which."""
    w, h = size
    def region(box):
        cx, cy = (box[0] + box[2] / 2) / w, (box[1] + box[3] / 2) / h
        row = "top" if cy < 0.36 else "bottom" if cy > 0.64 else "middle"
        col = "left" if cx < 0.36 else "right" if cx > 0.64 else "centre"
        return {("middle", "centre"): "in the centre"}.get((row, col), f"in the {row} {col}" if row != "middle" else f"in the middle {col}")
    by_kind: dict[str, dict[str, int]] = {}
    for p in placed:
        if p.get("kind") in ("item",):
            continue
        by_kind.setdefault(p["kind"], {})
        r = region(p["box"])
        by_kind[p["kind"]][r] = by_kind[p["kind"]].get(r, 0) + 1
    parts = []
    for kind, regions in by_kind.items():
        n = sum(regions.values())
        parts.append(f"{n} {kind}{'s' if n != 1 else ''} (" + ", ".join(f"{c} {r}" for r, c in regions.items()) + ")")
    return "; ".join(parts)



# Preferences ▸ Paint Provider (tests/quality/bfl_results/settlement): the provider's model options, each a recipe
# that paints a place from its plan's block-out. "flux-3-to-klein" is two passes: FLUX.3 Image paints from the
# block-out (layout and kinds), then FLUX.2 klein repaints its painting at its own size (the look).
PAINT_PROVIDERS: dict[str, dict[str, str]] = {
    "bfl": {
        "flux-3-image": "FLUX.3 Image: reads the plan best (layout, what each building is)",
        "flux-2-klein-4b": "FLUX.2 klein: the most attractive look, cheap; follows the layout loosely",
        "flux-2-max": "FLUX.2 max: rich art; drifts from the layout",
        "flux-3-to-klein": "FLUX.3 Image, then klein: FLUX.3's layout with klein's look (two images)",
        "flux-3-to-pro": "FLUX.3 Image, then FLUX.2 pro: FLUX.3's layout refined by FLUX.2 pro, the block-out as its kind map (two images)",
    },
}


def _png_bytes(image) -> bytes:
    import io as _io
    out = _io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def _four_mp(w: int, h: int) -> dict:
    k = (4_190_000 / (w * h)) ** 0.5
    return {"width": int(w * k) // 16 * 16, "height": int(h * k) // 16 * 16}


def paint_place(api_key: str, model: str, blockout_png: bytes, legend: dict, place: dict, story: dict, look: dict,
                seed: int = 7) -> tuple[bytes, list[dict]]:
    """A place painted from its block-out by one Paint Provider option. Returns the PNG and the steps taken (each
    model and prompt), for the build's log."""
    import io as _io
    from PIL import Image as _Image
    src = _Image.open(_io.BytesIO(blockout_png)).convert("RGB")
    size = _four_mp(src.width, src.height)
    steps: list[dict] = []
    if model in ("flux-3-image", "flux-2-max"):
        # The colour key, with each kind's count (prompt upsampling off for FLUX.2 max, in BflPainter).
        prompt = blockout_prompt(place, story, look, None, legend) + " " + counts_sentence(legend, with_colours=True)
        steps.append({"model": model, "prompt": prompt})
        return BflPainter(api_key, model).repaint(blockout_png, prompt, seed=seed, **size), steps
    if model == "flux-2-klein-4b":
        # klein copies a flat input it is told about: the block-out as a rough draft, scaled up, no colour key.
        big = src.resize((4096, round(4096 * src.height / src.width)), _Image.LANCZOS)
        prompt = repaint_prompt(place, story, look) + " " + counts_sentence(legend)
        steps.append({"model": model, "prompt": prompt})
        return BflPainter(api_key, model).repaint(_png_bytes(big), prompt, seed=seed, **size), steps
    if model == "flux-3-to-klein":
        first = blockout_prompt(place, story, look, None, legend)
        painted = BflPainter(api_key, "flux-3-image").repaint(blockout_png, first, seed=seed, **size)
        steps.append({"model": "flux-3-image", "prompt": first})
        second = repaint_prompt(place, story, look) + " " + counts_sentence(legend)
        steps.append({"model": "flux-2-klein-4b", "prompt": second})
        return BflPainter(api_key, "flux-2-klein-4b").repaint(painted, second, seed=seed, **size), steps
    if model == "flux-3-to-pro":
        # FLUX.3 Image paints the plan; FLUX.2 pro refines its painting, the block-out as the second image saying what
        # each building is (its colour key), where each kind stands in words, and the counts (prompt upsampling off).
        first = blockout_prompt(place, story, look, None, legend)
        painted = BflPainter(api_key, "flux-3-image").repaint(blockout_png, first, seed=seed, **size)
        steps.append({"model": "flux-3-image", "prompt": first})
        nature = ("tree", "bush", "flower", "crop", "plant", "grass", "item")
        key_lines = "; ".join(f"{t['colour']} = {t['kind']}" for t in legend.get("things", []) if t.get("kind") not in nature)
        second = (refine_prompt(place, story, look) + " WHAT EACH BUILDING IS: the second image is a colour-coded map of the same "
                  f"place, aligned with the first: each coloured box marks one building or thing ({key_lines}). Draw every "
                  "building in the first image as exactly the kind its box in the second image says, with the look that kind "
                  "has (as the game's ontology describes it). Never draw the second image's colours. Where they are: "
                  + where_words(legend.get("placed", []), legend.get("size", [src.width, src.height])) + ". " + counts_sentence(legend))
        steps.append({"model": "flux-2-pro", "prompt": second})
        return BflPainter(api_key, "flux-2-pro").repaint(painted, second, references=[blockout_png], seed=seed, **size), steps
    raise ProviderError(f"Unknown paint model {model}: one of {', '.join(PAINT_PROVIDERS['bfl'])}")
