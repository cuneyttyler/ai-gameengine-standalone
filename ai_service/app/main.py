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

import os
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from .models import EditorContext, EngineSchema, Proposal, Usage
from .consistency import check_consistency, writing_from_operations
from .images import ImageGenerator, MistralImageGenerator, generate_sprites
from .places import interpret, plan_root, plan_zone, repair_brief, repair_node
from .vision import describe_assets
from .providers import MistralEmbedder, MistralProvider, Provider, ProviderError
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


class RepairBriefRequest(BaseModel):
    query: str
    context: dict
    brief: dict
    problems: list[dict]   # {"where", "message"} from the validator


class PlanRootRequest(BaseModel):
    brief: dict
    context: dict          # the root's subgraph and its possible children (engine/places Planner)
    mode: str = "auto"


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


def default_provider() -> Provider | None:
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    if not key:
        return None
    return MistralProvider(api_key=key, model=os.environ.get("MISTRAL_MODEL", DEFAULT_MODEL))


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
    state = {
        "provider": provider,
        "schema": schema,
        "usage": Usage(),
        "embedder": embedder,
        "world": None,
        "checker": checker,
        "images": images,
        "vision": vision,
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
                state["provider"], state["schema"], request.context, request.instruction, request.history, canon
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
        if not request.query.strip():
            return {"ok": False, "errors": ["Say what to build"]}
        result = interpret(provider, request.query, request.context, request.drafts, request.mode)
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
        return _planning(lambda p: plan_root(p, request.brief, request.context, request.mode))

    @app.post("/places/plan-zone")
    def places_plan_zone(request: PlanZoneRequest) -> dict:
        return _planning(lambda p: plan_zone(p, request.brief, request.zone, request.siblings, request.context, request.mode))

    @app.post("/places/repair-node")
    def places_repair_node(request: RepairNodeRequest) -> dict:
        return _planning(lambda p: repair_node(p, request.node, request.problems, request.context))

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
