"""PixelLab (https://api.pixellab.ai/v2): pixel-art images made for games. A sprite or a ground
tile from a description (create-image-pixflux: isometric, a transparent background, a forced
palette), and a character seen from 4 or 8 directions (create-character-with-N-directions, a
background job). PIXELLAB_API_KEY in ai_service/.env."""

from __future__ import annotations

import base64
import io
import time

from PIL import Image

from .images import GeneratedImage
from .providers import ProviderError

BASE = "https://api.pixellab.ai/v2"
DIRECTIONS_4 = ["south", "west", "east", "north"]
DIRECTIONS_8 = ["south", "south-east", "east", "north-east", "north", "north-west", "west", "south-west"]


def palette_image(colours: list[str]) -> dict | None:
    """A strip of the project's colours, for PixelLab's color_image (its art then keeps to them)."""
    if not colours:
        return None
    strip = Image.new("RGB", (len(colours) * 4, 4))
    for i, h in enumerate(colours):
        try:
            rgb = (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16))
        except ValueError:
            continue
        for x in range(i * 4, i * 4 + 4):
            for y in range(4):
                strip.putpixel((x, y), rgb)
    out = io.BytesIO()
    strip.save(out, format="PNG")
    return {"type": "base64", "base64": base64.b64encode(out.getvalue()).decode(), "format": "png"}


class PixelLabImageGenerator:
    name = "pixellab"
    takes_options = True  # generate(prompt, options): its view, size, background, palette

    def __init__(self, api_key: str, timeout_s: float = 180.0):
        self._key = api_key
        self._timeout = timeout_s

    def _call(self, method: str, path: str, body: dict | None = None) -> dict:
        import httpx

        try:
            response = httpx.request(method, BASE + path, json=body, timeout=self._timeout,
                                     headers={"Authorization": f"Bearer {self._key}"})
        except Exception as error:
            raise ProviderError(f"PixelLab couldn't be reached: {error}") from error
        if response.status_code >= 400:
            detail = response.text[:300]
            raise ProviderError(f"PixelLab answered {response.status_code}: {detail}")
        return response.json()

    def generate(self, prompt: str, options: dict | None = None) -> GeneratedImage:
        """One image. Options: kind ("tile" or "object"), height (pixels), isometric, palette (hex colours)."""
        o = options or {}
        tile = o.get("kind") == "tile"
        side = max(32, min(400, int(o.get("height", 128))))
        body = {
            "description": prompt[:1800],
            "image_size": {"width": side, "height": side},
            "isometric": bool(o.get("isometric", not tile)) and not tile,
            "no_background": not tile,
            "outline": "lineless" if tile else "single color black outline",
            "shading": "medium shading",
            "detail": "medium detail",
        }
        if tile:
            body["view"] = "high top-down"
        if (colours := palette_image(o.get("palette", []))) is not None:
            body["color_image"] = colours
        answer = self._call("POST", "/create-image-pixflux", body)
        try:
            data = base64.b64decode(answer["image"]["base64"])
        except (KeyError, TypeError, ValueError) as error:
            raise ProviderError("PixelLab didn't return an image") from error
        return GeneratedImage(data, "pixellab pixflux", prompt)

    def character(self, description: str, directions: int = 4, size: int = 64, view: str = "low top-down",
                  isometric: bool = False, palette: list[str] | None = None, wait_s: float = 240.0) -> dict[str, bytes]:
        """A character from each direction (4: south, west, east, north; or 8), as PNG bytes by direction."""
        path = "/create-character-with-8-directions" if directions == 8 else "/create-character-with-4-directions"
        body = {"description": description[:1800], "image_size": {"width": size, "height": size}, "view": view,
                "isometric": isometric, "outline": "single color black outline", "shading": "basic shading", "detail": "medium detail"}
        if (colours := palette_image(palette or [])) is not None:
            body["color_image"] = colours
        started = self._call("POST", path, body)
        job = started.get("background_job_id")
        if not job:
            raise ProviderError("PixelLab didn't start the character")
        deadline = time.time() + wait_s
        while time.time() < deadline:
            state = self._call("GET", f"/background-jobs/{job}")
            if state.get("status") == "completed":
                return _images_by_direction(state.get("last_response") or {})
            if state.get("status") == "failed":
                raise ProviderError(f"PixelLab couldn't make the character: {state.get('last_response') or 'failed'}")
            time.sleep(3)
        raise ProviderError("PixelLab is still making the character: try again in a minute")


def as_reference(png: bytes, side: int = 64) -> dict:
    """An image as PixelLab's 64 x 64 reference: fitted inside the square, transparent round it."""
    with Image.open(io.BytesIO(png)) as source:
        image = source.convert("RGBA")
    image.thumbnail((side, side), Image.Resampling.NEAREST)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(image, ((side - image.width) // 2, side - image.height))
    out = io.BytesIO()
    square.save(out, format="PNG")
    return {"type": "base64", "base64": base64.b64encode(out.getvalue()).decode(), "format": "png"}


def animate(generator: "PixelLabImageGenerator", reference: bytes, description: str, action: str, direction: str = "south",
            view: str = "low top-down", palette: list[str] | None = None) -> list[bytes]:
    """Four frames of `reference` doing `action` (animate-with-text, 64 x 64), as PNG bytes."""
    body = {"image_size": {"width": 64, "height": 64}, "description": description[:900], "action": action[:300],
            "view": view, "direction": direction, "reference_image": as_reference(reference), "n_frames": 4}
    if (colours := palette_image(palette or [])) is not None:
        body["color_image"] = colours
    answer = generator._call("POST", "/animate-with-text", body)
    frames = []
    for f in answer.get("images", []):
        try:
            frames.append(base64.b64decode(f["base64"]))
        except (KeyError, TypeError, ValueError):
            continue
    if not frames:
        raise ProviderError("PixelLab sent no frames")
    return frames


def spritesheet(frames: list[bytes]) -> bytes:
    """The frames side by side, one row."""
    images = [Image.open(io.BytesIO(f)).convert("RGBA") for f in frames]
    w, h = max(i.width for i in images), max(i.height for i in images)
    sheet = Image.new("RGBA", (w * len(images), h), (0, 0, 0, 0))
    for k, i in enumerate(images):
        sheet.paste(i, (k * w, 0))
    out = io.BytesIO()
    sheet.save(out, format="PNG")
    return out.getvalue()


def _images_by_direction(answer: dict) -> dict[str, bytes]:
    """Each direction's image in a finished job's answer, wherever it puts them ({direction: {base64}}...)."""
    out: dict[str, bytes] = {}

    def visit(node, key: str = "") -> None:
        if isinstance(node, dict):
            b64 = node.get("base64")
            if isinstance(b64, str) and key in DIRECTIONS_8:
                try:
                    out[key] = base64.b64decode(b64)
                except ValueError:
                    pass
                return
            for k, v in node.items():
                visit(v, k if k in DIRECTIONS_8 else key)
        elif isinstance(node, list):
            for v in node:
                visit(v, key)

    visit(answer)
    return out
