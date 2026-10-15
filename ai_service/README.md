# AI Orchestration Service

The local Python sidecar behind the editor's AI features (`docs/technical.md` §7, §10).
The editor starts it automatically and stops it on exit; you only need this file to set
it up once or to run it by hand.

## Setup

Requires [uv](https://docs.astral.sh/uv/) (it manages the Python version and packages in
`.venv/`, nothing is installed system-wide).

```
cd ai_service
uv sync
```

Put your provider key in `ai_service/.env` (git-ignored — never commit it):

```
MISTRAL_API_KEY=...
# optional: MISTRAL_MODEL=mistral-large-latest
```

Without a key the service still runs; AI requests answer with a message saying how to
configure one, and the editor works normally without AI.

## Running by hand

```
uv run uvicorn app.main:app --host 127.0.0.1 --port 8765
```

The editor reuses a service already listening on port 8765, which is handy while
working on the service (`--reload`).

## How it works

1. At startup the editor sends its reflection schema (`PUT /schema`): every component,
   field, setting and command, with types, constraints and descriptions.
2. For each request (`POST /propose`) it sends the instruction plus a summary of the open
   project (entities and their values, prefabs, assets, scripts, settings).
3. The service prompts the model (`app/prompt.py`), parses the JSON answer into a
   `Proposal` (`app/models.py`) and validates it against the schema and project
   (`app/validation.py`). An invalid answer gets one repair round trip with the exact
   errors.
4. The editor validates the proposal again against its live registry, shows it in Diff
   Review, and applies only what the user accepts.

The model also sizes each request (`scope`): `small` (one proposal), `task` (a plan and
one step at a time: the editor applies each step to a working copy, plays it, and calls
`/propose` again with the results and the task's own history until the model says
`done`), or `split` (no operations, smaller requests suggested in `split`). The service
stays stateless; the editor's `ai_bridge/TaskRunner` drives tasks.

The World Bible grounds story requests: after `PUT /project` the service indexes
`world/` (`app/world.py`), and every `/propose` gets the full text of the canon most
related to the request, plus the tone guide, as a WORLD BIBLE block. Dialogue and quests
are written with the `write_dialogue` / `write_quest` operations (format in
`docs/story.md`), checked by `app/story.py` like the editor checks them.

Sprites (`POST /assets/generate`, `app/images.py` + `app/sprites.py`): a description
becomes a FLUX image through Mistral's Agents API image tool, then a cut-out, cropped,
scaled sprite in the project's `assets/textures/generated/`, with a `.gen.json` sidecar
(description, prompt sent, model, variant, processing). The service creates one agent on
the Mistral account, named `aige-sprite-generator`, and reuses it.

Writing that makes claims about the world (canon entries, dialogue, quests) can be
checked with `POST /consistency` (`app/consistency.py`). A second model
(`MISTRAL_CHECK_MODEL`, default `mistral-medium-latest`) compares it with the canon
retrieved for it, and each issue must quote the canon it contradicts. Issues are
verified against the entry before they're returned. Fixtures:
`tests/golden/checks/`, recorded by `python -m tests.golden.record_checks`.

Providers live in `app/providers.py` (`MistralProvider`, and `ScriptedProvider` for tests).

## Tests

```
uv run pytest
```

`tests/fixtures/engine_schema.json` is a snapshot of the engine's schema. The C++ test
`SchemaContractTest` fails if the registry changes without the snapshot being regenerated
(`AIGE_UPDATE_FIXTURES=1 aige_tests --gtest_filter=SchemaContract*`), so the service is
always tested against what the engine really exports.
