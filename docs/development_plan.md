# Development Plan — AI-Native Game Engine (From Scratch)

This is the engineering breakdown behind `product.md` §10's phase table, using the
architecture decisions from `technical.md`. It's a working plan, not a contract — expect
milestones to shift as spikes (§Open Questions in `technical.md`) resolve.

## Operating principles for this plan

1. **Nothing AI-native before something real.** Phases 0-1 build a working, manual,
   non-AI engine. This is deliberate: it proves the from-scratch foundation, gives us
   something to point the AI layer *at* in Phase 2, and means the project is never
   dependent on the AI layer working to have something to show.
2. **Every module ships with tests at the layer they can be tested at** (`technical.md`
   §14). Where a milestone's real success criterion is visual/experiential ("does this
   look right," "is this editor usable") and can't be judged headlessly, the milestone
   says so explicitly and is marked as a **user test checkpoint** — I'll flag it and you
   confirm on your machine, rather than me claiming success from a passing unit test.
3. **Small, working, playable increments over big-bang integration.** Each phase ends
   with something concretely runnable, not a pile of half-wired subsystems.
4. **Defer, don't design away.** Mobile and full 3D are sequenced late but the
   architecture (`technical.md` §8, §15) is chosen so reaching them later doesn't require
   redoing earlier work.

---

## Phase 0 — Engine Bootstrap

**Goal:** prove the from-scratch foundation. No AI, no editor polish — a window, a
renderer, an ECS, a game loop, entities that move and collide, a project that saves and
loads. This is the phase where "from scratch" either proves itself tractable or reveals
a wrong call early, while the cost of being wrong is still cheap.

### Milestones

**0.1 — Build & platform skeleton**
- CMake project builds on Windows (primary dev platform) with a clean dependency setup
  (`volk`, VMA, GLFW-equivalent windowing, EnTT, sol2/Lua as vendored/fetched deps; the
  editor later adds the WebView2 SDK, WIL and nlohmann/json in 1.1).
- Deliverable: a window opens and closes cleanly, no rendering yet.
- Tests: CI-style headless build check (compiles, links, exits 0). **User test
  checkpoint:** confirm the window actually opens and behaves (resizes, closes) on your
  machine — this is the first thing I categorically cannot verify myself.

**0.2 — RHI + first triangle**
- Thin Vulkan RHI (`technical.md` §8): device/swapchain setup, a single graphics
  pipeline, a hardcoded triangle or sprite quad on screen.
- Tests: headless smoke test (RHI initializes without validation-layer errors, given a
  Vulkan-capable environment). **User test checkpoint:** visually confirm the triangle
  renders correctly.

**0.3 — 2D sprite/tile renderer + ECS integration**
- Sprite batching, tilemap rendering, y-sort ordering; EnTT-backed ECS wired to the
  renderer (a "sprite component" on an entity gets drawn).
- Tests: golden-image test (render a small fixed scene headlessly, compare to a
  reference image within tolerance). **User test checkpoint:** confirm sprites/tiles
  look right and y-sorting behaves correctly for an isometric layout.

**0.4 — Game loop, input, basic movement/collision**
- Fixed-timestep update loop, keyboard/mouse input abstraction, the custom 2D
  AABB/tilemap collision system (`technical.md` §12) moving a controllable entity around
  a test map.
- Tests: GoogleTest on collision resolution math (deterministic, no rendering needed).
  **User test checkpoint:** does movement/collision *feel* right (no tunneling, no
  jitter) — inherently a hands-on-controls judgment.

**0.5 — Lua scripting hookup**
- sol2-bound Lua VM; a component's behavior can be scripted in `.lua`; hot reload on
  file save.
- Tests: embedded Lua test harness (a script runs, produces expected entity-state
  changes). **User test checkpoint:** hot reload actually reloads without a restart.

**0.6 — Save/load a project**
- The text-based scene/entity format (`technical.md` §13, open question in §17 to
  resolve here) — serialize/deserialize a scene with entities, components, and a script
  reference.
- Tests: round-trip test (save → load → identical entity/component state). Git-diff
  sanity check that the format is actually readable/diffable as intended.

**Phase 0 exit criteria:** a developer (you) can open the engine, see a moving,
Lua-scripted, collidable sprite on a tilemap, save the project, close it, reopen it, and
get the same scene back. No editor UI yet beyond whatever's needed to trigger this.

---

## Phase 1 — Manual Isometric Editor (AI-Assisted mode, standalone)

**Goal:** a genuinely usable manual editor and runtime for isometric 2.5D games — the
"AI-Assisted mode must stand on its own" requirement from `product.md` §10, built and
proven *before* any AI integration exists to lean on.

### Milestones

**1.1 — Editor shell (native viewport + WebView2/Vue UI)**
- First built in Dear ImGui, then replaced (see `technical.md` §4): the viewport is a
  native child window with its own Vulkan swapchain, and every other panel is Vue 3 UI
  in a WebView2 control filling the window.
- 1.1a ✅ Spike: WebView2 and the native Vulkan viewport coexist (z-order, resize, no
  flicker).
- 1.1b ✅ Vue/Vite frontend, built from CMake; viewport positioned from the Vue layout's
  placeholder rect over IPC.
- 1.1c ✅ Scene tree and inspector (hand-wired for now — the generated Reflection
  Inspector comes in Phase 2) working over the JSON bridge; inspector edits move the
  sprite live.
- 1.1d Play mode: Play/Stop toolbar runs the shared fixed-step simulation
  (`engine/gameplay`, also used by the sandbox) in the editor; the scene is snapshotted on
  Play and restored on Stop; the viewport takes keyboard focus on click.
- Remaining: resizable/rearrangeable panel layout.
- **User test checkpoint:** the editor shell is usable — panels resize sensibly, the
  viewport doesn't fight with input focus.

**1.2 — Scene editing: place, select, transform**
- Drag-and-drop/click-to-place entities and tiles in the viewport; select/move/delete;
  inspector edits reflected live in the viewport.
- Tests: GoogleTest on the underlying scene-mutation API (independent of UI). **User
  test checkpoint:** the actual editing feel — selection precision, gizmo behavior.
- ✅ Built (user test deferred to end of Phase 1): `engine/editing` (pick, paint/place,
  drag, duplicate, delete, snapshot undo/redo), tool palette (select, grass/dirt/wall
  brushes, NPC/player placement, erase), camera pan/zoom/frame, selection outlines,
  Ctrl+S save with unsaved-changes prompt. Tiles became a `Tile` grid component and
  collision now comes from the scene's tiles (empty cells are solid), so editor-built
  levels are walkable. No transform gizmo: tiles snap by cell and characters drag
  directly, which covers the 2D isometric case.

**1.3 — Asset import pipeline (manual)**
- Import sprites/tilesets/audio from disk into the project's asset format; basic asset
  browser panel.
- Tests: import round-trip tests (known source file → expected cooked asset).
- ✅ Built (user test deferred to end of Phase 1): projects as folders
  (`projects/demo`: `scenes/`, `scripts/`, `assets/`, git-ignored `.cache/`) with every
  stored path project-relative; `AssetDatabase` (import with collision-safe naming,
  `.meta` sidecars for import settings, texture cooking to raw RGBA8 with staleness
  checks, audio validation via miniaudio); multi-texture `Renderer2D` with lazy loading
  and invalidation; Assets panel (thumbnails, import dialog, texture/tileset settings,
  tile picker, tile brush, prop placement, apply-to-selected, audio preview); assets
  rescan when the editor regains focus, so external edits hot-reload. The sandbox is
  now a project player (`sandbox [--project] <dir>`).

**1.4 — Component authoring UI**
- Add/remove components on entities through the inspector; component data editing
  (numbers, strings, enums, references) without yet being registry-generated.
- Tests: GoogleTest on component add/remove/edit at the ECS level.
- ✅ Built (user test deferred to end of Phase 1): `engine/assets/ComponentTable` — one
  hand-written table of components, typed fields (float, int, bool, string, enum, entity
  reference, texture, script) and dependencies that now drives scene files, the
  Inspector, and add/remove. Entity references are saved by entity index and remapped
  on load. New components: `Name`, `Faction`, `Health`, and `Follow` (with a chase
  system in the shared game loop). Inspector renders typed editors, add/remove
  component, and "New script…" / "Open" for Lua behaviours. This table is the seed of
  Phase 2.1's reflection registry.

**1.5 — Physics & combat/movement primitives for the ARPG genre**
- Basic combat-relevant primitives: hitboxes/hurtboxes, simple stat components, a
  status-effect ECS pattern (per `technical.md`'s ECS-for-data-heavy-systems approach) —
  enough to build a minimal Diablo-like attack in scripting.
- Tests: GoogleTest on hit detection and status-effect application/expiry logic.
- ✅ Built (user test deferred to end of Phase 1): `engine/gameplay/Combat` — authorable
  `Stats`, `Hurtbox` (with invulnerability frames), `MeleeAttack` (cooldown, knockback,
  on-hit status effect) and `Pickup`; runtime-only `Hitbox` entities, status effects as
  their own entities (burn, poison, slow, regen; refresh rather than stack), hit flash,
  death (enemies removed, player kept). Player: Space attacks the facing direction,
  clicking in the viewport attacks toward the cursor. Enemy and Health Potion
  archetypes with editor tools; enemies chase via `Follow` and swing via
  `scripts/enemy_melee.lua`. Lua API documented in `docs/scripting.md`. Health bars over
  damaged characters. Also fixed: player speed was √2 too fast on screen-cardinal input;
  `Stats.moveSpeed` is now true cells/s.

**1.6 — Minimal playable vertical slice**
- One small hand-built isometric level: a controllable character, one enemy type, one
  attack, one pickup — built entirely through the Phase 1 manual tools, no AI involved.
- **User test checkpoint:** play it. This is the milestone that proves Phase 0-1 add up
  to an actual (tiny) game, and the single most important manual confirmation in the
  whole plan so far.
- ✅ Built (user test pending — see `docs/phase1-test-checklist.md`): `projects/demo` is now
  a small walled-courtyard level — hero, four skeletons (chase + melee via
  `enemy_melee.lua`), three health potions, trees/rocks/crates that block movement,
  a villager. Placeholder pixel art and sound effects are generated by
  `tools/demo-art/generate.mjs` and brought in through the asset pipeline; the scene is
  built from an ASCII layout by `tools/make_demo_level` using the same editing API as
  the editor's tools, so it's an ordinary, hand-editable scene. Also: camera follows the
  player in Play mode and the sandbox, sprites face their direction of travel,
  `Obstacle` and `Sounds` components, a Victory event, and an automated test that wins
  the demo level through the real scripts and game loop.
- Deviation from "built entirely through the Phase 1 manual tools": the level was
  assembled by a script calling the tools' underlying API rather than by clicking, so
  the manual check should include editing it by hand in the editor.

**1.7 — Prefabs and script properties (added after the Phase 1 review)**
- Prompted by the end-of-phase review: the toolbar's placement tools were hardcoded to the
  built-in placeholder look and not connected to project assets, and customizing an actor
  meant either C++ or per-entity hand edits.
- ✅ Built: prefabs (`<project>/prefabs/*.prefab`, one entity in scene format) with
  per-copy overrides, revert, and "Apply to prefab"; scenes resync with their prefabs on
  open (editor and sandbox). The toolbar is now Select / prefab brush / Erase — the palette
  is the project's prefabs (Prefabs tab, keys 1-9), and assets become placeable via "New
  tile prefab" / "New prop prefab". Script properties: a script's `properties = {...}` table
  becomes per-entity Inspector fields, saved with scenes and prefabs. Lua `get_field` /
  `set_field` reach any component field, `spawn(prefab, x, y)` places prefabs. A `Follow`
  with no target follows the player. The component table moved to `engine/gameplay` so
  scripts can use it.
- Deliberately not built: nested prefabs and prefab variants — revisit with Phase 2's
  reflection registry.

**Phase 1 exit criteria:** you can build a small isometric scene, script basic behavior,
place enemies/items, and play it — entirely by hand, with zero AI involvement. This is
the baseline `product.md` §4.2's "must be fully usable with AI disabled" guarantee has
to hold against, and it's proven before AI exists to make it moot.

---

## Phase 2 — Reflection Layer & AI-Driven Mode v1

**Goal:** build the Reflection & Command Registry (`technical.md` §7) and stand up the
first end-to-end AI-Driven flow: a natural-language instruction becomes a reviewed,
applied change.

### Milestones

**2.1 — Reflection registration API**
- The C++ macro/template registration system; retrofit Phase 1's components and a few
  engine settings (renderer, physics) onto it.
- Tests: GoogleTest contract tests — register a type, query it back, verify
  constraints/validation round-trip correctly.
- ✅ Built: `engine/reflection` — `ReflectionRegistry` with builder-style registration
  (`registry.Component<Health>("Health", "...").Field("max", &Health::max, "...").Min(1)`),
  field types inferred from C++ members, descriptions on everything, min/max/options
  constraints with readable validation errors, project settings sections bound to a live
  instance (Renderer, Camera, Combat, Physics — saved in `<project>/settings.aige` and
  wired into the systems they describe), typed commands with argument validation
  (`scene.spawn`, `scene.paintRect`, `entity.delete`, `prefab.createFromEntity`,
  `script.write`), and `ExportSchema()` for the AI service. Phase 1's component table was
  replaced by `gameplay/EngineReflection` registrations; scene files, prefabs, the
  Inspector and Lua all go through the registry. Registration is explicit (a
  `Register...()` function per module) rather than static-initialization macros, so
  tests can build fresh registries.

**2.2 — Reflection Inspector (generated UI)**
- Replace Phase 1's hand-wired inspector panels with panels generated from the registry.
- **User test checkpoint:** confirm the generated inspector is at least as usable as the
  hand-wired one it replaces — a real regression risk worth a manual look.
- ✅ Built (user check deferred to end of Phase 2): every Inspector section and field is
  generated from registry metadata — descriptions as tooltips (fields, components, the
  Add Component list), min/max on inputs, sliders for 0-1 values, enums by name, and every
  edit validated against the registry with the reason shown inline. With nothing selected
  the Inspector shows Project Settings, generated the same way and saved with the scene.
  Field editing is one reusable `FieldEditor` component for components and settings.

**2.3 — AI Orchestration Service skeleton**
- FastAPI sidecar; a schema-export endpoint that reads the reflection registry via
  `ai_bridge`; a single provider adapter (cloud LLM) wired end-to-end.
- Tests: `pytest` with a mocked provider; contract test that schema export matches what
  2.1's registry actually exposes.

**2.4 — AI chat + Diff Review panels**
- The webview host and JSON bridge already exist from 1.1; this milestone adds the Vue
  chat and diff review panels and extends the message schema for AI proposals
  (`technical.md` §9).
- **User test checkpoint:** a proposal round-trips from chat to diff review to the
  applied change in the viewport — the seam between the UI, `ai_bridge` and the AI
  service is where this breaks, not inside any one of them.

**2.5 — First end-to-end NL → structured diff → apply**
- A real instruction (e.g., "add a burning status effect that deals damage over time")
  produces a schema-validated proposal, shown in Diff Review, and on acceptance is
  applied through the same registry setters the manual inspector uses.
- Tests: golden-file tests (fixed prompts → expected diff shape, schema-valid).
  **User test checkpoint:** does the applied change actually behave correctly in a
  playtest — schema validity isn't the same as gameplay correctness.

**2.6 — Mode switcher**
- The persistent AI-Driven/AI-Assisted toggle (`product.md` §4.3); confirm switching
  mid-session causes no data loss or conversion step.
- Tests: automated check that both modes read/write identical on-disk state for the same
  edit performed through each mode's surface.

**Phase 2 exit criteria:** you can describe a mechanic in natural language, review a
real diff against the actual engine schema, accept it, and see it work in a playtest —
and you can freely toggle to manual mode and edit the same thing by hand.

---

## Phase 3 — Content Depth

**Goal:** the World Bible / lore-consistency layer and the runtime-content-synthesis
pieces that make the two v1 genres (`product.md` §6) actually rich, matching the depth
of the sibling project's Phase 2-3.

### Milestones
- **3.1 World Bible store** (SQLite + embeddings) and editor panel.
- **3.2 RAG-grounded generation** for dialogue/quests, using the World Bible as context.
- **3.3 Contradiction-checking pass** (fixture-tested per `technical.md` §14).
- **3.4 Dungeon/level layout generation** for the ARPG template.
- **3.5 NPC schedule/behavior generation** for the cozy-sim template, built on the
  Phase 2 behavior-graph runtime.
- **3.6 Asset Generator Studio**: text-to-sprite pipeline, variant-family generation,
  prompt/seed/model-tagged assets.

Each of 3.1-3.6 gets its own fixture/golden-file tests per `technical.md` §14; several
(especially 3.4, 3.5, 3.6) also get a **user test checkpoint** since "is this dungeon
layout actually fun/navigable" and "does this generated sprite look acceptable" are
judgment calls, not assertions.

---

## Phase 4 — Polish & Ecosystem

**Goal:** performance passes, local-model maturity, and the groundwork for the module
system (`technical.md` §6) to support third-party/plugin modules, not just internal
ones. Lower detail here deliberately — this phase's shape depends heavily on what Phases
0-3 reveal about where the real bottlenecks are.

---

## Phase 5 — Platform & Engine Expansion

**Goal:** mobile export hardening and the first real 3D rendering path
(`technical.md` §8's "path to 3D"), moving the engine from "isometric indie engine" toward
the general-purpose ambition in `product.md` §6. Also deliberately light on detail now —
revisit once Phase 3-4 output (and real usage) tells us which genre/platform gap matters
most to close first.

---

## Where we start

Phase 0 is unambiguous and doesn't depend on any open question being resolved except the
two called out in `technical.md` §17 (isometric rendering approach, ECS library choice)
— both are short spikes, not blockers to starting. Suggested first concrete step:
milestone 0.1 (build skeleton + window), since everything else in Phase 0 stacks on it.
