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


class AnimationRequest(BaseModel):
    path: str            # the project image to animate
    action: str          # walking, swinging a sword, flickering
    description: str = ""
    direction: str = "south"
    view: str = "low top-down"
    name: str = ""


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
            "pixellab": bool(os.environ.get("PIXELLAB_API_KEY"))}


ART_PROVIDERS = ("auto", "mistral", "pixellab")


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
    """Looks at images (6.2): a vision-capable model; MISTRAL_VISION_MODEL picks another."""
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    if not key:
        return None
    return MistralProvider(api_key=key, model=os.environ.get("MISTRAL_VISION_MODEL", "mistral-medium-latest"),
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
                writer, state["schema"], request.context, request.instruction, request.history, canon, make_art
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
        return describe_assets(vision, root, request.paths, items=request.items, roles=request.roles or None)

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

    @app.post("/places/repair-node")
    def places_repair_node(request: RepairNodeRequest) -> dict:
        return _planning(lambda p: repair_node(p, request.node, request.problems, request.context))

    @app.get("/provider")
    def provider_info() -> dict:
        current: Provider | None = state["provider"]
        images: ImageGenerator | None = state["images"]
        return {"ok": True, "provider": getattr(current, "name", None), "model": getattr(current, "model", None),
                "available": available_providers(), "artProvider": state.get("artProvider", "auto"),
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

    @app.put("/art-provider")
    def set_art_provider(request: ArtProviderRequest) -> dict:
        """What draws the art (Preferences ▸ AI ▸ Art): auto (follows the LLM), mistral or pixellab."""
        if request.provider not in ART_PROVIDERS:
            return {"ok": False, "errors": [f"Unknown art provider {request.provider}: one of {', '.join(ART_PROVIDERS)}"]}
        images = make_image_generator(request.provider, getattr(state["provider"], "name", None))
        if images is None:
            key = "PIXELLAB_API_KEY" if request.provider == "pixellab" else "MISTRAL_API_KEY"
            return {"ok": False, "errors": [f"No key for {request.provider}: set {key} in ai_service/.env"], "available": available_providers()}
        state["images"], state["artProvider"] = images, request.provider
        return {"ok": True, "artProvider": request.provider, "art": images.name, "available": available_providers()}

    @app.post("/animations/generate")
    def animations_generate(request: AnimationRequest) -> dict:
        """An asset of the project animated (PixelLab): its frames and a spritesheet, into the assets."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        if not os.environ.get("PIXELLAB_API_KEY"):
            return {"ok": False, "errors": ["Animations are made by PixelLab: set PIXELLAB_API_KEY in ai_service/.env"]}
        source = (root / request.path).resolve()
        if root.resolve() not in source.parents or not source.is_file():
            return {"ok": False, "errors": ["Pick an image of the project to animate"]}
        from .pixellab import PixelLabImageGenerator, animate, spritesheet
        name = re.sub(r"[^a-z0-9-]+", "-", (request.name or source.stem + "-" + request.action).lower()).strip("-")[:48] or "animation"
        try:
            frames = animate(PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]), source.read_bytes(),
                             request.description or source.stem.replace("-", " "), request.action, request.direction, request.view,
                             project_style.project_palette(root))
        except (ProviderError, OSError) as error:
            return {"ok": False, "errors": [str(error)]}
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
                "frames": len(frames), "provider": "pixellab", "model": "pixellab animate-with-text",
                "created": datetime.now(timezone.utc).isoformat(timespec="seconds")}
        (folder / f"{sheet.name}.gen.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
        return {"ok": True, "name": name, "frames": made, "sheet": sheet.relative_to(root).as_posix()}

    @app.post("/characters/generate")
    def characters_generate(request: CharacterRequest) -> dict:
        """A character from 4 or 8 directions (PixelLab), each into the project's assets."""
        root: Path | None = state["root"]
        if root is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        load_dotenv(Path(__file__).resolve().parent.parent / ".env")
        if not os.environ.get("PIXELLAB_API_KEY"):
            return {"ok": False, "errors": ["Characters are made by PixelLab: set PIXELLAB_API_KEY in ai_service/.env"]}
        from .pixellab import PixelLabImageGenerator
        name = re.sub(r"[^a-z0-9-]+", "-", request.name.lower()).strip("-")[:40] or "character"
        try:
            images = PixelLabImageGenerator(os.environ["PIXELLAB_API_KEY"]).character(
                request.description, 8 if request.directions == 8 else 4, max(32, min(256, request.size)), request.view,
                request.isometric, project_style.project_palette(root))
        except ProviderError as error:
            return {"ok": False, "errors": [str(error)]}
        if not images:
            return {"ok": False, "errors": ["PixelLab finished but sent no images"]}
        folder = root / "assets" / "textures" / "generated" / "characters" / name
        folder.mkdir(parents=True, exist_ok=True)
        made = []
        for direction, data in images.items():
            file = folder / f"{name}-{direction}.png"
            file.write_bytes(data)
            meta = {"description": request.description, "family": name, "kind": "character", "direction": direction,
                    "provider": "pixellab", "model": "pixellab character", "created": datetime.now(timezone.utc).isoformat(timespec="seconds")}
            (folder / f"{file.name}.gen.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
            made.append({"path": file.relative_to(root).as_posix(), "direction": direction})
        return {"ok": True, "name": name, "assets": made}

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
                                               room=request.room))

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
            results = generate_sprites(images, root, request.name, description, request.kind,
                                       request.height, request.style, request.variants, request.evenLight, brief)
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
