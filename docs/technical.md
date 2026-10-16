# Technical Design — AI-Native Game Engine (From Scratch)

See `product.md` for product rationale and the relationship to the Godot-based sibling
project. This doc is the "how it's actually built" reference for the from-scratch engine.
It assumes no prior game-engine-internals background and explains *why*, not just *what*.

## 1. What "from scratch" means here, precisely

We are not reimplementing every low-level primitive on Earth. "From scratch" means: **no
host game engine** (no Unity/Unreal/Godot runtime underneath us) — we own the renderer,
ECS, physics integration, scripting VM embedding, asset pipeline, and editor. We *do*
use focused, permissively-licensed C++ libraries for well-solved sub-problems (windowing,
math, an ECS storage library, a physics library, an audio mixer) the same way any serious
custom engine does — reimplementing GLFW or a SIMD math library would be cost with no
product benefit. Each such dependency is called out below with its rationale, and each
one sits behind an internal interface so it can be swapped without touching engine-level
code.

## 2. Language stack, layer by layer

| Layer | Language(s) | Why |
|---|---|---|
| Engine core (math, memory, jobs, platform, RHI, ECS glue) | **C++20** | Full control, zero marshaling overhead, the only realistic choice for a frame-critical engine core built from scratch |
| Reflection & Command Layer (the AI-adjustability backbone, §7) | **C++20** (compile-time + runtime reflection metadata) | Must live where the data lives; a bolt-on/out-of-process reflection layer would lag every engine change |
| Gameplay scripting (what a designer writes day to day, and the primary AI-codegen target for logic) | **Lua** (via LuaJIT where available) embedded through **sol2** bindings | Simple grammar an LLM emits reliably and a human reads easily; fast iteration/hot-reload; sandboxable; the standard scripting choice for custom C++ engines |
| Quests / dialogue / behavior trees / item-enemy defs | **Data-driven graphs** (our own JSON/text graph format), not code | Same rationale as the sibling project: structured data is far easier to validate, diff, and safely AI-regenerate than free text; visual graph editors give humans the same authoring surface |
| Editor viewport | **C++**: a native Win32 child window with its own Vulkan surface/swapchain | The one surface that must render the game scene at interactive framerates stays fully native — no texture streaming into a browser, no offscreen-texture indirection |
| All other editor UI (scene tree, inspector, layout, and later chat, diff review, asset gallery) | **TypeScript / Vue 3** (Vite), embedded via a native webview (WebView2 on Windows, WKWebView/CEF elsewhere) | A real modern-UI design ceiling for the whole editor, one UI stack for manual and AI panels alike, and UI iteration decoupled from the C++ build (see §4, §9) |
| AI Orchestration Service | **Python (FastAPI)**, local sidecar | Same rationale as the sibling project: the ML ecosystem (LLM SDKs, `diffusers`, `transformers`, local inference) is Python-first; never touches the frame loop |
| Tooling / build scripts | Python or shell | Not shipped |

**Rule of thumb**, same as the sibling project: if it runs every frame in the shipped
game, it's C++ (or Lua called from C++). If it's "talk to a model and figure out what to
generate," it's Python, and it always produces plain engine files as output.

## 3. Why Lua, and why not C# / a custom DSL

| Option | Verdict |
|---|---|
| **Lua + sol2 (chosen)** | Minimal, well-understood grammar — the best target for reliable LLM code generation and for hot-reloadable, sandboxed gameplay logic. Enormous prior art in custom engines (many commercial titles). |
| C# via CoreCLR/Mono | Better raw performance and a bigger ecosystem, but embedding a full CLR is a significant engineering cost we don't need for v1-era gameplay code, and it's a heavier, noisier target for AI code generation than Lua. Revisit only if profiling shows Lua is a real bottleneck for a specific system (candidate: large-population simulation code, which likely belongs in C++/ECS systems anyway, not scripting). |
| A bespoke DSL designed purely for AI generation | Tempting (full control over what the model can express), but reinvents a scripting language, its tooling (syntax highlighting, debugger, docs), and its own LLM-familiarity from zero — Lua already has all of that and is simple enough that the "control" argument mostly evaporates. Reconsider only if the reflection/graph layer (§7-8) turns out to cover so much that free-form scripting becomes a rare escape hatch rather than a daily tool — at that point a tiny bespoke expression language might be worth it. |

## 4. Why a native viewport inside a webview editor UI

| Option | Verdict |
|---|---|
| **Native Vulkan viewport + webview (Vue) for everything else (chosen)** | The viewport is a native child window rendering straight into its own swapchain, so the game scene pays no browser or texture-copy cost. Every other panel is web UI, which gives the editor a modern design ceiling and lets manual and AI panels share one stack, one component library, and one IPC path. Proven in milestone 1.1a-c. |
| ImGui shell + webview for AI panels only (original plan, built then reverted in 1.1) | Trivial renderer integration, but it means two UI stacks with different looks and idioms, and ImGui caps how polished the manual editor can look. Rejected once the 1.1a spike showed the native-viewport-in-webview seam (z-order, resize, flicker) works cleanly. |
| Pure web UI (engine headless, viewport streamed into the browser) | Maximum UI flexibility, but the viewport then needs a texture-streaming bridge into a browser process. Revisit only if the product moves toward a browser-hosted editor (e.g., cloud editing). |
| Qt / other native desktop UI toolkit | Mature and rich, but adds a large, license-bearing dependency and doesn't solve the rich chat/markdown problem any better than a webview does. |

**How the two coexist ("hole punching"):** the WebView2 control fills the whole top-level
window. The Vue layout reserves an empty placeholder element for the viewport
(`ViewportHole.vue`) and reports its rect to the host via `ResizeObserver` +
`postMessage`; the host positions the native viewport child window over that rect, above
the webview in z-order. The Vue app's own CSS layout decides where the viewport goes —
C++ never computes the layout.

The webview and the C++ host communicate over a JSON message channel (see §9) using the
same message shape the AI Orchestration Service itself uses, so "propose a diff" looks
identical whether it's triggered from an inspector's "Ask AI" button or the chat panel.

## 5. System architecture

```
┌───────────────────────────────────────────────────────────────────────┐
│                          Engine + Editor Process (C++)                 │
│                                                                          │
│  Engine Core: platform/windowing, job system, math, memory              │
│  RHI (Vulkan) → 2D/2.5D renderer → (later) 3D renderer                  │
│  ECS (EnTT-backed) + Reflection & Command Registry  ◄─────────────┐    │
│  Physics (2D custom / Jolt for 3D later), Audio (miniaudio)        │    │
│  Lua VM (sol2) — gameplay scripts, hot reload                      │    │
│  Behavior-graph runtime — quests/dialogue/AI behavior              │    │
│                                                                     │    │
│  Editor viewport: native child window, own Vulkan swapchain        │    │
│                                                                     │    │
│  Embedded webview (Vue/TS): scene tree, inspector, generated       │    │
│  Reflection Inspector, script editor, chat/Composer, AI Diff       │    │
│  Review, Asset Generator gallery, Quest/Dialogue graph editor       │    │
│         │ local IPC (shared message schema)                        │    │
└─────────┼────────────────────────────────────────────────────────┘    │
          │ localhost HTTP/JSON (async jobs)                             │
          ▼                                                              │
┌───────────────────────────────────────────────────────────────────┐   │
│           AI Orchestration Service (Python, FastAPI sidecar)        │   │
│  - reflection-schema client (queries the registry above) ──────────┘   │
│  - prompt templates / tool schemas per module                          │
│  - job queue (generation requests)                                     │
│  - world-bible store + embeddings (RAG)                                │
│  - contradiction/consistency checks                                    │
│  - provider adapters (cloud + local), cost/usage tracking              │
└───────┬───────────────────────────────────┬───────────────────────────┘
        │ cloud APIs                        │ optional, local machine
        ▼                                   ▼
Claude (text/logic/dialogue)        Local models (llama.cpp,
Diffusion API (art/sprites)         local diffusion, etc.)

Accepted output is always written back as plain project files: our text-based
scene/entity format, .lua scripts, our behavior-graph JSON, .png/.glb assets — a
project with all AI features disabled is a normal, valid project for this engine.
```

## 6. Module / plugin architecture

Modularity is a first-class requirement (product ambition to reach Unity/Unreal scale),
so the engine is split into modules from day one, not refactored into them later:

| Module | Responsibility |
|---|---|
| `core` | Platform abstraction, math, containers, job system, logging, serialization |
| `rhi` | Render Hardware Interface — thin abstraction over Vulkan (see §8) |
| `renderer2d` / `renderer3d` | Sprite/tilemap/y-sort rendering now; forward/deferred 3D later, both consuming `rhi`. `renderer2d/Text` bakes fonts (stb_truetype) and lays out text; the built-in font (Atkinson Hyperlegible, SIL OFL 1.1, `renderer2d/fonts/`) is compiled into the binary |
| `ui` | The game's own UI (dialogue box, banners, objective, prompts) as screen-space quads: what a shipped game shows, as opposed to the editor's web UI |
| `ecs` | EnTT-backed entity/component storage, wrapped by our own component registration macros |
| `reflection` | The Reflection & Command Registry (§7) — the AI-adjustability backbone. Engine types are registered in `gameplay/EngineReflection.cpp`, editor commands in `editing/Commands.cpp` |
| `physics2d` / `physics3d` | Custom lightweight 2D physics now; Jolt-backed 3D physics later |
| `audio` | miniaudio-backed mixer/playback |
| `scripting` | Lua VM embedding, hot reload, sandboxing |
| `behavior_graph` | Runtime for the data-driven quest/dialogue/behavior-tree format |
| `assets` | Import/cook pipeline, on-disk formats, hot reload |
| `world` | The World Bible: canon entries as `world/<kind>/<id>.md` files — parse, write, validate, rename |
| `editor` | Native viewport window + webview host, all editor-only code (stripped from shipped games) |
| `ai_bridge` | The engine-side client of the AI Orchestration Service: job submission, diff application, IPC with the webview |
| `platform_*` | Per-platform export/packaging glue (desktop first, mobile later) |

Each module builds as its own static library with an explicit public header surface;
`editor` and `ai_bridge` are excluded entirely from exported game builds. This mirrors
how Unreal's module system and Unity's package system are structured, which is
deliberate — it's the proven shape for an engine meant to scale to that size.

## 7. The Reflection & Command Registry — the AI-adjustability backbone

This is the mechanism behind `product.md` §3's central claim ("every module and every
feature must be adjustable using AI"). Without it, "AI-adjustable" would mean
re-implementing a bespoke NL integration per feature forever, which doesn't scale to an
engine with Unity/Unreal-level surface area.

**Design:**
1. **Registration**: every module registers its editable state through a common
   template/macro API (conceptually similar to Unreal's `UPROPERTY`/reflection macros,
   but scoped to our own needs) — type, human-readable name and description, valid
   range/enum/constraint, and a getter/setter or component-field binding. Actions (not
   just data) are registered too — e.g., "regenerate this dungeon section," "bake
   lighting," "export build" — as callable commands with typed parameters.
2. **Live registry**: at runtime/edit-time, this produces a queryable in-process
   registry: "what modules exist," "what does module X expose," "what's the current
   value of field Y."
3. **Schema export**: the `ai_bridge` module serializes the *currently relevant subset*
   of this registry (not the whole engine on every request) to the AI Orchestration
   Service as a tool/function-calling schema, scoped by context (active panel, selection,
   project type) to keep prompts small and relevant.
4. **NL → structured proposal**: the AI service resolves a natural-language instruction
   against that schema and returns a structured diff (field changes, new components,
   new graph nodes, generated asset references) — never arbitrary free-text engine
   internals, since it only knows what the registry exposed.
5. **Validation**: `ai_bridge` validates the proposal against the live registry again
   before showing it to the user (types match, referenced entities/fields still exist,
   no edits to unrelated files) — defense in depth against a stale schema or a
   hallucinated reference.
6. **Universal Diff Review**: every proposal, regardless of originating module, renders
   in the same review surface (webview panel) — accept as-is, edit inline, or reject.
7. **Apply**: on acceptance, `ai_bridge` writes the change through the *same* registry
   setters/commands the manual editor uses — so an AI-applied change and a
   human-applied change are indistinguishable to every other system in the engine.

This registry is also what makes the **Reflection Inspector** (`product.md` §7) possible:
its panels are *generated* from the registry rather than hand-built per module, and every
generated field automatically gets the "Ask AI" affordance for free.

## 8. Rendering architecture

| Decision | Choice | Rationale |
|---|---|---|
| Graphics API | **Vulkan**, via a thin internal RHI we own | One modern, explicit API to target first; the RHI abstraction (not Vulkan itself) is what gives us a path to Metal (iOS/macOS) and mobile Vulkan/GLES later without touching renderer-level code |
| Vulkan support libraries | `volk` (loader), **VMA** (Vulkan Memory Allocator) | Solved problems (function loading, GPU memory allocation) with no product value in reimplementing |
| v1 rendering mode | **2D/2.5D isometric**: sprite batching + tile rendering + y-sorting | Matches the v1 genres exactly; avoids building a full 3D pipeline (lighting, shadow mapping, PBR materials) before it's needed |
| Path to 3D | The RHI and asset pipeline are API-shaped for 3D from the start (buffers/textures/pipelines are generic, not 2D-specific), so a 3D renderer is an *addition* (new pipeline, new shaders, mesh import) later, not a rewrite of the RHI | Directly supports the "become a general-purpose engine" long-term goal without a foundational redo |
| Isometric implementation detail | 2D engine with sprite y-sorting first (simpler, matches genre exactly, cheapest to build); revisit "3D scene + orthographic camera" only if depth/lighting needs (e.g., dynamic shadows in dungeons) outgrow what y-sorted 2D can fake | Explicit open question, see §14 — needs a short spike before the ARPG template is built |
| Shaders | Author in **GLSL**, compile to **SPIR-V** at build/cook time | Standard Vulkan shader pipeline; SPIR-V cross-compilation (via `SPIRV-Cross`) is the standard path to Metal/HLSL later if needed |

## 9. Editor architecture detail

- The C++ host, the WebView2 control and the native viewport live in the same process.
  `engine/editor/WebViewHost` wraps WebView2 setup and the JSON bridge;
  `engine/editor/ViewportWindow` is the Win32 child window the Vulkan RHI renders into
  (via `VulkanRHI`'s raw-HWND constructor).
- The UI (`apps/editor/ui`) is served through WebView2's
  `SetVirtualHostNameToFolderMapping`, avoiding `file://` ES-module/CORS restrictions.
  Messages are JSON strings in both directions. The C++ side (`apps/editor/EditorApp`)
  owns all editor state; the UI renders what it's sent and sends requests:
  - UI → host: `scene:request`, `entity:select`, `inspector:setField`,
    `component:add|remove`, `script:create|open`, `tool:set`,
    `prefabs:request`, `prefab:createFromAsset|saveSelected|applySelected|revert`,
    `script:setProperty|resetProperty`,
    `edit:undo|redo|delete|duplicate`, `scene:save`, `view:frame`, `play:start|stop`,
    `viewport:bounds`, `assets:request|refresh`, `asset:import|settings|apply|preview`.
  - Host → UI: `scene:update`, `selection:changed`, `inspector:data|clear`,
    `editor:state` (tool, brush, playing, dirty, canUndo/canRedo, scene name),
    `assets:list`, `assets:select`, `prefabs:list`, `notify`.
  - AI: UI → host `ai:send {text, focus}`, `ai:apply {id, items}`, `ai:reject {id}`,
    `ai:setEnabled {enabled}`, `ai:setReviewPolicy {policy}`, `ai:undoAuto {id}`,
    `ai:taskStop {id}`; host → UI `ai:status {state, text, enabled, choice, reviewPolicy}`,
    `ai:busy`, `ai:autoUndone`,
    `ai:message`, `ai:proposal` (with `task` for a task's result), `ai:decided`, `ai:scriptCheck`,
    `ai:taskProgress`, `ai:taskStopped`, `ai:split`.
  - World Bible: UI → host `world:request`, `world:save {entry, originalKey}` (a changed kind/id
    is a rename that rewrites relations), `world:delete {key}`, `world:search {query}`;
    host → UI `world:list {kinds, entries, problems}`, `world:saved {key, error}`,
    `world:searchResults {result}`.
  - Consistency (3.3): host → UI `ai:consistency {id, result}` (first `{checking: true}`,
    then the service's `/consistency` answer); World tab UI → host `world:check {key}`,
    host → UI `world:checkResult {key, result}`.
  - Level (3.4): UI → host `level:defaults`, `level:preview {args}`, `level:generate {args}`;
    host → UI `level:defaults {x0, y0, tiles, things}`, `level:preview {text, error}`.
  - Art (3.6): UI → host `art:request`, `art:generate {request}` (forwarded to the service's
    `/assets/generate`), `art:discard {path}`; host → UI `art:list {items, busy}`,
    `art:result {result}`. `assets:list` entries carry `generation` when a `.gen.json` exists.
  - Shell (4.1): UI → host `viewport:occlude {rects}`, `app:quit`, `help:docs`,
    `scene:placeAt {prefab, gx, gy}`, `scene:addMarker {name, gx, gy}`, `asset:reveal {path}`,
    `asset:delete {path}`; host → UI `viewport:contextMenu {x, y, entity, cell, brush}`,
    `ui:shortcut {name}`. Open menus and dialogs are cut out of the native viewport
    window (`editor/Occlusion`), so HTML can be drawn over it.
  - Scene editing (4.3): UI → host `edit:scale {factor}`, `edit:nudge {dx, dy}`, `edit:copy`,
    `edit:paste {gx?, gy?}`, `view:grid {on?}`, `play:toggle`, `prefab:remove {path}`;
    `tool:set` takes `scale` (the brush size). `editor:state` carries `grid` and `brushScale`.
  - Story: UI → host `story:request`, `story:save {kind, id, originalId, content}`,
    `story:delete {file}`; host → UI `story:list {dialogues, quests, schedules, markers,
    problems, world, prefabs, usedBy}`, `story:saved {file, error}`. `story:save` kind
    `schedule` writes `schedules/<id>.json` (3.5).
- The open project folder is served to the UI under a second virtual host
  (`aige-project.local`), so the Assets panel shows thumbnails straight from disk.
- Viewport tools call the UI-independent `engine/editing` API (picking, painting,
  dragging, undo), which is where the GoogleTest coverage lives. Undo is whole-scene
  snapshots for now; the Phase 2 command layer replaces it.
- Play mode runs the same `engine/gameplay` `GameSession` the sandbox uses. The scene is
  serialized to an in-memory snapshot on Play and restored on Stop. Keyboard input is
  read from the viewport window only, so typing in the web UI never drives gameplay.
- Communication between C++ host ⇄ webview ⇄ `ai_bridge` ⇄ AI service uses one
  consistent JSON message schema end-to-end, so a "propose change" message looks the
  same whether it originated from a native inspector's "Ask AI" button or the webview
  chat panel — there is exactly one code path for "AI proposes, user reviews, registry
  applies," not two.
- The webview's local dev loop (Vue/TS via Vite) is decoupled from the C++ build, so UI
  iteration doesn't require recompiling the engine. The C++ build still runs
  `npm install && npm run build` as a custom command whenever `ui/src` or `package.json`
  changes, so a plain CMake build always produces a working editor.

## 10. AI Orchestration Service

Architecturally identical in spirit to the sibling project's design
(`docs/godot/technical.md` §4, §5, §7, §9): Python/FastAPI local sidecar, provider
abstraction (cloud-first, local-optional via `llama.cpp`/`diffusers`), World Bible in
SQLite with embeddings for RAG, contradiction-checking as a second cheap-model pass, cost
metering surfaced in the editor. The one structural difference: the schema it consumes
comes from **this engine's Reflection & Command Registry** (§7) instead of a
hand-curated ECS component list, since "every module" needs to be exposed generically,
not enumerated per feature.

## 11. Hand edits and AI edits, technically

There are no editor modes (`product.md` §4). Hand edits and AI edits read and write the
exact same project files through the exact same registry and asset pipeline — there is no
separate serialization path, conversion step, or "AI project" flag on disk. This is
enforced by construction: Inspector edits and applied AI proposals both go through
`engine/editing/Edits` (validate → write → record prefab override), never through a side
channel, and `EditParityTest` checks that the same edits made each way save
byte-identical files.

UI state: the AI panel collapses to a rail (`editorState.aiOpen`, localStorage); ✦ Ask AI
expands it. Whether AI is on at all is a per-user preference (`ai_bridge/UserPrefs`,
`%LOCALAPPDATA%\aige\editor.json` → `aiEnabled`). It's opt-in: absent means undecided,
and the AI panel asks on first run. It's toggled with `ai:setEnabled` and reported in
`ai:status {enabled, choice}`. `ai_bridge/AiConnection` launches the sidecar only while
the choice is On and stops it on Off, so until the user opts in no project data leaves
the machine.

## 12. Physics & audio

| System | Choice | Rationale |
|---|---|---|
| 2D physics (v1) | Small custom AABB/circle/tilemap collision + resolution system | Isometric ARPG/cozy-sim genres need simple, predictable 2D physics; a full physics engine (Box2D-level generality) is more than v1 needs, and owning it keeps the ECS integration simple |
| 3D physics (later) | **Jolt Physics** (MIT-licensed, modern, production-proven) | When full 3D lands, adopting a proven rigid-body library is the right call — physics correctness is a deep, well-solved problem with no product differentiation in reimplementing it |
| Audio | **miniaudio** (single-header, permissive license) | Solved problem (mixing, format decoding, spatialization basics); revisit only if console-class audio middleware (Wwise/FMOD) becomes a requirement |

## 13. Data & storage

| Data | Format / store |
|---|---|
| Scenes / entities | Our own text-based, git-diffable scene format (line-oriented, one entity/component per readable block — deliberately Godot-`.tscn`-like, since that format's diffability is exactly what AI Diff Review needs) |
| Components / resources | Same text format, typed per the reflection registry |
| Gameplay scripts | `.lua` |
| Quests / dialogue / behavior trees | Our own JSON-based graph format |
| Generated images/meshes | `.png`/`.glb` + sidecar metadata (prompt, seed, model) for reproducibility |
| World Bible / lore graph | One Markdown file per entry, `world/<kind>/<id>.md` (a `key: value` header with name, summary, tags and `rel:` lines, then free text; `engine/world`). The files are the canon: human-editable, diffable, usable with AI off |
| Generation job history, cost tracking | SQLite (same local DB) |
| Vector search (RAG) | `.cache/world_index.sqlite` in the project, owned by the AI service: one embedding per World Bible entry (`mistral-embed`), re-embedded only when the entry changes, cosine similarity in-process (hundreds of entries, no vector extension needed), keyword ranking when no embedder is available. Rebuildable, git-ignored |
| Version control | Git; Git LFS for binaries; text formats chosen specifically to diff well |

## 14. Testing strategy

| Layer | Approach |
|---|---|
| C++ engine core, RHI, ECS glue, reflection registry | **GoogleTest**, run headless outside the editor; reflection registry gets contract tests (register → query → validate round-trip) since it's the AI-adjustability backbone |
| Lua gameplay scripts | A small embedded Lua test harness (busted-style) run via the engine's headless test mode |
| Behavior-graph runtime | Fixture-based: known graph + known world state → expected traversal/output |
| Renderer | Golden-image tests where feasible (render a known scene headlessly, compare against a reference image with tolerance); manual visual confirmation for anything golden-image can't easily cover (see below) |
| Python AI service | `pytest`; mocked provider responses for determinism |
| NL → structured-diff correctness | Golden-file tests: fixed prompts → expected diffs, validated for schema correctness (not exact text match, since LLM output varies) |
| Lore consistency checks | Fixture-based: seeded World Bible + known-contradictory prompt → expect a flag |

**A hard limit on automated testing:** this environment cannot open a window, watch
frames render, or judge whether something "looks right" (a triangle appears, a sprite is
positioned correctly, the editor UI is usable, an animation reads well). For anything
that requires eyes on a running window, the plan is to say so explicitly and hand it to
you to confirm — `development_plan.md` marks these checkpoints per milestone rather than
silently claiming visual success from a passing headless test.

## 15. Build system & platform strategy

- **CMake** for the C++ engine/editor, cross-platform from the start.
- Desktop (Windows/macOS/Linux) is the v1 export target.
- Mobile (iOS/Android) is not deferred *architecturally* — the RHI, input abstraction,
  and asset pipeline are designed platform-agnostic from day one — but it's sequenced
  later (see `development_plan.md`) since mobile-specific concerns (touch input,
  power/thermal budgets, app-store packaging, mobile GPU driver quirks) are real
  engineering work that shouldn't block the desktop-first MVP.
- GDExtension-equivalent native plugin loading (for third-party engine modules, once the
  module system in §6 is proven) is a Phase 4+ concern, not v1.

## 16. Suggested repository layout

```
ai-gameengine-standalone/
├── docs/                       product.md, technical.md, development_plan.md
│   └── godot/                  reference docs for the sibling Godot-based project
├── engine/                     C++ engine core
│   ├── core/
│   ├── rhi/
│   ├── renderer2d/
│   ├── ecs/
│   ├── reflection/
│   ├── physics2d/
│   ├── audio/
│   ├── scripting/              Lua/sol2 embedding
│   ├── behavior_graph/
│   └── assets/
│   ├── editor/                 ViewportWindow, WebViewHost (native side of the editor)
│   └── ai_bridge/              engine-side client of the AI service
├── apps/
│   ├── editor/                 editor executable (C++ host, IPC handlers)
│   │   └── ui/                 Vue 3 + TS editor UI (all panels, manual and AI)
│   └── sandbox/                standalone runtime test app
├── ai_service/                 Python FastAPI orchestration service
│   ├── app/
│   │   ├── providers/
│   │   ├── lore/
│   │   ├── compiler/           NL -> reflection-schema-grounded proposal generation
│   │   └── jobs/
│   └── tests/
├── projects/                   game projects: scenes/, scripts/, assets/ (+ .meta), .cache/
│   │   (+ prefabs/: placeable entity templates — the editor's palette)
│   └── demo/                   the default project the editor and sandbox open
├── tools/                      dev tools: demo-art generator (Node), make_demo_level (C++)
├── templates/                  starter projects (Isometric ARPG, Cozy Sim)
├── tests/                      GoogleTest suites for engine/, top-level test runner
└── tools/                      build/dev scripts
```

## 17. Open technical questions to resolve during Phase 0

- Isometric rendering approach: 2D sprite y-sorting vs. 3D scene + orthographic camera —
  needs a short spike (§8) before the ARPG template is built.
- ECS storage library: EnTT vs. flecs vs. a minimal bespoke sparse-set implementation —
  needs a short spike against how the reflection registry wants to introspect component
  storage.
- Exact shape of the text-based scene/entity file format (custom vs. adapting an existing
  human-readable serialization convention) — needs to be settled before Phase 1, since
  changing it later touches every saved project.
- How the AI Orchestration Service ships alongside the editor for non-technical users
  (bundled Python runtime vs. one-time local install) — same open question as the
  sibling project, doesn't need resolving until closer to Phase 2.
- Webview technology per platform (WebView2 on Windows is in place as of 1.1; the
  macOS/Linux equivalents need a concrete choice — WKWebView vs. CEF vs. a lighter
  embed). Since the whole editor UI now depends on it, this needs resolving before the
  editor ships on a second platform, along with the native-child-window viewport on
  that platform.
