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
            f"thing (the colour key below), and its shape says how it stands. {layout} {EMPTY_RULE}")
    if guide.strip():
        text += f" THIS KIND OF PLACE: {guide.strip()}"
    if written.strip():
        text += f" THIS PLACE: {written.strip()[:900]}"
    things = (legend or {}).get("things", []) if objects else []
    if things:
        text += " COLOUR KEY: " + "; ".join(f"{t.get('colour')} {t.get('shape')} = {t.get('what')}" for t in things)[:2600] + "."
    lines = (legend or {}).get("lines", [])
    if lines:
        text += " " + "; ".join(f"{l.get('shape')} = {l.get('kind')}" for l in lines) + "."
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
    if guide.strip():
        text += guide.strip() + " "
    if written.strip():
        text += written.strip()[:500] + " "
    grounds = [g for g in (legend or {}).get("ground", []) if not g.get("empty")]
    if grounds and not objects:
        text += "In the draft: " + ", ".join(f"{g.get('hex')} = {g.get('terrain')}" for g in grounds) + ". "
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


def paint_place(key: str, model: str, blockout_png: bytes, legend: dict, place: dict, story: dict, look: dict,
                notes: str = "", seed: int = 7, reference: bytes | None = None, guide: str | None = None, written: str = "",
                objects: bool = True) -> tuple[bytes, str]:
    """A place painted from its block-out by one of PAINT_MODELS; the painting at the block-out's own size (so its
    pixels line up with the block-out's id map) and the prompt(s).
    - flux-3-image: the block-out with the colour key and the counts (keeps the layout best).
    - flux-2-klein-4b: klein copies a flat input it's told about: the block-out as a rough draft, scaled up, a short key.
    - flux-2-pro / flux-2-max: the block-out with the colour key and the counts, as FLUX.3 Image (prompt upsampling off);
      they look good but can drift from the layout (Phase 25's tests).
    - flux-3-to-klein / flux-3-to-pro: FLUX.3 Image paints the plan, then klein or FLUX.2 pro repaints its look, the
      block-out beside it (pro) saying what each building is."""
    import io
    from PIL import Image
    src = Image.open(io.BytesIO(blockout_png)).convert("RGB")
    size = _four_mp(src.width, src.height)
    # The ontology's guide for its kind unless one is given; the place's own words; the key only for things shown.
    guide = paint_guide(str(place.get("type") or "")) if guide is None else guide
    keyed = (blockout_prompt(place, story, look, legend, notes, guide, written, objects) + " " +
             (counts_sentence(legend) if objects else "")).strip()
    draft = draft_prompt(place, story, look, legend, notes, guide, written, objects)
    # A picture given to paint after (the Build panel's "as a reference"): its look, on the plan's layout.
    refs = [reference] if reference else []
    after = (" The second image is a picture of how this place should look: paint it in that picture's look, palette and "
             "details (or as that picture, changed as the notes say), but keep the first image's layout exactly.") if reference else ""
    keyed += after
    draft += after
    if model in ("flux-2-pro", "flux-2-max"):
        png, prompt = BflPainter(key, model).repaint(blockout_png, keyed, references=refs, seed=seed, **size), keyed
    elif model == "flux-2-klein-4b":
        big = src.resize((4096, round(4096 * src.height / src.width)), Image.LANCZOS)
        buf = io.BytesIO()
        big.save(buf, format="PNG")
        png, prompt = BflPainter(key, "flux-2-klein-4b").repaint(buf.getvalue(), draft, references=refs, seed=seed, **size), draft
    else:
        png, prompt = BflPainter(key, "flux-3-image").repaint(blockout_png, keyed, references=refs, seed=seed, **size), keyed
        if model == "flux-3-to-klein":
            png = BflPainter(key, "flux-2-klein-4b").repaint(png, draft, seed=seed, **size)
            prompt += "\n---\n" + draft
        elif model == "flux-3-to-pro":
            things = "; ".join(f"{t['colour']} = {t['kind']}" for t in (legend or {}).get("things", []) if t.get("kind"))
            refine = ("Refine this painted isometric game map into finished, hand-crafted art: richer materials, light and "
                      "detail, one consistent palette. Keep the camera, the layout and every building, road, river, fence and "
                      "tree exactly where it is. The second image is the place's colour-coded plan, aligned with the first: "
                      f"each coloured box marks one building or thing ({things}). Draw each as the kind its colour says; never "
                      "draw the second image's colours. " + counts_sentence(legend) + " No text, no UI, no characters.")
            png = BflPainter(key, "flux-2-pro").repaint(png, refine, references=[blockout_png], seed=seed, **size)
            prompt += "\n---\n" + refine
    painted = Image.open(io.BytesIO(png)).convert("RGB")
    if painted.size != src.size:
        painted = painted.resize(src.size, Image.LANCZOS)
    out = io.BytesIO()
    painted.save(out, format="PNG")
    return out.getvalue(), prompt
