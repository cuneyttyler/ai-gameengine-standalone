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
            size: int = 64, seed: int | None = None, resolution: str = "hd", clip_file: Path | None = None,
            info: dict | None = None, play_fps: float | None = None) -> list[bytes]:
    """`picture` (a sprite on transparency) moving: `frames` PNGs of one cycle, `size` px square. `clip_file`: the clip kept
    there (and taken from there when it's already made: the frames cut again without asking BFL). `info` gets the cycle's
    "seconds" and the "fps" that plays the frames at its real speed (frames ÷ seconds). `play_fps`: instead of `frames`, as
    many frames as play the cycle at its real speed at that rate (6 to 24)."""
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
    body = {"prompt": prompt, "mode": "i2v", "keyframes": [key, key], "duration": SECONDS, "resolution": resolution, "aspect_ratio": "1:1",
            "generate_audio": False}
    # BFL caps a low-credit account at one task at a time (429): waited for, then asked again.
    import time
    clip = clip_file.read_bytes() if clip_file and clip_file.exists() else b""
    for attempt in range(0 if clip else 12):
        try:
            clip = BflPainter(api_key, VIDEO_MODEL, timeout_s=900)._run(body)
            if clip_file:
                clip_file.write_bytes(clip)
            break
        except Exception as error:  # noqa: BLE001 - only a busy answer is retried
            if " 429 " not in str(error) or attempt == 11:
                raise
            time.sleep(15)
    every = _frames(clip)
    if not every:
        raise RuntimeError("BFL's video had no frames")
    start, length = _cycle(every)
    if play_fps:
        frames = max(6, min(24, round(play_fps * length * SECONDS / len(every))))
    if info is not None:
        seconds = length * SECONDS / len(every)
        info.update({"seconds": seconds, "fps": frames / max(0.1, seconds)})
    picked = [every[min(len(every) - 1, start + round(i * length / frames))] for i in range(frames)]
    cut = {str(i): _cut(_png(f)) for i, f in enumerate(picked)}
    fitted = _fit(cut, size)
    return [_png(fitted[str(i)]) for i in range(frames) if str(i) in fitted]


def animate_object(api_key: str, picture: Image.Image, action: str, description: str = "", frames: int = 12, loop: bool = False,
                   resolution: str = "hd") -> list[Image.Image]:
    """A thing (not a person: a door, a chest, a lever, a flag) moving, by FLUX 3 video (docs/assets/animations.md §4.1):
    `picture` at 80% of a magenta square, the first keyframe (`loop`: the last too, so a flag or a flame loops; else it
    ends where the motion does, a door open); `frames` frames taken evenly from the clip (a loop: one cycle of it), each
    cut out of the magenta and put back at the picture's own size, where it was."""
    picture = picture.convert("RGBA")
    w, h = picture.size
    side = 1024
    k = side * 0.8 / max(1, w, h)
    sw, sh = max(1, round(w * k)), max(1, round(h * k))
    ox, oy = (side - sw) // 2, (side - sh) // 2
    square = Image.new("RGB", (side, side), (255, 0, 255))
    square.paste(picture.resize((sw, sh), Image.LANCZOS), (ox, oy), picture.resize((sw, sh), Image.LANCZOS))
    buf = io.BytesIO()
    square.save(buf, "PNG")
    key = base64.b64encode(buf.getvalue()).decode()
    what = description.strip() or "the thing in the picture"
    prompt = (f"A game sprite of {what}, in its art style. {action}. Only it moves, as described; nothing else appears, no people. "
              "The camera is completely still: no camera movement, no zoom, no cuts. It stays at the same place and size in "
              "the frame. The flat magenta background stays plain and unchanged.")
    body = {"prompt": prompt, "mode": "i2v", "keyframes": [key, key] if loop else [key], "duration": SECONDS,
            "resolution": resolution, "aspect_ratio": "1:1", "generate_audio": False}
    import time
    clip = b""
    for attempt in range(12):  # a low-credit account: one task at a time (429), waited for
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
    start, length = _cycle(every) if loop else (0, len(every) - 1)
    picked = [every[min(len(every) - 1, start + round(i * length / max(1, frames - 1)))] for i in range(frames)]
    out = []
    for f in picked:
        f = f.resize((side, side), Image.LANCZOS) if f.size != (side, side) else f
        cut = _cut(_png(f.crop((ox, oy, ox + sw, oy + sh))))
        out.append(cut.resize((w, h), Image.LANCZOS))
    return out


def _png(image: Image.Image) -> bytes:
    buf = io.BytesIO()
    image.save(buf, "PNG")
    return buf.getvalue()

