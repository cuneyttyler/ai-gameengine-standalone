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

def blockout_prompt(place: dict, story: dict, look: dict, legend: dict | None = None, notes: str = "") -> str:
    """The prompt that paints a place from its block-out: the colour key (each colour the kind it stands for, in
    the ontology's words), the ground, the story's mood and the look; the layout kept exactly."""
    name = place.get("name") or "the place"
    kind = place.get("type") or "place"
    text = (
        f"Paint this isometric game map of a {kind} ({name}) as finished, hand-crafted art, as in classic isometric "
        "RPGs. The image is a colour-coded BLOCK-OUT of the place's plan, not art: each colour stands for one kind of "
        "thing (the colour key below), and its shape says how it stands. KEEP THE LAYOUT EXACTLY: the same camera and "
        "framing, the same diamond-shaped map, and every building, road, river, bridge, fence, tree and thing at the "
        "same position, size and orientation; add nothing large, remove nothing. Replace every coloured shape by the "
        "real thing its colour stands for, drawn in full detail; the key's colours themselves must not show.")
    things = (legend or {}).get("things", [])
    if things:
        text += " COLOUR KEY: " + "; ".join(f"{t.get('colour')} {t.get('shape')} = {t.get('what')}" for t in things)[:2600] + "."
    lines = (legend or {}).get("lines", [])
    if lines:
        text += " " + "; ".join(f"{l.get('shape')} = {l.get('kind')}" for l in lines) + "."
    grounds = (legend or {}).get("ground", [])
    if grounds:
        text += " The ground: " + ", ".join(f"{g.get('hex')} cells = {g.get('terrain')}" for g in grounds) + " (brown cells across water are a bridge)."
    mood = " ".join(str(story.get(k) or "") for k in ("mood", "purpose", "logline")).strip()
    if mood:
        text += f" The story of this place: {mood[:500]} Paint its mood in the palette, light, weathering and details."
    goal = str(look.get("goals") or look.get("style") or "").strip()
    if goal:
        text += f" Look: {goal[:300]}"
    if notes.strip():
        text += f" Also: {notes.strip()[:600]}"
    text += (" Roads worn with ruts, water with banks and ripples, woods as dense masses of overlapping crowns, ground "
             "with natural variation and no visible cells or grid; one light from the upper left; one palette "
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


def paint_place(painter: BflPainter, blockout_png: bytes, legend: dict, place: dict, story: dict, look: dict,
                notes: str = "", seed: int = 7) -> tuple[bytes, str]:
    """A place painted from its block-out; the painting at the block-out's own size (so its pixels line up with the
    block-out's id map) and the prompt."""
    import io
    from PIL import Image
    src = Image.open(io.BytesIO(blockout_png)).convert("RGB")
    prompt = (blockout_prompt(place, story, look, legend, notes) + " " + counts_sentence(legend)).strip()
    png = painter.repaint(blockout_png, prompt, seed=seed, **_four_mp(src.width, src.height))
    painted = Image.open(io.BytesIO(png)).convert("RGB")
    if painted.size != src.size:
        painted = painted.resize(src.size, Image.LANCZOS)
    out = io.BytesIO()
    painted.save(out, format="PNG")
    return out.getvalue(), prompt
