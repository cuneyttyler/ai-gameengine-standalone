"""HTTP API (local only; the editor starts and stops this process).

  GET  /health          is it up, which provider/model, is a schema loaded
  PUT  /schema          the editor sends its reflection schema at startup
  GET  /schema          the schema the service is working against
  POST /propose         {instruction, context, history?} -> validated proposal
  GET  /usage           token totals for this session (cost visibility)
  PUT  /project         {root}: the open project, so its World Bible can be indexed
  POST /world/search    {query, k?} -> the most related World Bible entries
  POST /consistency     {operations, exclude?} -> contradictions with the canon (a second, cheaper model)
  POST /assets/generate {name, description, kind, height, style, variants} -> sprites written to the project
  POST /assets/describe {paths} -> what each image is: name, kind, tags, direction, size in cells (6.2)

Run: uv run uvicorn app.main:app --port 8765
"""

from __future__ import annotations

import hmac
from datetime import datetime, timezone
import io
import json
import re
import os
from pathlib import Path

from dotenv import load_dotenv
from fastapi import Body, FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel

from .models import EditorContext, EngineSchema, Proposal, Usage
from .consistency import check_consistency, writing_from_operations
from .images import ImageGenerator, MistralImageGenerator, generate_sprites
from .planner import STEPS as PLANNER_STEPS
from .places import ask_questions, interpret, plan_root, plan_zone, repair_brief, repair_node, review_layout, revise_plan
from .inspect import inspect_place
from .sketch import sketch_place
from .vision import describe_assets, map_chunk
from .websource import all_candidates, candidates, fetch_file, search_web
from .art_brief import better_queries, write_art_prompt
from . import progress
from . import style as project_style
from .providers import ClaudeProvider, MistralEmbedder, MistralProvider, Provider, ProviderError
from .service import propose
from .world import Embedder, WorldIndex, format_entry, load_entries

DEFAULT_MODEL = "mistral-large-latest"


class ProposeRequest(BaseModel):
    instruction: str
    context: EditorContext = EditorContext()
    history: list[dict] = []
    images: list[str] = []   # pictures the model asked to look at (its look tool), project paths


class ProjectRequest(BaseModel):
    root: str


class SearchRequest(BaseModel):
    query: str
    k: int = 5


class ConsistencyRequest(BaseModel):
    operations: list[dict] = []
    exclude: list[str] = []  # canon keys not to compare against (an entry being re-checked)


class DescribeAssetsRequest(BaseModel):
    paths: list[str] = []
    items: list[dict] = []   # objects cut from sheets: {"id", "path", "rect": [x, y, w, h]} (7.3)
    roles: list[str] = []    # the ontology roles to choose from
    # What these were made or found for (a build's check): each also gets a verdict, fits and why not.
    wanted: dict = {}


class InterpretRequest(BaseModel):
    query: str
    context: dict          # what can be built (engine/places InterpretContext)
    drafts: int = 3
    mode: str = "auto"     # auto, native, two-pass, one-pass (place-interaction.md B3)
    choices: dict = {}     # what the user chose in the Build panel's form: {Brief path: value}, kept as given
    reading: str = ""      # one reading alone (literal, typical, generous); empty: `drafts` of them


class RepairBriefRequest(BaseModel):
    query: str
    context: dict
    brief: dict
    problems: list[dict]   # {"where", "message"} from the validator


class PlanRootRequest(BaseModel):
    brief: dict
    context: dict          # the root's subgraph and its possible children (engine/places Planner)
    mode: str = "auto"
    examples: list = []    # the project's approved places like this one (7.7)
    directions: dict = {}  # the user's answers to ② Assets' questions: {questions, answers}
    grid: dict = {}        # the site as a coarse grid (river, roads, walls drawn in) for the AI to sketch the zones on


class QuestionsRequest(BaseModel):
    query: str
    brief: dict
    context: dict
    choices: dict = {}


class PlanZoneRequest(BaseModel):
    brief: dict
    zone: dict             # the zone's Spec node as planned at the root step
    siblings: list[dict] = []
    context: dict
    mode: str = "auto"


class RepairNodeRequest(BaseModel):
    node: dict
    problems: list[dict]
    context: dict


class ProviderRequest(BaseModel):
    provider: str          # mistral or claude
    model: str = ""        # empty: the provider's default


class RevisePlanRequest(BaseModel):
    brief: dict
    plan: dict             # the Spec's zones and their parcels (engine/places Planner PlanForRevision)
    result: dict = {}      # how it was laid out: buildings per zone, what didn't fit
    instruction: str
    context: dict
    mode: str = "auto"
    where: dict = {}       # the layout now (engine/places Where): areas, banks, directions, a map
    stage: str = ""        # "layout": only the zones are planned yet; "plan": the zones are fixed
    room: list = []        # in the plan: each zone's land and about how many parcels it holds
    language: dict = {}    # the revision language at this step (data/places/revise-language.json): the answer is a program in it


class SketchRequest(BaseModel):
    context: dict = {}       # the place, its look and story, its zones, its items ({key, type, drawn, count})
    outDir: str              # where the painting and its crops go, relative to the project
    name: str = "sketch"


class InspectRequest(BaseModel):
    kind: str              # layout, plan or build (docs/build-fixes.md)
    images: list[str]      # relative to the project root: one, or one per window of a big scene
    windows: list[dict] = []  # per image: {label, items} (what was placed in that window)
    brief: dict = {}
    story: dict = {}
    look: dict = {}        # the goal for this kind of place (the build type's look)
    expected: dict = {}    # zones, items (type: count and where), legend (code: type)
    issues: list = []      # "fixes": the draft's mistakes, each judged fixed or not in the repaint


class ReviewLayoutRequest(BaseModel):
    image: str             # the map, relative to the project root (places/<id>/layout.png)
    brief: dict
    plan: dict
    result: dict = {}
    verify: list = []      # the Verify rows: asked, planned, built
    context: dict
    kind: str = "map"      # map (the solved layout) or scene (the built place, rendered)


class WebSearchRequest(BaseModel):
    query: str
    folder: str            # where its downloads go, inside the project (assets/web/<need>)
    maxAssets: int = 4
    shareAlike: bool = False
    queries: list[str] = []  # more ways to say it, tried after `query` ("cottage", "house")


class WebPreviewRequest(BaseModel):
    query: str
    maxResults: int = 12
    shareAlike: bool = False


class WebFetchRequest(BaseModel):
    url: str               # one file a preview listed
    folder: str = "assets/web/search"


class ArtProviderRequest(BaseModel):
    provider: str = "auto"
    style: str = ""   # Project Settings ▸ AI ▸ Art style (style.ART_STYLES); "" from the project


class ArrangeRequest(BaseModel):
    kinds: list[dict]                  # ArrangementKinds: role, label, footprint, in, statements, the ontology's defaults
    place: dict = {}
    relations: list[dict] = []         # the relation types it may use: {type, means}
    context: dict = {}


class BackgroundProviderRequest(BaseModel):
    provider: str = "bfl:flux-3-image"


class BackgroundRequest(BaseModel):
    dir: str                           # where it's written, relative to the project root (places/<id>/background)
    terrain: dict                      # the solved layout's terrain {palette, rows}
    cells: list[int]                   # [W, H]
    description: str = ""              # the background description (the planner's, edited by the user)
    provider: str = ""                 # this build's background model (Build ▸ Assets); "" for the service's (the project's)
    seed: int = 7


class PaintRequest(BaseModel):
    dir: str                           # the place's folder (places/<id>): blockout.png and its legend are there
    blockout: str = "blockout.png"     # in dir
    legend: dict = {}                  # the block-out's colour key (RenderPlanBlockout)
    place: dict = {}                   # {name, type}
    story: dict = {}                   # {mood, purpose, logline}
    look: dict = {}                    # {goals, style}
    notes: str = ""                    # the user's, when painting again
    model: str = "flux-3-image"        # flux-2-klein-4b, flux-3-image, flux-3-to-pro, flux-3-to-klein (Project Settings ▸ AI ▸ paint)
    seed: int = 7
    reference: str = ""                # in dir: a picture the painting is made after (docs/places/scene-types.md)


class InteriorsRequest(BaseModel):
    dir: str                           # the place's folder (places/<id>): painting.png and marks/ are there
    items: list[dict] = []             # [{id, box: [x, y, w, h], mask, role, purpose}]: each building's occluder
    place: dict = {}                   # {name, type, style}
    story: dict = {}
    look: dict = {}
    seed: int = 7


class SceneBackgroundRequest(BaseModel):
    path: str                          # where it's written, relative to the project (assets/backgrounds/<scene>.png)
    description: str                   # the ground, in words
    provider: str = ""                 # a background model (bfl:...); "" the project's
    view: str = "isometric"            # isometric or top-down: the project's grid
    width: int = 1536
    height: int = 1024
    seed: int = 7


class AssetInteriorRequest(BaseModel):
    path: str                          # the building's picture, relative to the project
    purpose: str = ""                  # what it is, for its rooms (its analysis' description)
    seed: int = 7


class SegmentRequest(BaseModel):
    path: str                          # the picture, relative to the project (places/<id>/painting.png)
    points: list[list[float]] = []     # [x, y, 1] to add, [x, y, 0] to leave out, in the picture's pixels
    box: list[float] | None = None     # [x0, y0, x1, y1]


class MatchedRequest(BaseModel):
    dir: str                           # the sheets' folder (MatchedArtSheets), relative to the project root
    sheets: dict                       # sheets.json
    place: dict = {}                   # {name, type}
    story: dict = {}
    look: dict = {}
    background: str = ""               # the place's background (project path), for the art coloured by it (coloured-by)
    provider: str = ""                 # this build's art provider (Build ▸ Assets); "" for the service's (the project's)


# What animates (Preferences ▸ AI ▸ Animate): AutoSprite (the default: a character's 8 directions in one render) or
# PixelLab's Animate with text v3. BFL's (FLUX.2 klein 4b editing copies of the pose, FLUX 3 video) are still taken
# when asked for by name, no longer offered.
ANIMATE_PROVIDERS = ("autosprite", "pixellab:animate-v3")
COPIES_MODEL = "flux-2-klein-4b"


class PartsRequest(BaseModel):
    path: str                       # a project picture: a building's art, a painting
    kinds: list[str] = ["door"]     # what to find: door, gate, shutter, hatch...
    rect: list[int] | None = None   # a box of it to look in (a painting's building), its pixels


class DoorRequest(BaseModel):
    path: str                       # the building's picture (or a painting)
    box: list[int]                  # its door, as found (x, y, w, h: its pixels)
    name: str                       # what its frames are kept as (assets/textures/generated/animations/<name>/)
    frames: int = 24
    exact: bool = False             # the box drawn by hand round the door: used as drawn, not fitted
    leaves: int = 0                 # 1, 2 (a double door: two halves, each on its own hinge); 0: from its shape
    animator: str = "pixellab"      # pixellab or bfl:flux-video (its crop animated, kept to the doorway), or engine (its own pixels swung)


class AnimationRequest(BaseModel):
    objectProvider: str = "pixellab"  # what animates a thing (not a person): Preferences ▸ AI ▸ Non-humanoid animation
    clip: str = ""       # the clip it's kept as (open, pull...); "" from the action (open, close, break, else idle)
    path: str            # the project image to animate
    provider: str = ""   # Preferences ▸ AI ▸ Animate ("" the default: AutoSprite)
    action: str          # walking, swinging a sword, flickering
    description: str = ""
    direction: str = "south"
    view: str = "low top-down"
    name: str = ""


class CharacterAnimateRequest(BaseModel):
    sheet: str           # a character sheet (assets/characters/<name>/<name>.character.anim.json)
    provider: str = ""   # Preferences ▸ AI ▸ Animate
    hold: bool = False   # holding a weapon: drawn with a rod, kept as the hand's points and removed (AutoSprite)
    weaponClass: str = ""  # its weapon class (sword, axe…): the clip attack_<class>
    action: str          # walking, swinging a sword, casting a spell
    description: str = ""


class CharacterPicturesRequest(BaseModel):
    pictures: list[str]  # a character's direction pictures (…-south.png…)
    name: str
    description: str = ""


class CharacterRequest(BaseModel):
    name: str
    description: str
    directions: int = 4
    size: int = 64
    view: str = "low top-down"
    isometric: bool = False


class ReferencesRequest(BaseModel):
    pieces: list[dict] = []  # {path, role?, name?}


class GenerateAssetRequest(BaseModel):
    name: str
    description: str
    kind: str = "object"
    height: int = 48
    style: str = "pixel"
    variants: list[str] = []
    # A tile of one ground (a build's grass, dirt, water): its frame trimmed and its light
    # evened, so it repeats without a grid (place-refine.md §1). Not for a tile with a feature.
    evenLight: bool = False
    # The piece's place (place-refine.md §14): what it is for and where it goes, its view, size and
    # style. Given, the text model first writes what exactly to draw for it there (art_brief).
    context: dict = {}
    # The art of the pieces it's designed against (a door's wall), relative to the project: the image
    # model draws to match them, before the project's other references.
    withImages: list[str] = []


class ProposeResponse(BaseModel):
    ok: bool
    proposal: Proposal | None = None
    errors: list[str] = []
    usage: Usage = Usage()
    attempts: int = 0
    model: str = ""


PROVIDERS = ("mistral", "claude")


def available_providers() -> dict[str, bool]:
    """Which models this machine has keys for (in ai_service/.env or the environment)."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    return {"mistral": bool(os.environ.get("MISTRAL_API_KEY")),
            "claude": bool(os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN")),
            "pixellab": bool(os.environ.get("PIXELLAB_API_KEY")),
            "bfl": bool(os.environ.get("BFL_API_KEY")),
            "autosprite": bool(os.environ.get("AUTO_SPRITE_API_KEY") or os.environ.get("AUTOSPRITE_API_KEY"))}


# What draws the art (Preferences ▸ AI ▸ Art): auto (follows the LLM), Mistral, PixelLab, or a BFL model ("bfl:<model>").
ART_PROVIDERS = ("auto", "mistral", "pixellab") + tuple(f"bfl:{m}" for m in ("flux-2-klein-4b", "flux-3-image", "flux-2-pro"))
# What paints a place's background (docs/places/background.md): FLUX.3 Image only, for now (klein invents objects).
# FLUX.3 Image keeps the map best; FLUX.2 max is as good, slower; pro and klein are weaker (builder_tests/background_tests).
BACKGROUND_PROVIDERS = ("bfl:flux-3-image", "bfl:flux-2-max", "bfl:flux-2-pro", "bfl:flux-2-klein-4b")


def make_image_generator(art: str, llm: str | None) -> ImageGenerator | None:
    """What draws the art: "auto" follows the LLM (Mistral's images with Mistral; Claude makes no
    images, so with Claude Mistral's if its key is there, else PixelLab), or one named."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    keys = available_providers()
    if art == "auto":
        art = "mistral" if keys["mistral"] else "pixellab"
    if art == "pixellab" and keys["pixellab"]:
        from .pixellab import PixelLabImageGenerator
        return PixelLabImageGenerator(api_key=os.environ["PIXELLAB_API_KEY"])
    if art == "mistral" and keys["mistral"]:
        return MistralImageGenerator(api_key=os.environ["MISTRAL_API_KEY"])
    if art.startswith("bfl:") and keys["bfl"]:
        from .bfl import BflImageGenerator
        return BflImageGenerator(api_key=os.environ["BFL_API_KEY"], model=art.split(":", 1)[1])
    return None


def list_models(provider: str) -> list[str]:
    """The models a provider offers this key (its own list)."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    if provider == "mistral" and os.environ.get("MISTRAL_API_KEY"):
        from mistralai.client import Mistral
        return sorted({m.id for m in Mistral(api_key=os.environ["MISTRAL_API_KEY"]).models.list().data})
    if provider == "claude" and (os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN")):
        import anthropic
        client = anthropic.Anthropic(api_key=os.environ.get("ANTHROPIC_API_KEY") or None)
        return [m.id for m in client.models.list(limit=100).data]
    return []


def make_provider(name: str, model: str | None = None) -> Provider | None:
    """The model that writes: Mistral (MISTRAL_API_KEY) or Claude (ANTHROPIC_API_KEY)."""
    if not available_providers().get(name):
        return None
    if name == "claude":
        return ClaudeProvider(api_key=os.environ.get("ANTHROPIC_API_KEY") or None,
                              model=model or os.environ.get("CLAUDE_MODEL", "claude-opus-5-5"),
                              effort=os.environ.get("CLAUDE_EFFORT", "high"))
    return MistralProvider(api_key=os.environ["MISTRAL_API_KEY"], model=model or os.environ.get("MISTRAL_MODEL", DEFAULT_MODEL))


def default_provider() -> Provider | None:
    """AIGE_PROVIDER picks it (mistral, the default, or claude); the editor can change it (PUT /provider)."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    return make_provider(os.environ.get("AIGE_PROVIDER", "mistral"))


def default_checker() -> Provider | None:
    """The contradiction checker: a cheaper model than the one that writes."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    if not key:
        return None
    # mistral-medium: on the recorded fixtures (tests/golden/checks) small flagged
    # consistent writing and large one false positive; medium was right on all.
    return MistralProvider(api_key=key, model=os.environ.get("MISTRAL_CHECK_MODEL", "mistral-medium-latest"))


def default_vision() -> Provider | None:
    """Looks at images (6.2): Mistral's best vision model (Large 4); MISTRAL_VISION_MODEL picks another."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    if not key:
        return None
    return MistralProvider(api_key=key, model=os.environ.get("MISTRAL_VISION_MODEL", "mistral-large-4"),
                           timeout_s=180.0)


def default_image_generator() -> ImageGenerator | None:
    """AIGE_ART_PROVIDER picks it (auto, the default: follows the LLM; mistral; pixellab); the editor can change it."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    return make_image_generator(os.environ.get("AIGE_ART_PROVIDER", "auto"), os.environ.get("AIGE_PROVIDER", "mistral"))


def default_embedder() -> Embedder | None:
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    return MistralEmbedder(api_key=key) if key else None


def create_app(
    provider: Provider | None = None,
    schema: EngineSchema | None = None,
    embedder: Embedder | None = None,
    checker: Provider | None = None,
    images: ImageGenerator | None = None,
    vision: Provider | None = None,
) -> FastAPI:
    app = FastAPI(title="AIGE AI Orchestration Service", version="0.1.0")

    # Only the editor that started it (development_plan.md 11): a web page in the user's browser
    # can reach 127.0.0.1 too. Every request but /health carries the editor's token (given to
    # this process in AIGE_SERVICE_TOKEN), and names this machine as its host (no DNS rebinding).
    # Without the variable (started by hand, or by the tests) it stays open.
    @app.middleware("http")
    async def editor_only(request: Request, call_next):
        token = os.environ.get("AIGE_SERVICE_TOKEN", "")
        if token and request.url.path != "/health":
            host = request.headers.get("host", "").split(":")[0].strip("[]").lower()
            if host not in ("127.0.0.1", "localhost", "::1"):
                return JSONResponse({"ok": False, "errors": ["not this machine"]}, status_code=403)
            given = request.headers.get("x-aige-token", "")
            if not hmac.compare_digest(given.encode(), token.encode()):
                return JSONResponse({"ok": False, "errors": ["not the editor"]}, status_code=401)
        return await call_next(request)

    # Each request that may call the model is one progress entry (app/progress.py), read by the
    # editor's progress strips through GET /progress. A request that never reaches the model (one
    # refused, a quick lookup) makes no model call and isn't shown.
    @app.middleware("http")
    async def track_progress(request: Request, call_next):
        if request.method != "POST":
            return await call_next(request)
        token = progress.begin(request.url.path, request.query_params.get("label", ""))
        ok = False
        try:
            response = await call_next(request)
            ok = response.status_code < 400
            return response
        finally:
            progress.end(token, ok)

    @app.get("/progress")
    def get_progress() -> dict:
        return progress.snapshot()

    state = {
        "provider": provider,
        "schema": schema,
        "usage": Usage(),
        "embedder": embedder,
        "world": None,
        "checker": checker,
        "images": images,
        "vision": vision,
        "mistralVision": vision,
        "root": None,
    }

    @app.get("/health")
    def health() -> dict:
        current: Provider | None = state["provider"]
        return {
            "ok": True,
            "provider": current.name if current else None,
            "model": current.model if current else None,
            "schemaLoaded": state["schema"] is not None,
            "usage": state["usage"].model_dump(),  # every model call so far
        }

    @app.put("/schema")
    def put_schema(new_schema: EngineSchema) -> dict:
        state["schema"] = new_schema
        return {
            "ok": True,
            "components": len(new_schema.components),
            "settings": len(new_schema.settings),
            "commands": len(new_schema.commands),
        }

    @app.get("/schema")
    def get_schema() -> EngineSchema:
        if state["schema"] is None:
            raise HTTPException(404, "the editor hasn't sent its schema yet")
        return state["schema"]

    @app.get("/usage")
    def usage() -> Usage:
        return state["usage"]

    @app.post("/propose")
    def post_propose(request: ProposeRequest) -> ProposeResponse:
        if state["provider"] is None:
            return ProposeResponse(ok=False, errors=["No AI provider configured: set MISTRAL_API_KEY in ai_service/.env"])
        # A change asked for in the AI panel is interactive: a thinking model at medium effort.
        writer: Provider = state["provider"].at("medium") if hasattr(state["provider"], "at") else state["provider"]
        if state["schema"] is None:
            return ProposeResponse(ok=False, errors=["The editor hasn't sent its schema yet"])
        if not request.instruction.strip():
            return ProposeResponse(ok=False, errors=["Say what you'd like to change"])
        canon = ""
        index: WorldIndex | None = state["world"]
        if index is not None:
            # Retrieval: the canon related to what's asked (and about what).
            canon = index.canon_for(" ".join(filter(None, [request.instruction, request.context.focus])))
        def looked() -> list[dict]:
            """The pictures it asked to look at, as image chunks (in the project; at most four)."""
            from .vision import map_chunk
            root = state["root"]
            chunks = []
            for path in request.images[:4]:
                file = (root / path).resolve() if root else None
                if file and root.resolve() in file.parents and file.is_file():
                    chunks.append(map_chunk(file))
            return chunks

        def make_generated(requests) -> dict:
            """A proposal's characters and animations (docs/ai/agent.md), made as Generate makes them: {"character:<name>"
            | "animation:<name>": the sheet's path}. An animation of "character:<name>" uses the one just made."""
            made: dict[str, str] = {}
            for g in requests[:4]:
                try:
                    if g.kind == "character":
                        answer = characters_generate(CharacterRequest(name=g.name, description=g.description or g.name,
                                                                      directions=8 if g.directions >= 8 else 4, isometric=True))
                        if answer.get("ok"):
                            made[f"character:{g.name}"] = answer["sheet"]
                        continue
                    target = g.target
                    if target.startswith("character:"):
                        target = made.get(target, "")
                    if not target:
                        continue
                    if target.endswith(".character.anim.json"):
                        answer = characters_animate(CharacterAnimateRequest(sheet=target, action=g.action or "walk",
                                                                            description=g.description))
                        if answer.get("ok"):
                            made[f"animation:{g.name}"] = answer["sheet"]
                    else:
                        answer = animations_generate(AnimationRequest(path=target, action=g.action or "idle",
                                                                      description=g.description, name=g.name))
                        if answer.get("ok"):
                            made[f"animation:{g.name}"] = answer["sheet"]
                except Exception:  # noqa: BLE001 - not made: the proposal is told
                    continue
            return made

        def make_art(requests) -> dict:
            """A proposal's art, drawn by the Art tool (with its brief), into the project."""
            root, images = state["root"], state["images"]
            made: dict[str, str] = {}
            if root is None or images is None:
                return made
            for a in requests[:6]:
                name = re.sub(r"[^a-z0-9-]+", "-", a.name.lower()).strip("-")[:40] or "art"
                context = {"view": "isometric", "cells": a.cells, "place": {"about": request.instruction}}
                description, brief = a.description, None
                try:
                    description, brief = write_art_prompt(state["provider"], a.description, context, a.kind)
                except ProviderError:
                    pass
                height = 64 if a.kind == "tile" else max(24, min(160, round(96 * a.cells)))
                try:
                    out = generate_sprites(images, root, "ai-" + name, description, a.kind, 256 if a.kind == "tile" else height,
                                           "pixel", None, a.kind == "tile", brief)
                except (ValueError, ProviderError):
                    continue
                if out:
                    made[a.name] = out[0]["path"]
            return made

        try:
            result = propose(
                writer, state["schema"], request.context, request.instruction, request.history, canon, make_art,
                make_generated, looked()
            )
        except ProviderError as error:
            return ProposeResponse(ok=False, errors=[str(error)])
        state["usage"].add(result.usage)
        return ProposeResponse(
            ok=result.proposal is not None and not result.errors,
            proposal=result.proposal,
            errors=result.errors,
            usage=result.usage,
            attempts=result.attempts,
            model=result.model,
        )

    @app.put("/project")
    def put_project(request: ProjectRequest) -> dict:
        root = Path(request.root)
        if not root.is_dir():
            raise HTTPException(400, f"not a folder: {request.root}")
        state["world"] = WorldIndex(root, state["embedder"])
        state["root"] = root
        return {"ok": True, "worldEntries": len(load_entries(root))}

    @app.post("/world/search")
    def world_search(request: SearchRequest) -> dict:
        index: WorldIndex | None = state["world"]
        if index is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        hits, method = index.search(request.query, max(1, min(request.k, 20)))
        return {"ok": True, "method": method, "hits": [hit.__dict__ for hit in hits]}

    @app.post("/assets/describe")
    def assets_describe(request: DescribeAssetsRequest) -> dict:
        root: Path | None = state["root"]
        vision: Provider | None = state["vision"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        if vision is None:
            return {"ok": False, "errors": ["No vision model available (MISTRAL_API_KEY)"]}
        if len(request.paths) + len(request.items) > 400:
            return {"ok": False, "errors": ["At most 400 images at a time"]}
        if request.wanted:
            # A build's check of art made or found for a part: the quick vision model (matched.quick_vision).
            from .matched import quick_vision
            vision = quick_vision(vision)
        return describe_assets(vision, root, request.paths, items=request.items, roles=request.roles or None,
                               judge=request.wanted or None)

    @app.post("/places/interpret")
    def places_interpret(request: InterpretRequest) -> dict:
        provider: Provider | None = state["provider"]
        if provider is None:
            return {"ok": False, "errors": ["No AI provider configured: set MISTRAL_API_KEY in ai_service/.env"]}
        if not request.query.strip() and not request.choices:
            return {"ok": False, "errors": ["Say what to build"]}
        result = interpret(provider, request.query, request.context, request.drafts, request.mode, choices=request.choices,
                           reading=request.reading)
        state["usage"].add(Usage(**result["usage"]))
        return result

    @app.post("/places/repair-brief")
    def places_repair_brief(request: RepairBriefRequest) -> dict:
        provider: Provider | None = state["provider"]
        if provider is None:
            return {"ok": False, "errors": ["No AI provider configured"]}
        result = repair_brief(provider, request.query, request.context, request.brief, request.problems)
        state["usage"].add(Usage(**result["usage"]))
        return result

    def _planning(call) -> dict:
        provider: Provider | None = state["provider"]
        if provider is None:
            return {"ok": False, "errors": ["No AI provider configured"]}
        result = call(provider)
        state["usage"].add(Usage(**result["usage"]))
        return result

    @app.post("/places/plan-root")
    def places_plan_root(request: PlanRootRequest) -> dict:
        return _planning(lambda p: plan_root(p, request.brief, request.context, request.mode, request.examples,
                                             directions=request.directions, grid=request.grid))

    @app.post("/places/questions")
    def places_questions(request: QuestionsRequest) -> dict:
        return _planning(lambda p: ask_questions(p, request.query, request.brief, request.context, request.choices))

    @app.post("/places/plan-zone")
    def places_plan_zone(request: PlanZoneRequest) -> dict:
        return _planning(lambda p: plan_zone(p, request.brief, request.zone, request.siblings, request.context, request.mode))

    @app.post("/places/arrange")
    def places_arrange(request: ArrangeRequest) -> dict:
        """The plan's arrangement (docs/dungeon.md §0): each kind's purpose, footprint, relations, place and facing."""
        from .places import arrange_kinds
        return _planning(lambda p: arrange_kinds(p, request.kinds, request.place, request.relations, request.context))

    @app.post("/places/repair-node")
    def places_repair_node(request: RepairNodeRequest) -> dict:
        return _planning(lambda p: repair_node(p, request.node, request.problems, request.context))

    @app.get("/provider")
    def provider_info() -> dict:
        current: Provider | None = state["provider"]
        images: ImageGenerator | None = state["images"]
        return {"ok": True, "provider": getattr(current, "name", None), "model": getattr(current, "model", None),
                "available": available_providers(), "artProvider": state.get("artProvider", "auto"),
                "backgroundProvider": state.get("backgroundProvider", BACKGROUND_PROVIDERS[0]),
                "art": getattr(images, "name", None)}

    @app.get("/models")
    def models(provider: str = "mistral") -> dict:
        """The models a provider offers (Preferences ▸ AI ▸ Model)."""
        if provider not in PROVIDERS:
            return {"ok": False, "errors": [f"Unknown provider {provider}"]}
        try:
            return {"ok": True, "provider": provider, "models": list_models(provider)}
        except Exception as error:
            return {"ok": False, "errors": [f"Couldn't list {provider}'s models: {error}"]}

    @app.post("/assets/matched")
    def assets_matched(request: MatchedRequest) -> dict:
        """A place's art made to fit together (docs/places/matched-art.md), by the art provider: the sheets painted
        whole (BFL), or each piece alone in the style sheet's look (PixelLab, Mistral). The editor cuts them."""
        from .matched import make
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        folder = (root / request.dir).resolve()
        if root.resolve() not in folder.parents or not folder.is_dir():
            return {"ok": False, "errors": [f"No sheets at {request.dir}"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        art = state.get("artProvider", "auto")
        generator = state.get("images")
        if request.provider and request.provider != art:
            # This build's own choice: its generator for this call.
            if request.provider not in ART_PROVIDERS:
                return {"ok": False, "errors": [f"Unknown art provider {request.provider}: one of {', '.join(ART_PROVIDERS)}"]}
            art = request.provider
            generator = make_image_generator(art, getattr(state["provider"], "name", None))
        if art == "auto":
            art = "mistral" if available_providers()["mistral"] else "pixellab"
        return make(art, root, request.dir, request.sheets, request.model_dump(), generator=generator, vision=state.get("vision"),
                    keys={"bfl": os.environ.get("BFL_API_KEY")}, report=progress.reporter())

    @app.put("/art-provider")
    def set_art_provider(request: ArtProviderRequest) -> dict:
        """What draws the art (Preferences ▸ AI ▸ Art): auto (follows the LLM), mistral or pixellab."""
        if request.provider not in ART_PROVIDERS:
            return {"ok": False, "errors": [f"Unknown art provider {request.provider}: one of {', '.join(ART_PROVIDERS)}"]}
        images = make_image_generator(request.provider, getattr(state["provider"], "name", None))
        if images is None:
            key = "PIXELLAB_API_KEY" if request.provider == "pixellab" else "BFL_API_KEY" if request.provider.startswith("bfl") else "MISTRAL_API_KEY"
            return {"ok": False, "errors": [f"No key for {request.provider}: set {key} in ai_service/.env"], "available": available_providers()}
        state["images"], state["artProvider"] = images, request.provider
        from .style import set_art_style
        set_art_style(request.style)
        return {"ok": True, "artProvider": request.provider, "art": images.name, "available": available_providers()}

    @app.put("/background-provider")
    def set_background_provider(request: BackgroundProviderRequest) -> dict:
        """What paints the backgrounds (Preferences ▸ AI ▸ Background, or the project's own)."""
        if request.provider not in BACKGROUND_PROVIDERS:
            return {"ok": False, "errors": [f"Unknown background provider {request.provider}: one of {', '.join(BACKGROUND_PROVIDERS)}"]}
        state["backgroundProvider"] = request.provider
        return {"ok": True, "backgroundProvider": request.provider}

    @app.post("/places/background")
    def places_background(request: BackgroundRequest) -> dict:
        """A place's background painted from its plan (docs/places/background.md): the whole in one call, then tiles
        from it where it needs more detail, checked and blended; written top-down to `dir`."""
        from .bfl import BflPainter
        from .matched import paint_background
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        folder = (root / request.dir).resolve()
        if root.resolve() not in folder.parents:
            return {"ok": False, "errors": [f"Not in the project: {request.dir}"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        key = os.environ.get("BFL_API_KEY")
        if not key:
            return {"ok": False, "errors": ["No key for BFL: set BFL_API_KEY in ai_service/.env"]}
        provider = request.provider or state.get("backgroundProvider", BACKGROUND_PROVIDERS[0])
        if provider not in BACKGROUND_PROVIDERS:
            return {"ok": False, "errors": [f"Unknown background model {provider}: one of {', '.join(BACKGROUND_PROVIDERS)}"]}
        try:
            made = paint_background(BflPainter(key, provider.split(":", 1)[1]), request.terrain, (request.cells[0], request.cells[1]),
                                    request.description, folder, seed=request.seed, report=progress.reporter())
        except Exception as error:  # noqa: BLE001 - said to the editor
            return {"ok": False, "errors": [f"The background couldn't be painted: {error}"]}
        return {"ok": True, "provider": provider, "path": made["path"].relative_to(root.resolve()).as_posix(), "cellPx": made["cellPx"],
                "cells": made["cells"], "calls": made["calls"], "tiles": made["tiles"], "prompt": made["prompt"]}

    @app.post("/images/segment")
    def images_segment(request: SegmentRequest) -> dict:
        """Click-to-select (docs/places/painted-places.md §4): the mask of the thing at the points, by SAM 2.1."""
        import base64 as _b64
        from . import segment
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        picture = (root / request.path).resolve()
        if root.resolve() not in picture.parents or not picture.is_file():
            return {"ok": False, "errors": [f"No picture at {request.path}"]}
        if why := segment.available():
            return {"ok": False, "errors": [why]}
        try:
            png, box = segment.segment(picture, request.points, request.box)
        except Exception as error:  # noqa: BLE001 - said to the editor
            return {"ok": False, "errors": [f"The selection failed: {error}"]}
        return {"ok": True, "png": _b64.b64encode(png).decode("ascii"), "box": box, "points": request.points}

    @app.post("/places/paint")
    def places_paint(request: PaintRequest) -> dict:
        """A place painted whole from its plan's block-out (docs/places/painted-places.md §2), by FLUX.3 Image;
        written as painting.png in `dir`, at the block-out's size."""
        import time as _time
        from .bfl import PAINT_MODELS, paint_place
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        folder = (root / request.dir).resolve()
        if root.resolve() not in folder.parents or not (folder / request.blockout).is_file():
            return {"ok": False, "errors": [f"No block-out in {request.dir}"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        key = os.environ.get("BFL_API_KEY")
        if not key:
            return {"ok": False, "errors": ["No key for BFL: set BFL_API_KEY in ai_service/.env"]}
        report = progress.reporter()
        if request.model not in PAINT_MODELS:
            return {"ok": False, "errors": [f"Unknown paint model {request.model}: one of {', '.join(PAINT_MODELS)}"]}
        report(0, 1, f"Painting the place from its plan ({PAINT_MODELS[request.model]})")
        started = _time.monotonic()
        try:
            reference = (folder / request.reference) if request.reference else None
            png, prompt = paint_place(key, request.model, (folder / request.blockout).read_bytes(), request.legend,
                                      request.place, request.story, request.look, request.notes, request.seed,
                                      reference.read_bytes() if reference and reference.is_file() else None)
        except Exception as error:  # noqa: BLE001 - said to the editor
            return {"ok": False, "errors": [f"The place couldn't be painted: {error}"]}
        (folder / "painting.png").write_bytes(png)
        (folder / "painting-prompt.txt").write_text(prompt, encoding="utf-8")
        report(1, 1, "Painted")
        return {"ok": True, "provider": PAINT_MODELS[request.model], "path": (folder / "painting.png").relative_to(root.resolve()).as_posix(),
                "prompt": prompt, "seconds": round(_time.monotonic() - started, 1)}

    @app.post("/places/interiors")
    def places_interiors(request: InteriorsRequest) -> dict:
        """A painted place's buildings' interiors (docs/places/interiors.md §7): each occluder's region of the painting
        cut away by BFL, written as marks/<id>-interior.png in `dir`."""
        from .bfl import BflPainter
        from .matched import paint_interiors
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        folder = (root / request.dir).resolve()
        if root.resolve() not in folder.parents or not (folder / "painting.png").is_file():
            return {"ok": False, "errors": [f"No painting in {request.dir}"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        key = os.environ.get("BFL_API_KEY")
        if not key:
            return {"ok": False, "errors": ["Interiors are painted by BFL: set BFL_API_KEY in ai_service/.env"]}
        report = progress.reporter()
        report(0, 1, f"Interiors: {len(request.items)} buildings cut away to show their rooms")
        made = paint_interiors(BflPainter(key, "flux-3-image"), folder, request.items,
                               {"place": request.place, "story": request.story, "look": request.look}, request.seed, vision=state["vision"])
        report(1, 1, f"Interiors: {len(made)} of {len(request.items)} made")
        return {"ok": True, "made": made, "files": {i: f"marks/{i}-interior.png" for i in made}}

    @app.post("/images/background")
    def images_background(request: SceneBackgroundRequest) -> dict:
        """Edit ▸ Background Image with no build (docs/places/background.md): a scene's ground painted from words alone,
        in the project's view, filling the picture."""
        from .bfl import BflPainter
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        target = (root / request.path).resolve()
        if root.resolve() not in target.parents or target.suffix.lower() != ".png":
            return {"ok": False, "errors": [f"Not a picture in the project: {request.path}"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        key = os.environ.get("BFL_API_KEY")
        if not key:
            return {"ok": False, "errors": ["No key for BFL: set BFL_API_KEY in ai_service/.env"]}
        provider = request.provider or state.get("backgroundProvider", BACKGROUND_PROVIDERS[0])
        if provider not in BACKGROUND_PROVIDERS:
            return {"ok": False, "errors": [f"Unknown background model {provider}: one of {', '.join(BACKGROUND_PROVIDERS)}"]}
        view = ("seen from above at the game's isometric angle (2:1), the ground receding diagonally" if request.view == "isometric"
                else "seen straight from above (top-down), as a map")
        prompt = (f"A game's background: the ground of a scene, {view}. {request.description.strip()}\n"
                  "The ground fills the whole picture edge to edge: no frame, no border, no sky, no horizon, no text, no people, "
                  "no buildings unless the words ask for them. Even light, so things placed on it fit.")
        report = progress.reporter()
        report(0, 1, f"Painting the scene's background ({provider.split(':', 1)[1]})")
        try:
            png = BflPainter(key, provider.split(":", 1)[1]).create(prompt, width=request.width, height=request.height, seed=request.seed)
        except Exception as error:  # noqa: BLE001 - said to the editor
            return {"ok": False, "errors": [f"The background couldn't be painted: {error}"]}
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(png)
        report(1, 1, "Background painted")
        return {"ok": True, "path": target.relative_to(root.resolve()).as_posix(), "provider": provider, "prompt": prompt}

    @app.post("/assets/interior")
    def assets_interior(request: AssetInteriorRequest) -> dict:
        """Generate interior (docs/places/interiors.md §5): a building's picture cut away by BFL, written beside it as
        <name>-interior.png, the same size."""
        from PIL import Image
        from .bfl import BflPainter
        from .matched import count_storeys, floor_name, make_floors
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        source = (root / request.path).resolve()
        if root.resolve() not in source.parents or not source.is_file():
            return {"ok": False, "errors": [f"No picture at {request.path}"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        key = os.environ.get("BFL_API_KEY")
        if not key:
            return {"ok": False, "errors": ["Interiors are painted by BFL: set BFL_API_KEY in ai_service/.env"]}
        report = progress.reporter()
        report(0, 1, f"The interior of {source.name}")
        with Image.open(source) as im:
            exterior = im.convert("RGBA")
        purpose = f"of {request.purpose}" if request.purpose else "a building like this has"
        storeys = count_storeys(state["vision"], exterior)
        report(0, 1, f"The interior of {source.name}: {storeys} floor{'s' if storeys > 1 else ''}")
        floors = make_floors(BflPainter(key, "flux-3-image"), exterior, purpose, "", request.seed, storeys)
        if not floors or floors[0] is None:
            return {"ok": False, "errors": ["BFL couldn't cut it away; try again"]}
        paths = []
        for k, floor in enumerate(floors, 1):
            if floor is None:
                break  # the floors above a missing one aren't reached
            target = source.with_name(floor_name(source.stem, k))
            floor.save(target)
            paths.append(target.relative_to(root.resolve()).as_posix())
        report(1, 1, "Interior made")
        return {"ok": True, "path": paths[0], "floors": paths}

    @app.post("/parts/find")
    def parts_find(request: PartsRequest) -> dict:
        """The moving parts of a picture (Build ▸ Animate: a building's door; docs/assets/animations.md §4): their boxes,
        found by the vision model."""
        from .vision import find_parts
        root: Path | None = state["root"]
        vision = state.get("vision")
        if root is None or vision is None:
            return {"ok": False, "errors": ["Finding parts needs the project open and a vision model (AI on)"]}
        return find_parts(vision, root, request.path, request.kinds, request.rect)

    @app.post("/parts/door")
    def parts_door(request: DoorRequest) -> dict:
        """A building's door opened from its own pixels (Build ▸ Animate; app/doors.py): its frames and a spritesheet
        into the assets, as /animations/generate gives them, and the doorway's box (where they go on the picture)."""
        from PIL import Image
        from .doors import door_frames, png
        from .pixellab import spritesheet
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        source = (root / request.path).resolve()
        if root.resolve() not in source.parents or not source.is_file() or len(request.box) != 4:
            return {"ok": False, "errors": [f"No picture {request.path}"]}
        with Image.open(source) as im:
            made = door_frames(im.convert("RGBA"), request.box, max(4, min(48, request.frames)), exact=request.exact,
                               leaves=request.leaves)
        if made is None:
            return {"ok": False, "errors": ["no doorway found where the door was seen"]}
        frames, doorway = made
        provider, warnings, fps = "engine", [], 20
        by_bfl = request.animator == "bfl:flux-video" and bool(os.environ.get("BFL_API_KEY"))
        if by_bfl or (request.animator == "pixellab" and os.environ.get("PIXELLAB_API_KEY")):
            # PixelLab (docs/assets/animations.md §4.1): the doorway's crop opened, kept to the doorway (what's around
            # it, and in front of it, the building's own); the engine's door if it can't.
            from PIL import ImageChops
            from .pixellab import PixelLabImageGenerator, animate_object
            x, y, w, h = doorway
            with Image.open(source) as im:
                crop = im.convert("RGBA").crop((x, y, x + w, y + h))
            mask = frames[0].getchannel("A")
            opening = ("the door swings slowly open inward on its hinges, showing the dark inside of the building; "
                       "the frame, the arch and the wall around it stay still; no people")
            try:
                if by_bfl:
                    from .bfl_video import animate_object as animate_by_video
                    opened = animate_by_video(os.environ["BFL_API_KEY"], crop, opening, "a building's closed wooden door", 12)
                else:
                    opened = animate_object(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), crop, opening, 8)
                # Each frame over the open doorway (its last frame, on dark): where the animator left a frame see-through
                # (asked for no background, it can take part of the door for it), the building's closed door would show.
                base = Image.new("RGBA", crop.size, (26, 18, 13, 255))
                base.alpha_composite(opened[-1])
                frames = []
                for f in opened:
                    under = base.copy()
                    under.alpha_composite(f)
                    under.putalpha(mask)
                    frames.append(under)
                provider, fps = ("bfl", 12) if by_bfl else ("pixellab", 12)
            except Exception as error:  # noqa: BLE001 - the door opened its own way instead
                warnings.append(f"{'FLUX video' if by_bfl else 'PixelLab'} couldn't animate it ({str(error)[:200]}): opened from its own pixels instead")
        name = re.sub(r"[^a-z0-9_-]+", "-", request.name.lower()).strip("-")[:60] or "door"
        folder = root / "assets" / "textures" / "generated" / "animations" / name
        folder.mkdir(parents=True, exist_ok=True)
        paths = []
        for k, f in enumerate(frames):
            file = folder / f"{name}-{k + 1}.png"
            file.write_bytes(png(f))
            paths.append(file.relative_to(root).as_posix())
        sheet = folder / f"{name}-sheet.png"
        sheet.write_bytes(spritesheet([png(f) for f in frames]))
        (folder / f"{sheet.name}.gen.json").write_text(json.dumps(
            {"description": "a door opening", "from": request.path, "frames": len(frames), "provider": provider,
             "model": {"pixellab": "animate-with-text-v3", "bfl": "flux-3-video"}.get(provider, "doors.py"),
             "doorway": doorway, "created": datetime.now(timezone.utc).isoformat(timespec="seconds")}, indent=2) + "\n", encoding="utf-8")
        return {"ok": True, "name": name, "frames": paths, "sheet": sheet.relative_to(root).as_posix(), "clip": "open", "fps": fps,
                "doorway": doorway, "humanoid": False, "provider": provider, "warnings": warnings}

    @app.post("/animations/generate")
    def animations_generate(request: AnimationRequest) -> dict:
        """An asset of the project animated (PixelLab): its frames and a spritesheet, into the assets."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        model = bfl_art()
        video = video_animator(request.provider)
        v3 = pixellab_animator(request.provider)
        copies = copies_animator(request.provider)
        auto = autosprite_animator(request.provider)
        if not auto and not copies and not v3 and not video and not model and not os.environ.get("PIXELLAB_API_KEY"):
            return {"ok": False, "errors": ["Animations are made by AutoSprite (AUTO_SPRITE_API_KEY in ai_service/.env) or PixelLab (PIXELLAB_API_KEY)"]}
        source = (root / request.path).resolve()
        if root.resolve() not in source.parents or not source.is_file():
            return {"ok": False, "errors": ["Pick an image of the project to animate"]}
        from .pixellab import PixelLabImageGenerator, animate, spritesheet
        name = re.sub(r"[^a-z0-9-]+", "-", (request.name or source.stem + "-" + request.action).lower()).strip("-")[:48] or "animation"
        what = request.description or source.stem.replace("-", " ")
        report = progress.reporter()
        humanoid = None
        # A thing (not a person: a chest, a lever, a flag) by PixelLab, a person by AutoSprite (a character animator: given
        # a thing it adds a person and redraws it; docs/assets/animations.md §4.1).
        thing = False
        thing_by_bfl = request.objectProvider == "bfl:flux-video" and bool(os.environ.get("BFL_API_KEY"))
        if thing_by_bfl or (request.objectProvider == "pixellab" and os.environ.get("PIXELLAB_API_KEY")):
            from . import autosprite
            told = source.with_name(source.name + ".gen.json")
            made_as = json.loads(told.read_text(encoding="utf-8")).get("description", "") if told.is_file() else ""
            humanoid = autosprite.is_humanoid(" ".join([request.description, made_as, source.stem]), False, state["provider"])
            thing = humanoid is False
        try:
            if thing:
                from PIL import Image
                from .charsheet import clip_for
                from .pixellab import animate_object
                once = (request.clip.strip() or clip_for(request.action, False)) in ("open", "close", "break")
                report(0, 1, f"Animating {source.name}: {request.action} ({'FLUX video' if thing_by_bfl else 'PixelLab'})")
                with Image.open(source) as im:
                    picture = im.convert("RGBA")
                if max(picture.size) > 384:  # its frames at most 384 px a side
                    picture.thumbnail((384, 384), Image.Resampling.LANCZOS)
                if thing_by_bfl:
                    from .bfl_video import animate_object as animate_by_video
                    made = animate_by_video(os.environ["BFL_API_KEY"], picture, request.action, what, 12, loop=not once)
                else:
                    made = animate_object(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), picture,
                                          f"{request.action}; only {what} moves, nothing else appears", 8, loop=not once,
                                          on_status=lambda text: report(0, 1, f"Animating {source.name}: {text}"))
                frames = []
                for f in made:
                    buf = io.BytesIO()
                    f.save(buf, "PNG")
                    frames.append(buf.getvalue())
            elif auto:
                from . import autosprite
                from PIL import Image
                report(0, 1, f"Animating {source.name}: {request.action} (AutoSprite)")
                told = source.with_name(source.name + ".gen.json")  # what the picture was made as, when it was made here
                made_as = json.loads(told.read_text(encoding="utf-8")).get("description", "") if told.is_file() else ""
                humanoid = autosprite.is_humanoid(" ".join([request.description, made_as, source.stem]), False, state["provider"])
                with Image.open(source) as im:
                    size = max(32, min(256, max(im.size)))
                    height = (im.convert("RGBA").getbbox() or (0, 0, 0, size))[3] - (im.convert("RGBA").getbbox() or (0, 0, 0, 0))[1]
                lab = autosprite.AutoSprite(autosprite.api_key())
                character = lab.create(source.stem, source.read_bytes(), humanoid, request.description or made_as or what)
                shot, _ = lab.animate(character, request.action, 12, 256,
                                      on_status=lambda text: report(0, 1, f"Animating {source.name}: {text}"))
                frames = []
                with Image.open(source) as im:
                    picture = im.convert("RGBA")
                for f in autosprite.fitted(autosprite.cleaned({"one": shot}, picture), size, height)["one"]:
                    buf = io.BytesIO()
                    f.save(buf, "PNG")
                    frames.append(buf.getvalue())
            elif copies:
                from .bfl import BflPainter
                from . import bfl_sprites
                report(0, 1, f"Animating {source.name}: {request.action} (BFL {COPIES_MODEL})")
                from PIL import Image
                with Image.open(source) as im:
                    size = max(32, min(256, max(im.size)))
                frames = bfl_sprites.animate_copies(BflPainter(os.environ["BFL_API_KEY"], COPIES_MODEL), source.read_bytes(),
                                                    request.action, request.direction, size)
            elif v3:
                from .pixellab import animate_v3
                report(0, 1, f"Animating {source.name}: {request.action} (PixelLab v3)")
                from PIL import Image
                with Image.open(source) as im:
                    side = max(64, min(256, max(im.size)))
                frames = animate_v3(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), source.read_bytes(), request.action, 8, side,
                                    on_status=lambda text: report(0, 1, f"Animating {source.name}: {text}"))
            elif video:
                from . import bfl_video
                report(0, 1, f"Animating {source.name}: {request.action} (FLUX 3 video)")
                from PIL import Image
                with Image.open(source) as im:
                    size = max(32, min(256, max(im.size)))
                frames = bfl_video.animate(os.environ["BFL_API_KEY"], source.read_bytes(), request.action, what, request.direction, 8, size)
            elif model:
                from .bfl import BflPainter
                from . import bfl_sprites
                report(0, 1, f"Animating {source.name}: {request.action} ({model})")
                from PIL import Image
                with Image.open(source) as im:
                    size = max(32, min(256, max(im.size)))
                frames = bfl_sprites.animate(BflPainter(os.environ["BFL_API_KEY"], model), source.read_bytes(), request.action, what,
                                             request.direction, request.view, 4, size)
            else:
                frames = animate(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), source.read_bytes(), what, request.action,
                                 request.direction, request.view, project_style.project_palette(root))
        except (ProviderError, OSError) as error:
            return {"ok": False, "errors": [str(error)]}
        except Exception as error:  # noqa: BLE001 - BFL's: said to the editor
            return {"ok": False, "errors": [f"BFL couldn't animate it: {error}"]}
        report(1, 1, "Animated")
        folder = root / "assets" / "textures" / "generated" / "animations" / name
        folder.mkdir(parents=True, exist_ok=True)
        made = []
        for k, data in enumerate(frames):
            file = folder / f"{name}-{k + 1}.png"
            file.write_bytes(data)
            made.append(file.relative_to(root).as_posix())
        sheet = folder / f"{name}-sheet.png"
        sheet.write_bytes(spritesheet(frames))
        meta = {"description": request.description, "action": request.action, "direction": request.direction, "from": request.path,
                "frames": len(frames), "provider": ("bfl" if thing_by_bfl else "pixellab") if thing else "autosprite" if auto else "pixellab",
                "model": "pixellab animate-with-text-v3" if thing else "autosprite" if auto else "pixellab animate-with-text", "humanoid": humanoid,
                "created": datetime.now(timezone.utc).isoformat(timespec="seconds")}
        (folder / f"{sheet.name}.gen.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
        # The clip it's kept as on its sheet (the editor's): named by what it does, so an animator plays it.
        from .charsheet import clip_for
        clip = clip_for(request.action, humanoid is not False)
        if humanoid is False and clip not in ("open", "close", "break"):
            clip = "idle"  # a thing's own motion (a flame, a flag): it loops
        if request.clip.strip():
            clip = re.sub(r"[^a-z0-9_]+", "_", request.clip.strip().lower()).strip("_") or clip
        return {"ok": True, "name": name, "frames": made, "sheet": sheet.relative_to(root).as_posix(), "humanoid": humanoid,
                "clip": clip}

    def bfl_art() -> str | None:
        """The project's art model when it's BFL's (Preferences / Project Settings ▸ AI ▸ Art): characters and animations
        are then drawn by it (bfl_sprites), else by PixelLab."""
        art = state.get("artProvider", "auto")
        return art.split(":", 1)[1] if isinstance(art, str) and art.startswith("bfl:") and os.environ.get("BFL_API_KEY") else None

    def video_animator(provider: str) -> bool:
        """Animate with FLUX 3 video (bfl_video): when it's the Animate choice, while there's a BFL key."""
        return provider == "bfl:flux-3-video" and bool(os.environ.get("BFL_API_KEY"))

    def pixellab_animator(provider: str) -> bool:
        """Animate with PixelLab's Animate with text v3: when it's the Animate choice, while there's a PixelLab key."""
        return provider == "pixellab:animate-v3" and bool(os.environ.get("PIXELLAB_API_KEY"))

    def copies_animator(provider: str) -> bool:
        """Animate by BFL image edits of the pose's copies (bfl_sprites.animate_copies): when asked for, with a BFL key."""
        return provider == "bfl:image" and bool(os.environ.get("BFL_API_KEY"))

    def autosprite_animator(provider: str) -> bool:
        """Animate with AutoSprite (the default), while there's its key."""
        from .autosprite import api_key
        return (provider or ANIMATE_PROVIDERS[0]) == "autosprite" and bool(api_key())

    def style_words(root: Path) -> str:
        colours = project_style.project_palette(root)
        words = project_style.art_style_words()
        text = f"Art style: {words}." if words else ""
        if colours:
            text += f" Colours like the project's other art: {', '.join(colours[:8])}."
        return text.strip()

    @app.post("/characters/generate")
    def characters_generate(request: CharacterRequest) -> dict:
        """A character from 4 or 8 directions, each into the project's assets: by the project's art model when it's BFL's
        (bfl_sprites: painted facing the camera, each direction an edit of it), else PixelLab."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        name = re.sub(r"[^a-z0-9-]+", "-", request.name.lower()).strip("-")[:40] or "character"
        directions, size = 8 if request.directions == 8 else 4, max(32, min(256, request.size))
        model = bfl_art()
        report = progress.reporter()
        if model:
            from .bfl import BflPainter
            from . import bfl_sprites
            report(0, 1, f"Drawing {name} from {directions} directions ({model})")
            try:
                images = bfl_sprites.character(BflPainter(os.environ["BFL_API_KEY"], model), request.description, directions, size,
                                               request.view, request.isometric, style_words(root))
            except Exception as error:  # noqa: BLE001 - said to the editor
                return {"ok": False, "errors": [f"BFL couldn't draw it: {error}"]}
            provider, model_name = "bfl", model
        else:
            if not os.environ.get("PIXELLAB_API_KEY"):
                return {"ok": False, "errors": ["Characters are drawn by the project's art model when it's BFL's, else PixelLab: "
                                                "set one in Project Settings ▸ AI ▸ Art (BFL_API_KEY or PIXELLAB_API_KEY in ai_service/.env)"]}
            from .pixellab import PixelLabImageGenerator
            try:
                images = PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]).character(
                    request.description, directions, size, request.view, request.isometric, project_style.project_palette(root))
            except ProviderError as error:
                return {"ok": False, "errors": [str(error)]}
            provider, model_name = "pixellab", "pixellab character"
        report(1, 1, f"{name} drawn")
        if not images:
            return {"ok": False, "errors": [f"{provider} finished but sent no images"]}
        # One character sheet (docs/assets/character-sheets.md): its idle clip, a pose each direction.
        from . import charsheet
        sheet = charsheet.new_sheet("", size, {"name": name, "description": request.description, "directions": directions, "size": size,
                                               "view": request.view, "isometric": request.isometric, "provider": provider, "model": model_name,
                                               "created": datetime.now(timezone.utc).isoformat(timespec="seconds")})
        sheet, atlas = charsheet.add_clip(sheet, None, "idle", {d: [data] for d, data in images.items()})
        path, texture = charsheet.write(root, f"assets/characters/{name}", name, sheet, atlas)
        return {"ok": True, "name": name, "sheet": path, "texture": texture, "assets": [{"path": path}, {"path": texture}]}

    @app.post("/characters/animate")
    def characters_animate(request: CharacterAnimateRequest) -> dict:
        """A character sheet's action, every direction it faces (docs/assets/character-sheets.md): each painted direction
        animated from its idle pose (the project's art model when it's BFL's, else PixelLab), the west side the east side
        mirrored; added to the sheet as one clip."""
        from concurrent.futures import ThreadPoolExecutor
        from PIL import Image
        from . import charsheet
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        target = (root / request.sheet).resolve()
        if root.resolve() not in target.parents or not target.is_file():
            return {"ok": False, "errors": [f"No character sheet {request.sheet}"]}
        sheet, atlas = charsheet.read(root, request.sheet)
        if sheet.get("kind") != "character":
            return {"ok": False, "errors": [f"{request.sheet} isn't a character sheet"]}
        info = sheet.get("character", {})
        faces = charsheet.directions_of(sheet)
        mirrored = {"west": "east", "south-west": "south-east", "north-west": "north-east"}
        painted = [d for d in faces if not (d in mirrored and mirrored[d] in faces)]
        if video_animator(request.provider) or pixellab_animator(request.provider):
            painted = [d for d in ("south", "east", "north") if d in faces]  # the calls it makes (see below)
        auto = autosprite_animator(request.provider)
        if auto:
            painted = list(faces)  # AutoSprite's isometric pack: all 8 in one render
        model = bfl_art()
        video = video_animator(request.provider)
        v3 = pixellab_animator(request.provider)
        copies = copies_animator(request.provider)
        if not auto and not copies and not v3 and not video and not model and not os.environ.get("PIXELLAB_API_KEY"):
            return {"ok": False, "errors": ["Animations are made by AutoSprite (AUTO_SPRITE_API_KEY in ai_service/.env) or PixelLab (PIXELLAB_API_KEY)"]}
        what = request.description or info.get("description", "") or info.get("name", "the character")
        size = sheet["grid"]["width"]
        report = progress.reporter()
        report(0, len(painted), f"Animating {info.get('name', 'it')}: {request.action}, {len(painted)} directions "
                                f"({'AutoSprite' if auto else 'BFL ' + COPIES_MODEL if copies else 'PixelLab v3' if v3 else 'FLUX 3 video' if video else model or 'PixelLab'})")
        done = [0]
        hands: dict[str, list] = {}

        def one(direction: str) -> tuple[str, list[bytes] | str]:
            pose = charsheet.frame(sheet, atlas, "idle", direction) or charsheet.frame(sheet, atlas, next(iter(sheet["clips"])), direction)
            if pose is None:
                return direction, "no pose to start from"
            buf = io.BytesIO()
            pose.save(buf, "PNG")
            try:
                if copies:
                    from .bfl import BflPainter
                    from . import bfl_sprites
                    frames = bfl_sprites.animate_copies(BflPainter(os.environ["BFL_API_KEY"], COPIES_MODEL), buf.getvalue(),
                                                        request.action, direction, size)
                elif v3:
                    from .pixellab import PixelLabImageGenerator, animate_v3
                    frames = animate_v3(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), buf.getvalue(), request.action, 8,
                                        max(64, min(256, size * 4)),
                                        on_status=lambda text: report(done[0], len(painted), f"Animating {info.get('name', 'it')}: {direction} {text}"))
                elif video:
                    from . import bfl_video
                    frames = bfl_video.animate(os.environ["BFL_API_KEY"], buf.getvalue(), request.action, what, direction, size=size,
                                               play_fps=12)
                elif model:
                    from .bfl import BflPainter
                    from . import bfl_sprites
                    frames = bfl_sprites.animate(BflPainter(os.environ["BFL_API_KEY"], model), buf.getvalue(), request.action, what,
                                                 direction, info.get("view", "low top-down"), 4, size)
                else:
                    from .pixellab import PixelLabImageGenerator, animate
                    frames = animate(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), buf.getvalue(), what, request.action,
                                     direction, info.get("view", "low top-down"), project_style.project_palette(root))
            except Exception as error:  # noqa: BLE001 - that direction is said; the others stand
                return direction, str(error)[:200]
            done[0] += 1
            report(done[0], len(painted), f"Animating {info.get('name', 'it')}: {direction} done")
            return direction, frames
        if auto:
            # AutoSprite: the character uploaded once (its id kept in the sheet), then ONE isometric pack, every direction
            # in one render (15 credits); the figure brought to the idle pose's height.
            from . import autosprite
            front = charsheet.frame(sheet, atlas, "idle", "south") or charsheet.frame(sheet, atlas, next(iter(sheet["clips"])), "south")
            if front is None:
                return {"ok": False, "errors": ["The character has no south pose to start from"]}
            buf = io.BytesIO()
            front.save(buf, "PNG")
            lab = autosprite.AutoSprite(autosprite.api_key())
            try:
                if not info.get("autosprite"):
                    humanoid = autosprite.is_humanoid(" ".join([what, info.get("name", "")]), True, state["provider"])
                    info["autosprite"], info["humanoid"] = lab.create(info.get("name", "character"), buf.getvalue(), humanoid, what), humanoid
                    sheet["character"] = info
                shot, shot_meta = lab.pack(info["autosprite"], request.action, 12, 256,
                                           on_status=lambda text: report(0, len(painted), f"Animating {info.get('name', 'it')}: {text}"),
                                           holding=autosprite.ROD if request.hold else "")
            except Exception as error:  # noqa: BLE001 - said to the editor
                return {"ok": False, "errors": [f"AutoSprite: {error}"]}
            box = front.getbbox() or (0, 0, size, size)
            hands: dict[str, list] = {}
            if request.hold:
                for d in list(shot):
                    shot[d], hands[d] = autosprite.take_rod(shot[d])
            shot = autosprite.cleaned({d: f for d, f in shot.items() if d in faces}, front)
            results = autosprite.fitted(shot, size, box[3] - box[1])
            if hands:
                hands = autosprite.fit_points(shot, size, box[3] - box[1], {d: p for d, p in hands.items() if d in shot})
            auto_fps = autosprite.fps_of(shot_meta, 12)
        elif copies:
            # One call a direction it faces, west's side mirrored from east's (south-east, east, north-east): five for eight,
            # one after another (a low-credit BFL account runs one task at a time).
            results = {d: one(d)[1] for d in painted}
        elif video or v3:
            # As few calls as keep it good: south, east and north alone, one each; west is east mirrored, and the diagonals
            # are left to the nearest direction the clip has (the animator's), their idle poses still their own. PixelLab's
            # all at once (its queue is the wait); BFL's one after another (a low-credit account runs one task at a time).
            wanted = [d for d in ("south", "east", "north") if d in faces]
            if v3:
                with ThreadPoolExecutor(3) as pool:
                    results = dict(pool.map(one, wanted))
            else:
                results = {d: one(d)[1] for d in wanted}
        else:
            with ThreadPoolExecutor(3) as pool:
                results = dict(pool.map(one, painted))
        frames = {d: f for d, f in results.items() if isinstance(f, list) and f}
        errors = [f"{d}: {f}" for d, f in results.items() if not isinstance(f, list)]
        for west, east in mirrored.items():
            if west in faces and east in frames and west not in frames:
                frames[west] = [(f if isinstance(f, Image.Image) else Image.open(io.BytesIO(f))).convert("RGBA").transpose(Image.FLIP_LEFT_RIGHT)
                                for f in frames[east]]
        if not frames:
            return {"ok": False, "errors": errors or ["nothing was drawn"]}
        clip = charsheet.clip_for(request.action, info.get("humanoid", True) is not False)
        base = clip.split("_")[0]
        if request.weaponClass.strip():
            clip = f"{base}_{re.sub(r'[^a-z0-9]+', '_', request.weaponClass.strip().lower()).strip('_')}"
        sheet, atlas = charsheet.add_clip(sheet, atlas, clip, frames, fps=auto_fps if auto else 12.0 if video else 10.0 if v3 or copies else 8.0 if clip in ("walk", "idle") else 10.0,
                                          loop="loop" if base in ("walk", "idle") or base not in ("attack", "cast", "hurt", "die", "use", "open", "close", "break") else "once",
                                          points={"hand": hands} if auto and hands else None)
        name = Path(request.sheet).name.replace(".character.anim.json", "")
        path, texture = charsheet.write(root, Path(request.sheet).parent.as_posix(), name, sheet, atlas)
        report(len(painted), len(painted), f"{clip} added")
        return {"ok": True, "sheet": path, "texture": texture, "clip": clip, "directions": sorted(frames), "errors": errors,
                "humanoid": info.get("humanoid"),
                "assets": [{"path": path}, {"path": texture}]}

    @app.post("/characters/from-pictures")
    def characters_from_pictures(request: CharacterPicturesRequest) -> dict:
        """A character's loose direction pictures (…-south.png, -east.png…, made before character sheets) as one sheet."""
        from PIL import Image
        from . import charsheet
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        by_direction: dict[str, list] = {}
        for picture in request.pictures:
            source = (root / picture).resolve()
            word = next((d for d in sorted(charsheet.DIRECTIONS, key=len, reverse=True) if source.stem.endswith("-" + d)), None)
            if root.resolve() not in source.parents or not source.is_file() or word is None:
                continue
            with Image.open(source) as im:
                by_direction[word] = [im.convert("RGBA")]
        if not by_direction:
            return {"ok": False, "errors": ["none of the pictures face a direction (…-south.png)"]}
        size = max(max(f[0].size) for f in by_direction.values())
        name = re.sub(r"[^a-z0-9-]+", "-", request.name.lower()).strip("-")[:40] or "character"
        sheet = charsheet.new_sheet("", size, {"name": name, "description": request.description, "directions": len(by_direction), "size": size,
                                               "from": request.pictures[:8]})
        sheet, atlas = charsheet.add_clip(sheet, None, "idle", by_direction)
        path, texture = charsheet.write(root, f"assets/characters/{name}", name, sheet, atlas)
        return {"ok": True, "name": name, "sheet": path, "texture": texture}

    @app.put("/provider")
    def set_provider(request: ProviderRequest) -> dict:
        """The model that writes (and, for Claude, looks at maps): the editor's Preferences."""
        if request.provider not in PROVIDERS:
            return {"ok": False, "errors": [f"Unknown provider {request.provider}: one of {', '.join(PROVIDERS)}"]}
        chosen = make_provider(request.provider, request.model or None)
        if chosen is None:
            key = "ANTHROPIC_API_KEY" if request.provider == "claude" else "MISTRAL_API_KEY"
            return {"ok": False, "errors": [f"No key for {request.provider}: set {key} in ai_service/.env"],
                    "available": available_providers()}
        state["provider"] = chosen
        # Claude sees images itself; with Mistral, its vision model looks at them.
        state["vision"] = chosen if request.provider == "claude" else (state.get("mistralVision") or state["vision"])
        return {"ok": True, "provider": chosen.name, "model": chosen.model, "available": available_providers()}

    @app.post("/places/revise-plan")
    def places_revise_plan(request: RevisePlanRequest) -> dict:
        if not request.instruction.strip():
            return {"ok": False, "errors": ["Say what to change"]}
        return _planning(lambda p: revise_plan(p, request.brief, request.plan, request.result, request.instruction,
                                               request.context, request.mode, where=request.where, stage=request.stage,
                                               room=request.room, language=request.language))

    @app.post("/places/review-layout")
    def places_review_layout(request: ReviewLayoutRequest) -> dict:
        root: Path | None = state["root"]
        vision: Provider | None = state["vision"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        if vision is None:
            return {"ok": False, "errors": ["No AI provider configured (a vision model, MISTRAL_API_KEY)"]}
        path = (root / request.image).resolve()
        if root.resolve() not in path.parents or not path.is_file():
            return {"ok": False, "errors": [f"No map at {request.image}"]}
        result = review_layout(vision, map_chunk(path), request.brief, request.plan, request.result, request.verify,
                               request.context, kind=request.kind)
        state["usage"].add(Usage(**result["usage"]))
        return result

    @app.post("/assets/sketch")
    def assets_sketch(request: SketchRequest) -> dict:
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        # The sketch is painted by Mistral's image model (big, from words alone), whatever draws the
        # pieces themselves (PixelLab, or the user's choice): it decides how things look, not where.
        painter = make_image_generator("mistral", None)
        if painter is None:
            return {"ok": False, "errors": ["Sketching needs Mistral's image model (MISTRAL_API_KEY)"]}
        return sketch_place(painter, state["provider"], state["vision"], root, request.context, request.outDir, request.name)

    @app.post("/places/inspect")
    def places_inspect(request: InspectRequest) -> dict:
        root: Path | None = state["root"]
        vision: Provider | None = state["vision"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        if vision is None:
            return {"ok": False, "errors": ["No vision model (MISTRAL_API_KEY)"]}
        chunks = []
        for image in request.images:
            path = (root / image).resolve()
            if root.resolve() not in path.parents or not path.is_file():
                return {"ok": False, "errors": [f"No image at {image}"]}
            chunks.append(map_chunk(path, longest=1600))
        if not chunks:
            return {"ok": False, "errors": ["No image to look at"]}
        result = inspect_place(vision, request.kind, chunks, request.model_dump(), request.windows or None)
        if "usage" in result:
            state["usage"].add(Usage(**result["usage"]))
        return result

    @app.post("/assets/web-search")
    def assets_web_search(request: WebSearchRequest) -> dict:
        """Free art from an approved site, licence checked (7.9, rule W)."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        if not request.query.strip():
            return {"ok": False, "errors": ["Say what to look for"]}
        return search_web(root, request.query, request.folder, max(1, min(request.maxAssets, 8)), share_alike=request.shareAlike,
                          queries=request.queries)

    @app.post("/assets/web-preview")
    def assets_web_preview(request: WebPreviewRequest) -> dict:
        """The Assets panel's Web Search: ranked results with their previews, nothing downloaded."""
        if not request.query.strip():
            return {"ok": False, "errors": ["Say what to look for"]}
        rejected: list = []
        root: Path | None = state["root"]
        palette = project_style.project_palette(root) if root else []
        found = all_candidates(request.query, max(1, min(request.maxResults, 24)), request.shareAlike, request.queries or None,
                               rejected=rejected, pause=0.2, palette=palette)
        revised: list[str] = []
        # Few that fit: the AI writes better searches from what came back, and they're run too.
        if len([c for c in found if c["score"] >= 0.5]) < 3 and state["provider"] is not None:
            revised = better_queries(state["provider"], request.query, [c["title"] for c in found[:10]])
            if revised:
                more = all_candidates(request.query, max(1, min(request.maxResults, 24)), request.shareAlike, revised,
                                      rejected=rejected, pause=0.2, palette=palette)
                seen = {c["page"] for c in found}
                found = sorted(found + [c for c in more if c["page"] not in seen], key=lambda c: -c["score"])[: max(1, min(request.maxResults, 24))]
        return {"ok": True, "results": found, "rejected": rejected[:20], "revisedQueries": revised,
                "sources": sorted({c.get("source", "") for c in found}), "palette": palette}

    @app.post("/assets/references")
    def assets_references(request: ReferencesRequest) -> dict:
        """A built place's chosen pieces kept as the project's style references (place-refine.md §15)."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        refs = project_style.add(root, request.pieces[:8])
        return {"ok": True, "references": len(refs), "palette": project_style.project_palette(root)}

    @app.post("/assets/web-fetch")
    def assets_web_fetch(request: WebFetchRequest) -> dict:
        """One file a preview listed, into the project."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        path, why, pixels = fetch_file(root, request.url, request.folder)
        if path is None:
            return {"ok": False, "errors": [why]}
        return {"ok": True, "path": path, "pixels": pixels}

    @app.post("/assets/generate")
    def assets_generate(request: GenerateAssetRequest) -> dict:
        root: Path | None = state["root"]
        images: ImageGenerator | None = state["images"]
        if root is None or images is None:
            return {"ok": False, "errors": ["No project or image generator available (MISTRAL_API_KEY)"]}
        if len(request.variants) > 6:
            return {"ok": False, "errors": ["At most 6 variants at a time"]}
        description, brief = request.description, None
        # The project's look, so new art matches what's there.
        if request.context and root is not None:
            if references := project_style.brief_context(root):
                request.context["matchProjectStyle"] = references
        if request.context and state["provider"] is not None:
            try:
                description, brief = write_art_prompt(state["provider"], request.description, request.context, request.kind)
            except ProviderError as error:
                brief = {"error": str(error)}  # drawn from the plain description
        try:
            with_images = [str((root / p).resolve()) for p in request.withImages
                           if root.resolve() in (root / p).resolve().parents and (root / p).is_file()]
            results = generate_sprites(images, root, request.name, description, request.kind,
                                       request.height, request.style, request.variants, request.evenLight, brief,
                                       layer=str(request.context.get("layer", "")) if request.context else "",
                                       with_images=with_images)
        except ValueError as error:
            return {"ok": False, "errors": [str(error)]}
        except ProviderError as error:
            return {"ok": False, "errors": [str(error)]}
        return {"ok": True, "assets": results}

    @app.post("/planner/{step}")
    def planner_step(step: str, body: dict = Body(default_factory=dict)) -> dict:
        """The AI Planner's steps (app/planner.py): concept, outline, act, scene, critic, character,
        quests, place, systems, visuals, review."""
        provider: Provider | None = state["provider"]
        if provider is None:
            return {"ok": False, "errors": ["No AI provider configured: set MISTRAL_API_KEY in ai_service/.env"]}
        run = PLANNER_STEPS.get(step)
        if run is None:
            return {"ok": False, "errors": [f"no planner step {step!r}"]}
        progress.label(body.pop("label", None))
        try:
            result = run(provider, body)
        except ProviderError as error:
            return {"ok": False, "errors": [str(error)]}
        except Exception as error:  # an answer of a shape the step's own code didn't expect
            return {"ok": False, "errors": [f"the step couldn't read the answer ({type(error).__name__}: {error})"]}
        if isinstance(result.get("usage"), dict):
            state["usage"].add(Usage(**result["usage"]))
        return result

    @app.post("/consistency")
    def consistency(request: ConsistencyRequest) -> dict:
        writing = writing_from_operations(request.operations)
        if not writing:
            return {"ok": True, "checked": False, "issues": [], "reason": "nothing that makes claims about the world"}
        index: WorldIndex | None = state["world"]
        checker: Provider | None = state["checker"]
        if index is None or checker is None:
            return {"ok": False, "checked": False, "issues": [], "errors": ["No checker or World Bible available"]}
        # Entries this proposal rewrites aren't compared with their old versions.
        exclude = set(request.exclude)
        for op in request.operations:
            if op.get("op") == "command" and op.get("name") == "world.write":
                args = op.get("args", {})
                if args.get("id"):
                    exclude.add(f"{args.get('kind')}/{args['id']}")
        entries = index.canon_entries(writing, k=8, max_chars=10000, exclude=exclude)
        canon = {e.key: format_entry(e) for e in entries}
        result = check_consistency(checker, canon, writing, len(request.operations))
        state["usage"].add(result.usage)
        return {
            "ok": result.checked,
            "checked": result.checked,
            "issues": [issue.__dict__ for issue in result.issues],
            "errors": result.errors,
            "canonKeys": result.canonKeys,
            "usage": result.usage.model_dump(),
        }

    return app


app = create_app(
    provider=default_provider(),
    embedder=default_embedder(),
    checker=default_checker(),
    images=default_image_generator(),
    vision=default_vision(),
)
