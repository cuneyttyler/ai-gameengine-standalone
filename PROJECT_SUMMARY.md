# PROJECT SUMMARY — ai-gameengine-standalone

**Repository:** `d:\Dev\ai-gameengine-standalone`  
**Remote:** `https://github.com/cuneyttyler/ai-gameengine-standalone.git` (origin/master)  
**Latest commit:** `f4c7e4b` — "Open worlds from the prompt: places, sizes and spacing in the Brief; land first, places on it"  
**Primary platform:** Windows (desktop-first); macOS/Linux webview choice still open  
**Build system:** CMake (C++20), uv (Python)

---

## 1. What this is

An **AI-native, from-scratch 2D game engine and editor**. It does not sit on top of Unity, Unreal, or Godot — the project owns its renderer, ECS, physics integration, scripting VM, asset pipeline, and editor. AI is woven into the core workflow rather than bolted on: the editor sends instructions to a local Python AI service, which proposes structured changes that the user reviews and applies.

**Rule of thumb:** If it runs every frame → C++ (or Lua called from C++). If it's "talk to a model and figure out what to generate" → Python, producing plain engine files as output.

---

## 2. Language stack, layer by layer

| Layer | Language | Purpose |
|---|---|---|
| Engine core (math, memory, jobs, platform, RHI, ECS glue) | **C++20** | Frame-critical, zero-marshaling control |
| Reflection & command layer (AI-adjustability backbone) | **C++20** (compile-time + runtime reflection) | Every component, field, setting, and command is reflectable; drives editor UI and AI schema |
| Gameplay scripting | **Lua** via **sol2** | LLM-friendly, hot-reloadable, sandboxed game logic |
| Quests / dialogue / behavior trees / item-enemy defs | **Data-driven graphs** (JSON/text) | Structured, diffable, AI-regeneratable |
| Editor viewport | **C++** native Win32 child window with Vulkan surface | Interactive framerate game scene rendering |
| All other editor UI (panels, inspector, chat, diff review) | **TypeScript / Vue 3** (Vite) in native webview (WebView2 on Windows) | Modern UI, decoupled from C++ build |
| AI Orchestration Service | **Python (FastAPI)** | Local sidecar; LLM SDKs, image generation, consistency checks; never touches the frame loop |
| Tooling / build scripts | Python or shell | Not shipped |

---

## 3. Repository layout

```
/ai-gameengine-standalone/
├── engine/              # C++ engine core, split into static libraries
├── apps/
│   ├── editor/          # Editor executable (C++ host) + Vue 3 UI
│   │   └── ui/          # Vue 3 + TypeScript editor UI (all panels)
│   └── sandbox/         # Standalone runtime test app (C++)
├── ai_service/          # Python FastAPI AI sidecar (localhost:8765)
│   ├── app/             # ~45 modules: proposals, places, sprites, story, vision, etc.
│   └── tests/           # Pytest suite (~30 test files)
├── ontology/            # TTL knowledge-graph vocabularies (GBO, GSO)
├── tests/               # GoogleTest C++ suite (~70 test files)
├── tools/               # Dev/build tools (C++ and Python)
├── docs/                # Technical, product, places, systems, testing docs
├── website/             # Vue.js marketing/documentation site
├── data/                # Data assets
├── builder_tests/       # Builder integration tests
├── dist/                # Distribution output
├── CMakeLists.txt       # Top-level CMake build
└── PROJECT_SUMMARY.md   # This file
```

---

## 4. Engine subsystems (`engine/`)

Built with CMake as modular static libraries. Vendored third-party deps (fetched via CMake `FetchContent`) include GLFW, nlohmann JSON, stb, EnTT, sol2/Lua, miniaudio, and Vulkan (VMA) — each behind an internal interface so it can be swapped.

| Subsystem | Contents |
|---|---|
| `platform/` | Windowing (Win32 native child window, Vulkan surface/swapchain for viewport) |
| `rhi/` | Rendering Hardware Interface (Vulkan + VMA memory allocator) |
| `ecs/` | Entity Component System (EnTT-based) |
| `reflection/` | Compile-time + runtime reflection metadata; generates the engine schema |
| `physics2d/` | 2D physics integration |
| `renderer2d/` | 2D sprite renderer with y-sorting, text rendering, shaders, fonts |
| `ui/` | UI system (documents, game UI, UI design) |
| `scripting/` | Lua VM embedding via sol2 (script system, components) |
| `audio/` | Audio (miniaudio) |
| `assets/` | Asset database, prefabs, scenes, texture cooking, import/export, tile layers |
| `places/` | Place/world generation (world sections, block-outs, painting, tiles) — *active focus of recent commits* |
| `gameplay/` | Gameplay systems |
| `world/` | World Bible (canon entries, story references) |
| `story/` | Story graph, story ontology, story rules (quests, dialogue, behavior) |
| `planner/` | AI planner (game plans, plan stages, planner store, docs) |
| `gamebuild/` | Game build pipeline |
| `game/` | Game session |
| `editing/` | Editing layer (undo/redo, commands) |
| `editor/` | Editor core (viewport host, IPC handlers) |
| `ai_bridge/` | Engine-side client of the AI service (AiClient, TaskRunner, Proposal, DryRun, ReviewPolicy, etc.) |
| `records/` | Record/playback system |



---

## 5. AI Orchestration Service (`ai_service/`)

A **local FastAPI service** running on `127.0.0.1:8765`, started/stopped automatically by the editor. Stateless; the editor's `ai_bridge/TaskRunner` drives multi-step tasks.

### API endpoints (`app/main.py`)
| Endpoint | Purpose |
|---|---|
| `GET /health` | Health check, provider/model status, schema loaded? |
| `PUT /schema` | Editor sends its reflection schema at startup |
| `GET /schema` | Schema the service is working against |
| `POST /propose` | `{instruction, context, history?}` → validated proposal |
| `GET /usage` | Token totals for this session (cost visibility) |
| `PUT /project` | Open project root; indexes World Bible |
| `POST /world/search` | Semantic search over World Bible entries |
| `POST /consistency` | Contradiction check against canon (cheaper second model) |
| `POST /assets/generate` | Generate sprites written to the project |
| `POST /assets/describe` | Describe images: name, kind, tags, direction, cell size |

### Key modules (~45 total)
- **`service.py` / `propose.py`** — Core proposal generation; LLM prompt → JSON `Proposal` → validation → one repair round-trip on invalid answers
- **`models.py`** — Pydantic models: `EditorContext`, `EngineSchema`, `Proposal`, `Usage`
- **`validation.py`** — Validates proposals against schema and live project
- **`prompt.py`** — System prompt construction
- **`providers.py`** — `MistralProvider`, `ClaudeProvider` (Anthropic), `OpenRouterProvider`, `ScriptedProvider` (tests)
- **`world.py`** — World Bible indexing and semantic search (embeddings)
- **`places.py`** — Place interpretation, planning, brief repair, layout review
- **`planner.py`** — Planner steps (`small`, `task`, `split` scoping)
- **`images.py`, `sprites.py`, `bfl.py`, `bfl_sprites.py`, `bfl_video.py`, `pixellab.py`, `autosprite.py`** — Image/sprite generation (FLUX via Mistral Agents, BFL, PixelLab, AutoSprite)
- **`story.py`, `storygraph.py`** — Story/dialogue/quest writing and validation
- **`consistency.py`** — Canon consistency checking
- **`vision.py`** — Image description and chunk mapping
- **`websource.py`** — Web search and fetching
- **`art_brief.py`** — Art prompt writing
- **`charsheet.py`, `doors.py`, `fx.py`, `gameui.py`, `help.py`, `inspect.py`, `interior_parts.py`, `magic.py`, `matched.py`, `physics.py`, `progress.py`, `records.py`, `segment.py`, `shipping.py`, `sketch.py`, `style.py`** — Domain-specific generators/validators

### Providers / API keys (`.env`, git-ignored)
Mistral, Anthropic (Claude), PixelLab, AutoSprite, OpenRouter, BFL. Check and vision models can use OpenRouter fallback models (e.g., `nvidia/nemotron-3-ultra-550b-a55b:free`).


---

## 6. Editor UI (`apps/editor/ui/src/`)

Vue 3 + TypeScript frontend embedded via webview. Major panels/components:

- Scene tree, inspector, viewport, project panel, asset gallery
- World panel, story panel, story graph panel
- AI panels: chat, planner, builder, methods, ontology, library, manual, speed, roadmap, signup, beta
- Component editors: prefab, sheet, script, rules, UI design, procedure
- Dialogs: new project, preferences, project settings, replace prefab, pack import, script editor, etc.
- Vitest unit tests (`buildwindow.test.ts`, `docktree.test.ts`, `keymap.test.ts`, `selection.test.ts`)

---

## 7. Ontology (`ontology/`)

TTL (RDF) knowledge-graph vocabularies:
- `gbo.ttl`, `gbo-library.ttl`, `gbo-shapes.ttl` — Game Body Ontology
- `gso.ttl`, `gso-vocab.ttl`, `gso-shapes.ttl`, `gso-align.ttl` — Game Scene Ontology

---

## 8. Testing

**C++ (GoogleTest):** ~70 test files in `tests/` covering reflection, ECS, physics, rendering, AI bridge, places, story, editing, gameplay, proposals, etc. Includes layout/plan revise tests with before/after PNGs and reports. `SchemaContractTest` fails if the engine registry changes without regenerating the Python fixture snapshot (`AIGE_UPDATE_FIXTURES=1`).

**Python (pytest):** ~30 test files in `ai_service/tests/` covering API, consistency, sprites, places, planner, vision, world, schema contract, security, etc.

**Builder tests:** `builder_tests/` integration tests.

---

## 9. Documentation (`docs/`)

- **Project:** `technical.md` (architecture), `product.md`, `development_plan.md`, `roadmap.md`, `security_review.md`, `shipping.md`, `engine-gaps.md`, `fixes.md`, `todo.md`
- **AI:** `ai/agent.md`, `ai/ai-building.md`, `ai/ai-packages.md`, `ai/ai-paths.md`, `ai/ai-planner.md`, `ai/planner-schema.md`
- **Places:** ~20 docs on place generation, layout, ontology, painted places, tiling, interiors, dungeons, etc.
- **Systems:** story, game-flow, gameplay-systems, physics, magic, records, scripting, asset management, effects-animation, difficulty, game-ui, game-types, actions
- **Testing:** Phase 1–26 test checklists, build-test, end-user-tests
- **Assets:** animations, autosprite, bfl-sprites, character-sheets
- **Manuals:** `.docx`, `.pptx`, `.pdf` user guides and builder docs


---

## 10. Recent development direction (from git log)

Recent commits focus heavily on the **places/world generation** system:
- Open worlds from prompts: places, sizes, and spacing in the Brief; land first, places on it
- Painted places: painting land (ground, roads, water, mountains, woods, fields as bare soil) at 48 px/cell; tiled repainting at full size (2864×1432), saved and resumable
- Block-outs of land alone; natural block-outs draw buildings with plaster walls and real roofing colours
- Relief zones (mountains ~24 storeys, hills ~3) with height solved once zones are laid out
- Vision model finds structures/marks in paintings (`find_structures`, `/places/find-marks`) as occluders and blocked footprints
- Docs: `painted-places.md` (2a), `tiling.md`, Phase 31.6a; knowledge base and website docs updated

---

## 11. Key technologies summary

- **Build:** CMake (C++20), uv (Python package management)
- **Graphics:** Vulkan (RHI), GLFW (windowing), stb (images/fonts)
- **ECS:** EnTT
- **Scripting:** Lua via sol2
- **Audio:** miniaudio
- **AI service:** FastAPI, Mistral AI, Anthropic Claude, OpenRouter, BFL, PixelLab, Pydantic, httpx, Pillow, numpy, torch/transformers (optional segmentation)
- **Editor UI:** Vue 3, TypeScript, Vite, WebView2
- **Tests:** GoogleTest (C++), pytest (Python), Vitest (editor UI)
- **Ontology:** TTL (RDF) vocabularies

---

## 12. Known open items / assumptions

- No top-level `README.md` at repo root; docs live in `docs/` and `ai_service/README.md`.
- `.env` contains API keys (git-ignored per its README, but present in this snapshot — treat as secrets if shared).
- Desktop-first (Windows primary); macOS/Linux webview choice (WKWebView vs. CEF vs. lighter embed) still open per `technical.md` §17.
- Mobile support is architecturally prepared but sequenced later (not v1).
- Custom text-based scene/entity format; changing it later touches every saved project (open question per `technical.md` §17).
- Isometric rendering approach (2D sprite y-sorting vs. 3D scene + orthographic camera) needs a spike before the ARPG template.

