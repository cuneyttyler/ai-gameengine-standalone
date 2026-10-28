"""AutoSprite (https://www.autosprite.io/docs/api): a picture animated, a character in all 8 directions at once.

A picture is uploaded as an AutoSprite character (free; `isHumanoid` said: a person, or a chest, a wolf…). An image is
animated by `POST /characters/{id}/spritesheets` (one view: the picture's own, side-on presets); a character by an
isometric pack, `POST /characters/{id}/packs`, every one of the 8 directions in one render (15 credits). Jobs are polled
(`GET /jobs/{id}`), the sheets read (`GET /spritesheets/{id}`: `sheetUrl`, `frameWidth`, `columns`, `frameCount`) and
cut into frames. Tested in builder_tests/autosprite."""
from __future__ import annotations

import io
import os
import re
import time
from typing import Callable

from PIL import Image

from .providers import ProviderError

BASE = "https://www.autosprite.io/api/v1"

# The motions it knows (docs/reference-animation-types, docs/api-spritesheets): a kind with no prompt; anything else is
# `custom`, said in words.
MOTIONS = ["idle", "walk", "run", "attack", "jump",
           "punch", "kick", "block", "hurt", "hit_react", "dodge", "dodge_lean", "death", "shoot",
           "cast", "pickup", "push", "pull", "throw",
           "wave", "sit", "kneel", "point", "sleep", "victory", "dance", "talk",
           "dash", "fall", "climb", "swim", "crouch", "roll", "fly", "slide"]
LOOPING = {"idle", "walk", "run", "swim", "fly", "dance", "sleep", "talk"}
# An isometric pack's sheets are named iso_<action>_<direction>.
PACK_DIRECTIONS = {"up": "north", "northeast": "north-east", "right": "east", "southeast": "south-east", "down": "south",
                   "southwest": "south-west", "left": "west", "northwest": "north-west"}
# Packs: these actions as they are; the presets too; the rest custom.
PACK_ACTIONS = {"idle", "walk", "run", "jump", "attack"}

# Said of a non-humanoid: animals, creatures that aren't people-shaped, things.
_NOT_HUMANOID = re.compile(
    r"\b(chest|box|crate|barrel|door|gate|chair|table|lamp|lantern|torch|fire|flame|campfire|candle|banner|flag|sign|tree|"
    r"bush|plant|flower|grass|rock|stone|crystal|gem|coin|potion|bottle|book|scroll|key|sword|shield|weapon|bow|chest|"
    r"cart|wagon|boat|ship|car|vehicle|machine|turret|cannon|trap|lever|fountain|well|water|smoke|portal|orb|egg|"
    r"wolf|dog|cat|horse|cow|pig|sheep|goat|chicken|bird|crow|eagle|owl|bat|rat|mouse|rabbit|fox|deer|bear|boar|snake|"
    r"spider|insect|bug|beetle|fish|shark|frog|toad|slime|blob|dragon|wyvern|drake|serpent|worm|crab|scorpion|"
    r"butterfly|bee|wasp|beast|animal|creature|monster|mount|pet|ghost|wisp|elemental|golem|tentacle|hydra)s?\b", re.I)
_HUMANOID = re.compile(
    r"\b(man|woman|men|women|boy|girl|child|kid|person|people|human|elf|dwarf|orc|goblin|troll|ogre|giant|knight|"
    r"warrior|soldier|guard|mage|wizard|witch|priest|monk|cleric|paladin|rogue|thief|archer|ranger|hunter|merchant|"
    r"farmer|villager|peasant|king|queen|prince|princess|lord|lady|noble|herbalist|healer|smith|blacksmith|baker|"
    r"innkeeper|bard|sailor|pirate|ninja|samurai|robot|android|zombie|skeleton|vampire|lich|demon|angel|hero|"
    r"heroine|npc|player|character|adventurer|traveler|traveller|elder|old|young|male|female)s?\b", re.I)


def api_key() -> str:
    return os.environ.get("AUTO_SPRITE_API_KEY") or os.environ.get("AUTOSPRITE_API_KEY") or ""


def is_humanoid(text: str, character: bool, provider=None) -> bool:
    """Whether what `text` describes is people-shaped (AutoSprite rigs it as one): the words it uses, else the LLM's
    answer, else a character is and a picture isn't."""
    text = text.replace("-", " ").replace("_", " ")
    people, things = bool(_HUMANOID.search(text)), bool(_NOT_HUMANOID.search(text))
    if people != things:
        return people
    if provider is not None and text.strip():
        try:
            import json
            answer = provider.complete([{"role": "user", "content":
                "Is this game sprite humanoid (people-shaped: a head, two arms, two legs, standing upright)? "
                f'Answer as JSON {{"humanoid": true}} or {{"humanoid": false}}.\nSprite: {text[:400]}'}])
            return bool(json.loads(answer.text).get("humanoid"))
        except Exception:  # noqa: BLE001 - the guess below stands
            pass
    return character


def motion_of(action: str) -> tuple[str, str]:
    """(`kind`, prompt): a motion it knows by name ("walking" → walk), else ("custom", the words)."""
    a = action.strip().lower()
    words = {"walking": "walk", "running": "run", "idle": "idle", "standing": "idle", "breathing": "idle",
             "attacking": "attack", "jumping": "jump", "dying": "death", "die": "death", "casting": "cast",
             "spell": "cast", "hit": "hurt", "hurting": "hurt", "waving": "wave", "dancing": "dance", "swimming": "swim",
             "flying": "fly", "climbing": "climb", "sleeping": "sleep", "talking": "talk", "shooting": "shoot"}
    key = a.replace(" ", "_")
    if key in MOTIONS:
        return key, ""
    for word, kind in words.items():
        if a == word or a.startswith(word + " ") and len(a.split()) <= 2:
            return kind, ""
    return "custom", action.strip()


class AutoSprite:
    def __init__(self, key: str, timeout_s: float = 120):
        self.key = key
        self.timeout_s = timeout_s

    def _call(self, method: str, path: str, **kw) -> dict:
        import httpx
        for _ in range(6):
            try:
                answer = httpx.request(method, BASE + path, headers={"x-api-key": self.key}, timeout=self.timeout_s, **kw)
            except httpx.HTTPError as error:
                raise ProviderError(f"AutoSprite: {error}") from error
            if answer.status_code == 429:  # its rate limit: wait as long as it says
                time.sleep(min(60.0, float(answer.headers.get("Retry-After", "10") or 10)))
                continue
            try:
                body = answer.json()
            except ValueError:
                body = {}
            if answer.status_code >= 400:
                error = body.get("error") if isinstance(body, dict) else None
                text = error.get("message") or error.get("code") if isinstance(error, dict) else answer.text[:200]
                raise ProviderError(f"AutoSprite {answer.status_code}: {text}")
            return body if isinstance(body, dict) else {}
        raise ProviderError("AutoSprite stayed busy (429)")

    def create(self, name: str, picture: bytes, humanoid: bool, description: str = "") -> str:
        """The picture as an AutoSprite character: its id."""
        name = (re.sub(r"[^A-Za-z0-9 _-]+", "", name)[:80] or "sprite") + f"-{int(time.time() * 1000) % 10**8}"
        body = self._call("POST", "/characters", data={"name": name, "isHumanoid": "true" if humanoid else "false",
                                                        "characterDescription": description[:1000]},
                          files={"image": ("picture.png", _png_on_white(picture), "image/png")})
        found = body.get("id") or body.get("characterId") or (body.get("character") or {}).get("id")
        if not found:
            raise ProviderError(f"AutoSprite made no character: {str(body)[:200]}")
        return str(found)

    def wait(self, job: str, on_status: Callable[[str], None] | None = None, wait_s: float = 900) -> dict:
        time.sleep(8)
        until = time.time() + wait_s
        while time.time() < until:
            state = self._call("GET", f"/jobs/{job}")
            status = state.get("status")
            if status == "succeeded":
                return state
            if status == "failed":
                raise ProviderError(f"AutoSprite: {state.get('error') or 'the animation failed'}")
            progress = state.get("progress") or {}
            if on_status and progress.get("total"):
                on_status(f"{status} {progress.get('completed', 0)}/{progress.get('total')}")
            elif on_status:
                on_status(str(status or "queued"))
            time.sleep(5)
        raise ProviderError("AutoSprite took too long")

    def sheet(self, sheet_id: str) -> tuple[dict, list[Image.Image]]:
        """A spritesheet's description and its frames."""
        import httpx
        meta = self._call("GET", f"/spritesheets/{sheet_id}")
        url = meta.get("sheetUrl")
        if not url:
            raise ProviderError(f"AutoSprite sheet {sheet_id} has no picture")
        picture = Image.open(io.BytesIO(httpx.get(url, timeout=self.timeout_s).content)).convert("RGBA")
        w, h = int(meta.get("frameWidth") or 0), int(meta.get("frameHeight") or 0)
        count = int(meta.get("frameCount") or 0)
        cols = int(meta.get("columns") or 0) or (picture.width // w if w else 1)
        if not w or not h:
            raise ProviderError(f"AutoSprite sheet {sheet_id}: no frame size")
        count = count or cols * (picture.height // h)
        frames = [picture.crop(((i % cols) * w, (i // cols) * h, (i % cols + 1) * w, (i // cols + 1) * h)) for i in range(count)]
        return meta, frames

    def animate(self, character: str, action: str, frames: int = 12, size: int = 256, tier: str = "turbo",
                on_status: Callable[[str], None] | None = None) -> tuple[list[Image.Image], dict]:
        """One view of `character` doing `action` (the picture's own view): its frames and the sheet's description."""
        kind, prompt = motion_of(action)
        entry: dict = {"kind": kind}
        if kind == "custom":
            entry |= {"name": re.sub(r"[^A-Za-z0-9 ]+", "", prompt)[:37] or "custom", "prompt": prompt[:1000],
                      "loop": False}
        started = self._call("POST", f"/characters/{character}/spritesheets",
                             json={"animations": [entry], "videoTier": tier, "frameCount": max(2, min(64, frames)),
                                   "frameSize": max(32, min(512, size))})
        if started.get("failed"):
            raise ProviderError(f"AutoSprite: {started['failed'][0].get('error', 'refused')}")
        jobs = [w.get("jobId") for w in started.get("workflows") or [] if w.get("jobId")]
        if not jobs:
            raise ProviderError(f"AutoSprite started nothing: {str(started)[:200]}")
        done = self.wait(jobs[0], on_status)
        ids = done.get("spritesheetIds") or []
        if not ids:
            raise ProviderError("AutoSprite returned no sheet")
        meta, out = self.sheet(ids[0])
        return out, meta

    def pack(self, character: str, action: str, frames: int = 12, size: int = 256,
             on_status: Callable[[str], None] | None = None) -> tuple[dict[str, list[Image.Image]], dict]:
        """`character` doing `action` in all 8 directions, one render: {"south": frames, …} and a sheet's description."""
        kind, prompt = motion_of(action)
        body: dict = {"action": kind, "frameCount": max(2, min(56, frames)), "frameSize": max(32, min(1024, size))}
        if kind == "custom":
            body |= {"name": re.sub(r"[^A-Za-z0-9 ]+", "", prompt)[:37] or "custom", "prompt": prompt[:1000], "loop": False}
        elif kind not in PACK_ACTIONS:
            body["loop"] = kind in LOOPING
        started = self._call("POST", f"/characters/{character}/packs", json=body)
        job = started.get("jobId")
        if not job:
            raise ProviderError(f"AutoSprite started no pack: {str(started)[:200]}")
        return self.pack_result(self.wait(job, on_status))

    def pack_result(self, done: dict) -> tuple[dict[str, list[Image.Image]], dict]:
        """A finished pack job's sheets by direction. The east side's are named iso_<action>_<direction>; the west
        side's come as kind "custom", named "Walk Left", "Walk Northwest"…"""
        out, meta = {}, {}
        for sheet_id in done.get("spritesheetIds") or []:
            meta, frames_ = self.sheet(sheet_id)
            words = [str(meta.get("kind", "")).rsplit("_", 1)[-1].lower(), str(meta.get("name", "")).rsplit(" ", 1)[-1].lower()]
            where = next((w for w in words if w in PACK_DIRECTIONS), None)
            if where:
                out[PACK_DIRECTIONS[where]] = frames_
        if not out:
            raise ProviderError("AutoSprite's pack had no directions")
        return out, meta


def fps_of(meta: dict, frames: int) -> float:
    """Its playing speed: AutoSprite plays a sheet at 24 frames a second times its speedMultiplier."""
    try:
        k = float(meta.get("speedMultiplier") or 0)
    except (TypeError, ValueError):
        k = 0.0
    return max(4.0, min(24.0, 24.0 * k)) if k > 0 else 12.0


def _png_on_white(picture: bytes) -> bytes:
    """As a PNG, its transparency kept (AutoSprite takes PNG with alpha)."""
    image = Image.open(io.BytesIO(picture)).convert("RGBA")
    if max(image.size) < 256:  # a 64 px sprite: bigger, so it reads
        k = 256 / max(image.size)
        image = image.resize((round(image.width * k), round(image.height * k)), Image.LANCZOS)
    buf = io.BytesIO()
    image.save(buf, "PNG")
    return buf.getvalue()


def fitted(by_direction: dict[str, list[Image.Image]], size: int, height: int) -> dict[str, list[Image.Image]]:
    """Every direction's frames cut from one box (the motion kept), scaled so the figure is `height` px tall, into
    `size` squares, feet at the bottom."""
    boxes = [f.getbbox() for frames in by_direction.values() for f in frames if f.getbbox()]
    if not boxes:
        return by_direction
    box = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    tall = [b[3] - b[1] for b in boxes]
    k = height / max(1, sorted(tall)[len(tall) // 2])  # the usual figure's height to the pose's
    k = min(k, size / max(1, box[2] - box[0]), size / max(1, box[3] - box[1]))
    out = {}
    for direction, frames in by_direction.items():
        row = []
        for f in frames:
            part = f.crop(box)
            part = part.resize((max(1, round(part.width * k)), max(1, round(part.height * k))), Image.LANCZOS)
            slot = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            slot.paste(part, ((size - part.width) // 2, size - part.height), part)
            row.append(slot)
        out[direction] = row
    return out
