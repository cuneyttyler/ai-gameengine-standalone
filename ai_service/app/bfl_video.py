"""Animation by BFL's FLUX 3 video (docs/assets/bfl-sprites.md, builder_tests/bfl_video): a pose made to move with
image-to-video. The pose is the first and the last keyframe, so the clip loops; the camera is held still on a flat
magenta ground; one cycle of the motion is found in the clip (where the pose comes back), frames taken from it evenly
and cut out of the magenta. Same results as the other animators: PNG frames, `size` px square."""
from __future__ import annotations

import base64
import io
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

from .bfl import BflPainter
from .bfl_sprites import _FACING, _cut, _fit

VIDEO_MODEL = "flux-3-video"
SECONDS = 5
# How an action shows at sprite size (a gentle walk under a robe doesn't read): big, clear motion, in place.
_MOTION = {
    "walk": "walks in place on the spot with a big, clearly visible, brisk walking cycle, like a game character's walk animation: "
            "each knee lifting high, the feet stepping well apart, clothes swinging with each step, the body bobbing up and down, "
            "the free arm swinging wide",
    "run": "runs in place on the spot with a big, clearly visible running cycle: knees driving high, arms pumping, the body leaning a little forward",
    "idle": "stands at rest, breathing: the chest and shoulders rising and falling gently, a slight sway, blinking",
    "attack": "attacks: a clear wind-up, a strong strike at full reach, a follow-through, then back to the starting stance",
    "cast": "casts a spell: hands raised, light gathering between them, the spell released with a flash, then back to the starting stance",
    "hurt": "is hit: recoils sharply, staggers, then recovers to the starting stance",
    "jump": "jumps in place: crouches, springs up high, lands, and settles back to the starting stance",
}


def _motion(action: str) -> str:
    from .charsheet import clip_for
    clip = clip_for(action)
    return _MOTION.get(clip, f"{action.strip()}, in place, a clear motion that ends back in the starting pose")


def _ffmpeg() -> str:
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:  # the system's
        return "ffmpeg"


def _frames(mp4: bytes) -> list[Image.Image]:
    with tempfile.TemporaryDirectory() as tmp:
        clip = Path(tmp) / "clip.mp4"
        clip.write_bytes(mp4)
        subprocess.run([_ffmpeg(), "-loglevel", "error", "-i", str(clip), str(Path(tmp) / "f%04d.png")], check=True,
                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return [Image.open(f).convert("RGB") for f in sorted(Path(tmp).glob("f*.png"))]


def _cycle(frames: list[Image.Image]) -> tuple[int, int]:
    """(start, length) of one cycle: from a frame past the start's settling to where the pose comes back closest."""
    import numpy as np
    small = [np.asarray(f.resize((96, 96)), dtype=np.float32) for f in frames]
    start = min(len(small) - 1, len(small) // 6)
    fps = max(1.0, len(frames) / SECONDS)
    lo, hi = start + int(0.5 * fps), min(len(small) - 1, start + int(2.4 * fps))
    if hi <= lo:
        return 0, max(1, len(frames))
    best = min(range(lo, hi + 1), key=lambda i: float(np.abs(small[i] - small[start]).mean()))
    return start, max(2, best - start)


def animate(api_key: str, picture: bytes, action: str, description: str = "", direction: str = "south", frames: int = 8,
            size: int = 64, seed: int | None = None) -> list[bytes]:
    """`picture` (a sprite on transparency) moving: `frames` PNGs of one cycle, `size` px square."""
    pose = Image.open(io.BytesIO(picture)).convert("RGBA")
    part = pose.crop(pose.getbbox() or (0, 0, pose.width, pose.height))
    k = 640 / max(1, part.height)
    part = part.resize((max(1, round(part.width * k)), 640), Image.LANCZOS)
    square = Image.new("RGB", (1024, 1024), (255, 0, 255))
    square.paste(part, ((1024 - part.width) // 2, 1024 - 150 - part.height), part)
    buf = io.BytesIO()
    square.save(buf, "PNG")
    key = base64.b64encode(buf.getvalue()).decode()
    what = description.strip() or "the game character in the picture"
    prompt = (f"A game sprite: {what}, {_FACING.get(direction, direction)}. It {_motion(action)}. It keeps facing the same way the whole "
              "time and never moves across the frame. The camera is completely still: no camera movement, no zoom, no cuts. It stays "
              "at the same place and size in the frame. The flat magenta background stays plain and unchanged.")
    body = {"prompt": prompt, "mode": "i2v", "keyframes": [key, key], "duration": SECONDS, "resolution": "hd", "aspect_ratio": "1:1",
            "generate_audio": False}
    # BFL caps a low-credit account at one task at a time (429): waited for, then asked again.
    import time
    for attempt in range(12):
        try:
            clip = BflPainter(api_key, VIDEO_MODEL, timeout_s=900)._run(body)
            break
        except Exception as error:  # noqa: BLE001 - only a busy answer is retried
            if " 429 " not in str(error) or attempt == 11:
                raise
            time.sleep(15)
    every = _frames(clip)
    if not every:
        raise RuntimeError("BFL's video had no frames")
    start, length = _cycle(every)
    picked = [every[min(len(every) - 1, start + round(i * length / frames))] for i in range(frames)]
    cut = {str(i): _cut(_png(f)) for i, f in enumerate(picked)}
    fitted = _fit(cut, size)
    return [_png(fitted[str(i)]) for i in range(frames) if str(i) in fitted]


def _png(image: Image.Image) -> bytes:
    buf = io.BytesIO()
    image.save(buf, "PNG")
    return buf.getvalue()
