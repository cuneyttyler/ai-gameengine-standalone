"""HTTP API (local only; the editor starts and stops this process).

  GET  /health          is it up, which provider/model, is a schema loaded
  PUT  /schema          the editor sends its reflection schema at startup
  GET  /schema          the schema the service is working against
  POST /propose         {instruction, context, history?} -> validated proposal
  GET  /usage           token totals for this session (cost visibility)
  PUT  /project         {root}: the open project, so its World Bible can be indexed
  POST /world/search    {query, k?} -> the most related World Bible entries

Run: uv run uvicorn app.main:app --port 8765
"""

from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from .models import EditorContext, EngineSchema, Proposal, Usage
from .providers import MistralEmbedder, MistralProvider, Provider, ProviderError
from .service import propose
from .world import Embedder, WorldIndex, load_entries

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


def default_embedder() -> Embedder | None:
    load_dotenv(Path(__file__).resolve().parent.parent / ".env")
    key = os.environ.get("MISTRAL_API_KEY")
    return MistralEmbedder(api_key=key) if key else None


def create_app(
    provider: Provider | None = None, schema: EngineSchema | None = None, embedder: Embedder | None = None
) -> FastAPI:
    app = FastAPI(title="AIGE AI Orchestration Service", version="0.1.0")
    state = {"provider": provider, "schema": schema, "usage": Usage(), "embedder": embedder, "world": None}

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
        try:
            result = propose(state["provider"], state["schema"], request.context, request.instruction, request.history)
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
        return {"ok": True, "worldEntries": len(load_entries(root))}

    @app.post("/world/search")
    def world_search(request: SearchRequest) -> dict:
        index: WorldIndex | None = state["world"]
        if index is None:
            return {"ok": False, "errors": ["The editor hasn't told the service which project is open"]}
        hits, method = index.search(request.query, max(1, min(request.k, 20)))
        return {"ok": True, "method": method, "hits": [hit.__dict__ for hit in hits]}

    return app


app = create_app(provider=default_provider(), embedder=default_embedder())
