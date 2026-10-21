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
import os
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel

from .models import EditorContext, EngineSchema, Proposal, Usage
from .consistency import check_consistency, writing_from_operations
from .images import ImageGenerator, MistralImageGenerator, generate_sprites
from .places import ask_questions, interpret, plan_root, plan_zone, repair_brief, repair_node, review_layout, revise_plan
from .vision import describe_assets, map_chunk
from .websource import search_web
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


class GenerateAssetRequest(BaseModel):
    name: str
    description: str
    kind: str = "object"
    height: int = 48
    style: str = "pixel"
    variants: list[str] = []


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
            "claude": bool(os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN"))}


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
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    return MistralImageGenerator(api_key=key) if key else None


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
        try:
            result = propose(
                writer, state["schema"], request.context, request.instruction, request.history, canon
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
        return {"ok": True, "provider": getattr(current, "name", None), "model": getattr(current, "model", None),
                "available": available_providers()}

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
        return search_web(root, request.query, request.folder, max(1, min(request.maxAssets, 8)), share_alike=request.shareAlike)

    @app.post("/assets/generate")
    def assets_generate(request: GenerateAssetRequest) -> dict:
        root: Path | None = state["root"]
        images: ImageGenerator | None = state["images"]
        if root is None or images is None:
            return {"ok": False, "errors": ["No project or image generator available (MISTRAL_API_KEY)"]}
        if len(request.variants) > 6:
            return {"ok": False, "errors": ["At most 6 variants at a time"]}
        try:
            results = generate_sprites(images, root, request.name, request.description, request.kind,
                                       request.height, request.style, request.variants)
        except ValueError as error:
            return {"ok": False, "errors": [str(error)]}
        except ProviderError as error:
            return {"ok": False, "errors": [str(error)]}
        return {"ok": True, "assets": results}

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
