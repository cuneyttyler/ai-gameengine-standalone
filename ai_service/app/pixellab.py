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

    # The account can't use style-v2 (not Pro, 402/401/403): not asked again this session.
    _style_refused = False

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
        side = max(32, min(400, int(o.get("height", 128)))) // 4 * 4  # PixelLab: sides divisible by 4 (2 for small ones)
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
        # The project's own pieces of this kind as style references: the new one matches their look
        # (generate-with-style-v2, a Pro endpoint). If that fails, pixflux below.
        if not tile and o.get("references") and not PixelLabImageGenerator._style_refused:
            if (data := self._with_style(prompt, o["references"][:4], side)) is not None:
                return GeneratedImage(data, "pixellab style-v2", prompt)
        answer = self._call("POST", "/create-image-pixflux", body)
        try:
            data = base64.b64decode(answer["image"]["base64"])
        except (KeyError, TypeError, ValueError) as error:
            raise ProviderError("PixelLab didn't return an image") from error
        return GeneratedImage(data, "pixellab pixflux", prompt)

    def _with_style(self, prompt: str, references: list, side: int) -> bytes | None:
        """An image in the look of the references, or None (pixflux then). The output's size is the
        references' largest side, so each is scaled to the size wanted, on a square canvas. A job not
        done in time is cancelled: a waiting one holds the account's concurrency slot, and every other
        PixelLab call (pixflux too) would queue behind it."""
        side = max(16, min(512, side))
        styles = []
        try:
            for path in references:
                with Image.open(path) as source:
                    image = source.convert("RGBA")
                bbox = image.getbbox()
                if bbox:
                    image = image.crop(bbox)
                scale = side / max(image.width, image.height)
                image = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.Resampling.NEAREST)
                square = Image.new("RGBA", (side, side))
                square.paste(image, ((side - image.width) // 2, side - image.height))
                out = io.BytesIO()
                square.save(out, format="PNG")
                styles.append({"image": {"type": "base64", "base64": base64.b64encode(out.getvalue()).decode(), "format": "png"},
                               "width": side, "height": side})
        except (OSError, ValueError):
            return None
        if not styles:
            return None
        try:
            started = self._call("POST", "/generate-with-style-v2", {
                "style_images": styles, "description": prompt[:1999], "no_background": True,
                "style_description": "the same pixel size, outline, shading and palette as these pieces of the same game"})
        except ProviderError as error:
            if any(f"answered {code}" in str(error) for code in (401, 402, 403)):
                PixelLabImageGenerator._style_refused = True
            return None
        data = _first_image(started)
        job = started.get("background_job_id")
        deadline = time.time() + 90  # then pixflux: a build isn't held for minutes
        try:
            while data is None and job and time.time() < deadline:
                time.sleep(5)  # as the API asks: every 5-10 s
                state = self._call("GET", f"/background-jobs/{job}")
                status = str(state.get("status", "")).lower()
                if status == "completed":
                    data = _first_image(state.get("last_response") or {})
                    return data
                if status == "failed":
                    return None
        except ProviderError:
            pass
        if job:
            try:
                self._call("DELETE", f"/background-jobs/{job}")  # frees the slot; a queued job isn't charged
            except ProviderError:
                pass
        return data

    def character(self, description: str, directions: int = 4, size: int = 64, view: str = "low top-down",
                  isometric: bool = False, palette: list[str] | None = None, wait_s: float = 240.0) -> dict[str, bytes]:
        """A character from each direction (4: south, west, east, north; or 8), as PNG bytes by direction."""
        path = "/create-character-with-8-directions" if directions == 8 else "/create-character-with-4-directions"
        size = int(size) // 2 * 2  # even sides
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


def _first_image(answer: dict) -> bytes | None:
    """The first image of an answer (generate-with-style returns a list; some endpoints one)."""
    candidates = []
    if isinstance(answer.get("images"), list):
        candidates = answer["images"]
    elif isinstance(answer.get("image"), dict):
        candidates = [answer["image"]]
    for c in candidates:
        if isinstance(c, dict) and isinstance(c.get("image"), dict):
            c = c["image"]  # {image: {base64}} items
        if isinstance(c, dict) and c.get("base64"):
            try:
                text = str(c["base64"])
                return base64.b64decode(text.split(",", 1)[1] if text.startswith("data:") else text)
            except (TypeError, ValueError):
                continue
    return None


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


def animate_v3(generator: "PixelLabImageGenerator", picture: bytes, action: str, frames: int = 8, side: int = 256,
               wait_s: float = 600.0) -> list[bytes]:
    """`picture` moving (Animate with text v3, docs/assets/bfl-sprites.md): fitted into a `side` square (at most 256) as
    both the first and the last frame, so it loops; `frames` frames (4-16, even), no background; PNG bytes each."""
    side = max(32, min(256, side))
    with Image.open(io.BytesIO(picture)) as source:
        image = source.convert("RGBA")
    part = image.crop(image.getbbox() or (0, 0, image.width, image.height))
    k = side * 0.9 / max(part.width, part.height)
    part = part.resize((max(1, round(part.width * k)), max(1, round(part.height * k))), Image.Resampling.LANCZOS)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(part, ((side - part.width) // 2, side - round(side * 0.04) - part.height), part)
    buf = io.BytesIO()
    square.save(buf, "PNG")
    frame = {"type": "base64", "base64": base64.b64encode(buf.getvalue()).decode(), "format": "png"}
    count = max(4, min(16, frames // 2 * 2))
    started = generator._call("POST", "/animate-with-text-v3", {"first_frame": frame, "last_frame": frame, "frame_count": count,
                                                                "action": action[:300], "no_background": True})
    job = started.get("background_job_id")
    if not job:
        raise ProviderError("PixelLab didn't start the animation")
    deadline = time.time() + wait_s
    while time.time() < deadline:
        state = generator._call("GET", f"/background-jobs/{job}")
        if state.get("status") == "completed":
            out = []
            for item in (state.get("last_response") or {}).get("images") or []:
                if isinstance(item, dict) and isinstance(item.get("image"), dict):
                    item = item["image"]
                text = str(item.get("base64", "")) if isinstance(item, dict) else ""
                if text:
                    out.append(base64.b64decode(text.split(",", 1)[1] if text.startswith("data:") else text))
            if not out:
                raise ProviderError("PixelLab finished the animation but sent no frames")
            return out
        if state.get("status") == "failed":
            raise ProviderError(f"PixelLab couldn't animate it: {state.get('last_response') or 'failed'}")
        time.sleep(3)
    raise ProviderError("PixelLab is still animating it: try again in a minute")
