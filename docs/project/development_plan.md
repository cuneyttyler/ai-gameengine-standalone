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

## Phase 1 — Manual Isometric Editor (no AI, standalone)

**Goal:** a genuinely usable manual editor and runtime for isometric 2.5D games — the
"manual editing must stand on its own" requirement from `product.md` §10, built and
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
  `scripts/enemy_melee.lua`. Lua API documented in `docs/systems/scripting.md`. Health bars over
  damaged characters. Also fixed: player speed was √2 too fast on screen-cardinal input;
  `Stats.moveSpeed` is now true cells/s.

**1.6 — Minimal playable vertical slice**
- One small hand-built isometric level: a controllable character, one enemy type, one
  attack, one pickup — built entirely through the Phase 1 manual tools, no AI involved.
- **User test checkpoint:** play it. This is the milestone that proves Phase 0-1 add up
  to an actual (tiny) game, and the single most important manual confirmation in the
  whole plan so far.
- ✅ Built (user test pending — see `docs/testing/phase1-test-checklist.md`): `projects/demo` is now
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
the baseline `product.md` §4.1's "must be fully usable with AI turned off" guarantee has
to hold against, and it's proven before AI exists to make it moot.

---

## Phase 2 — Reflection Layer & AI Editing v1

**Goal:** build the Reflection & Command Registry (`technical.md` §7) and stand up the
first end-to-end AI editing flow: a natural-language instruction becomes a reviewed,
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
- ✅ Built: `ai_service/` (uv project, FastAPI): `PUT/GET /schema`, `POST /propose`, `/health`,
  `/usage`; Mistral provider (JSON mode, `mistral-large-latest` by default) behind a provider
  interface; prompt built from a compact rendering of the schema plus the project context;
  proposals parsed into typed operations (`set_field`, `add_component`,
  `remove_component`, `set_prefab_field`, `set_setting`, `set_script_property`,
  `command`) and validated against schema and context, with one repair round trip.
  `engine/ai_bridge`: `AiClient` launches the sidecar (Windows job object, so it dies with
  the editor), pushes the schema, and runs requests on a worker thread; `BuildEditorContext`
  summarizes the open project. Contract: `ai_service/tests/fixtures/engine_schema.json` is
  checked against the live registry by a C++ test and drives the pytest suite. Verified
  with a real Mistral request (valid proposal on the first attempt, ~3s, ~3.3k tokens).

**2.4 — AI chat + Diff Review panels**
- The webview host and JSON bridge already exist from 1.1; this milestone adds the Vue
  chat and diff review panels and extends the message schema for AI proposals
  (`technical.md` §9).
- **User test checkpoint:** a proposal round-trips from chat to diff review to the
  applied change in the viewport — the seam between the UI, `ai_bridge` and the AI
  service is where this breaks, not inside any one of them.
- ✅ Built (user check deferred to end of Phase 2): an AI panel on the right (resizable,
  remembered width) with chat and a Diff Review card per proposal — per-change
  checkboxes, before → after, inline editing of values (the Inspector's field editor)
  and of written scripts, "file" badges for changes outside scene undo, token usage,
  Apply / Reject. `ai_bridge/Proposal`: `ResolveProposal` re-validates every operation
  against the live registry and scene and computes before/after; `ApplyOperations` runs
  accepted ones through the registry setters, prefab library and commands (one undo
  step for the scene side), recording prefab overrides like a manual edit. Every
  decision is appended to `<project>/ai_log.jsonl`. Follow-up requests carry the recent
  conversation.

**2.5 — First end-to-end NL → structured diff → apply**
- A real instruction (e.g., "add a burning status effect that deals damage over time")
  produces a schema-validated proposal, shown in Diff Review, and on acceptance is
  applied through the same registry setters the manual inspector uses.
- Tests: golden-file tests (fixed prompts → expected diff shape, schema-valid).
  **User test checkpoint:** does the applied change actually behave correctly in a
  playtest — schema validity isn't the same as gameplay correctness.
- ✅ Built (user playtest deferred to end of Phase 2): golden tests — six fixed prompts
  recorded against the real model (`ai_service/tests/golden/`, re-recordable with
  `record.py`) and replayed offline, asserting each diff's shape (the plan's "burning
  status effect" uses the built-in Burn effect on the hero prefab; "tougher skeletons"
  edits the prefab and keeps them at full health; etc.). A C++ test applies the recorded
  burning proposal to a copy of the demo project and simulates the fight: the hit sets
  the skeleton on fire and burn ticks keep hurting it. After applying a proposal that
  writes scripts the editor dry-runs the scene for 2 s and reports runtime script errors
  with an "Ask AI to fix" follow-up. Every Inspector field and setting has an "Ask AI"
  (✦) that focuses the chat on it. Recording surfaced real model mistakes, fixed in the
  prompt/context: writing unneeded scripts, not raising Health.current with max,
  replacing an existing script blind (script sources are now in the context), misusing
  `get_health`/`get_field` (the Lua API is now spelled out), and a command-shape near-miss
  (now normalized instead of costing a repair round). Known: the model's flee script
  still fights its own Follow component — valid, error-free, and wrong in play; exactly
  what the user playtest is for.

**2.6 — Hand edits and AI edits share one path** *(was: Mode switcher)*
- Originally: a persistent AI-Driven/AI-Assisted toggle, with an automated check that both
  modes write identical files for the same edit.
- ✅ Built, then revised: the toggle shipped, but the two modes only differed in panel
  width — the engine, files and workflow were the same — so the modes were **dropped**
  (`product.md` §4). What remains:
  - One write path: the Inspector and applied proposals share `engine/editing/Edits`
    (`EditField`, `EditAddComponent`, `EditRemoveComponent`, `EditScriptProperty`,
    `EditSetting`), so validation, the write and prefab override recording happen in
    one place (previously `EditorApp` and `Proposal` each had a copy).
  - `EditParityTest` makes the same six edits (fields, add/remove component, script
    property, setting) by hand and through an applied proposal, on two copies of the demo
    project, and asserts the saved scene and settings files are byte-identical and that
    the edits really landed.
  - The AI panel collapses to a ✦ rail (remembered); ✦ Ask AI expands it, and collapsing
    keeps the conversation and pending proposals.
  - An **AI on/off** switch (per user, `%LOCALAPPDATA%\aige\editor.json`): off stops the
    AI service so nothing is sent to the model, hides Ask AI, and the panel offers "Turn
    on AI". The start of 2.7.

**Phase 2 exit criteria:** you can describe a mechanic in natural language, review a
real diff against the actual engine schema, accept it, and see it work in a playtest —
and you can edit the same thing by hand, or turn AI off entirely, with the same files
either way.

### Follow-up milestones: how much the AI does

Automation grows inside the one editor as choices the developer makes, not as a separate
mode (`product.md` §4.3). These come after the Phase 2 playtest, so they're shaped by
what it finds.

**2.7 — Minimal AI**
- AI fully optional and out of the way for people who want little or none of it.
- ✅ Partly built in 2.6: the per-user on/off switch and the collapsible panel.
- Still to do: a first-run choice (AI on/off) with a plain statement of what is sent to
  the model; move the switch into an editor Preferences page next to provider settings
  once that exists; make sure nothing AI-related (sidecar, `uv`, network) is touched when
  it's off.
- Tests: with AI off the editor never launches the service (C++ test on `EditorApp`'s
  start logic); the Phase 1 manual checklist still passes with AI off.
- ✅ Built: AI is now **opt-in**. On first launch nothing AI-related runs. The AI panel
  asks "Use AI in this editor?" and lists exactly what a request sends: the message, the
  scene's entities/components/values and tile counts, prefabs, settings, asset file
  names, Lua script sources, and the earlier conversation. **Turn on AI** and **Not now**
  are both remembered. The start logic moved out of `EditorApp` into
  `ai_bridge/AiConnection`, which launches the service only while the choice is On and
  stops it on Off. `ai_bridge/UserPrefs` is the per-user preference file, which 2.8 reuses.
  `AiConnectionTest`, with an injected launcher that counts launches, checks: undecided
  and off never launch, on launches once, off stops it, and the choice survives a
  restart. Not done: a Preferences page, because none exists yet; the switch stays in
  the AI panel header until there is one.

**2.8 — Review policy**
- A per-user preference for which proposal items apply without review. Default: review
  everything (today's behavior). Options build up by risk: e.g. "apply simple field and
  setting changes directly, always review scripts, file writes, prefab edits and
  deletions."
- Auto-applied changes still show in the chat as a compact, already-applied card with
  one-click undo, and are still logged in `ai_log.jsonl`.
- Tests: policy classification per operation kind (unit); an auto-applied proposal is one
  undo step and is logged; a mixed proposal applies the safe items and leaves the rest
  pending.
- ✅ Built: two options under ⚙ in the AI panel (stored in `UserPrefs` as `reviewPolicy`):
  **Review every change** (the default) and **Apply small tweaks directly**. Turn AI off
  moved into the same menu.
  - A small tweak is a new value for an existing field, setting or script property, and
    only if it can be put back exactly (`ai_bridge/ReviewPolicy`: `AppliesWithoutReview`
    and `InverseOperation`). `LuaScript.path` and `PrefabInstance` never count as tweaks.
    Components, spawning, painting, deletions, scripts, prefab edits and new script
    properties are always reviewed.
  - When a proposal arrives, its tweaks are applied at once as one undo step and logged
    as `auto-applied`. The card marks them ✓, shows "N small tweaks applied
    automatically · Undo", and keeps the remaining items pending. "Reject the rest"
    rejects only those.
  - **Undo** restores each before value, but only if it's still the value the AI set. A
    value you've changed since is kept and named in the card. Undo is logged as
    `auto-undone`.
  - Tests: `ReviewPolicyTest` covers classification across ten operation kinds, including
    invalid ones; that `ReviewAll` never skips review; that policy names round-trip; and
    that a mixed proposal applies its three tweaks, leaves the new component pending, and
    the inverse operations restore every value. The editor glue (one undo step, logging,
    the card) is covered by the manual checklist, because `EditorApp` has no test harness.

**2.9 — Bigger tasks (multi-step requests)**
- A request the AI carries out in several steps — propose, apply to a scratch copy, dry
  run / headless playtest, read errors, revise — until checks pass or a step limit is hit,
  with progress shown in the chat. The developer reviews **one** combined result, never
  the intermediate attempts, and nothing touches the real project until it's accepted.
- **No separate button or mode.** Every request goes through the same Send. The AI
  decides how to handle it:
  - **Small:** one proposal, reviewed as today.
  - **Feature-sized:** the AI decides whether the work needs planning, then works through
    it in steps. The chat shows progress ("Step 2 of 5: playtesting…") with a **Stop**
    button, and the result is one combined proposal reviewed like any other.
  - **Too big for one go** (e.g. "make the whole dungeon crawler"): the AI doesn't start.
    It asks the user to split the request into smaller parts, suggesting a split.
- No user-facing "plan first" option and no per-request step setting. Planning and
  splitting are the AI's judgment, guided by the prompt. A fixed internal safety limit on
  steps and tokens still applies, so a runaway loop ends with an explanation instead of
  running on.
- Builds on the dry run (2.5) and the "Ask AI to fix" loop, made automatic.
- Tests: a scripted provider that fails the first attempt and fixes it on the second
  produces one final proposal; the internal limit is enforced; cancelling leaves the
  project untouched; golden prompts check the AI's judgment (a small request stays
  single-step, a feature-sized one goes multi-step, an oversized one is answered with a
  suggested split instead of a proposal). User test: a feature-sized request ("add a dodge roll with i-frames") lands
  working without manual follow-ups.
- ✅ Built:
  - **Answer format** (`ai_service`): `scope` = `small` | `task` | `split`, plus `plan`,
    `done` and `split`. The prompt explains how to size a request. A split must have no
    operations and at least two parts.
  - **`ai_bridge/TaskRunner`** carries out a task on a working copy:
    - `CloneScene` keeps entity ids, so the model's ids mean the same entities.
    - Scripts and prefabs are copied to a temp folder, and the working copy has its own
      project settings, swapped in only while it's being changed, played or described.
    - Each step is applied operation by operation, so a step can use what an earlier
      operation created. Written scripts are then played headless for 2 s.
    - The follow-up message (plan, what failed, script errors, "fix first", "set done
      when finished") goes back with the working copy as context and the task's own
      conversation.
    - It ends when the model says done and the step is clean, or at the internal limits
      (6 steps or 250k input tokens), with a reason.
    - The combined result is every operation that applied, with before/after from the
      working copy. Accepting replays it on the real scene as one undo step, and
      references to entities the task spawned are translated to the ones spawned on
      replay. Task results are always reviewed, whatever the review policy.
  - **Editor**: a live task card (plan, steps with ✓ / "N to fix", **Stop**). Stop
    discards the working copy; a step answer that arrives late is ignored. The result
    card says "Result of N steps — nothing has been applied yet". A split shows its
    suggested requests as buttons that fill the message box. Turning AI off stops a
    running task. Everything is logged (`task-finished`, `task-stopped`, `split`).
  - **Tests**:
    - `TaskRunnerTest`, with scripted answers: a script error goes back to the model and
      the fix finishes the task; the task's spawned entity is translated on replay even
      after the user took its id in the real scene; the step limit (including "done" with
      errors left); Stop leaves scene, settings, prefab and scripts untouched and deletes
      the working copy; an empty answer finishes.
    - Python: scope and split validation, the API passing a split through, and golden
      sizing with the real model. Five ordinary requests stay small, the necromancer
      ("new enemy + prefab + two copies") is a task with a plan whose first step only
      spawns a base, and "a complete open-world RPG" is split into six requests.
  - **Prompt changes from recording**: the model tried to edit a prefab it was creating
    in the same answer, and to set prefab fields for a component the prefab lacks. Rules
    for both were added, and a sizing hint that mentioned scripts was reworded because it
    led to an unneeded script. "Skeletons flee below 3 health" is now judged a task (write,
    attach, test); the golden test accepts either form.
  - **Not done:** an automated end-to-end run of a whole task against the real model.
    Only the first answer of each golden prompt is recorded; whole runs are for the
    manual checklist.

---

## Phase 3 — Content Depth

**Goal:** the World Bible / lore-consistency layer and the runtime-content-synthesis
pieces that make the two v1 genres (`product.md` §6) actually rich, matching the depth
of the sibling project's Phase 2-3.

### Milestones
- **3.1 World Bible store** (SQLite + embeddings) and editor panel.
  - **Canon lives in project files**, one human-readable entry per file:
    `world/<kind>/<id>.md`, with a small `key: value` header (kind, name, summary, tags,
    `rel:` lines naming related entries) and a free-text Markdown body. This follows
    `product.md` §4.4: the files are the source of truth, they diff in git, and they're
    editable with AI off. SQLite with embeddings is the **search index** over them
    (`.cache/world_index.sqlite`, owned by the AI service, rebuildable at any time),
    not a second store. (`technical.md` §13 said "SQLite store"; this refines it.)
  - Kinds: character, faction, location, item, event, lore, style (tone and style guide).
  - `engine/world`: parse, write, list and validate entries (unknown kind, missing name,
    relation to a missing entry, duplicate ids), plus renaming with relations updated.
    `world.*` commands go in the reflection registry so the AI can propose entries
    through Diff Review like any other change (generation itself is 3.2).
  - Editor: a **World** tab in the bottom panel, with entries grouped by kind, search,
    an editing form with problems shown inline, new, delete (with confirmation), and
    clickable relations.
  - AI service: `PUT /project` tells it the project root. It indexes `world/` (Mistral
    embeddings, re-embedding only changed entries) and serves `POST /world/search`.
    Without embeddings (no key, offline) it falls back to keyword ranking, so search
    always works.
  - The demo project gets a small seed World Bible: hero, village, skeletons, the old
    keep, tone.
  - Tests: C++ round trip, validation and rename; Python index with a deterministic fake
    embedder (only changed entries are re-embedded, ranking, keyword fallback).
  - ✅ Built as described:
    - `engine/world/WorldBible` (`aige_world`) does parse, format, refresh, validate,
      save, remove and rename. Rename rewrites relations that point at the entry, and
      header keys this version doesn't know survive a save.
    - Commands `world.write` and `world.delete`. Diff Review shows a write as the exact
      file it will produce, and says when it replaces an existing entry.
    - The World tab in the bottom panel: entries grouped by kind with + per kind, text
      search, and "Related by meaning" (semantic search, while AI is on). The form edits
      kind, id, name, summary, tags, relations (go-to, remove, backlinks) and body.
      Problems show inline, Ctrl+S saves the entry, and Delete confirms, naming the
      entries that refer to it.
    - Files edited outside the editor are picked up when the window regains focus.
    - The AI service gets `PUT /project` (sent right after the schema) and
      `POST /world/search`, plus a `MistralEmbedder`.
    - Task working copies (2.9) include `world/`.
    - The demo has seven canon entries: Aren, Old Tamsin, Brookvale, the Old Keep, the
      Restless Dead, the Wardens, and the tone guide.
    - Tests: `WorldBibleTest` (round trip, byte stability, forgiving parsing, validation
      messages, refusals, rename and remove, the AI command through Resolve and Apply,
      the demo canon is valid); `test_world.py` (parsing matches the engine,
      incremental embedding across restarts, ranking, keyword fallback when there's no
      embedder or it fails, the API).
    - A smoke run against real Mistral embeddings ranked sensibly: "who heals people in
      the village?" → Old Tamsin; "what is haunting the ruins" → the Old Keep, then the
      Restless Dead; "how should dialogue sound" → the tone guide.
    - The AI doesn't read the World Bible yet; that's 3.2.
- **3.2 RAG-grounded generation** for dialogue/quests, using the World Bible as context.
  The engine has no dialogue or quests yet, so 3.2 builds them first, in four steps:
  - **3.2a In-game text and dialogue box.** A game's dialogue is drawn by the game, not
    the editor's web UI: font atlas baking (stb_truetype, a bundled OFL font),
    screen-space text with word wrap in `renderer2d`, and a dialogue box with speaker,
    text and numbered choices.
    - ✅ Built:
      - `renderer2d/Text` (`Font`): bakes Latin-1 from a TTF at any pixel size, cached per
        size. Glyphs are 1:1 and pixel-snapped, so they're crisp with the nearest
        sampler; other characters draw as `?`. It measures with kerning and wraps
        (greedy, `
`, long words broken between characters).
      - The built-in font is compiled in from `fonts/` by CMake, and stb moved up to the
        engine level so both modules share it.
      - `Renderer2D` gained `RegisterTexture` and a `ui` quad layer drawn last in
        viewport pixels.
      - `engine/ui/GameUi`: banner, dialogue box (speaker, wrapped text, numbered
        choices, highlight, hint), objective panel and key prompt.
      - First use: "Victory!" / "You died" banners drawn by the game in the standalone
        player and in the editor's Play mode.
      - The sandbox gained `--ui-preview` (sample UI over the level) and
        `--exit-after <s>` for smoke runs.
      - Tests (`text_test.cpp`): baking, measuring, one quad per visible character,
        pixel alignment, UTF-8 decoding including invalid and truncated input,
        wrapping at three widths with nothing lost, and the dialogue box and other
        widgets staying inside their bounds and the screen.
      - A screenshot of `--ui-preview` confirmed crisp text and the intended layout.
  - **3.2b Dialogue and quest data and runtime.**
    - `dialogue/<id>.json`: a graph of nodes, each with a speaker, text and choices that
      lead to other nodes. Choices can have conditions (flags, quest state) and effects
      (set a flag, start a quest, complete an objective).
    - `quests/<id>.json`: name, giver, objectives (defeat N of an enemy, talk to someone,
      collect an item) and what completing it does.
    - Runtime: a `Talk` component (which dialogue an NPC has), **E** to talk when close,
      choices with 1-4 or the arrows plus Enter, objectives tracked from game events,
      and the current objective shown on screen. Lua: `start_quest`, `quest_state`,
      `set_flag`, `get_flag`.
    - ✅ Built:
      - `gameplay/Story`: the dialogue and quest formats (documented in
        `docs/systems/story.md`) and `StoryLibrary`, which loads and validates them: parse
        errors, broken links, unreachable nodes, more than 4 choices, unknown quests,
        objectives, effects and objective types.
      - `StoryState` holds flags and quests. Objectives are done in order, and only
        the current one counts. Defeat counts by prefab or team, collect by prefab,
        talk by World Bible key; `onComplete` effects and notices follow.
      - `DialogueRunner`: starts depend on conditions, choices are hidden by their
        conditions, effects run on node and choice, and the highlight wraps.
      - `world/StoryReferences` checks speakers, givers and talk targets against the
        World Bible, and defeat/collect targets against prefabs. It lives there
        because `gameplay` can't depend on `world`.
      - `Talk` component; new keys E, Enter and 1-4 (GLFW and the editor viewport).
        Death and Pickup events now carry the prefab and team.
      - `GameSession`: E talks to the nearest Talk entity in range, and a
        conversation pauses the world. Deaths, pickups and conversations advance
        objectives. Victory is suppressed while a quest is active, since the quest
        is the goal.
      - `GameSession::BuildUi` draws the dialogue box, "E  Talk to <name>" (names come
        from the World Bible), the current objective with progress, and notices
        ("New quest: …").
      - Lua: `start_quest`, `complete_objective`, `quest_state`, `set_flag`,
        `get_flag`, also added to the AI prompt's API list.
      - The sandbox and the editor's Play load the story fresh each run.
      - Demo: Old Tamsin (the villager prefab gets Talk) offers "Quiet the
        courtyard": defeat 3 skeletons, then return. She greets you differently while
        it's active and once it's done.
      - Tests (`story_test.cpp`): JSON round trip, conditions and effects,
        objectives in order (including team targets and notices), validation and
        reference problems, the demo story is valid, and a scripted
        `GameSession` playthrough of the demo quest: E out of range does nothing;
        in range, the world stays still during the talk; choices 1 then 1 accept,
        Space closes, killing 3 skeletons moves the objective to "Return to Old
        Tamsin", and E completes the quest, sets the flag and opens the "thanks"
        line.
  - **3.2c Editing.** A **Story** tab for dialogues and quests: a node list with links
    you can follow and problems inline. Talk is generated in the Inspector like any
    component.
    - ✅ Built: a **Story** tab in the bottom panel.
      - The list shows dialogues (⚠ for problems, "N×" for how many entities in the
        open scene use each) and quests.
      - The dialogue editor covers file id, speaker (World Bible characters), start
        node, and "different start when…" rows. Each node is a card: id (renaming
        rewrites every link), speaker (default, the player, or another character),
        text, and either "then end / go to" or up to 4 choices with next node,
        conditions and effects. → jumps to a linked node.
      - The quest editor covers name, giver, description, ordered objectives (type,
        and a target picked from prefabs, "Any enemy" or World Bible characters,
        plus count) and completion effects.
      - Problems from both validators show at the top. Save goes through the
        engine's own reader and writer, so the files stay in one shape. A changed
        id renames the file. Delete confirms and names the entities using the
        dialogue. Ctrl+S saves.
      - Shared `ed-form` control styles moved to `main.css`, so child editors match.
      - Talk is edited in the Inspector like every component.
      - Not covered by automated tests (`EditorApp` has no harness). The JSON round
        trip and validation it relies on are tested in `story_test.cpp`, and the
        panel is on the Phase 3 checklist.
  - **3.2d Grounded generation.** For each request, the World Bible entries most related
    to it (3.1 search) plus the tone guide go into the AI context. Commands
    `dialogue.write` and `quest.write` go through Diff Review, and the prompt asks the
    AI to stay consistent with canon and add new canon via `world.write`.
    - ✅ Built:
      - **Operations.** `write_dialogue` / `write_quest` carry the file content as a
        JSON object, which is more reliable than JSON inside a command's string
        argument. They're checked the same way on both sides: `app/story.py` mirrors
        the engine's `ValidateDialogue`, `ValidateQuest` and reference checks. A
        proposal is checked as a whole, so a dialogue may start a quest, and speak
        for a character, that the same proposal writes. A Talk component must point
        at a dialogue that exists or is being written. Diff Review shows the file
        itself. `WriteStoryFile` is shared with the Story tab.
      - **Context.** All World Bible entries (key, name, summary) plus the
        dialogues and quests in full, up to a size cap.
      - **Retrieval.** The service's `canon_for` adds the full text of the six
        entries most related to the request, plus every style entry, as a
        WORLD BIBLE block. It uses the same index as 3.1's search, with no extra
        round trip.
      - **Prompt.** A STORY section with both formats and the CANON rule: canon is
        true, use its names and voice, and add new named things with `world.write`.
      - **Normalization.** A recurring Mistral JSON-mode glitch, a bare number at
        the start of a list of objects, is now dropped before validation instead of
        costing a repair round.
      - **Golden recordings, all against the real model with retrieval.**
        - "A second quest from Tamsin" became a task whose first step writes the
          quest. The target is her red draughts (the potion prefab, count 2), the
          giver and last objective are her by World Bible key, and the dialogue
          update is in the plan.
        - "The Wardens' quartermaster" became a task whose first step adds Mira to
          the canon, with relations to the Wardens and Brookvale.
        - "What Tamsin thinks of the Wardens" was a single `write_dialogue` that
          keeps every original node and both quest-dependent starts, and adds a
          choice and a reply in her voice that echoes the canon ("thin on the
          ground, but they're all we've got").
        - Only one of 11 recordings needed a repair: the villager, where the model
          set Follow fields before adding the component.
      - **Tests.** C++: a quest, a new character and the dialogue that uses both
        resolve and apply together, and broken links and references block review.
        Python: the story checks (ten precise problems), a same-proposal
        character speaking, the stray-scalar cleanup, and canon retrieval reaching
        the prompt in full with the tone guide. The demo context fixture is now
        generated from a clean `git archive` of the committed demo
        (`AIGE_DEMO_PROJECT`), so local edits can't leak in.
      - Not automated: a whole multi-step story task against the real model. That's
        on the checklist, along with the user test of whether the writing reads
        well.
  - Tests: text layout (wrapping, measuring), dialogue/quest parsing and validation
    (dangling links, unknown speakers, unreachable nodes), a scripted playthrough
    (talk → accept the quest → defeat skeletons → objective completes → return), and
    golden prompts checking generated dialogue uses canon names and places.
    **User test:** does the generated dialogue read well and fit the tone?
- **3.3 Contradiction-checking pass** (fixture-tested per `technical.md` §14).
  - After a proposal that writes canon, dialogue or quests (`world.write`,
    `write_dialogue`, `write_quest`), the editor asks the service to check it
    (`POST /consistency`). A second, cheaper model (`mistral-small-latest` by default)
    gets the new writing and the canon most related to it (3.1 retrieval, by the new
    text itself), and lists each contradiction: the quote, which entry it conflicts
    with, why, and how sure it is (`contradiction` or `possible`).
  - The proposal card shows "Checking against the World Bible…", then either
    "Consistent" or the issues, each marked on its item, with **Ask AI to fix**. It
    flags but doesn't block: the developer decides. Task results are checked as a
    whole when the task finishes.
  - The World tab's entry form gets **Check against canon** for hand-written entries,
    checked against the rest of canon.
  - Tests: content extraction from operations, parsing and sanity-checking of the
    checker's answer (unknown entries dropped, op numbers mapped to items), and the
    endpoint with a scripted checker. Golden fixtures recorded against the real
    model: known contradictions (Tamsin calling herself the village blacksmith;
    Aren as a thirty-year veteran) must be flagged against the right entries, and a
    consistent piece (the recorded `tamsin_wardens` dialogue) must not be.
  - ✅ Built:
    - **Evidence.** The first recordings showed the checker treating "the canon
      doesn't mention this" as a contradiction, even when told not to. So every
      issue must now copy the exact canon words it contradicts (`canonSays`), and
      the service verifies them sentence by sentence against the entry it showed the
      model, along with the quote against the new writing. Anything it can't back up
      is dropped. Keys written with extra text (`character/villager/Old Tamsin`)
      still resolve.
    - **Model.** Chosen by comparing small, medium and large on the five fixtures.
      Small kept producing false alarms; large had one; **medium was right on all
      five**, so it's the default (`MISTRAL_CHECK_MODEL`).
    - **What gets compared.** The canon is retrieved by the new writing itself
      (8 entries plus the tone guide). Entries the proposal rewrites are excluded,
      so an edit isn't compared with its old version.
    - **Fixtures** (`tests/golden/checks/`, re-recordable with `record_checks.py`):
      Tamsin as the blacksmith, Aren as a thirty-year veteran, and "the richest
      order in the realm" are each flagged against the right entry with a verified
      quote. The recorded Wardens dialogue and the new quartermaster entry are not
      flagged.
    - **Editor.** After a proposal or a finished task that writes canon or story,
      the card shows "Checking against the World Bible…", then either "✓
      Consistent" or the issues: severity, the quote, what the canon says, why, and
      ⚠ on the item. **Ask AI to fix** sends them back as a follow-up. Results are
      logged as `consistency-check`. The World tab's form has **Check against
      canon**, which checks the saved entry against the rest of canon.
    - **Tests.** Python: what gets extracted from which operations, unverifiable
      issues dropped (invented evidence, misquotes, entries not shown,
      out-of-range op numbers), the endpoint with a scripted checker (no project,
      nothing to check, retrieval, self-exclusion), and the golden fixtures.
    - An end-to-end smoke run with the real embedder and checker flagged "Aren,
      the oldest Warden alive" against the hero entry, for about 1k input tokens.
- **3.4 Dungeon/level layout generation** for the ARPG template.
  - A deterministic, seeded generator (`gameplay/LevelGen`, pure data): rooms placed
    in a rectangle of cells, joined by a minimum spanning tree of corridors plus a few
    loops, walled in. The player starts in the room nearest the area's corner; enemies
    are kept away from the start and get denser with walking distance; pickups are
    spread over the rooms.
  - `editing/LevelBuild` puts a layout into the scene: it clears the area (except the
    player), paints floor and wall tile prefabs, and places enemies, pickups and the
    player. The command `level.generate` ties both together for the AI, and its review
    shows a summary and an ASCII map of the real layout before anything changes.
  - A **Level** tab drives the same command by hand: area (suggested east of the
    current scene), seed with 🎲, rooms and sizes, corridors and loops, prefabs and
    counts. It shows a live map preview, and Generate is one undo step.
  - Tests: 60 seeds checked for full connectivity, walls enclosing every floor cell,
    staying in the area, exact counts and no overlaps; enemies away from the start
    and denser further in; determinism; impossible parameters explained; building
    through the command (area cleared, walls solid, floor walkable, a `GameSession`
    walk); bad prefabs refused with nothing changed; and the AI's recorded proposal
    actually generating.
  - **User test:** play a few generated dungeons. Are they navigable and fun to clear?
  - ✅ Built as described. The 60-seed test found a real bug on its first run: an
    L-shaped corridor whose corner coincided with its start carved nothing, leaving
    rooms unreachable. Fixed, with explicit legs. Golden: "a small dungeon east of the
    village" → one `level.generate` in an area just outside the village's tiles, using
    stone floor, walls, skeletons and potions, and no tile-by-tile painting.
- **3.5 NPC schedule/behavior generation** for the cozy-sim template, built on the
  Phase 2 behavior-graph runtime.
  - Revised: Phase 2 never built a behavior-graph runtime (scripts and components
    covered its needs). A cozy-sim NPC's day is a schedule, not a graph, so 3.5
    builds that directly:
    - **Game clock**: time of day, with `Time` settings for start hour and minutes per
      real second.
    - **Markers**: named places in the scene, such as `tamsin_home` or `herb_garden`.
    - **Schedules**: `schedules/<id>.json`, a day as entries
      `{"from": "06:00", "activity": "stay" | "wander" | "sleep", "place": <marker>,
      "radius": n}`, each lasting until the next.
    - **Routine component**: an NPC follows a schedule.
    - **Pathfinding**: A* on the tile grid, so NPCs walk around walls to the place.
      `wander` roams within the radius; `sleep` goes home and is hidden and can't be
      talked to until morning.
  - In play, a clock shows when anyone has a routine. In the editor, markers show as
    labelled diamonds. The Story tab gets a schedule editor. Lua: `time_of_day()` and
    `day()`.
  - AI: `write_schedule` (checked like dialogue: known markers and activities, times
    that parse) and a `scene.addMarker` command, with markers and schedules in the
    context, so "Tamsin tends her herbs in the morning and sleeps at home at night"
    becomes markers + schedule + Routine in one reviewed proposal.
  - Tests: clock and entry selection (including past midnight); A* (shortest
    route, around walls, no corner cutting, unreachable); schedule parsing and
    validation; a simulated day where an NPC walks round a wall to each place in time,
    wanders within its radius, and sleeps (hidden, can't be talked to) then wakes;
    golden prompt.
  - **User test:** does a village with a few routines feel alive?
  - ✅ Built as described:
    - Engine: `gameplay/Pathfinding` (A*, octile, no corner cutting, expansion cap)
      and `gameplay/Schedule` (clock times, active entry with midnight wrap, JSON,
      validation, library, `WriteScheduleFile`). `UpdateRoutines` plans a path on
      each new entry, walks it with collision, re-plans when stuck, wanders on
      2–5 s strolls inside the radius, and handles sleep (hidden, restored on
      waking).
    - `Marker`, `Routine` and `Time` settings are in the registry; `RoutineState`
      and `Sleeping` are runtime only, so Stop restores the scene exactly.
    - `GameSession` runs the clock, routines, `time_of_day()` and `day()`, and
      draws a top-right clock. Sleepers can't be talked to. Both hosts load
      schedules and restart the day on each run.
    - Editor: markers are drawn as diamonds with "@ name" labels while editing,
      and the Story tab has a Routines section (time pickers, activity, place from
      the scene's markers, radius).
    - AI: `scene.addMarker` and `write_schedule`, checked on both sides; a schedule
      may use markers the same proposal adds, and a Routine must point at an
      existing or new schedule. Markers and schedules are in the context, and the
      prompt has a ROUTINES section.
    - Tests: clock parsing and wrap; A* (shortest, around a wall through its gap,
      no corner squeeze, unreachable, wall goal); validation; and a simulated day.
      The NPC is asleep at 05:00; wakes at 06:00, walks round a wall to the
      garden, and wanders within its radius (and does move); at 20:00 goes home,
      sleeps and can't be talked to; and is up again on day 2. Also markers plus a
      schedule resolved and applied together, and duplicate markers refused.
    - Golden ("give Tamsin a daily routine…"): a task whose first step adds the
      garden and home markers on the map. In every recording the model assumed a
      village well existed even with no markers in the context, despite an explicit
      rule. The step that uses it is refused with "no marker called 'village_well'
      (add it with scene.addMarker)", which the repair round or the next task step
      fixes; a test covers that safety net.
    - Demo: the demo scene has no markers or routines, because adding them would
      mean editing `main.aige`, which has your local changes. Ask the AI, or place
      Marker components, to try it.
- **3.6 Asset Generator Studio**: text-to-sprite pipeline, variant-family generation,
  prompt/seed/model-tagged assets.
  - Images come from Mistral's image generation (FLUX, through the Agents API's
    `image_generation` tool, with the same key). A probe gave a clean isometric pixel-art
    barrel on white in about 7.5 s. The service reuses one agent and downloads the
    result.
  - **Sprite pipeline** (`ai_service/app/sprites.py`, Pillow): remove the background (a
    flood fill from the borders at the background colour, so white inside the object
    survives, then defringe the light halo), crop to the object, scale to the target
    height, and optionally reduce the palette for a crisp pixel-art look. Saved as
    `assets/textures/generated/<name>.png`.
  - **Tagged**: each image has a `<file>.gen.json` sidecar with the description, the
    prompt actually sent, provider and model, seed (recorded as not exposed: Mistral
    doesn't return one), variant, family, processing settings and time. That's enough
    to regenerate it or explain where it came from.
  - **Variant families**: one description, several variations ("mossy", "broken",
    "painted red"), saved as `<name>-<variant>.png` in one family.
  - **Studio**: an **Art** tab with description, name, kind (object or character),
    height, style, and variants. It shows generation progress and a gallery of
    results with Keep, Discard, Regenerate and Make prefab. The Assets tab shows each
    generated texture's description and model.
  - v1 scope: objects and characters. Floor tiles need exact isometric diamonds,
    which free-form generation rarely produces. Proposals don't generate images yet;
    applying is synchronous, and a generation takes seconds.
  - Tests: the pipeline on synthetic images (background removed, white interior
    kept, halo gone, crop, size, palette limit), file naming and sidecars, the
    endpoint with a fake generator (variants, unsafe names refused, no project or
    generator), and a real end-to-end run with the image kept for the user test.
  - **User test:** do generated sprites look acceptable in the game?
  - ✅ Built as described:
    - `app/images.py`: `MistralImageGenerator` (one reused agent named
      `aige-sprite-generator` on the account; it downloads the result URL or file),
      `build_prompt` (subject, isometric view, style, cutout backdrop) and
      `generate_sprites` (variants, files, sidecars).
    - `app/sprites.py`: background colour from the border, flood-fill removal,
      halo defringe, crop, premultiplied resize, and palette reduction for the
      pixel style.
    - `POST /assets/generate`.
    - Editor: the **Art** tab (form; gallery on a checkerboard with Regenerate,
      Make prefab through the existing asset → prefab path, and Discard, which
      removes the png, sidecar and meta). The Assets tab shows "Generated: <what>
      (model)".
    - Tests (`test_sprites.py`): on a synthetic anti-aliased object with a white
      patch inside, the backdrop is gone, the patch kept and no light halo
      remains; crop, size, hard alpha and the palette limit for pixel style; no
      white fringe after the smooth resize; an empty picture doesn't fail;
      variants become a tagged family (sidecar contents); prompt shape; the
      endpoint (no project, unsafe names, bad kind, size and count refused).
    - Real runs: a barrel, a mossy variant and an old herbalist, each about 10 s
      and 48 px tall, cut out cleanly.
    - Learned from them: the model tends to draw a patch of ground under the
      subject despite "no ground". A stronger backdrop phrase ("isolated cutout…
      nothing under it, no floor, no grass, no base") reduced it to a faint ring
      or strip, but didn't remove it. That's documented in the panel ("regenerate
      if so"), with no fragile pixel cleanup.
    - The seed is recorded as not exposed, because Mistral's tool doesn't return
      one. The exact provider prompt and model are recorded.

Each of 3.1-3.6 gets its own fixture/golden-file tests per `technical.md` §14; several
(especially 3.4, 3.5, 3.6) also get a **user test checkpoint** since "is this dungeon
layout actually fun/navigable" and "does this generated sprite look acceptable" are
judgment calls, not assertions.

---

## Phase 4 — Polish & Ecosystem

**Goal:** performance passes, local-model maturity, and the groundwork for the module
system (`technical.md` §6) to support third-party/plugin modules, not just internal
ones. Phase 2-3 testing also produced a list of editor usability needs and bugs
(`docs/notes.md`); they come first, because they're what using the editor every day
runs into.

### Milestones

**4.1 — Editor shell: menus, context menus, dialogs, panels** *(notes)*
- The native viewport sits on top of the web UI, so HTML can't overlap it. Fix:
  open menus and dialogs are cut out of the viewport window's region (`SetWindowRgn`).
- Built on that:
  - **In-app confirmation dialogs** replace `window.confirm`. Bug from testing:
    deleting a quest showed an "aige-editor.local says" box, did nothing, and locked
    the window.
  - A **menu bar**: File (New/Open/Recent Projects, Save, Save As…, Settings), Edit,
    View, Help.
  - **Right-click menus** on the scene (entity / empty ground), the Scene Tree,
    prefabs and assets. Destructive items confirm first.
  - A **collapsible left panel**, **shortcuts to show or hide each panel**, and a
    **Keyboard Shortcuts** reference (Help, F1).
- Tests: occlusion-region maths (C++); the rest goes on the checklist.
- ✅ Built:
  - **Occlusion**: `editor/Occlusion` (`LocalOcclusion`) and
    `ViewportWindow::SetOcclusion` (`SetWindowRgn` with the rects subtracted). The UI's
    `overlay.ts` reports the rectangles of open overlays (`viewport:occlude`), and they
    are re-applied when the viewport moves.
  - **Dialogs**: `dialogs.ts` with `DialogHost` (confirm, prompt, info; Esc and Enter;
    modal backdrop) replaces every `window.confirm`. The quest-delete lockup came from
    `window.confirm`.
  - **Context menus**: `contextMenu.ts` with `ContextMenuHost` (kept on screen; closes on
    an outside click, Esc or blur; items can confirm first).
    - **Viewport**: a right-click that doesn't drag (more than 4 px is a pan) gives
      `viewport:contextMenu` with the entity under the cursor (now selected) and the
      cell. Entity menu: duplicate, save as prefab…, revert to prefab, ask AI, delete.
      Ground menu: paint or place the brush here, add a marker here…, frame.
    - **Scene Tree**: the same entity menu.
    - **Prefab cards**: place, show in Explorer.
    - **Asset cards**: details, show in Explorer, delete… (confirmed).
    - Features add their own items through `menuExtensions`, `entityMenuExtensions`
      and `prefabMenuExtensions`.
  - **Menu bar**: File / Edit / View / Play / Help. Its dropdowns are context menus;
    moving across the bar switches between them.
  - **Panels**: the left panel collapses to a rail and the bottom panel to a strip, both
    remembered. Ctrl+B, Ctrl+J and Ctrl+I work from the web UI and from the viewport
    (`ui:shortcut`).
  - **Help**: Keyboard Shortcuts (F1) is built from the single `shortcuts.ts` table;
    Documentation opens `docs/`.
  - Fixed on the way: `entity:select` with a null id threw. Deselecting (as Project
    Settings needs) now works.

**4.2 — Projects** *(notes)*
- **New Project** (folder picker, then a starter project), **Open Project**,
  **Recent Projects** (per-user, `UserPrefs`), and **Save Project As…** (copies the
  project, then opens it). Switching projects relaunches the editor on the other
  folder, after asking about unsaved changes.
- **Settings**: Project Settings, plus a Preferences dialog for the per-user choices
  (AI on/off, review policy).
- Tests: the new-project template is complete and valid; Save As copies everything a
  project owns and nothing from `.cache`.
- ✅ Built:
  - `assets/ProjectTemplate` (`CreateProject`, optionally with the demo's art, prefabs
    and scripts as a palette but not its scene, story, canon or generated art;
    `CopyProject` without `.cache` or `ai_log.jsonl`; `LooksLikeProject`).
  - File menu: New Project… (asks for a name and whether to start from the demo palette,
    then a folder picker), Open Project… (folder picker; refuses non-projects), up to 8
    Recent projects (per-user `recentProjects`, pruned when missing), and Save Project
    As…, which gives the copy the scene and settings as they are now, saved or not.
  - Switching starts a fresh editor process on the other project (`--project`), after
    the usual unsaved-changes question, so every system starts clean.
  - Ctrl+N, Ctrl+O and Ctrl+Shift+S work from the web UI and the viewport.
  - Preferences… gathers the per-user choices (AI on/off, review policy).
  - Tests: `ProjectTemplateTest` (folders, settings and README; the starter palette
    without scene, canon or generated art; never over an existing folder; Save As
    leaving out `.cache` and the log).
  - Also: three tests that read the demo assumed its exact contents (4 skeletons, the
    village's tile bounds, the "thanks" line). They now check that the designed
    content is still there, because the demo is a working project you're adding to.

**4.3 — Scene editing polish** *(notes)*
- **Resizing**: `[` / `]` and Alt+wheel scale the selection, and the brush has a size %
  that applies when placing or painting. Sprite and colliders scale together, as an
  override on prefab copies.
- **Removing a prefab**: copies in the scene become ordinary entities, with a
  confirmation naming how many.
- **Tile borders** on empty ground while editing (G toggles).
- **Shortcuts refined**: arrows nudge (Shift: finer), Ctrl+C / Ctrl+V copy and paste,
  F5 Play/Stop, Ctrl+Shift+S Save As, and panel toggles.
- Tests: scaling (sprite, colliders and overrides), removing a prefab (copies kept and
  unlinked), copy/paste round trip, nudge.
- ✅ Built:
  - `editing/SceneTools`: `ScaleEntity` (Sprite size, Collider2D and Hurtbox together,
    through the shared edit path so prefab copies record overrides; tiles refused; sizes
    clamped), `NudgeEntity` (whole cells for tiles, refusing an occupied cell; exact
    otherwise), `CopyEntities` / `PasteEntities` (scene text; pasted at a cell, a pasted
    tile replaces the tile there). `PrefabLibrary::Remove` deletes the file and unlinks
    its copies.
  - Editor: `[` / `]` (Shift: finer) and Alt+wheel resize the selection; the brush's
    size % applies to placed prefabs (tiles are the grid, so they stay one cell); arrows
    nudge (Shift: a tenth of a cell); Ctrl+C / Ctrl+V (pastes under the cursor, or "Paste
    here" on the ground menu); F5 plays and stops; G draws tile borders on the empty
    ground in view. Every change is one undo step.
  - Menus: Edit ▸ Copy, Paste, Bigger, Smaller; View ▸ Tile grid; entity menu Copy,
    Bigger, Smaller; prefab card Delete prefab… (confirmed). All in the shortcuts table.
  - Tests: `SceneToolsTest` (4). Paste found a real bug on its first run: replacing a
    tile moved the Tile storage under a held pointer.

**4.4 — Organizing prefabs and assets** *(notes)*
- Other engines use folders plus search plus filters. So: the Prefabs and Assets
  tabs gain a **folder tree** (subfolders of `prefabs/` and `assets/`), **search**, and
  **kind filters** (tiles, characters, items, other; textures, audio, generated).
- **Move to folder… / Rename…** from the right-click menu. Prefab moves rewrite every
  reference: scene copies, other prefabs, and story targets.
- Tests: moving and renaming update every reference; grouping and filtering.
- ✅ Built:
  - `assets/ProjectMove`: `MoveProjectFile` moves a prefab (staying under `prefabs/`) or
    an asset (staying under `assets/`, with its `.meta` and `.gen.json`) and rewrites
    every exact reference in the project. That covers `key=value` values in scenes,
    prefabs and settings (re-quoted when the new name has a space), string literals in
    JSON (story, world, schedules), and quoted strings in Lua. `.cache` is left alone.
    It refuses type changes, leaving the root, `..`, and existing destinations.
  - The open scene follows the move with every entity id kept (`RewriteScene`), and so
    does the undo history (`EditHistory::RewriteSnapshots`), so undo never brings back
    a path that's gone. The brush follows too.
  - Prefabs and Assets tabs: a folder column (subfolders, nested, with "All"), kind
    chips (prefabs: tiles, characters, items, other, from the new `PrefabInfo::kind`;
    assets: textures, tilesets, audio, generated), and search over the path.
    Right-click offers Rename… and Move to folder…, and a card can be dragged onto a
    folder. Number keys still pick from the whole palette.
  - Tests: `ProjectMoveTest` (6): whole-value matching, quoting both ways, a prefab move
    rewriting a scene, another prefab, a quest and a script but not `.cache`, an asset
    move with its sidecars, the refusals, and the live scene plus undo keeping ids.

**4.5 — Keeping AI work** *(notes)*
- **Conversations persist per project**, so saving, quitting and reopening brings the
  AI chat back. This was a bug from testing. Stored in `.cache/ai_conversation.json`,
  with Clear available.
- **Art tab history**: every generation request (description, settings, results,
  including discarded ones) is kept, with "use again".
- Tests: the conversation round trip, history kept across restarts, and decided
  proposals not re-appliable after a reload.
- ✅ Built:
  - `ai_bridge/AiTranscript`: every host → UI chat message (requests, proposals,
    decisions, task progress, checks, splits) is kept with the model's context turns in
    `.cache/ai_conversation.json`, the newest 400. On open the panel asks for it
    (`ai:history`) and replays it through its usual handlers. Earlier runs' ids become
    negative and unique per run, so they can't collide with new requests (ids restart
    at 1). Their undecided proposals show as "not applied, from before the editor
    restarted", unfinished tasks as stopped, and their auto-applied Undo is hidden.
    Follow-ups keep their context. ⌫ in the panel header clears it (confirmed).
  - `ai_bridge/ArtHistory`: every Art request (`.cache/art_history.json`) with its
    settings, state, errors and results. Discards, renames and moves are recorded on
    the results, and a request still drawing when the editor closed comes back as
    failed. The Art tab has Files and History views. History shows thumbnails
    ("discarded" or "redrawn" when the file is gone or was overwritten), **Use again**
    (back into the form) and **Generate again**, and Clear history.
  - Tests: `AiTranscriptTest` (restart round trip, ids across three runs, context,
    trimming, a broken file, clear) and `ArtHistoryTest`.

**4.6 — Performance pass**
- A frame-stats overlay (F3: frame and update ms, sprites, draw calls), and a
  benchmark scene of 10k tiles plus 500 characters.
- Lift the fixed 4096-sprite batch limit (vertex buffers grow), and cull sprites
  outside the camera.
- Tests: a benchmark with a time budget (Release), and culling correctness.
- ✅ Built (Release budget not yet verified, see below):
  - `renderer2d/SpriteCulling`: `VisibleSprites` gives what the camera sees, in
    painter's order, without a GPU. Renderer2D draws only those, and its per-frame
    vertex buffers grow (doubling) instead of stopping at 4096 sprites. `LastStats()`
    reports sprites drawn and culled, draw calls and vertices.
  - **F3** frame stats (frame and update ms, smoothed; sprites drawn and culled; draw
    calls) in the editor (editing and play; also View ▸ Frame stats) and in the
    sandbox player.
  - `gameplay/Benchmark`: `BuildBenchmarkScene` (100×100 tiles with a wall ring, the
    player, 500 scripted skeletons). `sandbox --bench` plays it zoomed out (about 10k
    sprites on screen) with stats on and prints averages on exit.
  - Found by profiling: every script checked its file's time every step for hot
    reload (500 filesystem calls per step), now once per file per step, and the
    "(re)loaded" log is once per file. `find_nearest` compares squared distances, and
    tiles aren't projected twice.
  - Tests: `CullingTest` (edges, off-screen, a sprite larger than the view, depth
    order) and `PerfTest` (the benchmark scene: counts, no script errors, most of the
    field culled, with an 8 ms step-plus-sprite-list budget in Release builds).
  - **Not verified here:** on this machine Bitdefender refuses to start freshly built
    Release executables ("Access is denied"), so the Release budget hasn't been seen
    to pass. In Debug a benchmark step is about 185 ms, about 150 of it Lua (500
    scripts × find_nearest over 500 characters through unoptimized sol2) and about 38
    without scripts. Debug is no guide to Release, but Lua per entity is the place to
    look if Release misses the budget: shared environments, or spatial queries
    instead of `find_nearest`'s scan.

**4.7 — Local models** → moved to Later, 2026-10-16.

**4.8 — Module groundwork**
- **Lua modules**: a `modules/<name>/module.json` declares components (fields with
  types and ranges) and commands implemented in Lua. They join the reflection
  registry, so the Inspector, scenes, scripts and the AI see them like built-ins.
- Tests: a sample module (a `Lantern` component with a light radius, and a command)
  shows up in the schema, round-trips through a scene, and runs its Lua.
- ✅ Built:
  - `engine/modules` (`LoadModules`): reads `modules/*/module.json` and registers its
    components and commands in the reflection registry. Module components keep their
    values in one bookkeeping `ModuleData` per entity; each registered component reads
    and writes its part through the usual field functions, so scenes, prefabs and
    overrides, the Inspector, copy/paste, Lua's `get_field`/`set_field` and AI
    proposals need no changes.
  - Commands run their Lua function in the module's own Lua state, with
    `get_field`, `set_field` (validated), `entities_with` and `log`. They fail with
    `false, "why"` or an error.
  - The editor and the player load modules before the scene. Problems go to the
    console, and the editor shows the first one. A broken module never stops the
    rest.
  - Sample: `projects/demo/modules/lantern` (`Lantern`: radius, lit, color, fuel;
    `lantern.setAll`, `lantern.scaleRadius`). Guide: `docs/places/modules.md`.
  - Tests: `ModulesTest` (4): in the schema with types and ranges, a scene round
    trip, the commands with checks and a refusal from the Lua, and six kinds of
    broken module reported while the good part still loads. That test found a crash:
    a Lua syntax error's result outlived its VM.

**Phase 4 exit criteria:** the notes are resolved; big scenes stay smooth; a project
can add its own components without C++. (Local models moved to Later.)

---

## Phase 5 — Shipping & Workflow

**Goal:** games ship as standalone builds, a game can span several connected scenes,
and art made top-down works on the isometric grid. Plus the editor workflow notes from
the Phase 4 testing (2026-10-16).

### Milestones

**5.1 — Build & Export (desktop)**
- File → **Export Game…** builds a folder with the player (Release, renamed), the
  project's runtime content, and none of `.cache`, AI logs or editor-only files. The
  window title and icon come from the project name, and there's a zip option.
- The player loads the exported folder.
- Tests: the export has everything the game needs and nothing it doesn't; the exported
  player runs the scene headless for a few seconds (smoke).
- ✅ Built:
  - `assets/Export`: `ExportGame` writes `<Name>/<Name>.exe` (the player, renamed),
    `shaders/`, and `game/` with the runtime content (`IsRuntimeContent`: settings,
    scenes, prefabs, scripts, assets with their metas, dialogue, quests, schedules,
    world, modules; never `.cache`, the AI log, `.gen.json`, hidden files or loose
    top-level files) plus `export.json`. There's an optional `.zip` from a built-in
    zip writer (stored, UTF-8 names, verified with Python's `zipfile`). It never
    overwrites an earlier export.
  - The player (`apps/sandbox`) plays `game/` next to itself when started with no
    project. The window title is the game's name and the icon is `assets/icon.png`
    when present. `--headless [--exit-after s]` runs the game without a window and
    reports (exit 0 when no script errors).
  - Editor: File ▸ Export Game… asks for the name and folder-or-zip, saves unsaved
    changes, asks for the destination, exports, and offers Show in Explorer. It ships
    a Release player when one is built, else the Debug one, and says so.
  - Tests: `ExportTest` (3): what's shipped and what isn't, the layout and zip, the
    refusals, and the smoke test, which exports the real demo with the real player
    and runs the copy headless for 2 s (120 steps, no script errors).
  - Note: on this machine Bitdefender blocks freshly built Release executables (see
    4.6), so exports made here use the Debug player.

**5.2 — Top-down art on the isometric grid** *(testing; most important)*
- **The problem (from testing):** the grid is isometric, with diamond-shaped cells at
  2:1, but most tile art is drawn **top-down**: a square image seen from straight
  above, such as a road running up the image. Painted as a tile, that square is drawn
  as an upright rectangle over the diamond. It doesn't line up with the cell, so a road
  can't follow the grid.
- **The fix: a tile's art has a projection.**
  - **Isometric** (what the engine does now): the image is already drawn as a diamond,
    or a block standing on one.
  - **Top-down:** the square image is mapped corner to corner onto the cell's diamond
    (turned 45° and halved in height), so it lies flat on the ground and neighbouring
    tiles join edge to edge. A road then runs along the grid's axes.
  - Chosen when making a tile prefab from an asset. A square image suggests top-down,
    and a 2:1 diamond image suggests isometric. It can be changed later in the
    Inspector, and the Art tab can generate tiles either way.
- **Rotation in 90° steps** for tiles (the brush's R key while painting, or the
  Inspector), so the same road piece runs either way, and corners and T-pieces turn.
- **Props from top-down art:** they still stand upright, and a "lies flat" option
  (rugs, decals, shadows) maps them onto the ground like tiles.
- The renderer draws sprites as four free corners instead of upright rectangles. Picking,
  culling and the grid overlay follow the same diamond.
- Tests: the corner mapping for each rotation, neighbouring tiles sharing edges, picking
  on a mapped tile, the projection and rotation round trip through scenes and prefabs,
  and old scenes loading unchanged. Checklist: import a top-down road and paint a
  straight road and a corner.
- ✅ Built (tests run at the end of the phase):
  - `Sprite.flat` and `Sprite.rotation` (0–3), both reflected so the Inspector, scenes,
    prefab overrides and the AI see them. `SpriteQuadOf` gives the four corners and
    texture coordinates: the upright rectangle, or the diamond with the image's corners
    on the cell's corners, turned by quarter turns. The renderer draws every sprite
    through it.
  - Draw order: flat tiles go to a ground layer under everything (nothing can be behind
    the ground). Flat props (rugs, shadows) lie centered on their point, above their
    cell's tile and below whatever stands there. Isometric tiles keep their depth, so
    the demo looks the same.
  - Making a prefab from an asset asks how the art is drawn: **top-down** (laid onto
    one cell's diamond, whatever the image's size) or **isometric/upright**. Square
    images suggest top-down, 2:1 ones isometric. For prefabs made earlier, the
    right-click item **Lay flat on the grid** (or Stand upright) rewrites the prefab
    and every copy follows.
  - **R** turns the brush while painting (shown in the toolbar as ↻ 90°), else the
    selected flat sprite. It's one undo step and an override on prefab copies. The
    entity menu has "Turn a quarter".
  - Art tab: a **ground tile (top-down)** kind. It asks for a seamless top-down texture,
    keeps it square and opaque (no cutout), and Make prefab turns it into a flat tile.
  - Flat props outline as their diamond; tiles still pick and outline by cell.
  - **Resizing fixed (from testing):** `[` / `]` and Alt+wheel did nothing on a tile
    (tiles were refused) and nothing on keyboards where `[` needs AltGr. Tiles now
    resize their image while staying in one cell, the brush size % applies to tiles
    too, and the viewport reads the typed character (WM_CHAR), so `[` `]` (`{` `}`
    finer) and `-` `+` work on any layout, in the viewport and the UI. Resizing with
    nothing selected says so.
  - Tests: `FlatSpritesTest` (5: the rectangle, the diamond and turns, neighbours
    sharing edges, draw order, the scene round trip with old scenes upright), a tile
    resizing in `SceneToolsTest`, and two pytest cases for tile generation.

**5.3 — Multiple scenes and gateways** *(testing)*
- A project has any number of scenes (`scenes/*.aige`). One is the **start scene**,
  set in Project Settings.
- A **Scene Manager** panel lists them, with new, open, rename (references follow, as
  in 4.4), duplicate, delete (confirmed, and refused while a gateway points at the
  scene) and set as start. The editor title and File menu show the open scene.
  Switching asks about unsaved changes.
- **Gateways:** a component (target scene and target marker, with the trigger area of
  its cell) that moves the player to that scene's marker during play. Quest progress,
  story flags, the clock and the player's health carry over. A gateway prefab is added
  to the starter palette (your `portal` prefab can become one). Broken targets show as
  problems, like the Story tab's.
- The AI is told the scene list and can propose gateways. Export ships every scene, and
  the player starts in the start scene.
- Tests: scene list operations and reference rewriting, a gateway round trip, play state
  carrying across a scene change (headless), and export with several scenes.
- ✅ Built (build and tests at the end of the phase):
  - `assets/SceneFiles`:
    - `ListScenes`, `StartScene`/`StartScenePath` (the new `Game.startScene` setting,
      default `scenes/main.aige`), `CreateScene`, `DuplicateScene` (never overwrites).
    - `SceneReferences` and `DeleteScene`, which refuses the start scene and any scene a
      gateway leads to.
    - `GatewayProblems` (a missing scene or marker) and `SceneMarkers`.
    - Renaming goes through 4.4's `MoveProjectFile`, now allowed under `scenes/`, so
      gateways and the start setting follow.
  - **Travel** (`TravelToScene`): the player entity is carried over whole, serialized
    with every component (health, scripts, module data). This differs from the plan's
    first idea of copying values onto the new scene's player, and is simpler and loses
    nothing. The new scene's own player gives way, and references to it point at the
    arrival. The player stands at the marker; a missing marker gives a warning and a
    fallback spot.
  - `Gateway` component (scene, marker) and `CreateGateway`, a flat glowing ring (5.2).
    `GameSession` asks for travel when the player steps into a gateway's cell, never
    while standing on one since arriving or starting. The host changes scene, and story,
    flags and the clock carry on (`TakeTravel`, `OnSceneChanged`).
  - Editor:
    - A **Scenes** tab (Scene Manager): the list with start and open badges and marker
      counts, New scene…, double-click to open, and right-click for Open, Set as start
      scene, Rename…, Duplicate… and Delete…. Gateway problems are listed beside it.
    - File ▸ New Scene… and Scenes…. Opening a scene asks about unsaved changes and
      clears undo. The title shows the open scene.
    - In Play, gateways work. The open scene plays as edited and others as saved, and
      Stop returns to the scene being edited.
  - The player (windowed and headless) starts in the start scene and follows gateways.
    Export accepts any project with a scene and ships them all.
  - AI: the context lists every scene with its markers plus the open one, and the
    prompt explains gateways (only listed scenes and markers).
  - Starter palette and demo: a `gateway` prefab (new projects get a placeholder ring;
    the demo's uses `assets/textures/gateway.png`).
  - Tests: `ScenesTest` (6): create, duplicate and list; delete refusals; a rename
    rewriting gateways and settings; gateway problems; travel carrying the player and
    retargeting a follower; a gateway firing only on the way in.
- After testing (2026-10-18): a new scene looked like a separate file. The window title
  showed only its file name, and the Scene panel showed an empty tree, while scenes
  were listed only in Assets ▸ Scenes. Now:
  - The **Scene panel lists the project's scenes** at the top of its tree. The open
    scene is expanded into its entities (with a hint when it's empty), and the start
    scene is starred. Clicking another scene opens it (offering to save first);
    right-clicking gives open, set as start, rename, duplicate, new and delete; the **+**
    button makes a new scene.
  - The **window title** names the project, then the scene
    (`MyGame — cellar* - …`).
  - The scene list and its actions are one shared store (`ui/src/scenes.ts`), used by
    the Scene panel, the Scene Manager tab and the File menu.

**5.4 — Start window** *(testing)*
- Launching the editor without a project opens a **welcome window**:
  - recent projects (name, folder, last opened; missing ones marked, removable);
  - **New Project**, **Open Folder…**, and **Open the demo**.
- Opening one starts the editor on it, as File ▸ Open does.
- A preference makes it reopen the last project directly. File ▸ Close Project goes
  back to the window.
- Tests: the recent list (ordering, missing folders, pruning); the rest is on the
  checklist.
- ✅ Built (build and tests at the end of the phase):
  - `editor.exe` with no project (or with `--welcome`) runs the **welcome window**:
    just the web UI (`index.html#welcome`, `WelcomeApp.vue`), with no viewport and no
    project loaded. It shows recent projects (name, folder, last opened; missing ones
    marked; × removes one), New Project… (name, palette, folder), Open Folder… (refuses
    non-projects) and Open the demo. A choice starts the editor on that project as a
    new process and closes the window (`apps/editor/Launcher`).
  - "Open the last project directly at launch" (on the welcome page and in
    Preferences) skips the window when the last project still exists. File ▸ Close
    Project asks about unsaved changes, then goes back to the welcome window.
  - `ai_bridge/RecentProjects`: remember (newest first, once each, capped, with
    when-opened), forget, and list with an on-disk check. Paths compare whatever
    their separators, `..` or (on Windows) case. The editor's recent list (4.2) uses
    it too.
  - Changes in behaviour: starting `editor.exe` with no arguments used to open the
    demo; it now shows the welcome window. `WebViewHost` gained a URL fragment, and
    its pending navigation now gets the cache-busting stamp too.
  - Tests: `RecentProjectsTest` (3).

**5.5 — Editor refinements** *(testing)*
- **File ▸ Recent Projects** becomes a submenu instead of a list of items. Context
  menus gain submenus, which other menus can use too.
- **Assets and Prefabs multi-select:** Ctrl+click, Shift+click ranges and Ctrl+A.
  Move to folder…, Delete… (one confirmation listing them all) and drag-to-folder act
  on the whole selection.
- **A shortcut to show or hide the viewport**, like the panel toggles (Ctrl+B, Ctrl+J
  and Ctrl+I). The panels take its space. It's listed in the shortcuts table and the
  View menu.
- The shortcuts list's F1 entry reads "Shortcuts" instead of "This list".
- **Project Settings in its own window:** a separate panel with a close button (and
  Esc), instead of taking over the Inspector where it can't be closed.
- Tests: the selection logic (ranges, Ctrl+A in a filtered view), multi-file moves
  through the 4.4 path, and the checklist for the rest.
- ✅ Built (build and tests at the end of the phase):
  - **Submenus** in context menus and the menu bar (`MenuItem.submenu`, opening on
    hover beside the item, flipping left at the screen edge, cut out of the viewport
    like other overlays). **File ▸ Recent Projects ▸** uses one, with the full path on
    hover.
  - **Multi-select** in Prefabs and Assets (`selection.ts`): Ctrl+click toggles,
    Shift+click picks a range from the anchor (Ctrl+Shift adds it), Ctrl+A picks what
    the folder, filter and search show, Esc clears, and Delete deletes the picks after
    one confirmation listing them. Right-clicking a picked card acts on all of them
    (Move N to folder…, Delete N…), and dragging one drags them all onto a folder. A
    plain click on a prefab still picks the brush. Assets show a summary pane when
    several are picked. Moves keep the picks under their new paths.
  - **Ctrl+M** shows or hides the viewport. The bottom panel takes its space and the
    native window is hidden (`viewport:visible`). It's remembered, it works from the
    viewport too, and it's in View ▸ Viewport and the shortcuts list. Hiding the bottom
    panel brings the viewport back, so the middle is never empty.
  - The F1 entry reads "Shortcuts".
  - **Project Settings in its own window** (`ProjectSettingsDialog`): File ▸ Project
    Settings… opens it, with × and Esc to close, collapsible sections, field errors
    and ✦ Ask AI. The Inspector with nothing selected now just points there.
  - Tests: `apps/editor/ui/tests/selection.test.ts` (7, `npm test`, Node's own test
    runner with no new dependency). Multi-file moves reuse 4.4's tested path.

**5.6 — Layout and keys** *(testing, 2026-10-16)*
- Panels can move: the Scene, Assets and AI panels each dock left, bottom or right.
- Shortcuts can be edited, in settings.
- The panel shortcuts drop Ctrl: B, J, I and M.
- With several assets picked, the side pane keeps "Drawn" and the make-a-prefab buttons.
- Prefabs and Assets folders get right-click menus: Rename folder…, Show in Explorer and
  Remove folder…. Removing asks first, with a checkbox for "Also remove the assets/prefabs
  in it".
- The viewport pointer shows the tool: select, brush or erase. The brush shows a
  see-through copy of what it would put down.
- ✅ Built:
  - **Docking** (`layout.ts`, `PanelFrame`, `ScenePanel`):
    - Each panel lives in a dock (left, right, bottom), shown or hidden, with dock
      sizes and per-panel shares saved per user. The 4.1 layout is migrated.
    - Panels are teleported into their dock, so moving one keeps its state (drafts, the
      AI chat). Two panels in one dock share it with a handle between them.
    - Moving: right-click a panel's header ▸ Dock left/bottom/right or Hide, or View ▸
      <panel> ▸. Hidden panels leave a rail (sides) or a strip (bottom) to bring them
      back.
    - The Scene panel stacks the tree and Inspector in a side dock and puts them side by
      side at the bottom. With the viewport hidden, the bottom dock fills the middle.
  - **Shortcuts as data** (`keymap.ts`, `keys.ts`):
    - Every rebindable action has its default keys, and the user's changes are kept by
      the host (`UserPrefs` "keymap", only the differences).
    - Keys are named by the character typed for symbols, so any layout works.
    - The viewport no longer has its own shortcuts. It forwards each key as a combo
      (`viewport:key`, Alt combinations too) and the UI runs whatever it's bound to, so
      one keymap rules both. Only arrows (nudge), 1–9 (prefabs) and Esc (stop Play) stay
      fixed. In Play only Play-time actions run (F5, F3, F1); the game reads its own keys
      as before.
    - Panel toggles are B, J, I and M.
    - **Help ▸ Keyboard Shortcuts** (F1, also in Preferences) edits them. Click a key to
      record a replacement, + to add one, × to remove one. A key taken from another
      action says so. Reset works per action or for all.
    - Menus show the current keys.
  - **Folders**: right-click a folder in Prefabs or Assets:
    - Rename folder…: everything in it moves, references follow (4.4).
    - Show in Explorer.
    - Remove folder…: one confirmation with a checkbox (the new `confirmWithOption`).
      Ticked, the contents are deleted (prefab copies in the scene stay, unlinked, as one
      undo step). Unticked, they move up a level, references followed. The folder goes
      once empty; anything else in it stays and is reported.
  - **Multi-select pane**: with several assets picked, it keeps "Drawn" and **New tile
    prefabs / New prop prefabs** (one each, named after the files) and Solid.
  - **Pointers**: Select shows an arrow, or move arrows over something that can be moved;
    the brush shows a crosshair; Erase shows a drawn eraser; panning shows a hand.
    **Ghost**: a half-transparent copy of the brush prefab follows the cursor, snapped
    to its cell, with the brush's size and rotation. It lives in its own registry, which
    `Renderer2D::Render` draws with the scene (never saved, never undone).
  - Tests:
    - UI: `keymap.test.ts` (6: naming on any layout, defaults, rebinding with a taken
      key, unbinding, reset).
    - C++: folder listing, moving up and empty-folder removal (`ProjectMoveTest`).

**Fixed after the phase-end run (from testing, 2026-10-16):** the editor opened with no UI
(only the viewport, or a gray window). The 5.3 File-menu code rewrote a menu list it also
watched, so it re-ran itself forever before the UI mounted. Vue's production build has no
guard against that, and nothing reported it. Fixed by giving each feature its own menu slot.
Found by loading the built UI in headless Chrome over the DevTools protocol and pausing the
stuck page. Along the way:
- a JavaScript error before the UI is up is now shown in a message box instead of leaving a
  blank window;
- if WebView2 fails to start, the editor retries a few times and then reports it, instead of
  failing silently;
- an editor started by another one (switching projects, the welcome window, Close Project)
  waits for that one to exit before starting WebView2 (`--after <pid>`).

**Phase 5 exit criteria:** a game can be exported and played outside the editor;
top-down tiles paint cleanly onto the isometric grid; a game can span several connected
scenes; the editor opens to a project picker; the Phase 4 testing notes are resolved.

---

## Phase 6 — AI that understands the art

**Goal:** the AI builds good-looking places from an art pack it has never seen. It knows
what each image is, how big it should be, which piece of a road set goes where and how
to lay out a settlement, instead of guessing from file names.

**Why (from testing, 2026-10-17):** asked to build "a relatively large village" from a
free top-down medieval pack (56 tiles named "Ground 01–56" plus 33 props), the AI:
- picked the wrong road tiles: the names say nothing, and the set is a grass/dirt
  transition set with edges, corners and multi-cell curves;
- built a small village: placing objects one at a time doesn't scale;
- left the houses at their image size: nothing told it the pack's scale (a 480×640
  house on a 64×32 cell);
- placed houses poorly and left out the well, fences and decorations.

Also, that pack is drawn for a **top-down square grid**. Its fences and bridges run along
the screen axes, which an isometric grid never lines up with.

Decisions (2026-10-17, defaults the user can still change):
- The grid style is a per-project setting.
- Analysis keeps what it learns in the asset's metadata and only *suggests* new file
  names; renaming happens on request, with references following.
- Rotation comes in from Later.
- The AI checking its own result from a screenshot is optional, at the end.

### Milestones

**6.1 — Grid style: isometric or top-down**
- A project setting, Grid ▸ style: Isometric (diamond cells, as now) or Top-down
  (square cells, axis-aligned). Everything goes through one projection
  (`ProjectIso`/`UnprojectIso`), so cells, picking, the grid overlay, depth order,
  flat tiles (drawn as squares, not diamonds), the camera and player movement (WASD
  along the screen axes) all follow.
- New projects choose a style. Existing projects stay isometric.
- Tests: projection round trips for both styles, depth order, flat quads as squares,
  movement direction, and the setting round trip.
- ✅ Built (build and tests at the end of the phase):
  - Project Settings ▸ **Grid ▸ style** (Isometric / TopDown). `IsoParams.topDown`, set by
    `BootstrapIso()` from the setting, switches `ProjectIso`/`UnprojectIso` to square
    64×64 cells along the screen axes, with depth by row. Cells, picking, the grid
    overlay, outlines, the camera, levels and everything else that goes through the
    projection follow.
  - Flat images lie as squares (`SpriteQuadOf(…, diamond)`, `Renderer2D::SetFlatDiamonds`).
    The player walks along the screen axes, faces left or right by screen direction, and
    the built-in starter tiles fill a square cell.
  - Changing the setting re-lays the open scene on its next frame, however it was
    changed (Project Settings, an AI proposal, undo).
  - New Project (editor and welcome window) asks which grid; existing projects stay
    isometric.
  - Tests: `GridStyleTest` (6).

**6.2 — Asset understanding**
- **Image analysis** (AI service, a vision model):
  - Each image gets a description, a kind (ground tile, building, nature, decoration,
    item, character, other), tags (well, fence, road-edge…), a direction where one
    applies (a fence or bridge that runs left–right or up–down), a rough size in cells,
    whether it blocks movement, and a suggested name.
  - Kept in the asset's `.meta`. Assets ▸ "Analyze" works on the picked assets or a
    whole folder.
- **Tile edges** (engine, no AI): every tile's border is read from its pixels and each
  corner and edge classified by terrain (e.g. grass or dirt, clustered over the set).
  That gives each tile a signature, the basis of autotiling (6.4). Exact where a vision
  model would guess.
- **Names**: the suggested names can be applied (one confirmation; references follow,
  as in 4.4). Otherwise the files stay as they are.
- The AI's context carries descriptions, kinds, tags, directions and sizes, so it
  chooses by meaning, not file name.
- Tests: edge classification on generated tiles (corners, edges, inner corners, a 2×2
  curve), the `.meta` round trip, the analysis request and validation against
  recorded answers, renaming with references.
- ✅ Built (build and tests at the end of the phase):
  - **`.meta` keeps analysis**: any key besides the import settings is kept
    (`AssetInfo::analysis`, `AssetDatabase::SetAnalysis`). Changing import settings no
    longer wipes it.
  - **Tile edges** (`assets/TileEdges`): eight border patches per image (corners and
    edge midpoints, inset past outlines). The ground tiles of a folder are clustered
    (deterministic k-means) into terrains named by colour (grass, dirt, sand, water,
    stone…), giving `edges=grass,grass,dirt,…` per tile. Only square, opaque-bordered
    images count as ground; props with transparent backgrounds are skipped.
  - **Vision** (AI service `POST /assets/describe`, `app/vision.py`):
    - Images are downsized to 256 px over a light checker and sent six to a request,
      three requests at a time, to a vision model (`MISTRAL_VISION_MODEL`, default
      `mistral-medium-latest`), with each file name and pixel size as hints only.
    - The answers are validated: kind from a fixed list, direction
      none/horizontal/vertical, size clamped to 0.1–20 cells, kebab-case names, tidy
      tags. Images left undescribed are reported.
  - **Editor**:
    - Assets ▸ **Analyze N** (what's shown) and Analyze on one or several picked assets.
      Edges are computed at once; descriptions arrive from the service.
    - The details pane shows what an asset is: kind, direction, whether it blocks the
      way, tags, size in cells, border terrains.
    - **Rename to the suggested name / Apply N suggested names**: one confirmation,
      duplicates numbered, references followed.
    - Search also matches descriptions, tags and kinds.
  - **The AI** gets every asset's analysis and pixel size in its context, and the prompt
    tells it to choose by meaning, match road tiles by their edges, and size by cells.
  - Tests: `TileEdgesTest` (4: classification of synthetic tiles, transparent props, the
    signature format, the `.meta` round trip) and `test_vision.py` (4: batching and the
    images sent, cleaning bad answers, only project images, the endpoint).

**6.3 — Importing a pack**
- **One scale per pack**: pixels per cell, taken from the pack's ground tiles (a 256 px
  tile = one cell) or set by hand. Every prop keeps the artist's proportions (a house
  about 2 cells, a barrel half a cell) instead of each item being guessed separately.
- **Import as prefabs** (picked assets or a folder): one reviewed proposal that makes
  every prefab. Ground tiles become flat tiles, buildings and big props solid, the rest
  props, all sized by the pack scale and named from the analysis.
- A `prefab.create` command so the AI can make prefabs in its proposals.
- Tests: pack scale from tiles, the sizes of imported props, the command round trip.
- ✅ Built (build and tests at the end of the phase):
  - `assets/PackImport`:
    - **Pack scale**: `PackScaleFor` gives pixels per cell for a folder. Set by hand
      (`<folder>/pack.json`), else the median width of its ground tiles, else what the
      analysis measured (image width ÷ "cells"), else one cell per tile width; it says
      which.
    - **Plan**: `PlanPackImport` makes ground tiles (analysis kind, or edges found)
      flat one-cell tiles. Everything else becomes a prop sized image px ÷ scale × cell
      width, keeping the image's proportions; solid when the analysis says it blocks or
      it's a building. Names come from the analysis (else the file), at
      `prefabs/<pack>/<name>.prefab`, never clashing with existing ones.
    - `CreatePrefabFromPlan` makes the prefab.
  - **Assets ▸ Import as prefabs…** (what's shown, or the picked assets) opens a review
    window. Each folder's scale (with where it came from, editable, recomputing the
    plan) and one row per image: include, thumbnail, name, kind, Tile/Prop, width in
    cells, solid. Then Create N prefabs.
  - **`prefab.create`** (asset, name, tile, solid, cells; 0 cells = the pack scale) so
    the AI can make prefabs in a proposal. The review shows it as the prefab it will
    make; it runs on Apply, before anything in the same proposal that uses it.
  - Tests: `PackImportTest` (5: scale from tiles or set by hand, from the analysis,
    plan sizes and proportions, making prefabs and unique names, the AI command).

**6.4 — Autotiling**
- **Terrain sets**: ground tiles grouped by their terrains (grass/dirt), with each
  tile's corner signature (6.2).
- **Terrain brush**: paint "dirt" on grass (a road, a square) and the engine picks every
  cell's piece, edges, corners and ends, re-fitting the neighbours. The same goes for
  erasing.
- `terrain.paint` for the AI: it says which cells are road, the engine picks the pieces.
- Tests: the right piece for every corner pattern, neighbours re-fitted, missing
  pieces falling back gracefully.
- ✅ Built (build and tests at the end of the phase):
  - The edge analysis also records each tile's **middle** terrain (`center=`), the
    cell's own terrain, so a narrow path's tile counts as road although its border is
    mostly grass.
  - `editing/Autotile`:
    - `BuildTerrainSet`: every tile prefab whose texture has edges and a middle.
    - `TargetSignature`, one rule for paths and areas alike:
      - edges: the cell's terrain where the neighbour shares it;
      - corners: the cell's terrain only where all four cells do;
      - background cells: always plain, so roads stay as wide as painted.
    - `ChooseTile`: fewest mismatches, edges weighted double; the middle must match;
      quarter turns allowed (flat art), unturned preferred.
    - `PaintTerrain` paints and re-fits the neighbours, without changing tiles that
      already fit. `PathCells` lays grid-following roads of any width.
  - **Auto-fit** on the brush (toolbar, for analyzed ground tiles, on by default):
    painting lays that tile's terrain with fitted pieces. Erasing a fitted tile turns it
    back into the background and re-fits around it.
  - For the AI: **`terrain.paintPath`** (points, width) and **`terrain.paintRect`**, with
    terrain names from the edges. The prompt says to lay roads this way, never tile by
    tile, after filling the ground.
  - Tests: `AutotileTest` (5: the set, straights in both directions, ends, bends and
    areas, painting into a field and re-painting, path cells) and the centre in
    `TileEdgesTest`.

**6.5 — Settlement generator**
- Like the dungeon generator (3.4): the AI or you choose the size (hamlet, village or
  town, or a number of houses), the style and which assets fill each role. The engine
  lays out:
  - a main road and side roads, autotiled (6.4), with a square in the middle;
  - plots along the roads, with houses facing the road and kept apart;
  - a well in the square, fenced gardens (fence variants by direction), trees and
    bushes towards the edges;
  - banners and a watchtower at the entrances, barrels, carts and a campfire as
    decoration.
- Roles come from the analysis (6.2): house, well, fence (left–right / up–down),
  tree, bush, rock, decoration, landmark.
- `level.village` for the AI: one command, any size, still a reviewed proposal.
  It's also in the Level tab.
- Tests: connectivity (every house reachable by road), no overlaps, plots facing roads,
  every role used when available, size scaling, determinism by seed.
- ✅ Built (build and tests at the end of the phase):
  - `gameplay/VillageGen`, pure and seeded:
    - streets left–right (one per band of plots) and one cross street; the main
      street and the cross street leave the village at all four sides;
    - a square with a well where they cross, decorated at the corners;
    - plots on both sides of every street: a front yard on the road, the house, a
      two-row garden; houses go on the plots nearest the square first;
    - fences around the gardens (one fence between neighbours, not two), something
      by most doors, banners either side of each road out, one landmark by the west
      entrance, and trees (never touching), bushes and rocks in the green edge and
      empty plots;
    - the number of streets is chosen to keep the village roughly square, so it
      grows in both directions with the number of houses.
  - `editing/VillageBuild`:
    - `VillageRolesOf` reads the art's analysis (name, tags, kind, description; the
      prefab name when unanalyzed). Wall pieces count as fences; a fence drawn one way
      also stands in for the other until 6.6.
    - `FindVillageRoles` also picks the ground and road terrains (grass-like and
      dirt-like) and the house size from the house prefabs' widths.
    - `PlanVillage` plans without changing anything (for review); `BuildVillage`
      clears the area, fills the ground, paints streets and the square with autotiling
      (6.4), places everything (upright art mirrored for variety) and moves the player
      onto the main street.
  - **`level.village`** for the AI. Review shows the counts, which prefabs fill each
    role, what the pack lacks, and a map. The prompt says to use it for any settlement
    ("relatively large" is 15–25 houses).
  - **Level tab ▸ Village**, with a live preview.
  - After the first user test (2026-10-17): the village failed on a pack that had never
    been analyzed, and the AI then placed pieces by hand (huge houses on sand). Now:
    - ground tiles without analysis get their edges from their pixels
      (`ProjectAnalysis` / `ProjectTerrainSet`, used by the village and the terrain
      commands; the editor also saves them when a project opens);
    - every piece is sized for the village (`VillageRoles::cells`): houses fill a
      footprint of 3 cells when their art's size says nothing, other roles have
      ranges, and pieces of one role keep their relative sizes; upright pieces stand on
      the front of their footprint;
    - roles also come from prefab names (fence direction, "bushes"); props made as
      ground tiles are named in the review so they can be re-imported as props;
    - the prompt tells the AI not to build by hand when the generator fails.
  - Tests: `VillageGenTest` (8: determinism, house counts and growth, no overlaps or
    props on roads, connected streets and houses facing them, the whole kit used,
    bad parameters, roles from analysis, building from a small pack).

**6.6 — Rotation and facing** *(from Later)*
- Like resizing: Shift+wheel, R and Shift+R turn the brush (and its preview) while one
  is armed, else the selection. Flat images turn a quarter; upright ones mirror.
- For the AI, the same rules: quarter turns for flat images, mirroring for upright ones
  (a proper `entity.mirror`, not field tricks), and the artist's variant for directional
  props.
- Tests: rotation and mirroring as overrides, the brush preview, the AI operations.
- ✅ Built (build and tests at the end of the phase):
  - `editing/SceneTools`: `TurnEntity` (flat: quarter turns either way; upright:
    mirrored on odd turns), `MirrorEntity` and `IsMirrored`, all through the edit path,
    so prefab copies keep them as overrides.
  - Editor: **R** turns, **Shift+R** turns back, **Shift+wheel** turns either way, the
    brush (and its see-through preview) while one is armed, else the selection; both
    keys can be rebound. Upright brushes toggle between "as drawn" and "mirrored"; the
    toolbar button shows which (↻ 90° or ⇋ mirrored).
  - For the AI: **`entity.turn`** and **`entity.mirror`**; the prompt says fences and
    walls running the other way use the pack's piece drawn that way. The village
    generator mirrors upright art for variety through the same `MirrorEntity`.
  - Tests: `SceneToolsTest.TurningFlatImagesAndMirroringUprightOnes`.

**6.7 — The AI checks its work** *(optional)*
- After a big change (a generated village, a pack import), the editor renders the area
  and a vision model compares it with the request, suggesting fixes as a follow-up
  proposal.

**6.8 — Panels anywhere** *(added 2026-10-18, before Phase 7)*
- Every panel can be dragged by its tab to anywhere in the editor:
  - **onto another panel**, to make tabs (e.g. AI as a tab next to the viewport);
  - **onto a side of a panel**, to split it;
  - **onto a window edge**, to dock along the whole edge (left, top, right or bottom).
- The **viewport is a panel** like the others.
- ✅ Built:
  - The layout is a tree (`ui/src/docktree.ts`) of splits whose leaves are tab groups.
    It is pure and tested (13 tests: tabs, sides, edges, same-direction joins,
    reordering, no-op drops, emptying, saved-tree checking).
  - Panels are mounted once in a single layer and laid over their group's body, so
    moving never resets them (the AI conversation, drafts, scroll).
  - Dragging shows a marker where the panel will land and its name under the pointer.
    Escape cancels.
  - The native viewport window hides while a panel is dragged (the markers must show
    over it) and whenever its tab isn't in front.
  - The panel shortcuts (B, J, I, M): a panel behind another tab comes to the front, one
    in front hides, and a hidden one comes back to the group it left (else its usual
    edge). Hidden panels appear on a rail on the left.
  - The tab's right-click menu and View ▸ *panel*: dock on an edge, hide. View and the
    tab menu also have **Reset layout**.
  - The Scene panel lays out side by side when it's wide and stacked when it's tall.
  - The 5.6 layout is migrated on first launch.
  - Checked in headless Chrome: drags to tabs, edges and sides; Escape; reload keeps the
    layout; shortcuts.
  - After the first test (2026-10-18):
    - **One area per window edge.** Docking on an edge that already has panels puts
      the new one next to them, along the edge, instead of adding another strip. All
      four panels docked on top make one top row.
    - **Fixed:** with every panel as tabs of one group, the content disappeared. The
      root element was reused when the tree collapsed to a single group, and never
      re-measured.
    - **Saved per project:** File ▸ Save writes the layout to
      `<project>/editor-layout.json`, which is restored when the project opens. Moving
      a panel counts as an unsaved change. A project without a saved layout starts
      from the last layout used.
    - **Double-click a tab** to fill the window with that panel; double-click again
      to bring the others back (the tab's menu has *Maximize* too).
    - **The Assets panel is split in three** (dockable like the rest):
      - **Assets**: Prefabs and Assets, the files a scene is made from;
      - **Project** (P): Scenes, World and Story, the game's project-wide content;
      - **Generate** (K): Level and Art.
      They start as tabs in the bottom group. Layouts saved before the split get the
      new panels as tabs beside Assets. File ▸ Scenes… opens the Project panel on
      Scenes.

**6.9 — The story across scenes** *(added 2026-10-18, before Phase 7)*
- Quests, dialogue, schedules and the World Bible were already project-level (files in
  the project root; progress carried across gateways). Four gaps are closed:
  1. **Objectives can name a scene** (they count only there), and a new **reach**
     objective is done at a Marker or, with only a scene, on arriving there.
  2. **Schedule entries can name a scene.** While an NPC's day puts them in another
     scene, they're away (hidden) in this one; places are checked against that scene's
     markers.
  3. **World Bible locations name their scene** (`scene:`). The Scene panel shows each
     scene's place, and the AI context carries every scene's location and the open
     scene's entry in full. `world.write` takes an optional `scene`. Renaming a scene
     rewrites it, along with the quests and schedules that name it.
  4. **Saved games:** F6 saves and F9 loads (the scene, the player's place and health,
     the clock, flags and quest progress), in the game and in Play mode. Play ▸ Continue
     from save.
- ✅ Built, with:
  - command parameters that can be **optional** (in the engine, the schema and the AI
    service), so adding one keeps older proposals valid;
  - the editors: reach, a scene per objective and per part of the day, and a location's
    scene;
  - AI checks mirroring the engine;
  - `docs/systems/story.md` updated;
  - tests: `ProjectStoryTest` (8) and two AI validation tests.

**Phase 6 exit criteria:** from an unfamiliar pack, the AI imports everything at sensible
sizes, lays roads with the right pieces, and builds a village of the requested size that
uses the pack's houses, well, fences and decorations, on the grid the art was drawn for.

---

## Phase 7 — AI-Generated Building

**Status: design reviewed and approved (2026-10-18); 7.1 in progress.** Documents (the
authority for each subject is listed at the top of `place-schema.md`):
- `docs/ai/ai-building.md`: why, the research, and what each source contributes;
- `docs/places/place-schema.md`: **the procedure, its documents, the intermediate language and
  the constraints**;
- `docs/places/place-modules.md`: **the modules per build type**, with their quantity and
  quality fields;
- `docs/places/place-ontology.md`: **the place ontology**, a knowledge graph of modules,
  fields, vocabularies, roles, relations and rules that every step consults;
- `docs/places/layout-ir.md`: **PLL, the Place Layout Language**, the intermediate language
  (our own, aligned with GLDL's terms);
- `docs/places/place-assets.md`: **where each asset comes from**: the project, the AIGE
  library, the web, or the Art tool, by a fixed decision procedure, with licences;
- `docs/places/place-interaction.md`: **what the user sees** (every stage, the result card,
  "Why?") and **how each AI call is made** (thinking, then a strict answer, then
  validation).

**Agreed (2026-10-18):**
- **size:** very big, up to regions of several settlements;
- **confirmation:** the window opens for every build;
- **editing:** after the build, per part;
- **perspective:** top-down and ¾ / isometric art both, mixed where compatible;
- **assets:** per component in the window: auto, project, AIGE library, web or
  generate (`auto` follows `place-assets.md`);
- **model budget:** no limit.

**Goal:** a framework, not a recipe. Any build follows one structured procedure, and every
part of it is described by well-defined, constrained schemas:

```
Query → Interpret (Brief drafts) → Confirm (Brief) → Plan (Spec: quantity + quality per part)
      → Compile (Layout IR) → Solve & build → Verify → (edit any part → regenerate it)
```

**Why:** 6.5 testing showed:
- "large" meant 20–30 houses;
- fences were wrong (an isometric grid with top-down art, and pieces at cell centres);
- sizes were guessed;
- there was one layout recipe;
- nothing checked the result.

**Built on the research:**
- **Holodeck:** stages as modules, relations instead of coordinates, an anchor-first
  solver, asset retrieval with a size check;
- **SceneCraft:** n-ary relation graph with scoring functions; render-critique loop;
- **GLDL / Constraint Is All You Need:** fixed vs placed facilities, anchors, a penalty
  objective with clustering and sparsity terms, agent-tested reachability;
- **database-driven levels:** a module library authored offline, few AI calls at scale,
  essential vs adaptable repair;
- **Hierarchical WFC:** a weighted "consists-of" tree, semantic selection and regeneration;
- **Emilien and Vanegas:** interest maps, organic growth, parcel styles, persistence
  across edits;
- **Dormans / Unexplored:** mission → space, cycles, lock–key;
- **GameTileNet:** the affordance taxonomy;
- **ClarEval / Active Disambiguation:** batched questions, asked where interpretations
  disagree.

**7.1 — Ontology, schemas and validators (first; nothing else starts before it)**

**Progress (2026-10-18):** `engine/places/` (`aige_places`): the ontology loader and
consistency check, subgraph retrieval, the expression language (interval / three-valued
semantics), the Brief, Spec and PLL validators; the first library in `data/places/`
(72 shared and settlement modules, 13 module classes, vocabularies, roles, 26 relation
types, sourced design rules, tolerances); the **published ontology** in `ontology/`
(Turtle: hand-written OWL core, generated OWL + SKOS library and SHACL shapes, kept
current by a test); `tests/places_test.cpp`. Compile templates come with the compiler (7.6).

- The **place ontology** (`docs/places/place-ontology.md`):
  - classes, properties and rules in the engine's JSON graph format, loaded in memory,
    with subgraph retrieval for prompts;
  - export to OWL, SKOS and SHACL;
  - the module files are part of it (one source).
- The documents and schemas:
  - `aige.brief/1`, `aige.spec/1` (the plan tree with quantity and quality fields);
  - `aige.module/1` (a module's fields, slots, children, constraints, relations, compile
    rules and checks);
  - `aige.ir/1` (the Layout IR statements);
  - `aige.tolerance/1`;
  - the shared **vocabularies**;
  - the **scale tables** per build type.
- The **constraint expression language** (paths, aggregates, arithmetic, logic) with a
  parser and evaluator.
- Validators for each document:
  - field types and ranges;
  - structure (parents and children, cardinality, levels, references);
  - aggregate constraints;
  - IR references and feasibility pre-checks (areas fit their parents).
- The **first module library**: shared modules and the settlement build type, from
  `place-modules.md`, as module files with density maps and expansion defaults.
- Tests: valid and invalid documents of every kind; constraint evaluation; the example
  village Spec validating; a broken aggregate reported against the right node.

**7.2 — Big scenes (engine prerequisite)**

**Done (2026-10-18):**
- **Tile layers** (`gameplay/TileLayer.h`): a palette of tile kinds and, per 32×32
  chunk, one 16-bit index per cell; held in the registry's context, not as entities.
  Hand-painted tiles stay entities (the editor's tile tools are unchanged); builds
  write layers. Collision (`TileGrid`) reads the layers in place: a painted tile wins,
  then the top layer.
- **Drawing:** `CollectLayerSprites` visits only the chunks near the view and hands
  the visible cells to `Renderer2D::Render`, merged with entity sprites by depth.
- **Regions on disk** (`assets/TileLayerIO.h`): `scenes/<name>.tiles/` holds
  `layers.json` (names, order, palettes) and one run-length-encoded file per
  128×128-cell region. Saving rewrites only changed regions. Duplicating, deleting
  and moving a scene carry its layers; travel and Play / Stop keep them right.
- **Command undo** (`EditHistory::PushCommand`): `TileLayerCommand` (changed cells),
  `PlaceEntitiesCommand` (a build's entities, found by `PlaceNode`) and
  `CompoundCommand`, mixed with whole-scene snapshots, which skip the layers.
- **`PlaceNode {place, node, statement}`** on built entities.
- Tests: `tests/big_scene_test.cpp`. A 400×400 place (160k ground cells, a road grid,
  6000 built objects) in Release: build 85 ms, save 214 ms, load 105 ms, undo 153 ms,
  redo 97 ms, one frame's culling 0.2 ms; culling 100k entity sprites 1.5 ms.

Not in this milestone: streaming regions in and out by camera (everything loads at
once), and editing layer cells with the editor's tile tools (7.8, editing after the
build).
- Tiles stored as **chunked layers**, not one entity per tile.
- **Command-based undo** for bulk operations.
- Scenes stored and loaded in **regions**.
- Rendering and culling fast at 100k+ sprites.
- `PlaceNode {place, node, statement}` on built entities.
- Tests: a 400×400 place builds, saves, loads and undoes within set time budgets.

**7.3 — Asset catalog v2**

**Done (2026-10-18):**
- **From the pixels** (`assets/ArtAnalysis.h`):
  - the solid bounds, with soft shadows left out;
  - the **base** (the solid columns across the lowest rows) and the **anchor** (its
    middle, at the lowest solid row);
  - the tile type: texture (complete and seamless), complete or partial;
  - the silhouette (square tile, diamond tile, upright, blob), which gives a
    perspective cue;
  - symmetry, the main axis, the palette, and where a piece touches its borders.
- **Footprint:** upright art covers its base width (depth about half of it); flat art
  covers its solid bounds; all at the pack's pixels per cell.
- **Sheets cut into whole objects:** pieces closer than a gap join; on a tileset,
  tiles join where their content runs across a border.
- **The catalog** (`assets/Catalog.h`), kept in each asset's `.meta`:
  - roles (from the vision model, else inferred from words through the ontology);
  - concepts, affordance, perspective, flat, layer, footprint, anchor, sprite size,
    facings, variant group, named anchors (door…), passability, entering;
  - **piece sets** and their grid, from where the pieces join;
  - provenance;
  - **problems**: no role, a perspective that doesn't lie on the project's grid,
    pieces made for the other grid, an incomplete piece set, a measured size far
    from the described one, an unknown licence.
- **Slot retrieval** (`RetrieveForSlot`):
  - hard filters: role (or a narrower one), concept (or a narrower one), affordance,
    perspective vs grid, footprint within the range after scaling by up to a third,
    layer, licence, a usable piece set;
  - then the fit score of `place-assets.md` §4 (semantic, style, size, quality),
    graded good or usable, with every rejection's reason kept.
- **Vision v2** (the AI service): role chosen from the ontology's, perspective, named
  anchors and entering. Sheets are described object by object, and small pixel art is
  enlarged before the model sees it.
- **Review:** the Assets panel's **Catalog** tab. Each entry shows its art with the base
  and anchor marked, the fields (editable), its problems and its piece set.
- Tests: `tests/catalog_test.cpp` (footprints, anchors and shadows; tile types;
  segmentation; piece kinds; piece sets and grids; perspective mismatches;
  retrieval) and `ai_service/tests/test_vision.py`.
- Per asset:
  - affordance (GameTileNet's five);
  - tile type (complete, partial, texture);
  - **footprint** from the alpha base, and **anchor**;
  - sprite size from the footprint;
  - **perspective**;
  - facing and variants;
  - palette and layer;
  - passability and accessibility;
  - **piece sets** for walls, fences and hedges.
- Tilesheets are segmented into whole objects first; images are upscaled before the
  vision model sees them.
- **Slot retrieval:** hard filters (role, affordance, perspective compatible with the
  grid, footprint fits), then ranking.
- Catalog review in the Assets panel.
- Tests: footprints and anchors on sample art; piece sets; perspective mismatches flagged.

**7.4 — Interpret and Confirm**

**Done (2026-10-18):**
- **The AI-call protocol** (`ai_service/app/places.py`): think in free text, then answer
  in the strict schema. Native (a reasoning model's own thinking), two-pass (the
  default) or one-pass, chosen per provider. Answers wrapped in prose are still read.
  One repair round lists the exact violations (`/places/repair-brief`).
- **Interpret** (`/places/interpret`): three drafts from three readings (literal,
  typical, generous) drawn up in parallel, each with confidences, assumptions,
  questions and a rationale.
- **In the engine** (`places/Brief.h`):
  - what can be built, for the prompt;
  - recognising a build request (anything else stays a proposal);
  - merging drafts: the majority value per field, a tie going to the typical
    reading; a component is on by majority; disputed and guessed fields are marked,
    with the other readings' values;
  - completing a Brief from the scale table (the numbers win over the class word)
    and from the project's earlier answers (`places/defaults.json`).
- **The window** (`editing/BuildWindow.h`), generated from the build type's Brief
  schema:
  - round 1: size with numbers, layout with sketches, walls, features, style, and
    every component with on, amount, thumbnails and source;
  - round 2: the asked module fields, and the **asset plan**. Each slot's needs come
    from what the components contain; the project's candidates are found by catalog
    retrieval, and the proposed source has a reason.
- **The build** (`editing/PlaceBuild.h`): request → drafts → validation → repair once
  → merge → ① Request → ② Assets → the confirmed Brief in `places/<id>/brief.json`.
  Every call (its thinking too) goes to `places/<id>/log.jsonl`.
- **The Build panel**: a dockable panel with the stepper. It opens when a build starts
  and docks at the bottom by default. The AI panel shows a compact **build card**,
  with "Open build panel" and "Ask as a change instead".
- Tests: `tests/brief_test.cpp`, `ai_service/tests/test_places.py`,
  `ui/tests/buildwindow.test.ts`.

- **Layout is a mix** of street patterns (`{"mix": {"grid": 0.4, "organic": 0.6}}`, or one
  pattern by name). The window toggles patterns and sets their shares, and
  `shape.description` says where each goes. The plan (7.5) gives each ward one pattern,
  by the design rules. Any `mix` field accepts a single value as a mix of one.

Moved to 7.5: the plan preview needs the plan.
- The AI drafts **several Briefs** for the query. Fields where the drafts disagree, or
  with low confidence, are highlighted.
- The **Build panel** (`place-interaction.md` A2): a new dockable panel (bottom by
  default) with the stepper Request · Assets · Plan · Build · Result. It opens when a
  building query starts. The AI panel gets a compact build card that links to it; small,
  non-build AI requests keep the existing proposal card.
- The **confirmation window** (the Request and Assets steps) is generated from the
  build type's Brief schema:
  - round 1: size with numbers, layout with sketches, components with thumbnails and a
    **source** (auto, project, library, web, generate), style, location;
  - round 2: densities, asked module fields, and the **asset plan** (needs, proposed
    sources, alternatives).
- **Every new build** shows it, as does any regeneration that changes Brief fields.
  Answers become project defaults.
- The AI-call protocol (`place-interaction.md` Part B): **think in free text, then
  answer in the strict schema** (native reasoning, two-pass or one-pass, by provider),
  then validate, with one repair round. Everything is logged per place.
- The **plan preview** (the plan view over the viewport) for large places, before
  building.
- Tests: Briefs from vague and detailed queries; disagreement highlighting; window
  output validating as a Brief.

**7.5 — Plan (the Spec)**

**Done (2026-10-18):**
- **The steps** (`places/Planner.h`, run by `editing/PlaceBuild`):
  1. **Root (AI):** the zones, networks and ground, with their fields, relations and a
     rationale each. The Brief's numbers and choices win. Unknown or disabled modules
     are dropped, and requested components the answer left out are added (walls when
     walled, the ground and the main road the root needs). Ids are made unique, and
     relations follow them.
  2. **A zone step per zone (AI, in parallel):** its parcels as templates (`repeat`,
     `share`), told about the other zones for balance.
  3. **Expansion rules (no AI):**
     - each node's required children;
     - each parcel's typical children, two levels down, by their rule weights, with
       the Brief's density for a component (common: 60% of plots) and its settings
       (a flower garden has no vegetable beds);
     - dressing is left to the compile step, and children counted by a field are left
       to the compiler.
  4. **Rebalance (no AI):** building-parcel repeats are scaled to the Brief's range;
     the zones' plots and the root's buildings follow.
- **Checks:** each AI step's nodes are validated (Draft), and a broken node goes back
  once with its violations (`/places/repair-node`). The final Spec is validated (Final)
  and saved to `places/<id>/spec.json`. Every call is logged.
- **③ Plan:**
  - the headline (buildings in zones, area);
  - a table per part (buildings, area, what it holds, its character, its rationale);
  - a **sketch**: a squarified treemap of the zones' areas, sizes only;
  - the site and the sketch outlined over the viewport;
  - problems, then Back or **Approve plan**.
- Tests: `tests/plan_test.cpp` (all five settlement scales validate; buildings add up;
  expansion rules; the root answer checked and completed; the sketch; the whole flow
  with a repair) and `test_places.py`.
- The AI expands the Brief level by level: the root, then zones, then per-zone totals and
  qualities. Each step's output is validated against the module schemas and constraints,
  and failures go back with the violated expression.
- **Lower levels come from the module's expansion rules** (the encyclopedia), so a city
  takes tens of AI calls, not thousands.
- `repeat` and `share` keep large Specs compact.
- The **plan preview** (the Build panel's ③ Plan step and a plan view over the
  viewport) for large places, before building (moved here from 7.4).
- Tests: Specs for every settlement scale validate; aggregates hold (Σ houses =
  buildings); repair of a broken node.

**7.6 — Compile and solve (the generators)**

**Done (2026-10-18), for the settlement build type:**
- **Compile** (`places/Compile.h`), Spec → PLL:
  - every `repeat` is written out as instances, and each `share` child goes to exactly
    that share of its parent's instances, by a seeded hash;
  - templates by level and class (module files may override): the site with its
    ground, entrances, main road (a crossroads from town size), river, walls with
    gates where roads cross, and the partition into zones by their site preferences;
  - zones: a street network in the zone's street style (or the layout mix), and
    parcels by frontage subdivision. A zone too small for its parcels is enlarged to
    about 1.9 times their area;
  - parcels: the primary object at the front, facing the street, and the features as
    regions related to it;
  - features: fences and hedges along edges, beds and fields in rows, orchards and
    graves on a grid, plants scattered with the plan's mixes;
  - relations from the modules; checks (parcels placed, fences closed, doors
    reachable, footprint density).
  - The zones' relations come from the plan's root step, which already reasoned about
    them, so there is no separate AI call here.
- **Solve** (`places/Solver.h`): a cell grid, top-down through the region tree, steps
  b–i inside each region, then fill and checks. Per step:
  - **paths:** least-cost A\* with road reuse; rivers meander;
  - **networks:** grid, radial, linear, and organic (a main street along the zone,
    then lanes from the cells furthest from any street);
  - **partition:** seeds by interest (Emilien et al.) and compact growth that reaches
    across roads;
  - **subdivide:** frontage plots packed along streets;
  - **place:** primaries at their setback; others by relation scores;
  - **line:** cell edges, gates at gaps, no fence across a street;
  - **rows, grid, scatter:** Poisson-disk with density;
  - **fill:** terrain per cell, roads by class, water, bridges;
  - **checks:** count, closed, reachable, footprint-density.

  Every statement has its own seeded random stream. The result is written to
  `places/<id>/layout.solved.json`; Approve plan runs compile and solve, and the
  viewport shows the solved buildings.
- **Not yet:** floorplan, graph and connect (interiors and dungeons, 7.10), annealing
  over the soft objective (placement is greedy best-score), and the 6.5 village
  command rebuilt on modules (7.7, together with putting the result into the scene).
- Tests: `tests/solve_test.cpp`:
  - a village compiles to a valid layout and solves within the hard rules (no
    overlaps, regions inside their parents, every plot on a street and its house
    facing it, every door reachable, fences and wall closed with gates);
  - the same seed gives the same layout;
  - paths join existing roads, zones follow their preferences, scatter keeps density
    and spacing;
  - hamlet, village and large village place at least 80% of their buildings.
- Each module's **compile template** turns Spec nodes into **PLL**. The AI adds
  relations at the upper levels.
- The solver goes **top-down through the region tree**, in a fixed order of steps inside
  each region (primary objects before sub-regions; `layout-ir.md` §7):
  - `partition` (relations + interest maps with vetoes);
  - `path` and `network` (least-cost with road reuse; grid, radial, organic, growth);
  - `subdivide` (skeleton, split, mixed), `floorplan`, `graph`, `connect` (lock–key);
  - `place` (anchor first, large first, then annealing over scored relations, with
    clustering and sparsity terms);
  - `line` (edge-joined piece sets);
  - `rows`, `scatter` (Poisson disk, density maps) and `fill` (autotiling).
- The 6.5 village becomes a set of modules on this framework.
- Tests: each solver on fixtures; hard rules never broken; seeded determinism.

**7.7 — Build, verify, repair**

**Done (2026-10-20), for the settlement build type:**
- **The plan stage, made reliable first** (`editing/PlaceBuild.h`): planning ends laid
  out, so ③ Plan shows the real map; fitting rounds give a zone that couldn't place its
  parcels more land; Verify shows asked / planned / built per zone and component; the AI
  reviews the map; Revise takes a request in words, shows the AI where everything is
  (`places/Where.h`: areas, banks, directions, a text map), checks the spatial facts it
  asked for on the new layout, keeps the zones it doesn't name in place, and takes parts
  out by editing the layout itself (nothing else moves). Retries and fallbacks keep a
  build from stopping for the AI's mistakes; builds reopen across sessions. The model is
  a choice (Mistral or Claude).
- **Build** (`editing/PlaceScene.h`):
  - the asset plan: each need gets the catalog's best fit, made a prefab if needed, else
    a placeholder (a plain shape per role), so the place is whole;
  - the ground in a tile layer of its own through the autotiler; buildings, props,
    decals and fence pieces as prefab instances tagged with PlaceNode; a building blocks
    its footprint (Obstacle left/right/back);
  - beside what the scene has (never over it); a rebuild replaces only the place; one
    undo step.
- **Verify and repair** in the scene: every object there at its cell, the ground whole,
  every door reachable over the scene's real collision, no two solid things on a cell;
  a missing object gets a placeholder, an adaptable thing in a door's way is removed.
- **Visual critique of the rendered part**: `editing/ScenePreview.h` draws the place as
  the renderer does, on the CPU; the AI looks at it (and at the plan's map before).
- **Verified examples**: a build that passes its checks is kept in
  `places/examples.json`; planning a place of the same type and size shows the planner
  up to two of them.
- ④ Build and ⑤ Result: the rendered place, the checks before and after repair, the
  repairs, assets by source with licences and why a placeholder, the AI's critique with
  "Revise with these", next steps. **Why?** per part: its rationale, how it's placed
  (relations, those asked for marked), what the fixes did to it.
- Tests: `tests/place_scene_test.cpp` (a village built beside existing content, undo and
  rebuild, injected failures found and repaired, the project's art used), `tests/
  build_robust_test.cpp`, `tests/where_test.cpp`, `tests/live_plan_test.cpp` (opt-in,
  both models).
- **Not yet:** progress and resume (a village builds in well under a second; a city
  will need it), re-solving one node as a repair (a revision does it for zones).

The plan as first written:
- Bulk commands per node, with progress and resume; nodes are independent where
  possible.
- **Verification** after each level:
  - hard rules (overlap, terrain, **reachability**, accessibility, continuity, autotile,
    perspective);
  - module checks;
  - **conformity** against the Spec within tolerances (density bands included);
  - **visual critique** of the rendered part *and* of the plan view.
- **Repair:** move or remove *adaptable* slots, re-solve the node, or re-plan it with the
  reason. Then report.
- Approved builds join the **verified examples** used in prompts.
- The Build panel's **Build** and **Result** steps: progress, then asked vs built,
  assets by source with licences, checks, repairs and next steps. The Result stays as
  the place's page. **"Why?"** per part (`place-interaction.md` Part A).
- Tests: injected failures are repaired; reports are accurate.

**7.8 — Edit after the build**

**Done (2026-10-20):**
- **A partial solve** (`places/Solver.h`, `SolveOptions::previous/resolve/locked`): the
  previous layout is restored (regions, objects, lines, paths, roads and water), only the
  statements of the parts to redo run, and locked parts stay even inside them (a locked
  instance keeps its lot when its ward is re-cut).
- **Click a part** in the viewport: the Build panel shows it (its place in the tree, its
  fields as editors, whether it's locked). Changing a field lays that part out again
  with everything else as it was (`PlaceBuild::EditNode`); "Lay out again" redoes its
  subtree; each edit is a revision Undo can take back.
- **Hand edits lock**: moving or changing a built entity locks its part; a locked part is
  kept by the solver and its entities by the scene build, so the edit survives the
  parent being laid out or built again.
- **The module brush**: pick a module the zones can hold and drag an area in the
  viewport; a new part goes where it was drawn (`at`), and only it is laid out.
- **The scene follows**: an edit replaces only its parts' entities and the ground cells
  that changed (`PlaceSceneOptions::only/locked`), as one undo step.
- **Chat edits** in the AI panel that name a part of the open place go to it as a
  revision.
- Tests: `tests/solve_test.cpp` (a partial solve redoes only its subtree, a lock holds),
  `tests/build_robust_test.cpp` (an edit changes only its part, a lock survives the
  ward, the brush paints where drawn, chat routing), `tests/place_scene_test.cpp` (an
  edit replaces only its parts, a hand-moved house stays).

The plan as first written:
- Click any part to select its node (garden, plot, ward). The Inspector shows its Spec
  fields; changing one recompiles and regenerates that subtree, with siblings `fixed`.
- Hand edits lock a node.
- Regenerating a parent **matches** children to their previous selves, so edits and
  locks survive.
- A **module brush** paints a new node of any module over an area.
- Chat edits are routed to the right node.
- Tests: an edit regenerates only its subtree; locks and customisations survive a
  parent's regeneration.

**7.9 — Asset sourcing** (`place-assets.md`)

**Done (2026-10-20):**
- **The decision procedure** (`assets/Sourcing.h`): each need's importance (critical,
  normal, optional) and whether it's style-critical; the steps A1-A7 and rule G under
  each policy (auto, project, library, web, generate), with what was tried so the
  procedure moves on; the user's per-need choices (exclude, a source) from
  `places/<id>/assets.json`, which the build writes with its decisions.
- **Licences**: CC0, CC-BY and OGA-BY by default, CC-BY-SA when the project allows
  share-alike (`places/sourcing.json`, switches in ② Assets); unknown or disallowed
  licences are never used; a hard check in Verify; `CREDITS.md` kept in a section of its
  own (the user's text stays) and copied to the top of an exported game.
- **Sources**: the project catalog; the AIGE library interface (`library/catalog.json`,
  nothing until it exists); **the web tool** (`ai_service/app/websource.py`, off
  unless the project turns it on): searches OpenGameArt, reads each result's licence and
  author from its page, downloads PNGs only (size and type checked) into the need's own
  folder, and rejects what it can't read; **the Art tool** with the need and the view,
  then the critic (the vision model describes it; a mismatch is made again with the
  reasons, 3 tries at most). Found and made art gets its provenance in its `.meta`.
- **④ Build** sources first: the needs to make or fetch go out, the new assets are
  described and catalogued, and the build goes on with them (placeholders meanwhile,
  or without the AI). Sheet objects (a tileset's cut-outs) become prefabs of their part
  of the image; a piece set with one straight piece fences both ways (mirrored).
- Tests: `tests/sourcing_test.cpp` (importance, the decision table per policy,
  licence filtering, credits, the library, the sourcing step with the critic's retry
  and the web's provenance), `tests/place_scene_test.cpp` (the asset plan decides each
  need, exclusions are dropped not missing, one straight piece fences both ways),
  `ai_service/tests/test_websource.py`.
- **Not yet:** references (project images) sent to the image model, recolouring a usable
  candidate to the palette, generating a piece set's missing pieces as images.

The plan as first written:
- The **asset plan**: needs from the Brief's components and their slots, merged, with an
  importance and a style-critical flag.
- **Fit**: hard filters (role and concept, perspective, footprint, layer, licence, piece
  completeness), then a weighted score (semantic, style profile, size, quality) with
  good and usable thresholds.
- **The decision procedure** per policy:
  - `auto`: the project, then the library, then generation for style-critical needs, the
    web for others, then generation, then the best usable candidate, then dropping or a
    placeholder;
  - `project`, `library`, `web` and `generate` narrow that order.
- **Sources:**
  - the **project** catalog;
  - the **AIGE library** interface (returns nothing until the library exists);
  - the **web tool**: search approved sites, download into isolation, read the licence,
    segment, analyse;
  - the **Art tool**: generation with the style profile and references; missing pieces
    of a set generated from an existing piece; critic-checked, 3 attempts.
- **Licences and provenance** in `.meta`; an allowed-licence hard rule; `CREDITS.md`
  written and exported.
- Tests: the decision table, licence filtering, piece-set completion, `assets.json`
  round-trip.

**7.10 — More build types**
- Region, castle, interior (multi-floor), dungeon and cave, farmland, wilderness and
  camp, each with its module set from `place-modules.md`, on the same procedure.

**Done (2026-10-20):**
- **Seven build types more** in `data/places/buildtypes.json`, about 75 new modules
  (written by a generator from `place-modules.md` §3–§8), with their roles, classes
  (Room, Level, FurnitureGroup, PointOfInterest, Site, Fortification) and vocabularies
  (dungeon themes and goals, connectors, points of interest, camp and castle kinds,
  building kinds, floors, enemies, rarity, traps).
- **Each counts in its own unit** (`unit`: field, class, label): buildings, farm
  buildings, points of interest, tents, sites, rooms. The Brief's `scale.buildings`
  counts it; the plan, the rebalancing, Verify, the map's title and the panel use it.
  Each type has `words` that name it ("cave", "crypt", "fortress") and a `rank` for a
  request naming several kinds of place ("a kingdom with towns and castles" is a
  region). The settlement stays the default.
- **The same procedure**: Brief → plan → layout → Verify → map → scene, with and
  without the AI. The offline fallbacks plan every type (zones from the root's rules,
  levels and floors as many as their fields say, an interior's rooms from a programme
  by building kind: an inn's tavern hall and kitchen below, guest rooms above).
- **Layouts** (compile hints on the modules, `compile`):
  - **site** (settlement, farmland, camp, castle, region, wilderness): zones partitioned
    as before. Zones may scatter their parcels apart (`placement: scattered`), joined by
    trails or roads (`connect`: a tree by Prim, each leg A*). Background plants by a
    field (a biome's trees). Ground by a field (a swamp's mud). A castle's enclosure:
    a moat (`flood`) with a bridge where the road crosses, the curtain wall with its
    gate, towers at the corners, the gatehouse at the road.
  - **dungeon**: levels side by side; `rooms` places rooms apart, joins them by a
    spanning tree of corridors from the entrance with a few loops, secret rooms as
    leaves; locks on the way to the goal with each key before its lock; caves as
    smoothed blobs with winding tunnels; rock everywhere else; stairs link the levels.
  - **interior**: floors side by side; `floorplan` cuts each into rooms along a hall
    (walls on cell edges, a door per room, the front door, windows, stairs linked to the
    floor below); furniture groups place their anchor (centre, wall, far wall, corner,
    facing the door) and members near it.
- **Room rules**: nothing solid stands in a doorway or a window, and nothing solid cuts a
  room's floor off from its doors (checked as each thing is placed).
- **Checks**: completable (start to goal, collecting keys), rooms-reachable,
  groups-reachable, doors-free, beside count, closed and reachable.
- **A region's sites** are listed in the panel, each built as a place of its own where
  the region put it ("Build this site").
- **City scale**: the lanes' search stays in its ward's window with a distance field
  updated lane by lane (69 s to under 1 s for a city's networks). Statements are found by
  id. The share children are ranked once. Big zones are split into wards of about 90
  plots. Streets are spaced by the depth most lots have. The fitting rounds keep the
  first round's site and zone seeds, and may run to six while they improve. A walled
  city from its request: about 1,600 buildings in about 30 s.
- **Responsive**: the build's heavy steps (planning, laying out, partial solves) run off
  the editor's UI thread, one at a time; the panel shows it's working; the build's AI
  answers wait until the job is done.
- Tests: `tests/buildtypes_test.cpp`: every type loads; a request names its type; each
  runs offline to a layout whose checks pass; a dungeon's keys come before their locks,
  across levels too; a cave's chambers are ragged; an inn has rooms, doors, stairs,
  windows and furniture that can be reached; a castle has its moat, bridge, wall, gate,
  towers and gatehouse; a region's sites are joined by roads; a walled city.

**Phase 7 exit (2026-10-20):**
- A query for a town or city opens the confirmation window with the scale in numbers
  and gives a validated Spec and IR. **Met.**
- It builds hundreds of buildings in wards (about 1,600 for a city), with fitted roads
  and walls with gates. The props are the project's, or placeholders and generated art
  (7.9). **Met.**
- Every part passes its hard checks. Soft checks are shown in Verify; a city's
  residential wards come out sparser than their module's density band. **Met, with
  that noted.**
- Any part can be edited and regenerated alone (7.8). **Met.**
- The editor stays responsive: laying out runs off the UI thread. **Met.**

**Phase 7 exit criteria:**
- A query for a town or city opens the confirmation window with the scale in numbers,
  then produces a validated Spec and IR.
- It builds hundreds of buildings in wards, with fitted roads, walls with gates, and the
  pack's (or generated) props at the specified quantities and qualities.
- Every part passes its checks.
- Any part can be edited and regenerated alone.
- The editor stays responsive throughout.

---

## Phase 8 — Fixes from using the Build panel

Added 2026-10-20 from testing. The fixes made before it (after 7.10) are in `fixes.md`.

### Milestones

**8.1 The editor's look**
- Preferences: its sections in a list on the left, the chosen section on the right.
- Select boxes styled the same across the app (they look like the OS's now).
- A shortcut to maximise the focused panel to the whole window and to bring the layout
  back (what double-clicking a panel's title does).
- A shortcut for the Build panel.
- F1 toggles the shortcuts page (it opens it; it should close it too).
- A new project starts with nothing placed in the viewport, and the grid shown.

**8.2 The Build panel**
- An audit of each build: the conversation (kept already) and every action taken (each
  edit, revision, paint stroke, approval), with undo and redo across them.
- Undo for the brush: in the Layout tab and in the painting popup.
- "Revising the layout…" / "Revising the plan…" at the top of the page while a revision runs.
- "The layout could be better" goes from the Layout tab.
- A button to remove older builds (with a confirmation).
- An advanced view: the build's PLL document, the solved layout, the Spec, the log.

**8.3 The built place: realistic, with nothing bare**
- Empty land, inside zones and outside them, is dressed: grass and its variants, bushes,
  trees, small hills, flowers, rocks, by the terrain and the zone (a ward's yards, a
  field's edges, the wild outside), so the place reads as one landscape.
- The spaces between elements handled well: verges along roads, yards around houses,
  no grid of identical tiles (variants mixed, seams hidden).
- One image isn't used for two different things (a sign and a doorstep shared a picture):
  a need takes art only of its own role.

Done when: a built village has no bare or tiled-looking land and reads as a place; the
Build panel's actions can be undone and redone; the listed UI fixes are in.

**Status (2026-10-21): built, to be tried.**
- 8.1: Preferences in two panes; one select-box style everywhere (`main.css`); F11 maximises
  the panel in use (or brings the layout back); U toggles the Build panel; F1 toggles the
  shortcuts; a new project's scene is empty, the grid on.
- 8.2: every Layout-tab action (paint, draw, a size, a kind of building) and every revision and
  edit kept in the build's history with its time, undone and redone exactly (each restores the
  saved layout, not a new solve); Undo/Redo by the map, in the painting popup and in Revise;
  "Revising the layout/plan…" at the top while one runs; "The layout could be better" gone;
  Remove older builds (asked first); Advanced: the PLL, the solved layout, the Spec, the Brief,
  the asset plan, the log. Not yet: Contents edits as undoable steps.
- 8.3: the `landscape` module (dressing): on land nothing covers, outside the zones (wild),
  inside them (yards) and along the main roads (verges), scattered trees, bushes, flowers,
  grass, rocks and mounds (a new role); made by the Art tool like any art, left out when it
  can't be. Generated ground tiles are made seamless (wrapped and blended). A slot of
  alternatives ("barrel|pot|bench|sign|lamp") gives each object one of them, so each kind
  gets its own art.

## Phase 9 — The ontology, editable

**9.1 The ontology panel** (from a context menu)
- Every module that can be built (village, interior, dungeon...), its fields, children,
  slots and relations, shown; the defaults read-only.
- Users add modules and properties within the ontology schema (`gbo-shapes.ttl`); the
  additions are validated by the same shapes.
- The AI works with the extended ontology (the contexts it's given are built from it).

**9.2 Per user, across projects**
- The user's extensions live in the user's folder, used by every project, and can be
  exported and imported (a file).

Done when: a user adds a module (a "watermill" parcel) with its fields, it appears in the
Build panel's choices, and a build places one.

**Status (2026-10-21): built, to be tried.** `places/OntologyEdit` (the panel's view of the
whole, saving with checks, export and import), `Ontology::ApplyExtensions` (a module's
`extends`, `extensions.json`), the user folder `%LOCALAPPDATA%\aige\ontology` loaded between
the engine's library and the project's; `OntologyDialog.vue` (File ▸ Ontology…, the Build
panel's right-click menu): build types and their components (more can be offered), modules
by level (yours marked), a built-in's fields and parts with "add a field" and "allow a part",
your own modules made and edited with a form (id, level, classes, where it goes, which builds
offer it, fields, slots), deleted (asked first). Tested (`ontology_edit_test.cpp`): a
watermill saved, found where it said, planned in a craftsmen's ward and placed by the solver;
built-ins and broken modules refused and not kept; fields added to a built-in; exported and
imported on another folder. Not yet: new roles and vocabularies (a module uses the existing ones).

## Phase 10 — The asset library

- In the user library: folders, made by the user, holding assets from the web, from the
  AIGE library, and generated ones.
- Every generated asset is kept (outside the project too), reachable across sessions, and
  exportable.
- Sharing: the user is asked whether to share what they generated with other users
  through the AIGE library (when the AIGE web service exists, it stores each user's shared
  art); the question, its timing and its consent are designed in `asset_management.md`.

Done when: assets can be organised in folders, generated art is never lost between
sessions, and the sharing choice is asked and kept.

**Status (2026-10-21): built, to be tried.** The user library (`%LOCALAPPDATA%\aige\library`,
`engine/assets/UserLibrary`) and the Assets panel's **Library** tab: folders, items into a
project, "Keep in my library" from the Assets tab, export and import (`.aige-library`
folders); generated art kept from every project on each asset refresh; the sharing question
asked in the Library tab the first time it holds generated art (offer by default, decide for
each, keep private), changeable any time, per item too. Designed in `asset_management.md`; the
upload waits for the AIGE service.

## Phase 11 — Review and polish

- A code review of the codebase, and security checks (the AI service, file access, web
  fetching, keys).
- CSS across the app: select boxes, input fields, spacings, layouts.

Done when: the review's findings are fixed or recorded, and the UI is consistent.

**Status (2026-10-21): done, to be tried.** `security_review.md`: the AI service answers only
the editor that started it (a per-session token, and localhost Host names only); the UI's file
actions can't reach outside the project (`Project::IsInside`: scene delete, folder rename and
remove); a path check in the vision model's batches; a second source rule against loops over a
temporary's element; recommendations recorded. CSS: one look for selects, text and number
fields, text areas, checkboxes, buttons and scrollbars across the app (`main.css`, at zero
specificity so panels' own rules still apply).

---

## Phases 12–24 — A complete engine, then the AI Planner

**Status: designed, for review (2026-10-21).** The goal is a finished product: after these
phases, real games can be made with AIGE, by hand or by the AI Planner, which designs a
whole game from a request. The gap analysis is `engine-gaps.md`; the order:

- **12–20 fill the engine's gaps** (records, events, game flow, saves, audio, items,
  character AI, quests v2, difficulty, game UI, animation and effects, abilities and magic,
  physics). The Planner can only generate what the engine can run, and every system's
  schema must exist first (the rule Phase 7 followed: schemas before generators).
- **21–23 are the AI Planner**: the story ontology, then designing a game, then building it.
- **24 is shipping quality.**

Phases 0–24 make **v1.0** of the product (`roadmap.md`): 2D isometric and top-down games
with AI. Later versions (v1.1–v2.0) add genres in the order games appeared, then early 3D,
then advanced graphics.

Design documents:

| Document | Subject |
|---|---|
| `engine-gaps.md` | what's missing for a finished game, by phase |
| `ai-planner.md` | the AI Planner: procedure, panel, multi-scene builds, quest binding, audit |
| `planner-schema.md` | the Game Brief (input schema) and the Planner's documents |
| `story-ontology.md` | GSO, the story ontology, and the story knowledge graph |
| `ai-packages.md` | character AI: packages, behaviour, perception, scripted AI |
| `difficulty.md` | difficulty levels, scaling, assists, the curve |
| `game-ui.md` | menus, windows, HUD, notifications, themes, the UI designer |
| `effects-animation.md` | sprite animation, particles, effects, lighting, cutscenes |
| `magic.md` | abilities, magic effects, spells |
| `physics.md` | kinematic layer, Box2D for rigid bodies, controllers |
| `game-types.md` | the kinds of game AIGE aims to support |

Each phase ends with its reflection registrations (Inspector, Lua, AI see everything new),
validators, tests, docs, and the AI panel able to propose changes to the new data.

## Phase 12 — Game foundations

**12.1 Records** — typed game data (`records/<type>/<id>.json`), schemas in the reflection
registry, references between records checked, a **Records panel** (browse, filter, edit
from the schema, duplicate, find references). Prefab-like inheritance (`base` + overrides).
**12.2 Events** — a typed event bus (`on_death`, `on_hit`, `on_pickup`, `on_enter`,
`on_interact`, `quest_stage`…); Lua `on(event, fn)`, `after(s, fn)`, coroutines with
`wait(s)`; per-frame module systems (`modules.md` "Not yet").
**12.3 Input actions** — named actions, bindings for keyboard, mouse and gamepad, contexts
(gameplay, menu, dialogue), rebinding at run time; `Key` stays as the device layer.
**12.4 Game flow** — `game.json` (title, genre, systems on, start scene, flow); title →
new/continue/load → play → pause → game over / victory → credits; scene transitions with
fades; Escape opens pause instead of quitting.
**12.5 Saves v2** — slots, autosave, metadata (place, time, thumbnail), versioned; **world
state persists** per scene (dead stay dead, opened stays open, moved stays moved).
**12.6 Audio v2** — music (streaming, loops, cross-fades by place and state), ambience, buses
with volumes, 2D positional sound, ducking.
**12.7 Camera v2, localisation, runtime settings** — bounds, dead zone, look-ahead, zoom;
string tables (`loc/<lang>.json`) used by every player-facing string; video, audio,
language and controls settings saved per player.

Done when: the demo-sized game has a title screen, rebindable controls with a gamepad,
music, three save slots that keep killed enemies dead, and items defined as records.

**Status (2026-10-21): built, to be tried** (`phase12-test-checklist.md`).
- 12.1 Records: record types in the reflection registry (`RecordTypeInfo`: values, references
  to other records, objects, lists, required, defaults), in the schema the AI gets;
  `engine/records` (`RecordDatabase`: load, validate, bases, references, write, rename with
  references rewritten, delete); the engine's `item` type; module record types; Lua
  `record()` / `records()`; the AI's `write_record` / `delete_record` (Diff Review, the
  service's mirror in `records.py`); the Project panel's **Records** tab; exported. `records.md`.
- 12.2 Events: `on`, `emit`, `after`, `every`, `cancel`, `start`, `wait` (handlers and timers
  run as coroutines); combat, story and talk events; per-frame module systems.
- 12.3 Input actions: every key, mouse and gamepad button (`platform/Input.h`), GLFW and
  XInput gamepads, `InputMap` (input.json, defaults, contexts: gameplay, dialogue, menu, any),
  the game reads actions; Project Settings ▸ Input to rebind; Lua `action_down`,
  `action_pressed`, `axis`.
- 12.4–12.5 `engine/game`: `GameFlow` (title, pause, load, save, settings, game over, victory,
  credits), `game.json`, `SaveGames` (quick, auto, six slots; v2 files; v1 still loads),
  world memory per scene (`SceneOrigin`), `GameRunner` (the player runs every game through it;
  fades between scenes, autosave). The editor's Play keeps its own loop with the same saves
  and world memory. `game-flow.md`.
- 12.6 Audio v2: buses, positional sounds, streamed music and ambience with cross-fades,
  ducking in dialogue; the `Music` component; combat music; Lua `play_sound_at`,
  `play_music`.
- 12.7 Camera rig (dead zone, look-ahead, level edges); localization (`loc/<lang>.json`,
  keys for dialogue, quests, records, menus); player settings (volumes, language,
  fullscreen, subtitles) on a Settings screen; player rebinding file.
- Not yet: a thumbnail per save slot (with the game UI, Phase 17); rebinding inside the game
  (Phase 17's settings screen); UI scale and vsync are stored but not applied yet.

## Phase 13 — Gameplay systems

**13.1 Attributes** (base + modifiers; `Stats` and `Health` become attributes, scripts keep
working). **13.2 Progression** (experience, levels, skills or perks, as records).
**13.3 Items, inventory, equipment** (item records, equip slots changing attributes and
visuals, containers, currency). **13.4 Loot and economy** (levelled loot tables, merchants,
prices). **13.5 Interactables** (doors with locks and keys, levers, chests, beds, signs,
readables; the dungeon builder's locks and keys now work in play). **13.6 Combat v2**
(ranged, projectiles, damage types and resistances, block and dodge, combat styles as
records). **13.7 Factions** (records with relations and dispositions; replaces the three
teams with a mapping) and **spawners/encounter tables**.

Done when: a dungeon built by the place builder plays with its locks and keys, chests with
loot, an archer enemy, a merchant, and a level-up.

**Status (2026-10-21): built, to be tried** (`phase13-test-checklist.md`, `gameplay-systems.md`).
- 13.1 `Attributes` (base values; effective = (base + adds) x multiplies from equipment, perks,
  timed modifiers) written to Stats, Health.max and `Resources` (mana, stamina, regeneration).
- 13.2 `Progression` (experience, levels, perk points, perks) from the progression record
  "default"; `Rewards` give the killer experience; `level_up`.
- 13.3 item records gain slot, damage, armour, modifiers, use; `Inventory` (text contents,
  gold, slots, weight), `Equipment`, `ItemPickup`, `Container`; the in-play inventory window.
- 13.4 loot records (weighted, nested, level bands, gold), drops where enemies fall;
  `Merchant` and the shop window (buy, sell, daily restock).
- 13.5 `Interactable` and `Door` (locks, keys, flags, readable text, events); the place
  builder's dungeon locks and keys now work in play.
- 13.6 damage types and resistances; `RangedAttack` and projectiles, enemy archers; block
  and dodge (stamina); the killer recorded for experience.
- 13.7 faction records and `FactionMember` (relations, dispositions); encounter records and
  `Spawner`.
- Engine record types: item (extended), loot, perk, progression, faction, encounter. Lua
  functions for all of it. Tests: `gameplay_systems_test.cpp`.
- Not yet: combat styles as records (with character AI, Phase 14); a perk screen and a
  character sheet (the game UI, Phase 17).

## Phase 14 — Character AI (`ai-packages.md`)

**14.1** The behaviour runtime (procedure trees with a blackboard). **14.2** Packages,
the stack, the template library; schedules read as packages (old projects work).
**14.3** Smart objects; **module anchors and actor slots** in `aige.module/1`, written as
markers and smart objects by the scene build. **14.4** Persistent actors across scenes.
**14.5** Perception, awareness, combat styles, reactions, barks. **14.6** Scripted
packages (Lua AI API). **14.7** Editor: Behaviour section with the day timeline, debug
overlay, simulate a day.

Done when: in a built village, the innkeeper works, eats and sleeps on their own; guards
patrol and challenge; a bandit ambushes and flees when hurt; a scripted ghost appears only
to the amulet's bearer; a killed NPC stays dead across scenes.

**Status (2026-10-21): built, to be tried** (`phase14-test-checklist.md`; design in `ai-packages.md`).
- 14.1 The procedure runtime (`gameplay/Behaviour`): composites (sequence, selector, random,
  parallel, loop), decorators (condition, cooldown, invert) and leaves (move-to with A*,
  wander, wait, idle, face, bark, use-object, talk-to-player, follow, flee, patrol, ambush,
  escort, set, emit, script), with a blackboard; procedure records for the project's own.
- 14.2 Package records (template, location, target, schedule, conditions, flags, data) and
  the `Behaviour` stack (first that holds runs; re-checked each second); 20 built-in templates.
- 14.3 `SmartObject` (type, affordances, slots, owner, durations); the place builder makes
  its beds, chairs, tables, counters, forges, wells, altars into smart objects.
- 14.4 `Actor` and the roster (`game/Actors`): one person across scenes, dead stays dead,
  a package in another scene takes the character there; kept in saves.
- 14.5 `Perception` (sight cone with line of sight, hearing of noises), awareness, combat
  behaviour by combat-style records (melee, ranged, kiting, fleeing by confidence, calling
  for help), reactions to being hurt, `Barks` shown at the bottom.
- 14.6 Scripted packages: a Lua `run()` coroutine with `move_to`, `bark`, `face`,
  `start_dialogue`, `push_package` and the rest.
- 14.7 F4 shows what every character is doing. Tests: `character_ai_test.cpp`.
- Not yet: the Behaviour section's day timeline in the Inspector and "simulate a day" (the
  packages are edited as records and as the component's text); module anchors and actor
  slots beyond the role-based smart objects (the AI Planner's needs, Phase 22).

## Phase 15 — Narrative runtime v2

**15.1** Quests v2: stages with several objectives, optional objectives, branches, failure,
**aliases** filled at start, rewards, more objective types (deliver, escort, use, defend,
examine, survive). Old quest files load as v2. **15.2** Story variables (typed) and the
condition language over attributes, items, factions, time and stages. **15.3** Dialogue v2:
the new conditions and effects, skill checks, more than four choices, portraits, barks,
voice lines; a node-graph editor in the Story panel. **15.4** Journal and objective markers.
**15.5** Cutscenes (`effects-animation.md` §8) — the timeline format and runtime first; the
timeline editor with Phase 18.

Done when: a branching quest with aliases, a reward and a failure path plays end to end,
with its journal, markers and a cutscene.

**Status (2026-10-21): built, to be tried** (`phase15-test-checklist.md`; formats in `story.md`).
- 15.1 Quests v2 (`gameplay/Story`): numbered stages with several objectives (any order,
  optional ones), branches by condition, `next`, stages that complete or fail the quest,
  aliases (`@thief`), rewards (experience, gold, items, reputation), `onFail`; objectives
  deliver, use, examine, have, survive, defend, escort; an escorted or defended character's
  death fails the quest. v1 files load as one stage per objective and save back as they were.
- 15.2 Story variables (`set_var`, `add_var`, compared in conditions) and world conditions
  answered by the game: items, gold, level, attributes, time, day, reputation; quest stage
  conditions; world effects (items, gold, experience, reputation, cutscenes) applied by the
  session.
- 15.3 Dialogue v2: up to nine choices, skill checks with a fail node ("[Speech 5]"),
  portraits (UI textures load by path now), voice lines; the Story panel edits stages, roles,
  rewards and the new conditions and effects, and shows a dialogue as a graph.
- 15.4 The journal window (J, `PlayWindows`), journal lines per stage, the objective marker;
  Project Settings ▸ Story turns them off.
- 15.5 Cutscenes (`gameplay/Cutscene`): `cutscenes/<id>.json` tracks (actor, dialogue, camera,
  audio, screen, logic), skipping to the end state, `wait_input`, letterbox and title cards,
  `CutsceneTrigger`, `play_cutscene` in effects and Lua; hosts follow the session's camera target.
- The AI service checks v2 quests and dialogue and its prompt describes them. Tests:
  `narrative_test.cpp`, `test_story_validation.py`.
- Not yet: the cutscene timeline editor and camera shake (Phase 18); bark lines in dialogue
  nodes beyond the Phase 14 `Barks`; aliases are filled by the author (the Planner fills
  them in Phase 22); the editor's own Play has no journal window (the game and the sandbox do).

## Phase 16 — Difficulty (`difficulty.md`)

**16.1** Scalable fields and categories; level records; effective values. **16.2** Per-entity
overrides in the Inspector. **16.3** Level scaling and zone bands. **16.4** Assists; adaptive
difficulty (optional). **16.5** Difficulty panel with previews, the combat preview and the
curve view; play at a level.

Done when: switching from Normal to Hard during play changes enemies as the panel predicts,
a boss's hand-set Hard health is kept, and the curve view flags a chapter that's too hard.

**Status (2026-10-21): built, to be tried** (`phase16-test-checklist.md`; "As built" in `difficulty.md`).
- 16.1 Fields marked scalable with a kind (exported in the schema); categories from kind and owner;
  `difficulty` and `difficulty-table` records, the built-in five levels; effective values with
  the reasons, applied at play start, scene loads, spawns and level changes from kept base values
  (`gameplay/Difficulty`).
- 16.2 `DifficultyOverrides` per entity; the Inspector's ⚖ opens the Difficulty tab's preview,
  where a value typed for one level is kept for that level only (undoable).
- 16.3 `Zone` bands and Project Settings ▸ Difficulty.scaling (none, fixed zones, scaled, hybrid)
  for spawners.
- 16.4 Assists in the player's settings (invincible, damage reduction, growing protection, game
  speed) and adaptive difficulty (±1 step, opt-in, in saves).
- 16.5 Project ▸ Difficulty tab: levels with steps and modifiers, the category table, the preview
  and combat preview of the selection, the curve over the scenes along their gateways, and
  "Play at" a level. The game asks for the level at New game and offers it (with the assists) in
  Settings; `difficulty()`, `difficulty_step()`, `set_difficulty()` in Lua; `{"difficulty": …}`
  conditions in dialogue and quests; the XP reward multiplier.
- AI panel: the context carries the levels and scaling, the prompt explains base values, level
  records, overrides and zones; overrides and difficulty conditions are checked. Tests:
  `difficulty_test.cpp`, `test_story_validation.py`.
- Not yet: timing, checkpoint and puzzle categories (their systems come later); record and
  settings modifier targets, `when` and `curve` ops; chapters from the story's critical path (Phase 21).

## Phase 17 — Game UI (`game-ui.md`)

**17.1** RmlUi on our RHI and input actions; layers and the screen stack. **17.2** Data models
from reflection and game state; UI actions as commands. **17.3** Built-in screens, windows,
HUD widgets and the six current overlays as documents. **17.4** Themes (built-in set, tokens,
9-slice frames, sounds) and the theme editor. **17.5** Notifications (channels, rules,
coalescing, history) and pop-ups. **17.6** Navigation and glyphs for gamepad, keyboard,
mouse. **17.7** The UI designer panel with checks.

Done when: a game has a themed title, pause, settings (with rebinding), save/load, inventory,
journal and map, a customised HUD, and notifications by rule; changing the theme restyles
all of it.

**Status (2026-10-22): built, to be tried** (`phase17-test-checklist.md`; "As built" in `game-ui.md`).
- 17.1 RmlUi 6.0 under `ui/Documents`: our fonts, our renderer (geometry to UI quads, clipped),
  documents from the project's `ui/` then `data/ui`, layers by document.
- 17.2 Models from game state (`menu`, `dialogue`, `hud`, `notes`, `ui`); a click calls the same
  `Choose()` the keys do; `action('…')` reaches Lua as `ui_action`.
- 17.3 The title, menus (pause, settings, save/load, difficulty, game over, victory, credits),
  windows (inventory, shop, journal), dialogue and HUD as documents; the old overlays stay as the
  fallback (Project Settings ▸ UI.documents).
- 17.4 Seven built-in themes as tokens; `ui/theme.json` picks one and overrides tokens.
- 17.5 Notification channels (toast, feed coalesced, banner, alert), rules from
  `ui/notifications.json`, history; Lua `notify`.
- 17.6 Prompts show the bound key or gamepad button for the device in use.
- 17.7 Project ▸ UI tab: screens with source, Customise, preview with sample data, new screens;
  theme; notification rules; Check all. Editor Play uses the documents.
- AI panel: `write_ui` (screens, styles, theme, manifest, rules) checked like the UI tab; the
  context lists the themes and the project's UI files. Tests: `game_ui_test.cpp`, `test_gameui.py`.
- Not yet: pop-ups, the map and other system screens, world-space UI, HUD adjustments by the
  player, mouse in the standalone player, a drag-and-drop designer, contrast checks. Rebinding in
  the game's settings screen comes with the settings tabs.

## Phase 18 — Animation and effects (`effects-animation.md`)

**18.1** Clips, directional sets, Aseprite and sheet import. **18.2** Animators bound to
components; templates. **18.3** Tweens and property tracks. **18.4** Particles (CPU, module
stack, presets). **18.5** Effects (cues) and juice (shake, hit-stop, flash, slow motion).
**18.6** 2D lighting, day–night, weather, post-processing. **18.7** Editors: animation,
particles, effects, cutscene timeline.

Done when: characters walk, attack, cast and die with animations in 8 directions; a fire
spell has its particles, light, sound and shake; a night falls with lit windows; a cutscene
plays from the timeline editor.

**Status (2026-10-22): built, to be tried** (`phase18-test-checklist.md`; "As built" in `effects-animation.md`).
- 18.1 Sheets (`*.anim.json`): grid or region frames, directions with mirroring, frame events;
  Aseprite JSON and grid import (`gameplay/Animation`).
- 18.2 Animators (built-in humanoid, quadruped, flyer, prop; `animator` records) bound to movement,
  facing, combat and death; animated corpses stay for their death clip.
- 18.3 Tweens over reflected numbers (Lua, cutscene keys).
- 18.4 CPU particles with presets and records, drawn y-sorted with the world (`gameplay/Effects`).
- 18.5 Effects (particles, sound, light, flash, shake, hit-stop, slow motion) from cues, animation
  events, cutscenes and Lua.
- 18.6 Lighting by sprite corner (ambient by the clock, `Light2D`, emissive sprites), weather,
  a vignette.
- 18.7 Project ▸ Effects tab: sheets playing, grid and Aseprite import, particles and effects to try
  and customise, the cutscene timeline with Play.
- AI panel: `write_anim`; fx in the context; checks on the names components use. Tests:
  `fx_test.cpp`, `test_fx.py`.
- Not yet: skeletal animation, additive particles and collisions, shadows, colour grading and
  bloom (render targets), an animator graph editor and an onion-skin clip editor.

## Phase 19 — Abilities and magic (`magic.md`)

**19.1** Magic effects (archetypes, stacking, tags; the four status effects migrated).
**19.2** Abilities (activation, delivery, costs, cooldowns), projectiles. **19.3** Items:
potions, scrolls, enchantments; perks as tag modifiers. **19.4** Cost and balance figures;
the ability editor with Try. **19.5** Character AI uses abilities by their hints.

Done when: a player learns spells from a book, casts a fire projectile, a heal and an aura,
drinks a potion, wields an enchanted sword; an enemy mage heals itself when hurt.

**Status (2026-10-22): built, to be tried** (`phase19-test-checklist.md`; "As built" in `magic.md`).
- 19.1 `magic-effect` records and presets (damage, heal, modify, over-time, absorb, state, summon,
  displace, dispel, light; formulas, stacking, tags, requires/blockedBy, cues); the four status
  effects have preset equivalents (`gameplay/Magic`).
- 19.2 `ability` records and presets: activation, delivery (self, touch, projectile, area, cone,
  aura), costs, cooldowns, cast time; ability projectiles through combat; keys 1-4 and K.
- 19.3 Books, scrolls, potions and enchantments (worn, on hit); perks scale abilities by tag.
- 19.4 The cost formula and figures; Project ▸ Magic tab with Try and Customise.
- 19.5 Characters cast by their abilities' AI hints.
- AI panel: magic in the context and prompt; checks on abilities, effects and what names them.
  Tests: `magic_test.cpp`, `test_magic.py`.
- Not yet: hold and charge, beams and homing, transforms, scripted effects, surfaces, a test arena.

## Phase 20 — Physics v2 (`physics.md`)

**20.1** Layers and masks, shapes, sensors, queries, swept movement. **20.2** Character
controllers: top-down (current, refactored) and grid-step. **20.3** Box2D v3 for dynamic
bodies and joints. **20.4** Editor gizmos, debug draw.

Moved to v1.2 (`roadmap.md`): side-view games (gravity, parallax, one-way and moving
platforms), the platformer and free-flight controllers, the jump preview.

Done when: projectiles and triggers use layers and sensors, a stack of crates topples, a
grid-step game moves tile by tile, and existing top-down games behave as before.

**Status (2026-10-22): built, to be tried** (`phase20-test-checklist.md`; "As built" in `physics.md`).
- 20.1 Collider shapes (box, circle), layers and masks; `Trigger` sensors with `trigger_enter` /
  `trigger_exit`; raycast, overlap and line-of-sight queries (C++ and Lua); swept, masked projectiles.
- 20.2 `CharacterController2D`: top-down (unchanged) or grid-step for the player.
- 20.3 Box2D v3: `RigidBody2D`, `Joint2D`, characters push bodies, sprites turn with their bodies;
  gravity in Project Settings ▸ Physics.
- 20.4 F5 debug view in play, View ▸ Physics shapes in the editor.
- AI panel: physics in the context and prompt; checks on layers, triggers, bodies and joints.
  Tests: `physics_test.cpp`, `test_physics.py`.
- Not yet: draggable collider gizmos, one-way tiles, characters as Box2D bodies against each other.

## Phase 21 — The story ontology (`story-ontology.md`)

**21.1** GSO in `data/story/` (classes, properties, vocabularies, templates) loaded by
`engine/story/`; published Turtle with alignments and a staleness test. **21.2** The story
knowledge graph (`planner/kg.json`): temporal facts, branches, world state at a scene,
subgraph retrieval; identity with the World Bible (`rel:` lines derived). **21.3** The rules
(§7) as checks, with messages and severities. **21.4** A viewer in the editor (a node's facts
over time, dependents, decisions). **21.5** The competency questions as tests.

Done when: a hand-written test story's graph answers every competency question, and each
rule catches its planted error.

**Status (2026-10-22): built, to be tried** (`phase21-test-checklist.md`; "As built" in `story-ontology.md`).
- 21.1 GSO in `data/story/` loaded by `engine/story/` (project extensions); `ontology/gso*.ttl`
  published with alignments and a staleness test.
- 21.2 `planner/kg.json`: temporal facts, branches, world state at a scene, the subgraph for a
  scene, timelines, dependents; World Bible `rel:` lines read in and written out.
- 21.3 The rules as checks with messages and severities (critic rules left to the AI).
- 21.4 Project ▸ Story graph viewer.
- 21.5 The competency questions and planted errors as tests.
- AI panel: the `story_graph` op, refused on new rule errors; context, prompt and checks.
  Tests: `story_graph_test.cpp`, `test_storygraph.py`.
- Not yet: embedding retrieval (§6.5, with the Planner), condition-based facts for open maps.

## Phase 22 — AI Planner I: designing the game (`ai-planner.md` §5.1–5.9)

**22.1** The Planner panel, documents, decision log, job engine (resumable), dependencies and
staleness, history and baseline. **22.2** ① Concept: the Game Brief, three readings, the
confirmation window with Difficulty and Visuals tabs, genre profiles, scope tables.
**22.3** ② Outline and the graph seeded. **22.4** ③ Story scene by scene with the main quest's
skeleton; act critic. **22.5** ④ Characters (biographies, classes, packages). **22.6** ⑤
Quests (main in full, side quests from motivations). **22.7** ⑥ Places (descriptions →
Briefs with story anchors and exits). **22.8** ⑦ Systems and ⑧ Visuals. **22.9** ⑨ Validate.
**22.10** Build panel: **pre-filled Briefs** and the **automatic driver**.

Done when: a request for a short ARPG gives a validated design (story, cast, quests, places
with Briefs, records, UI documents) whose every item answers Why?, survives closing the
editor mid-plan, and regenerates only stale parts after an edit.

**Status (2026-10-22): built, to be tried** (`phase22-test-checklist.md`; "As built" in `ai-planner.md`).
- 22.1 `engine/planner/`: documents with inputs and staleness, decisions, call log, resumable
  jobs, history snapshots and the baseline; the Planner panel (Y).
- 22.2 ① the Game Brief: three readings merged with marks, genre profiles (arpg, rpg,
  action-adventure, life-sim), the scope table, the confirmation window from the schema.
- 22.3–22.9 ② outline and the graph seeded; ③ acts, scene cards and scenes in order against the
  graph, the main quest's skeleton, the act critic; ④ characters; ⑤ quests; ⑥ places with draft
  Briefs; ⑦ systems; ⑧ visuals; ⑨ the report and the baseline.
- 22.10 A place's Brief opens in the Build panel (`PlaceBuild::StartFromBrief`); the automatic
  driver over every place comes with ⑩ in Phase 23.
- AI panel: "plan a game…", the plan card, revisions in words, context `plan`, the PLANNER prompt.
  Tests: `planner_test.cpp`, `test_planner.py`.

## Phase 23 — AI Planner II: building the game (`ai-planner.md` §5.10–5.13, §8–§11)

**23.1** ⑩ The build plan; manual and automatic building; gateways from the world graph;
failures and placeholder places. **23.2** ⑪ Binding: aliases by story anchor, query, AI
choice among candidates, or local build edit; characters, objects, triggers placed.
**23.3** ⑫ Dialogue trees and cutscenes written against the bound world; the game's files
with provenance; the automated playtest. **23.4** ⑬ The
Result page. **23.5** Genre profiles for v1.0's genres: isometric action RPG, top-down RPG,
action-adventure, life-sim. (Games without a story come in v1.1, `roadmap.md`.)

Done when: from one request, a short ARPG is planned, built (9 places, automatic), bound and
playtested with every quest completable; the same flow makes a playable top-down life-sim.

**Status (2026-10-22): built, to be tried** (`phase23-test-checklist.md`; "As built" §17 in `ai-planner.md`).
- 23.1 `engine/gamebuild/`: the build plan, automatic and manual building through the Build panel's
  `PlaceBuild`, scenes with their story anchors, gateways from the world graph, placeholders.
- 23.2 Binding: characters placed, aliases fixed / by story anchor / made, items and markers.
- 23.3 Quests, dialogue (AI, checked), cutscenes, records, packages, `game.json` with provenance; the
  logic playtest (scenes, gateways, reachability, every quest walked).
- 23.4 The Result page in the Planner panel, Play from the start.
- 23.5 Profiles arpg, rpg, action-adventure, life-sim; the life-sim through the same flow.
- AI panel: "build the game", `plan.build` in the context. Tests: `gamebuild_test.cpp`.
- Not yet: module anchors and actor slots, AI choice among query candidates, local build edits,
  simulated combat in the playtest.

## Phase 24 — Shipping quality

Accessibility options, an in-game debug console, a profiler overlay, achievements (local,
with a platform layer), export polish (icon, splash, version, crash log), streaming of big
scenes, and a template project per supported game type (`game-types.md`).

Done when: an exported game passes an accessibility checklist and ships with its template's
features.

**Status (2026-10-23): built, to be tried** (`phase24-test-checklist.md`; `shipping.md`).
- Accessibility options (text size, high contrast, colour-blind palettes, reduce motion and flashes,
  hold to toggle) and the accessibility report, shipped as `accessibility.json`.
- The debug console (`) and the profiler overlay (F2).
- Achievements: records, a local platform layer, unlock banners, the pause menu's list, Lua.
- Export polish: version, the exe's icon, a splash, a crash log; exports known by `export.json`.
- Streaming: far chunks' scripts, characters, routines and spawners sleep.
- Template projects: arpg, rpg, action-adventure, life-sim.
- AI panel: achievement checks, the SHIPPING prompt, context `shipping`. Tests: `shipping_test.cpp`,
  `test_shipping.py`.
- Not yet: full-picture colour filters, sound captions, loading chunks from disk, store platforms.

---

## Phase 25 — Build improvements

Builds that look hand-crafted: every step of a build checked by a vision model and scored, art
that fits its place, and two ways to build a place's art: **Assembled** (each piece's art made and
put together on the grid) and **Painted** (the place painted from its plan). Modern places in the
ontology, and layout parameters any place can use. The record is `docs/build-fixes.md` (what
changed, round by round) and `docs/build-scores.md` (the scores).

Done when: a place built from a request (a village, a dungeon, an interior, a modern city) scores
well on every check, and looks planned and hand-crafted when its project is opened.

**Status (2026-10-26): built, partly tried** (`phase25-test-checklist.md`).
- Planner builds: stop, continue (from the step it stopped at) and rebuild; levels of a place as
  scenes of their own, joined by stairs.
- Checks by the vision model (Mistral Large 4), each scored: the layout (its zones), the plan (each
  kind of thing and where it stands), the built scene (in windows: placement, fit, quality). The
  layout check revises the zones; a redo that scores worse is undone. Scores per item in
  `docs/build-scores.md` (`tools/build_scores.py`).
- Art: found art checked by vision before it's used (the next candidate, then generated); a piece
  designed against what it depends on (a door against its wall); the ontology's placements (wall,
  corner, centre, near), sizes and drawing words used throughout.
- Assembled, sketch first: the place painted once (Mistral's image model), each piece cropped from
  the painting and its art drawn from the crop; the ground's patches from it too.
- The plan's block-out (`PlaceBlockout`): the plan drawn in the game's view without art, colour-coded
  by kind with a legend in the ontology's words, storeys and roofs from the roles.
- Painted: the block-out painted by a **Paint Provider** (Preferences ▸ AI ▸ Paint Provider: Black
  Forest Labs; FLUX.3 Image, FLUX.2 klein, FLUX.2 max, FLUX.3 → klein, FLUX.3 → FLUX.2 pro), each
  model with its own recipe (`bfl.py`); the painting checked against the layout and plan (`match`),
  building by building (`/places/check-painting`) and for its looks (`appeal`). A backdrop painted
  from words alone (`/places/backdrop`) is kept for comparison. Results: `tests/quality/bfl_results`.
- The build method (Places ▸ method: Assembled or Painted) chosen when a project is made, shown in
  Project Settings and fixed once places are built.
- Modern places: the `modern-city` build type (districts, office towers, apartments, shops, malls,
  schools, hospitals, stations, parking, parks), vehicles, street furniture, modern interiors and
  rooms, modern terrains and vocabularies (an `era` scheme), urban design rules. Sidewalks along
  streets, traffic in the lanes, a root's own ground, roads and landscape set (its compile hints).
- The layout form (any place with zones; each build type its defaults; asked in the Brief):
  zone-shape (grown, blocks), street-grid (per-zone, shared lattice), block-size, block-fill
  (frontage, built through), coverage (partial, full), arrangement (natural, ordered).
- Designs: `docs/places/big-scenes.md` (big places by tiles: zones as tiles, buffers between them),
  `docs/build/ontology-gaps.md` (what the build ontology still lacks, with priorities).
- AI panel: the checks' answers and scores on the panel; the Paint Provider's status.
- Not yet: sprites and tiles extracted from a painting to build the scene in the renderer (Assembled
  from the block-out painting: occlusion and editing for free); filler for a city's empty blocks;
  buildings growing with their lots; shared radial and linear street lattices; tiling big places.

## Phase 26 — Painted places, marked by hand (`docs/places/painted-places.md`)

A place painted whole, the way Baldur's Gate and Pillars of Eternity draw theirs: one picture in
the game's view, never cut into assets. A painting alone isn't a level. It also needs what the
player can walk on, and the parts drawn again over a character who walks behind them (a house
front, a tree's canopy, a wall top). Code can't find those parts reliably in a painting (trees,
wooden walls, a tower), and a person can, in minutes. So the build gets a step where the user marks
them over the painting, pre-filled from the plan and helped by click-to-select.

Two **build methods** (Project Settings ▸ Places ▸ method, asked in New Project; each build chooses
on the Build panel's start page, each plan on the Planner's home page):
- **Assembled:** ① Confirm → ② Layout → ③ Plan → ④ Assets → ⑤ Build, each piece's art put
  together over a painted background, as before.
- **Paint:** ① Confirm → ② Layout → ③ Plan → **④ Paint** → **⑤ Mark** → ⑥ Assets → ⑦ Build. The
  Assets step only makes what the painting doesn't hold: things the player uses (doors, chests,
  items), characters, and anything that moves.

The paint model is a project setting (AI ▸ paint): FLUX-3-Image (default), FLUX-2-Klein-4b,
FLUX-2-Pro, FLUX-2-Max, FLUX-3-Image-to-Pro, FLUX-3-Image-to-Klein.

**26.1 Paint.** The plan's block-out (`PlaceBlockout`, the game's iso view, colour-coded by kind)
painted by the Paint Provider (FLUX.3 Image) from the place's words and its legend, in tiles when
it's big, as the background is (`background.md` §2). The Paint tab has a progress bar, the steps,
the painting (zoom on click), and "Paint again" with notes. It's saved as `painting.png` with its
view (`painting.json`: the cell-to-pixel mapping) and the block-out's id map (`blockout-ids.png`).

**26.2 Mark.** An editor over the painting with layers:
- **Walkable:** cells over the picture, painted walkable or blocked (brush, rectangle, fill).
- **Occluders:** each one a region (brush, eraser) and a **base line**, where it meets the ground,
  with two handles. A character whose feet are behind the base line is drawn under the region.
  There's a list of occluders, each named after the plan object it came from, which can be selected,
  hidden or deleted.
- Zoom and pan, undo and redo, the layer's opacity, and keys for the tools. Saved as `marks.json`
  (base lines, walkable cells, links to plan objects) with one mask PNG per occluder under `marks/`.
- When the painting is painted again, the step says the marks are stale. The base lines and
  walkable cells stay, since they follow the plan, and the regions are shown for checking.

**26.3 Pre-fill from the plan.** Before the user starts:
- **Walkable** from the plan's solid cells and the footprints of things that block.
- **One occluder for each tall thing** (buildings, trees, walls, towers; read from the role's
  height), with its region from the block-out's id map, which shows that object's pixels in the
  same view as the painting, and its base line along the front edges of its footprint.
- The user trims each region to the painted outline instead of drawing from nothing.

**26.4 Click-to-select.** A point-prompted segmentation model (SAM 2, the small one, on the CPU in
the AI service: `POST /images/segment`, the image embedding cached per painting):
- click to add an object's outline to the region, Alt+click to take it away;
- the brush for touch-ups.

**26.5 The scene from the painting.** Build ⑦ lays:
- the painting at the back, in chunks of at most 2048 px;
- an invisible walk layer from the walkable cells, so collision and pathing follow the marks;
- each occluder cut from the painting by its mask and sliced into strips about half a tile wide,
  each sorted by the depth of its base line under it, so a long wall on a slant hides a character
  only where they stand behind it;
- the Assets step's sprites placed as before.

The scene check runs on the result, and occluders show in the viewport's debug view.

**26.6 AI panel.**
- The Paint and Mark steps on the build card.
- The context says the place is painted, with its counts of occluders and walkable cells, and
  whether the marks are stale.
- Ops: `place.paint` (paint again, with notes), `place.marks` (read), and `place.prefillMarks`
  (pre-fill again).
- The prompt says that marking is the user's step: the AI pre-fills and checks, and never claims
  that the marks are done.

**Status (2026-10-27): built, to be tried.** 26.1–26.6 and the build methods are in; tests
`painted_place_test.cpp` (view, pre-fill, edits kept, strip depths) and `brief_test.cpp`. Not yet:
painting big places in tiles.

Done when: a village painted from its plan is marked in a few minutes (pre-fill, then a few clicks
and brush strokes) and can be walked in the editor's Play mode. A character goes behind houses and
trees and in front of them correctly, and can't walk through walls or water.

## Phase 27 — Interiors

When a character goes into a building (or another closed place), the inside shows: the same building as a cutaway,
its roof and the walls facing the camera taken away, its rooms, floor and furniture showing, and the walls' outline
keeping the outside around them. One complementing asset a building: its interior, paired with its exterior.
Two kinds, both in one project and one scene: a **cutaway** (houses, workshops: drawn cut away in place) and a
**scene** (inns, temples, manors, keeps: a scene of its own, built by the interior build type, entered through the
doorway; no cutaway asset). The design, the approved look and the status are in `docs/places/interiors.md`. Interiors are always made with BFL
(an edit of the exterior) and never skipped for another provider.

- **Ontology:** a role's look `has_interior` (inherited: every building has one; set off where a thing has none).
- **Art:** each piece of a role with an interior gets its interior made with it (Assembled and Paint): an edit of the
  exterior picture (the same building, cut away), on the same canvas, so the two line up exactly and swap in place.
- **Assets:** the pair kept in the catalog (`.meta`): the exterior's `interior` names its interior, the interior's
  `exteriorOf` names its exterior; the Assets panel shows the pair and sets it ("Interior: …", any project image).
  Interiors aren't candidates for a place's needs on their own.
- **Library:** a pair stays a pair: keeping an exterior in the user's library keeps its interior with it (an item's
  `interior`), and copying it into a project copies both and pairs them there; the AIGE library's items the same.
- **Scene:** a building whose art has an interior gets an `Interior` component (the interior's texture, its door side).
  Its collision is then its walls only, with a doorway on its front, so one can walk in.
- **Play:** while the player stands inside its footprint, the building shows its interior; outside, its exterior. In
  the editor, the Inspector's Interior ▸ shown previews it.
- **Editor:** the Assets panel's right-click on an asset or prefab with a pair: Go to interior / exterior. The
  viewport's right-click on a building: Show interior / exterior; on the ground: Show interiors / exteriors (all);
  **H** toggles the selected building's.
- **Paint method:** after Mark, each building's region of the painting is edited into its cutaway (BFL), shown by an
  entity whose `Interior` hides that building's occluders while it shows; its walk cells become walls and a door.
- **AI panel:** context and ops know an entity's interior (show it, set its texture); the build card says how many
  buildings have interiors.

Done when: a village built with the words art method has an interior for every house, the player walks into one
through its door and sees its rooms, and walks out again; a pair kept in the library comes back as a pair.

---

## Phase 28 — Actions, reactions and items, for designers (`docs/systems/actions.md`)

**Status (2026-11-02): built, to be tried** (tests pass with a fake AI; no live AI run yet). Built: several items required, `if` and `do`, opening panels without scripts, recipes, the
item in hand, the Crafting list, clicks on the game's windows, the Reaction name.

Also written: the Brief's Gameplay mechanics, and ⑦ Systems designing gameplay features that ⑫ sets up through the AI
panel (`docs/ai/ai-planner.md` §5.7).

**The planner designs in depth** (`docs/ai/ai-planner.md` §5.7), before the rest of this list's designer work:

- **Feature steps by place**: each feature's steps say where they run (empty: the whole project; a place: its scene, opened from disk, the AI panel run on it, saved).
- **A detailed design per system**: one AI call per feature, after the feature list: its rules, items, recipes, numbers, events it sends, and how it fails.
- **A behaviour plan per character**: one AI call per character: for each quest stage, world event and player action that concerns them, what they do (a package, a reaction, a line, a script), and where.
- **The event map checked**: every event listened to is sent by something; every quest stage's actors exist; nothing a character does names a package, clip or item that doesn't exist. Errors go back for repair.
- **Depth follows Gameplay mechanics**: small, medium or extensive sets how many, and how much AI use a plan costs.
- **Needs NPC behaviour without scripts** (step 3 below) first, and quests that can change what a character does.

**One concept for designers, as far as it goes**: Reaction, Moving Part, Door/Chest/Sign and Character Rules are named as
one family in the Inspector; they're still separate components underneath (merging them means migrating saved scenes).

In this order:

1. **Build and test what's written**: fix what doesn't compile, run the tests, regenerate the schema snapshot, try it in
   a game.
2. **A visual rule editor**: `if` and `do` picked from lists, as the story editor's effects are, instead of typed JSON,
   on Reactions, Interactables, animated parts and recipes.
3. **NPC behaviour without scripts** (its effects in the rule editor's lists):
   - `push_package` and `pop_package` effects, acting on the NPC the Reaction is on: when this happens, this NPC does that;
   - packages' conditions the full rule set dialogue has (items, variables, attributes, time, factions...), not only
     flags and quests.
4. **Reactions that take time**:
   - a duration: seconds, or until its animation ends;
   - three stages, each its own `do`: at the start, at the end, if interrupted (hit, a cutscene, the scene left: what it
     took can be given back);
   - events sent by themselves: `reaction_began`, `reaction_ended`, `reaction_interrupted` (with the thing as their
     entity), which scripts hear with `on(...)`. Direct `on_begin` and `on_end` script fields only if they're asked for.
   Events get no begin or end: an event is an instant.
5. **Animated parts and recipes on the same stages**, in place of their own durations (a timed recipe's items are then
   safe when it's interrupted or saved).
6. **One concept for designers**: Reaction, Interactable and AnimatedPart shown as one thing, the animation an option on it.

---

## Phase 29 — Actions anyone can do (`docs/systems/actions.md`)

**Status (2026-11-04): planned.** Today a part or a thing's Action on an input action ("pull", "cook") answers only
the player's key; NPCs can set off a part only by coming near an "auto" one or by a script sending an "event" part's
event, from anywhere, with no one standing at it. Here the project's input actions become **verbs any character can
do**: the player by a key, an NPC when its behaviour, a script or a dialogue says so. What answers "cook" doesn't care
who cooked.

**What doing an action is**: what the player pressing its key there does, done by the NPC.

- its clip on the NPC (`plays`: the baker stirs), its effect on the NPC, its event sent with the NPC as its source
  (quests and scripts see who did it);
- the nearest part or Action within range that answers it is activated: opens, plays, runs its `do`;
- `nearOnly` and `group` as for the player: only near a target, the nearest of a shared key's targets.

**Not for NPCs**: what only the player can mean.

- an action's `screen` (inventory, journal), axis actions, actions of the dialogue, menu or cutscene context;
- the engine's own: moving and attacking (NPCs move and fight by their own AI), pause, and interact with chests and
  pickups (what they give goes to the player's pockets).

**How NPCs come to do one**:

- **Behaviour**: a `perform` step for packages ("go to the oven, do *cook* for 10 seconds"): the NPC walks to the
  nearest thing answering that action (or a marker named), does it, and stays the time given. A package template
  `perform` built on it, so a work package can be "cook at the oven in the morning, sell at the counter after".
- **Scripts and story**: Lua `perform(action, who?)`; a `perform` effect in dialogue, rules and quests
  (`{"op": "perform", "action": "pull", "who": "Bren"}`): a dialogue choice "Lower the bridge" sends Bren to the lever.

**Parts**:

- one at a time: an NPC waits while the player has a part active, and the other way round;
- their `do` runs for NPCs too, with the NPC as who (effects that give already go to whoever runs them); a part's
  "player only" switch keeps it the player's ("only the player gets the bread");
- doors unchanged: NPCs already open them as they pass.

**The AI panel**: the prompt learns `perform` (the step, the effect, the Lua call) and the part's "player only"
switch, so "make the baker use the oven every morning" or "Bren lowers the bridge when asked" work; the populate
outline (docs/ai/agent.md) can give people a working day at their building's parts. Its checks: an action performed
exists and isn't one of the player-only kinds; something in the scene answers it.

In this order:

1. **The runtime**: `Perform(who, action)` in GameSession, the player's key path going through it too (one code path:
   clip, effect, event, the nearest part or Action); parts' one user at a time; "player only".
2. **Lua and effects**: `perform()`, the `perform` effect (`who`: a name, empty the one it runs on).
3. **Behaviour**: the `perform` step and package template; the editor's package form picks the action from the input
   actions, the target from the scene.
4. **Inspector**: the part's and the Action's "player only"; the input action's page says which NPCs' packages do it.
5. **The AI panel**: prompt, checks, examples; tests (an NPC cooks at a stove part; a dialogue sends one to a lever;
   a player-only part ignores NPCs; two users wait their turn).

---

## Phase 30 — Living NPCs: conversations in natural language (`docs/systems/conversations.md`)

**Status (2026-11-04): planned.** Today an NPC speaks only the lines its dialogue tree has, and the player only picks
from the choices it offers. Here an NPC can also **talk**: the player types (later, speaks) in their own words, and
the NPC answers in character. It answers from who it is (its **profile**) and what it knows (its **knowledge**), and
from the state of the story: quests, flags, what happened, what the player told it before.

The story stays the author's. A conversation can move the story only through what the dialogue, the quest and the
character allow: start or advance a quest, give an item, set a flag, change how the NPC feels about the player. It
never invents quests, items or facts that aren't there.

### 30.1 Character profile

Every character in the World Bible gets a **profile**, edited in Project ▸ Characters (the World panel's characters,
a new Profile tab). It shapes how the character talks.

- **Personality**: outgoing ↔ reserved, talkative ↔ terse, warm ↔ cold, honest ↔ deceitful, brave ↔ timid, curious,
  humorous, patient. These are sliders, with a sentence of their own for nuance.
- **Speech**: formality, vocabulary and education, dialect or verbal tics, catchphrases, how long its answers run, and
  what it won't talk about. Sample lines in its voice are written or generated, then kept.
- **Inner life**:
  - its goals, fears and wants;
  - its mood today, which can follow events ("grieving since the fire");
  - its secrets, each with when it would tell (trust above a level, a quest stage, an item shown).
- **Toward the player**: a starting disposition, plus what raises or lowers it: flattery, threats, gifts, helping its
  friends. Its trust and how much it likes the player are kept in the save, as reputation is.
- **Relationships**: the World Bible's relations (friend of, rival of, works for), and what it thinks of each.
- **Conversation rules**:
  - whether it talks freely at all (some NPCs only use their dialogue tree);
  - how long it will chat before it has to go;
  - topics that bore it;
  - whether it gossips (§30.3);
  - the actions it may take in a conversation (§30.4).

Profiles are stored with the character's World Bible entry (`world/character/<id>.md`: front matter for the fields,
the body as its background). The AI panel and the AI Planner can write and revise them like any entry.

### 30.2 Knowledge

What an NPC can say it knows. There are three layers, merged when it answers:

| Layer | What | Example |
|---|---|---|
| **World** | the setting's general knowledge: history, geography, customs, common facts (the World Bible's lore, places, factions, events) | "The old king died in the winter of the flood" |
| **Groups** | what members of a group share: a village, a guild, a family, the guard, a faction; a character can be in several | Millbrook villagers know the mill burned; the guard knows the patrol routes |
| **Character** | what only it knows | where it hid the key; that it saw the stranger at night |

**Each fact** is a short entry with:
- its topic and aliases;
- **who knows it** (the world, groups, characters);
- **when** it's known: always, from a quest stage, after an event, while a flag holds;
- **how freely it's told**: freely, to friends (trust above a level), only when asked about it directly, or never (a
  secret it lies about or deflects);
- **its source**: authored, from the story, learned in play.

**Learned in play**:
- What happens in the game becomes knowledge for those who saw it, or who would hear of it: quest stages, deaths, the
  player's deeds, items stolen. A rule per event says who learns it.
- With **gossip**, it spreads in a group after a while ("everyone in the village knows by tomorrow").
- What the player tells an NPC is remembered as "the player said …", true or not.

**Picked per answer**: an NPC can't be given everything. Each turn the facts it may use are filtered by who knows them,
when, and how freely, then ranked by relevance to what was said. The World Bible's embeddings index
(`ai_service/app/world.py`) does the ranking, with a token budget per answer. The character's own facts and the
current quest's always come first.

**Edited in** a Knowledge panel:
- world facts, groups and their members, characters' own facts;
- for any NPC and story moment, what it would know right now;
- facts that contradict the World Bible or each other, flagged.

### 30.3 Memory and relationship

- **Conversation memory**: each NPC remembers its conversations with the player as short summaries, what it promised
  and what it was told. They're kept in the save, so a later conversation picks up ("Did you find my brother?").
- **Disposition**: trust and liking change with what's said and done, within limits the profile sets. They open
  secrets and better prices, or end a conversation ("I've nothing more to say to you").
- **Between NPCs** (later in the phase): gossip spreads facts within groups. NPCs near each other can exchange a few
  lines in character as barks, from their profiles and shared knowledge, generated ahead and cached rather than live.

### 30.4 With dialogues, quests and the story

Free talk works inside the existing systems, not beside them:

- **Dialogue trees stay**: quest-critical lines are authored. A dialogue node can open **free talk** ("Ask her
  anything…"): the player types, and the NPC answers until the player or the NPC returns to the tree.
- **Intent to choices**: what the player types is matched to the node's choices when it means one of them. "Fine, I'll
  find your ring" takes "Accept the quest", with its effects. So a quest advances whether the player clicks or talks.
- **Allowed actions**: per character and per dialogue node, what a conversation may do:
  - start or advance these quests;
  - give these items;
  - set these flags;
  - change disposition by up to so much;
  - start a trade;
  - push a package ("follow me", "wait here").

  The model answers in structure: what it says, its emotion and clip, at most a few allowed actions, and whether the
  conversation ends. The engine carries out only actions on the list, and checks their conditions as dialogue would.
- **Quest objectives by talk**: a new objective type, `talk-about` ("ask Mara about the mill"). It's met when the
  conversation covers that topic (judged by the model against the topic's description and aliases), or by the tree's
  choice as before.
- **Consistency**: before an answer is shown it's checked:
  - no item, quest, place or character that doesn't exist;
  - no secret told before its time;
  - nothing contradicting what the story has made true (a dead character isn't alive).

  An answer that fails is asked again once; otherwise a safe in-character line is used ("I'd rather not say").
- **Speech in the world**: answers show as dialogue lines (portrait, name, the conversation box) and as speech bubbles
  for short exchanges. Barks keep working as before.

### 30.5 Runtime

- **Who answers in play**:
  - in the editor's Play, the AI service (the project's provider);
  - in a shipped game, the developer's choice in Project Settings ▸ Conversations: their own server or endpoint, the
    player's own key, or a local model;
  - with no model: free talk is off and the trees work alone, so every game still plays.
- **Speed**: answers stream word by word with a "thinking" pause the profile can colour ("hmm…"). Facts are picked
  before the player finishes typing. A conversation's knowledge is cached.
- **Cost and safety**:
  - token budgets per answer and per conversation, which the player can see in the editor;
  - a content rating per project, which the prompt holds to;
  - the player's input filtered, and attempts to break the character answered in character;
  - logs of conversations in Play for the designer to review.
- **Determinism**: tests and playtests can use a scripted provider. Recorded conversations replay, as the AI panel's
  golden tests do.

### 30.6 Editor and AI

- **Project ▸ Characters ▸ Profile**: the fields of §30.1, sample lines, and **Talk to them**. That's a chat window for
  trying the character in any story state (pick the quest stages and flags). It shows the facts used, the actions
  proposed and the check's verdict.
- **Knowledge panel** (§30.2), and the groups in the World panel (a character's groups, a group's members).
- **Dialogue editor**: a node's free-talk switch, its allowed actions, its intents to choices, its topics.
- **AI panel**: it writes and revises profiles, knowledge and groups ("make the blacksmith gruff and secretive about
  his past", "everyone in Millbrook knows about the fire"). It can set up free talk on a dialogue, and add `talk-about`
  objectives. Its checks cover facts that name entries that exist, groups with members, and allowed actions that name
  existing quests and items.
- **AI Planner**: the characters stage writes each character's profile. A new knowledge stage writes world and group
  knowledge from the story and the World Bible, and each character's own facts from its role in the quests: what it
  knows, hides, and learns at each stage.

### 30.7 Later

Spoken conversation: speech to text for the player, and text to speech with a voice per character, matched to its
profile. NPCs who remember the player across playthroughs (opt-in). Free talk between the player's companions about
what they've seen.

In this order:

1. **Profiles**: fields on World Bible characters, the Profile tab, the AI panel writing them, and a migration that
   gives existing characters a default profile from their summary.
2. **Knowledge**: facts, groups and layers, the Knowledge panel, and selection per answer with the embeddings index;
   the AI panel writing knowledge.
3. **Talk to them** in the editor: the conversation service (`/conversations/turn`), structured answers, the
   consistency check, and the facts used shown. This is tried with no game running.
4. **In play**: a dialogue node's free talk, intents to choices, allowed actions carried out by the engine, the
   conversation UI (typing, streaming, the box and bubbles), and Project Settings ▸ Conversations.
5. **Memory and disposition** kept in the save; secrets that open with trust; conversation summaries.
6. **Quests**: `talk-about` objectives; knowledge learned from events; gossip.
7. **The AI Planner**: the profile and knowledge stages. Tests: scripted conversations (a secret kept until trust, a
   quest accepted by words, an invented item refused, a dead character spoken of as dead), and an evaluation set
   recorded against live models.

## Phase 31 — Open worlds: sections and tiles (`docs/places/tiling.md`)

**Status (2026-11-06): started.** The Build panel builds one place of one kind. Here it also builds an **open
world**: one scene made of any number of **sections**, each a place of its own kind (villages, camps, castles,
dungeons, and the wild land between), planned inside one **world plan** so that roads, rivers and forests meet
across them. And paintings and backgrounds are made of **tiles**, as many as the place's cells need.

### 31.1 Tiled painting, by cell count

- The Paint method's block-out is drawn at the project's density (`paintCell`, 80 px across a cell: `(W + H) × 40`
  px wide), so the painting is as big as its cells need, and the block-out, id map and painting stay the same size.
- The painting is made of `n × n` tiles, `n = ceil((W + H) / 60)`, each cut from the block-out with a 12% overlap,
  painted at the model's full size (4 at a time, one seed) and joined by cross-fading the overlaps.
- Each tile's prompt says it is one part of a bigger map (paint to the cut edges, no frame), its part's scale, and
  the counts as "of the whole map's".
- The Paint page says how many tiles and shows their progress.
- Tests: the tile count from the cells; the boxes cover the image with their overlap; the join of flat tiles is
  seamless (pixel test); the prompt's tile note.

### 31.2 An open world, chosen in the Build panel

- Next to the method, the panel asks **A single place** or **An open world**, with *"Select build method and type
  first."* above the request. An open world is built as a `region`.
- A world build (`WorldBuild`) owns the world's Brief and plan and a place build per section; the panel's state
  says which section is selected, and each page shows it.
- Its steps: Assembled: Confirm, **World**, Layout, Plan, Assets, Build. Paint: Confirm, **World**, Layout,
  Plan, Paint, Mark, Assets, Animate, Build.
- Tests: the scope reaches the build; a world's steps per method; the request parsed as a region.

### 31.2a The ontology of a world (done)

- A `world` build type (`"scope": "world"`, its `sections`: from, wild, wildSide, margin, carried) and root module
  (landform, relief, woods, biomes); sea, ocean (ringing an island), coast, mountains (not walkable), hills and marsh;
  lakes laid out at a place's top level; the `World` and `Section` classes, the `landform` vocabulary, world rules.
- The Build panel follows it: "An open world" when a world type exists; the Place menu the others; ② World for a
  build whose type is a world.
- The solver: a zone never starts on another's seed, and a zone others are placed by is seeded first.
- Tested from a Brief alone (`tools/plan_blockout --brief`): a coastal world and an island plan whole.

### 31.3 The world plan and its sections

- The region root solved over the whole world (sites, roads, rivers, lakes, coast, forests, relief), then cut into
  sections: each site's rectangle (with its margin), the rest in wild sections of at most 64 cells a side.
- The **World** page: the world plan drawn with the sections on it, and their list (name, kind, size, what each
  carries). *Lay out the zones* lays the sections out one by one.
- Stored as `places/<world>/world.json`, `world.solved.json`, `world.png`, and a place folder per section.
- Tests: every world cell in exactly one section; sites whole in their sections; small leftovers merged.

### 31.4 Sections carry the world plan

- A section's compile gets its carried things as fixed statements, solved first: its terrain and relief, its road
  and river cells with their edge entries, its masses (lakes, coast, forests), and its margin, which is the world
  plan's exactly.
- The Layout and Plan pages list the sections above and show the selected one below, as today.
- Tests: a road crossing two sections has the same cells on both sides; nothing tall in a margin; a section
  revised keeps its carried things.

### 31.5 Assembled worlds

- Backgrounds per section, in tiles, their block-outs extended by the margin and cross-faded with their neighbours'.
- One asset plan for the world (a kind used in many sections made once); every section built into the one scene,
  under a group entity per section, its background as its own sprites.
- Tests: the backgrounds' join; assets shared; the scene's sections at their origins.

### 31.6 Painted worlds

- Each section painted in its tiles, its block-out extended by its margin into its neighbours, the paintings
  cross-faded over the margins.
- The Mark page lists the sections on the left and shows the selected section's marks on the right.
- Tests: neighbours' paintings agree over the margin; marks per section line up in world cells.

### 31.6a Painted freely (painted-places.md §2a)

- The Paint method on a site place is painted freely: its plan's land alone in the block-out (ground, main and world
  roads, water, mountains, woods, fields as bare soil; no buildings), 48 px a cell, flat; the painter composes the place
  in one call from a prompt with its kind and size, each kind of building and how many, the request's and Brief's
  design, "decorate it as you like", farm fields empty, the land kept at exactly its location; then tiles of 2864 × 1432
  repaint it at full size (saved and resumable); the black put back.
- ③ Plan and ④ Paint show the calls before painting.
- ⑤ Mark: the land's marks, and what the vision model finds standing in the painting as occluders and blocked
  footprints, for the user to adjust.
- For FLUX.2 klein: a strict composing call, then a ground-only call.
- A whole world: each section and the world's own land painted so, joined by masks (tested: 22 images).

As built (later fixes):
- The land block-out keeps the main roads and every district's streets (a ward's, a section's); plots' lanes and yards
  are left to the painter.
- ⑥ Assets' interiors list only the real buildings the vision model finds in the painting (houses, shops, halls, barns,
  mills), not the plan's counts, nor walls, towers, gates, stalls, wells or a hidden occluder.
- ④ Paint stays on Paint after painting (to look it over, paint again or compare it with its plan); ⑤ Mark → is the
  user's.
- A built painted place has **Mark again** on its Result page: ⑤ Mark reopens on its marks, and **Update the scene**
  builds just that place into the scene again (its interiors kept).
- Paint models FLUX-2-Pro, FLUX-2-Max, FLUX-3-Image-to-Pro and FLUX-3-Image-to-Klein, and background models flux-2-max
  and flux-2-pro, are removed; a saved preference or project setting naming one falls back to the default
  (FLUX-3-Image).
- The build method follows the scene's type (Assembled or Paint): the Build panel's first page reads the open scene's
  type again whenever it's set or a build ends; the AI Planner builds each place by its target scene's type when it has
  one, else the plan's method.
- The AI Planner stops for the painting's call count (**Approve painting**) before painting, unless plan.building is
  automatic.

### 31.7 The AI panel

- New ops: build an open world, list its sections, select one, lay out / plan / paint a section again.
- Its prompt and context: the world's sections and their stages; what a world is and when to suggest one.
- Checks: a section op names a section the world has.

Done when: a request like "a big island with several villages joined by long roads, forests and camps" builds one
scene of sections, by either method, whose roads, rivers and forests run on across every seam, each painting at
the detail its cells need.

---

## Phase 32 — AI Planner III: a game that plays as it was designed (`ai-planner.md`)

**Status (2026-11-07): mostly done (not built or tested yet).** Done: 32.1; 32.2 (characters at their anchors with
schedules, class stats, Actor prefabs; the player from its document); 32.3 (placed enemies fight, loot, chests; enemy
class prefabs); 32.4 (full formats to ⑦, item variety, per-sub-field validation and repair,
consistency checks, kits, merchants, chests); 32.5 (stage onEnter effects: move, hide, show, hostile, friendly,
follow, spawn, give_to, kept by the actor roster); 32.6 (characters drawn and animated by role, held weapons, item
icons and in-hand pictures, the crafting item in hand, run and climb); 32.7 (keys applied without clashes, the planned
HUD; windows of their own for the shop, the quest log, the map (M, the place's painting), the character (P) and the
controls (Esc ▸ Controls); items with icons and a second column; a panel picture drawn in the game's style and a font
from the project's assets/fonts as the theme); 32.3's spawners (enemies come back in dungeons, camps and the wilds);
items drawn one per type (every sword one picture); 32.10 (scripts: a feature that needs code gets its Lua from ⑦, compiled by the
engine, put on its character, enemy class, thing or the game); the map shows where the player is; the engine has six
open-licence fonts (data/ui/fonts) that ⑧ chooses from;
32.8; 32.9 (the AI panel's effects, classes and keys). Steps ①–⑨ write rich documents for every character, quest, place, item and
system. The build (⑩–⑬) uses little of them: every character is a starter template (fixed stats, a placeholder
square, no animation), NPCs stand around the arrival point, no one carries anything, enemies in places can't be
fought, the planned UI and keys are noted but not applied. Here the plan becomes the game: each character where its
story puts it, doing what it does, with its stats, kit, art and animations; items designed with real attributes and
effects and placed in inventories, shops, chests and loot; quests that move characters; the planned UI and controls.

### 32.1 Bugs first

- AI packages with a place are written as `location: {in: <place>}`, a field the package record doesn't have: the
  write is refused and the error dropped, so most NPCs have nothing to do (`GameBuild.cpp` WriteFiles). Write a
  location the runtime reads (a marker made for the place's anchor, or `scene` + marker), and report refused records.
- A *defeat* objective on a named boss never completes: `OnDefeated` matches only a prefab or the team "Enemy", and
  a plan's boss has neither. Match the character's id (its `Actor` / world key) too; the playtest stops passing it
  when it can't complete.
- NPCs with no daily packages get no `Behaviour` component, so packages pushed onto them later (quest responses) do
  nothing: every NPC gets one.
- Tests: a planned NPC's package record is written and run; killing a named boss completes its objective.

### 32.2 Characters from their documents

- **Where they are:** the places' `actors: [{character, anchor}]` and the characters' home, work and bed become
  markers (`home-<slug>`, `work-<slug>`, `bed-<slug>`) at the anchors the place's plan has (a house, a shop, a
  forge), and `Behaviour.home / work / bed` are set; no one is dropped at the arrival point unless nothing fits.
- **What they do:** daily routines planned as schedules (from/to/days per package: work by day, the inn in the
  evening, bed at night), written and run; perception, barks, aggression and confidence from personality and role.
- **Moving through the story:** the time-bounded `locatedAt` facts (a character who leaves the village in act 2)
  create the character in each scene it's in, shown only while the story says it's there.
- **Stats:** level, stat class (`gameplay.class`), importance and role give `Health`, `Attributes`, attack,
  defense and move speed, from the `progression` and stat-class records ⑦ writes (a record type for stat classes:
  base attributes and per-level growth); difficulty applied on top.
- **The player:** built from the player-character document: its class, attributes, level, abilities, starting
  inventory and equipment, appearance; combat, magic and stamina switched on by the Brief's systems.
- Tests: each planned character in its scene at its anchor; a vendor at its shop by day and in bed at night; a
  boss's health from its level and class; the player starts with its kit equipped.

### 32.3 Enemies and encounters

- Enemies a place's plan scatters (dungeon `enemy` slots) become real enemies: `Health`, `Faction`, combat,
  perception, `Rewards`, from ⑦'s enemy classes for that place's region and level band.
- Their number from the module's count, scaled by the Brief's difficulty and the place's level band (⑦'s "enemies
  per region and level band"); `Spawner`s with `encounter` records where enemies return.
- Loot tables on enemies, bosses, chests and loot caches (`Rewards`).
- Tests: every enemy in a built dungeon can be fought and drops its loot; counts follow difficulty.

### 32.4 Items: designed fully, then placed

- **The designer's inputs (⑦ Systems):** the full record formats, not just top-level field names: sub-fields
  (modifiers' attribute/op/value, enchantment's effects and on: worn|hit, use's heal/effect/duration...), the
  allowed values (kind, slot, damageType: physical, fire, frost, shock, poison; status effects), field descriptions,
  the attribute names a modifier changes (resistShock, moveSpeed...), and each effect preset's archetype, damage
  type, magnitude and duration.
- **The prompt:** elemental damage through on-hit enchantments (a lightning sword: `shock-damage` on hit),
  resistances on armour, use effects for potions, food, scrolls and books, weapon class and grip for the hand; a
  worked example of each. New `magic-effect` records when no preset fits.
- **Item variety** in the Game Brief (low, medium, high): how many items ⑦ designs, and how many kinds.
- **Validation per sub-field:** only the wrong value goes (today a bad `on` drops the whole enchantment), and the
  problems go back to the AI to fix, as other steps do.
- **Consistency checks:** effect, ability, loot, recipe and item references exist; an item's name and description
  agree with its effects ("storm", "lightning" ⇒ shock; "fire" ⇒ fire); values within the level bands.
- **Placed:** every NPC's and enemy's carried items and equipment (a kit per role and class, weapons and armour
  equipped and shown), merchants' stock from `gameplay.stock` as items with prices, chests and loot caches in places
  filled from loot tables.
- Tests: a lightning sword's hit deals shock damage reduced by resistShock; a merchant sells its stock; an enemy
  wields its weapon; no item record loses a field to a wrong sub-value.

### 32.5 Quests that move characters

- Quest-stage and event effects: show or hide a character, move it to another scene or marker, turn it hostile or
  friendly, make it attack or follow, spawn enemies, give an item to an NPC, open or lock a door.
- Quest stages with effects of their own (on entering a stage), not only objectives and journal text.
- Effects that reach characters in scenes not loaded (applied when the scene loads).
- The ③ and ⑤ prompts: when a scene or stage moves a character, write it as these effects.
- Tests: a stage moves an NPC to another village; a betrayal turns an ally hostile; an ambush spawns its enemies.

### 32.6 Character art and animation

- A build step that draws each character's sheet from its `appearance` (`/characters/generate`, 4 or 8 directions,
  the project's art model and view), as a prefab with its animator, for the player, NPCs and enemy classes.
- Its animations chosen from its role, class and abilities: everyone idle, walk, hurt, die; fighters the attack
  variants of their weapon classes, casters cast, workers use; held weapons drawn with `hold` so the hand point is
  recorded. New clips: run and climb.
- Item art: an inventory icon for every item, and for weapons and armour the sprite, grip, pointing and class that
  draw them in the hand or on the body; equipment switched in play changes what's drawn (the engine does this).
- The crafting item in hand drawn at the hand point.
- Calls counted and shown before drawing (as painting does), resumable, with a cheaper look for minor characters.
- Tests: a planned game's characters all have sheets and the clips their roles need; equipping a sword shows it in
  the hand and plays its attack clip.

### 32.7 UI and controls

- The planned HUD and windows applied: game.json's `hud` and `screens` read by the runtime, widgets shown by the plan.
- Layouts for the windows that fall back to the generic menu today: character sheet, map, crafting, shop, quest log;
  designed screens bound to live data beyond the HUD's (attributes, equipment, quest lists, recipes, shop stock).
- UI art in the game's style: frames, buttons, backgrounds and item icons from an image model; themes with fonts and
  images, not only colours.
- Keys: ⑦'s planned actions applied to `input.json`, conflicts checked and fixed, abilities and consumables
  assigned to hotbar slots, a controls screen listing the bindings, key hints in the HUD.
- Tests: a planned game shows the HUD widgets its plan lists; no two actions share a key unless grouped; the
  controls screen lists every action.

### 32.8 Open-world leftovers

- The Planner pauses before painting to show the calls and ask (as the Build panel does).
- A dungeon's way back out at its entrance room, not its edge.
- Free painting keeps a section's streets; the interior list counts only the buildings found in the painting.

### 32.10 Scripts

- A feature the engine can't do without code ("needsScript") gets its scripts from ⑦'s system design: the Lua API
  in its prompt, each script with its path, purpose, what it goes on (a character, an enemy class, a thing in a place,
  or the game: an entity of its own in every scene) and its whole source.
- The engine compiles each one (Lua): one that doesn't goes back to the AI with the error.
- ⑫ writes them and attaches them (prefabs, scenes); the feature's AI-panel steps are told they're done.
- Tests: a needsScript feature's script is written, compiles and is on its target.

### 32.11 Going back

- Restart from any step: that step and every later one made again from the plan as the step before left it (its
  snapshot), the earlier steps' documents kept as they are now (the user's edits), the graph brought up to them;
  Restart from ① reads the request again.
- ② Outline edited field by field (story, cast, places, connections, world; add and remove): the story graph follows
  (people, places, roles and links again; later steps' facts kept), later steps go stale.

### 32.12 Where things are

- Each character where it belongs: the shop, smithy or inn whose purpose names it (an interior); else its home; else
  the place a home it names is in ("bellwether-bakery" in Bellwether). In a place, at its anchor, else at the door of
  what its words name (its home, the bakery, the inn, the graves, the gate: a house of its own each while there are
  enough), from the place's plan; never on a gateway.
- A story anchor no object shows goes to its module's part of the plan, else to what its words name (the boundary at
  the gate).
- The ways through: one per pair of places, made once (built again, nothing added). In a painted place, ⑪ asks the
  vision model which of the buildings and gates it finds in the painting is each place's way in, and that one's door
  (/places/find-entrances): the gateway goes on the door's cell, one arrives a cell before it. A gateway nothing shows
  (the edge, a crypt among the graves) gets art drawn for it (a gate, an archway, a crypt's door) standing on it.
  planner/gateways.json keeps each one's cell, arrival and art, so a rebuild keeps them; Continue does it for a game
  built before.

### 32.9 The AI panel

- Ops and context for what's new: a character's anchor, schedule, stats and kit; an item's effects; quest effects;
  drawing and animating a character; keys.
- Checks: references exist (items, effects, markers, characters); a key isn't taken.

Done when: "a small open-world RPG with several villages and dungeons" builds a game where each NPC lives and works
where its story says, merchants sell their stock, enemies fight with their weapons and drop loot, a lightning sword
deals shock damage, quests move characters, every character is drawn and animated with what it holds, and the HUD,
windows and keys are the ones the plan chose.

---

## Phase 33 — The story, built and tested (⑬ Validate, Story, Test)

The story (③) is the requirement; ⑩–⑫ build it, partly through the AI panel, which can rewrite what the plan made. Each
scene is traced to what was built for it, the build is checked against it and fixed, and each scene can be played on
its own.

### 33.1 Trace (BuildCheck.cpp)

- Each story scene: its beats; the quest stages that advance it (the design's objectives, the role each targets and
  what it was bound to, the effects as it begins) beside the stage as the game's quest file has it; its cast (prefab,
  home, a copy in the scene's place, the scripts on its prefab, dialogue); the scripts in its place's scene; its cast's
  packages; the gameplay designs that name its cast or place; its cutscene. In planner/buildcheck.json (`trace`),
  refreshed when ⑬ is written.

### 33.2 Validate

- After ⑬, a tab: rules first, then the AI scene by scene (/planner/buildcheck: the scene, what was built, the quests'
  documents; findings not already found by the rules).
- Rules: a cast member with no prefab; one who fights in a scene but lives elsewhere and nothing brings them there (no
  copy in the place, no stage moving them); a role bound to any enemy where the scene names who fights; a role not
  found; a design objective missing from the game's quest stage (by the words of its role, what it's bound to and the
  character's name); a cutscene scene with no cutscene; a played scene no stage advances; a script asked for or attached
  but not there; a quest the playtest found stuck, on the stage's scene.
- Fixes: plan edits (the quests', characters', behaviours', designs' documents; a JSON pointer each) merged into the
  game's quest files as they are now: the roles they bind (fill, and objectives where the stage lacks them, with their
  journal text), the effects of the stages they change. Nothing is placed again and nothing is written over: what ⑫'s
  steps did stays. Game fixes are tasks for the AI panel, as ⑫'s steps. Then the playtest and the result again.
- Every finding has a fix where one can be made: a marker no one can walk to (from where one arrives) placed again in
  its anchor's room (RoomOfAnchor: the room holding all its words, else its module's, else its words'; also when a
  place is built), on a reachable cell; an enemy's script attached but missing written; a place a quest without
  aliases goes to given a marker of its own there ("q-main/bellwether"); a quest the playtest found stuck: its
  unreachable marker placed again, else a task for the AI panel; the playtest's other failed checks (side quests,
  keys, dialogue) as tasks for the AI panel.
- The playtest walks to a thing one uses beside it (its own cell is solid), and counts a placed copy of a prefab a quest
  names to defeat.
- `build_scenes <project> --check [--fix]`: the rules (and their fixes) without the editor.

### 33.3 Test a scene (SceneTest.cpp)

- Story tab: each act's scenes, each with its beats and what was built for it; ▶ Play from here: the start scene
  played, each quest the story's scenes advance at the stage under way as this scene begins (every stage before it
  entered in turn, their effects done, what they had the player find in its pockets; a quest whose stages are all
  earlier completed), then the player to the scene's place (TestStart, ApplyTestStart).
- The editor's Play keeps the roster as a shipped game does (ActorRoster, 14.4): a quest's move_actor takes a character
  to another scene, made there when the player arrives.

### 33.4 Test automatically

- Test (each scene, or every scene in turn): played headless from that start by an autopilot: to each objective of the
  scene's stage in the game's quest (in another scene: travelled to, the roster's moves kept), on foot when a path
  reaches it, within a cell of it, and done as the player does it (interact to talk or use, conversations played
  through with their lines kept, a fight ended by the test, standing on a place). Its steps, the events, the quest's
  stage before and after, and script errors are its log; the AI judges the log against the scene (/planner/judgetest):
  passed, or what didn't happen, which joins Validate's findings with its fix.
- `build_scenes <project> --test <scene>`: the autopilot's run without the editor.

### 33.5 The AI panel

- Validate's findings and a scene's trace in its context; fixes it's given carry the finding and the scene.

### 33.6 Built from the story (so Validate has little to find)

The first Validate of Haunted Village found 25 problems. Most came from ⑦ and ⑩–⑫ not looking at the story scenes:
items, spots and enemies were bound without them, and the systems designed the quests again. Now:

- ⑪ binds each role where the story uses it: the location of the scene of the first stage that targets it
  (`advancesScene`), else of a scene that needs it as a part. The plan's own place is kept only when it is an interior
  (a shop, a crypt).
  - An item is made as an item pickup lying there, from its alias when it has no fill, with a quest-item record when
    the systems made none. It is bound as `item`.
  - A thing the player uses (interact, use, free) is what's there by that name, else an Interactable made at its
    marker. It is bound as `thing`.
  - A described enemy is one of the scene's cast who fights and whom the description names. Otherwise, one is made of
    its class at its anchor, with the Actor key `<quest>/<alias>`; defeating that one counts, not any enemy.
  - Each scene's cast (except the player) gets a marker beside where the scene happens: `story/<scene>/<who>`.
- Quests (`EngineQuestFromDesign`): a collected item is "have" the item; a delivered item goes to whom the stage talks
  to, else the giver; a used thing is targeted by its name; a made enemy by its key. `StoryStaging`: as a stage begins,
  its scene's cast who live elsewhere are moved to their markers there. Their day takes them home once the player
  leaves (ActorRoster). It is applied when quests are written and when they are merged.
- ⑦ System and behaviour steps get the quests' objectives, aliases and scenes, and the story's scenes (place, cast,
  parts, beats). They're told never to create a quest, change its stages or objectives, or set its stage. A step whose
  request does is sent back (`_quest_rewrites`).
- ⑫ Every feature request says the quests come from the story. After each step (not a Validate fix), what it added to a
  stage's objectives becomes optional, and what it took out or changed is put back (`KeepQuestAsks`). A stage it added
  is kept and logged.
- Still to do (cutscenes, d): scenes that no stage plays (dialogue and cutscene scenes such as s3-02 and s3-03) and the
  events scenes describe that no objective makes happen.

---

## Phase 34 — Requirements and implementation: the story built, item by item

Steps ①–⑨ are the game's requirements. The story's scenes (their events, beats, cast and parts), the quests, the
characters, the places and the systems all say what must happen. Steps ⑩–⑬ are the implementation. Until now they
built places, characters and quests, but no list said which story event needs which piece of the game. So events like
"Grey Lament slips through the boundary and attacks Ina", "a skeleton rises from a broken grave" or "Sella follows
Ashen into the graveyard" were built by chance, by a later AI fix, or not at all. Nothing showed which.

Two lists close that gap:
1. **Requirements**: what must be true in the game, each one testable.
2. **Implementations**: how each requirement is built.

The build works through the implementations and ticks each one. Validate checks each requirement against the game it
built.

### 34.1 Requirements (planner/requirements.json)

Requirements are made after ⑪, once the places and cast exist, so they can name real scenes, markers and prefabs. Each
one is `{id, scene, act, kind, text, when, where, who, source, accept}`:

- **kind**:
  - presence (someone is there);
  - arrival (someone comes into the scene: through a gateway, rising from the ground, out of a door);
  - movement (someone goes somewhere, follows someone);
  - combat (who attacks whom; how strong);
  - dialogue (a line said; a conversation);
  - cutscene (watched, not played);
  - item (lies somewhere; is given; is taken);
  - prop (a thing the scene needs: a record board, a ledger niche);
  - gateway (a way between places that can be seen and used);
  - effect (light, sound, a door opening);
  - quest (an objective; a stage that moves on when something happens);
  - guidance (the player can tell where to go and what to do);
  - system (a feature from ⑦);
  - as built, also: scene (a scene that must be played: `scene-played`), asset (a separate piece of art, below),
    animation (a character's, a creature's or a thing's: Grey Lament rising from the grave, a gate opening, spirits
    drifting) and character-state (a character's state at a quest stage, from the cast timeline, 34.7).
- **text**: the requirement in a sentence, taken from the story ("Grey Lament comes out of the graveyard entrance in
  Bellwether and attacks Ina Crook").
- **when**: a quest and stage (on entering it, or while it's on), after another requirement, on an event, or on the
  player reaching a place or leaving a building ("when Ashen leaves the apothecary after talking to Nima").
- **where** and **who**: a place and its marker; the cast's keys.
- **source**: what in the plan it came from (scene s1-02, event lament-attacks; quest q-main stage 20).
- **accept**: how to tell it's done.
  - Static checks that can be read from the files: an entity in a scene, a prefab with art, an objective of a type on a
    target, a cutscene file, a script attached, journal text that names the place.
  - Played checks, read from a test run's events: Grey Lament is in Bellwether while stage 20 is on; Ina takes damage
    from Grey Lament; cutscene s1-02 plays; q-main goes from 20 to 30 when Grey Lament dies.

Presence reads the story, the characters and the places together: a character who works in their own shop (a merchant,
the apothecary) is required where they're met, in that shop (`MetAt`), not in the scene's outdoor place.

Requirements are made in two passes:
- **By rule, from the structured plan**:
  - each scene's participants are present;
  - its needsParts are props;
  - each quest objective is a quest requirement;
  - each stage's location gets a guidance requirement (the journal names the place; its gateway can be seen; the
    objective marker points to it);
  - each of ⑦'s system steps is a system requirement.
- **By the AI, from the text**: the requirements step reads each scene's events and beats. It splits them into single,
  testable requirements with their when and accept, and merges duplicates of the ones made by rule.

#### Visual requirements (the game's assets)

Requirements of kind **asset** say which separate pieces of art the game needs and how each one looks. Each carries
`asset: {id, name, type (prop, gateway, item, creature, effect, character), separate, look, size, states, places}`.
- **By rule**:
  - each character in a cast, with the look from its appearance;
  - each scene's needsParts;
  - each item an objective collects, delivers or uses;
  - the way between two outdoor places the story moves between, as a gateway that can be seen (the graveyard gate in
    Bellwether).
- **By the AI from each scene**: what the scene shows that must be drawn on its own (a broken grave the skeleton rises
  from, a spirit-knot of light). It is given every asset already named, so it doesn't name one twice.
- **The assets step** (one call, `/planner/assets`) merges the ones that are the same thing (the east gate, the
  graveyard entrance). It decides which are separate assets and which are only painted with their place's ground (a
  village square, the boundary). It writes each one's look in the Brief's art style and tone, with its size and the
  states the story needs (chained, open; whole, broken).

The Requirements tab shows them under **Assets**: each asset's name, type, size, look, states, places and scenes.

### 34.2 Implementations

For each requirement, one or more implementation items `{id, req, how, does, by, status, made}`:

- **how**:
  - quest-effect (stage onEnter: move, spawn, set hostile, give);
  - objective (type, target, binding, journal text);
  - spawn-point (a marker and when something comes from it);
  - prefab (a prop or gateway made: art, then placed);
  - cutscene (its tracks, its trigger);
  - script (Lua, on whom, reacting to what);
  - dialogue (lines and choices added to a character's conversation);
  - stats (health, damage);
  - art (a picture for a prefab);
  - guidance (journal text, a signpost, an objective marker);
  - as built, also: animation (a clip on a sheet), marker, package (an AI package for a character at a stage) and
    trigger (a cutscene's), so the full set is art, animation, prefab, marker, stats, quest-effect, objective, package,
    guidance, cutscene, trigger, script, dialogue.
- **does**: a short plain description ("a cutscene when Ashen leaves the apothecary in stage 10 with Nima talked to:
  Grey Lament comes out of the graveyard entrance and attacks Ina; it ends as the attack starts").
- **by**:
  - rule: the build does it itself, e.g. quest effects, objectives, markers, stats;
  - ai-step: a Planner AI step writes it, e.g. a cutscene's tracks, a dialogue's lines;
  - ai-panel: the AI panel builds it, e.g. a script, or a prefab with its behaviour;
  - art: the art pipeline draws it, e.g. the graveyard entrance.
- **status**: todo, building, built or failed, and **made**: the files and entities it made.

The AI plans the implementations once for every requirement, scene by scene, from the requirement, the engine's
capabilities (the op lists, cutscene keys and Lua API) and what is already built. The build then fills in the
rule-made ones.

As built: Make the requirements runs four passes in turn: the rule pass, the AI pass per scene, the assets step, and
then the implementations. The implementations are made in two parts:
- **By rule**, from each requirement's kind (`RuleImplementations`):
  - presence: ⑪'s placing, plus the stage's move;
  - prop and gateway: a prefab, after its asset's art;
  - quest: the objective and its journal;
  - guidance: journal text (ai-step), the gateway's prefab, the objective marker;
  - cutscene: its tracks (ai-step) and a trigger;
  - system: an AI-panel script;
  - asset: its art (by art), then a prefab placed in each of its places; a painted one is named in its place's
    painting.
- **By the AI, per scene** (`/planner/implement`): for the requirements it read from the scenes, given the scene's
  markers, cast, stages, the assets and the rule-made requirements. Each item is `{req, how, does, by, after}`; every
  requirement gets at least one.

"Plan the implementations" re-plans them and keeps the requirements. They're stored in `requirements.json`
(`implementations`, `implScenes`) and shown under each requirement in the tab.

### 34.3 ⑫ builds the implementations

⑫ becomes "Implement". It works through the items in dependency order: prefabs and art first, then markers and
spawns, quest effects and objectives, cutscenes, scripts, and dialogue last.
- Each item is ticked as built only when its `made` files and entities exist.
- An AI-panel item's request carries its requirement, its description, its accept checks and the names to use.
- The current features from ⑦ become system requirements built the same way, so there is one list rather than two.

As built (Implement.cpp): after ⑫'s writing the build goes to **implement**. It makes the requirements first when they
aren't made, then builds every item.
- **Order:** items go in rank order (art, animation, prefab, marker and spawn, stats, quest effect and objective,
  guidance, cutscene, trigger, script, dialogue). An item waits for its own requirement's earlier items and for the
  requirements in its `after`.
- **Art:** paid art waits for **Draw them** or **Skip** unless plan.building is automatic.
  - A separate asset is drawn through `/assets/generate`; a creature is drawn and animated as a character.
  - An animation is added to its character's or creature's sheet and drawn there.
  - A thing's animation (a gate swinging open, a glow pulsing, a grave cracking) waits for its asset's picture. It is
    animated from that picture by the thing's animator (`/animations/generate` with `thing`, Project Settings ▸ AI ▸
    Non-humanoid animation). Its frames become an aige.anim/1 sheet: idle loops; open, close and break play once, and
    open/close are each other reversed. The sheet is kept on the asset (`sheets`), and the placed copies play it with
    the prop animator.
- **Prefabs:**
  - A prop stands at its marker in each of its places.
  - A gateway's art is stood on the gateway itself and kept in gateways.json.
- **Guidance:** the journal item adds the place's name to the stage's objective and journal. The objective-marker item
  checks that the setting is on and that the objective has a target.
- **Cutscenes:** a cutscene is written by `/planner/cutscene` (marked `madeBy: implement`, so WriteFiles' own version
  from the beats no longer overwrites it). Its trigger is placed in the same step, with its quest stage as the
  condition: on the way in from where the requirement says it comes from, at its marker, at the scene's story marker,
  or where one arrives.
- **Rule items:** these were already made by ⑪ and ⑫'s writing. They're ticked when their requirement's static
  checks hold; otherwise they go to the AI panel.
- **AI panel:** scripts, dialogue, and anything asked again after a failure go to the AI panel as ⑫'s tasks (the
  features list). The request names the requirement, how it's built, when, where, who, its accept checks, and what
  failed before.
- **Replaces the features step:** when the plan has requirements, ⑫ Implement replaces the old gameplay-features step;
  ⑦'s features are system requirements in the same list.
- **Animations by name:** an animation named in plain words ("the east gate swinging open") finds its drawn asset by
  name. One planned for something with no asset yet adds that asset (`AddAnimatedAssets`), draws it, places it at the
  requirement's marker, then animates it.
- **Implement what's left** (`ImplementNow`, the Requirements tab's button, "Implement" before anything is built):
  failed, skipped and never-started items, and those a stop left half-way (their art or AI-panel task no longer going),
  go back to todo; built ones stay. Their failed drawings (no credits, the service down) are drawn again.
- **No paid redraws from a vision check:** a picture the vision check finds fault with is kept and the finding shown;
  it isn't drawn again.

### 34.4 Validate checks the requirements

- **Static checks** run on every requirement as soon as it's built.
- **Played checks** run in the scene's test. The autopilot's log adds an event recorder: who entered which scene and
  when, damage dealt (who to whom), stage changes, cutscenes played, entities spawned. Each played check is matched
  against it.
- **A requirement is verified** when all its checks hold. Otherwise it fails, and the failure says which check and
  why. Fixing a failed requirement re-plans or rebuilds its implementation items.
- **The AI judge** still reads the log, for what checks can't express ("it looks like the ghost drove her back").

As built:
- **The record:** the autopilot keeps a `record` in its log:
  - who was present in each scene (every second);
  - the scenes entered;
  - damage, by who to whom;
  - the stages entered;
  - the cutscenes played;
  - spawns, barks and the events seen;
  - seconds each character kept within three cells of the player.
- **Checks:**
  - `StaticChecks` reads present, prop/placed, asset/painted, animation, objective, journal-names, gateway-visible,
    objective-marker, cutscene and feature-step from the files.
  - `PlayedChecks` reads present/enters, damages, says, cutscene-plays, stage-advances, event, follows and
    scene-played from the scene's latest test.
- **Status:** a requirement is verified when every check holds and failed when one doesn't (with why). It is built
  when its items are and nothing is against it yet, else todo.
- **When the checks run:**
  - when ⑫ Implement finishes;
  - on Check;
  - when a test's log comes in;
  - in Validate, which lists each failed requirement as a finding with fix `{how: requirement}`. Fixing it is
    `FixRequirements`: its items go back to todo with what failed; a rule item that was built but still fails goes to
    the AI panel.

### 34.5 The Build page in tabs

The Planner's build page becomes tabs instead of one long column:
- Places (⑩)
- Characters (⑪)
- Requirements
- Quests
- Implement (⑫)
- Validate
- Story & Test
- Result

The Requirements tab lists requirements by act and scene. Each shows its status (todo, built, verified, failed), its
implementation items, and what was made. There are filters by status and kind, and actions to Build, Check, Fix and
Test the scene.

As built: the tabs are Places, Characters, Quests, Requirements, Cast (34.7), Implement, Validate, Story & Test and
Result. The Requirements tab has **Make the requirements** / **Make them again**, **Plan the implementations**,
**Implement** / **Implement what's left**, **Check** and **Fix the failed (N)**; filters by status, kind and by rule or
from the scenes; **implementations** shows each requirement's items. Its views:
- **By scene**: each requirement's kind, text, when and where, its checks (✓, ✗, ? a played test tells) and **Fix**.
- **Assets**: each separate asset (name, type, size, look, states, places, scenes) with its animations and their status.
- Paid art (assets and animations) waits on a bar with **Draw them** / **Skip**, unless plan.building is automatic.
- **Fix** puts a failed requirement's items back to todo with what failed; a rule item that was built and still fails
  is re-queued to the AI panel with the problem.

Since 34.17 the tabs are Requirements, Places, Characters, Props & objects, Quests, Placements, Behaviour, Scenes,
Systems, Visuals, Validate, Story & Test and Result; Implement is gone, its work split across them.

### 34.6 Guidance, in every game

For each stage the player has to go somewhere, three requirements are always made:
- The journal and objective text name the place in the player's terms ("the graveyard gate, north of the square").
- The way there can be seen: a gateway has a prefab with art, not a bare tile.
- The objective marker points to it.

Lines like "Reach the east gate" in a village with no visible gate are caught this way, in every game.

As built:
- **The way there can be seen:** the rule pass makes a gateway asset for each move between two outdoor places, and
  Implement stands its art on that gateway. Its check (gateway-visible) fails while the way in is a bare tile.
- **The journal names the place:** the journal item writes the place's name into the stage's objective and journal.
- **The objective marker** is checked against Project Settings ▸ Story and the objective's target.

**The AI panel:**
- The `requirements` tool reads planner/requirements.json, filtered by id, scene, status, kind or words. It returns
  each requirement's when, where, who, accept checks, their results and its items.
- Its prompt says to build exactly what a named requirement says and make its checks hold.
- **open_scene {path}** opens the scene a request is about, saving the open one first, so its lookups and edits are
  made there. Every edit applies to the open scene. Before this, a request about Bellwether made while Reed Apothecary
  was open put the graveyard gate and Ina Crook in the apothecary, and a later attempt edited an entity id from
  another scene.

### 34.7 The cast timeline and character validation

Who is where, doing what, at each quest stage is design, so it's decided in the plan, not guessed by the build.
Before this, the build moved every scene participant who lives elsewhere to the scene's place, which walked Bera,
Torren and Nima out of their shops into Bellwether.

- **⑥b Cast timeline** is a planner stage after ⑥ Places, once the quests' stages and the places exist. One AI step
  (`/planner/timeline`) reads:
  - the written scenes: where each happens, its cast, its events;
  - the quests' stages;
  - the characters: home, roles, packages, what they talk about;
  - the places: what's inside what, and who stands or works in each.

  For each character it writes a start state `{place, spot, packages, behaviour, dialogue}` and the changes, each
  `{quest, stage, on: enter|complete, only what changes, why}`. It's checked against the plan's characters, places,
  quest stages and package templates. It's stored in `planner/timeline.json`, stale when its inputs change,
  regenerated like any document, and shown as a grid.
- **Build:** each start and each change is a `character-state` requirement. Its accept checks are located, package,
  behaviour and dialogue, each at its quest stage (the start at the first stage of q-main).
  - Presence requirements take the character's place at that stage from the timeline.
  - When the plan has a timeline, `StoryStaging` writes its moves and behaviours as the quests' stage effects:
    move_actor to the place's scene at the spot's marker (else where one arrives); set_hostile, set_friendly,
    follow_player, stop_following, hide_actor and show_actor. A change on a stage's completion is done as the next
    stage begins.
- **Implement:** each part of a state is an item that's checked first. One that doesn't hold goes to the AI panel,
  which decides how to make it so: a stage effect, an AI package that starts at the stage, dialogue conditioned on
  it, or a script.
- **Checks:**
  - Static: the quests' stage effects are followed from where ⑪ placed the character. The packages on their entity
    must include one of that kind, and their dialogue must talk about the topic.
  - Played: the autopilot records `stageStates`, a snapshot of the cast as the test starts and a second after each
    stage begins. Each character's snapshot holds its scene from the roster or the scene, hidden, side, dead, its
    packages and the one running, and whether it follows the player.
- **Cutscenes in the timeline:** it also plans every cutscene: `{id, name, scene, quest, stage, on, place, trigger:
  {kind: stage|reach|leave|talk-end|event, ...}, cast: [{who, role, does}], beats, after}`. Every scene whose mode is
  cutscene must have one. Each becomes a `cutscene` requirement (`cs-<id>`, under its story scene), which replaces
  the scene-mode rule requirement for that scene.
  - Implement writes the cutscene from that plan (`/planner/cutscene` is given its cast, what each does and its
    beats).
  - Its trigger is placed by rule for stage, reach and leave triggers (on the way out of the place it says, at its
    spot). The AI panel wires talk-end and event triggers.
  - The check `cutscene-cast` reads the cutscene file: each of its cast is in an actor track, or speaks or talks in
    it.
- **When quests overlap:** two quests can hold one character at once (the main quest's stage 10 and a side quest's
  stage 10). Each change carries a `priority`: `story` (a scene needs it), `normal` or `ambient`. Quests are ranked
  by that priority, then the main quest before the side quests, then the side quests in their order
  (`TimelineRank`).
  - **Packages are all kept:** Populate makes a record for each quest's span of stages (`TimelinePackagePlan`), with
    the condition `{quest, stages: [...]}` and its place as the location. Once a quest is complete, its last state
    holds through a copy conditioned on `completed`. The records are stacked in rank order before the character's
    own day, and the engine runs the first one that holds (PackageHolds now reads `stage` and `stages`). So a side
    quest's follow package runs only while no higher-ranked quest's package holds.
  - **Where they are follows those packages:** the roster puts an actor in the scene of its active package's
    location. The stage's move effect is written only for the top-ranked quest that moves them.
  - **How they behave** (hostile, following, hidden) is the top-ranked quest's. A lower-ranked quest's behaviour
    effect isn't written.
  - **Dialogue and cutscenes are all kept.**
  - A lower-ranked change that's overridden says so ("yields to the higher-ranked quest for where they are: q-main")
    and isn't checked for that part. ⑥b and the Cast tab mark it ⇅, and its detail says which quest wins.
- **The grid:** there's a tab per quest (main, then each side quest), with a column for each of the quest's stages
  (and a stage's completion where something changes then) and a horizontal scrollbar. A Cutscenes row sits on top,
  and short cells open their full detail on click: where, what they do, the packages with their places and hours,
  behaviour, what they talk about, why, the cutscenes they're in, and, in the build, each check and Fix.
- **The Cast tab** (character validation) shows the grid of characters by stages: what each cell should be, ✓, ✗ and
  why, with **Fix** and **Fix the failed**.


### 34.8 Play from here and Test start at the stage

- ▶ Play from here and Test start where that story scene's quest stage begins (`TestStart`): every quest at its stage
  then, earlier stages' effects applied, and the player where that stage begins: where the last objective of the stage
  before it is done. q-main stage 20 is set by talking to Nima Reed, so s1-02 starts beside her in Reed Apothecary,
  the journal reading "Reach the shut east gate…". Earlier scenes aren't played again.
- The autopilot walks between scenes through the gateways, by the shortest route (`PlanHop`: a breadth-first search
  over each scene's gateways); it only teleports when no route on foot exists.
- `build_scenes <project> --start <story scene>` prints where Play from here starts.

### 34.9 Fixes from building Haunted Village

- **Duplicate NPCs:** one copy per character per scene is kept; the AI panel's requests are told who's already
  placed.
- **Merchants in their own shops:** placed only there, and met there (34.1).
- **Interiors' way back out:** each interior gets its gateway back out and its arrival marker at its entry door; the
  vision model finds a door one can walk through in its painting (`/places/find-way-out`). A dungeon's way out is at
  its entrance (its stairs, else the middle of its entrance room).
- **Gateway assets** are named by a slug of their name ("way-to-haunted-graveyard"), as the art service takes it.
- **Vision check failures** no longer trigger paid redraws (34.3).
- **Painting waits for approval** of its call count (31.6a).

### 34.10 The AI panel and the editor

- **Stop:** a Stop button beside "Thinking…" while the AI works; nothing is changed.
- **Routing:** the AI panel does requests itself. Only a whole-new-game design asks "Open the Planner with it" or "Do it
  here in the AI panel", and only building a brand-new place asks about the Build panel. A request that names a scene
  the project has ("in Bellwether"), or edits a place that exists, is never routed. The plan-revision route is gone; a
  Planner route while the project already has its game is asked again, once, to do it here. Tool rounds are capped at
  100 (a safety stop only).
- **New tools:** `requirements` (34.6) and `open_scene`.
- **Out of credits:** when any provider (PixelLab, OpenRouter, BFL, Anthropic, OpenAI, Gemini, AutoSprite…) answers
  that it's out of credits or over quota, a red banner names it and stays until closed (**Show what it said**); once
  per provider per 15 minutes; also in Notifications (`AiClient::NoteCredits`, `CreditBanner.vue`).
- **Edit ▸ Search in Project** (Ctrl+Shift+F, `ProjectSearch.cpp`, `ProjectSearchDialog.vue`): text across scenes,
  prefabs, packages, records, story, scripts, UI, art (by file name) and other files; filters by type; results grouped
  by file, each opening where it's edited.
- **Show uses:** right-click a prefab, art, scene or a scene's thing (its prefab); the Records page's button for records
  and packages; story rows; cutscenes; the Scripts tab's **Uses**; the UI tab's **Show uses**.
- **The Scene panel's search box** filters things by name, kind and prefab.
- **Label types:** name, place, sign, status, quest and danger, each with its own HUD look; the UI designer recolours
  each type.

---

### 34.11 Placements: who lives and works where, and where everyone is over time

Haunted Village put every villager at the middle of the painted Bellwether: ⑪ ran before the painting's buildings were
found (structures.json, written by the gateways step minutes later), so no building was known, the story's anchors had no
place in the painting either, and everyone fell back beside `arrive`; the story's scene markers were then made beside the
same point. As built (`engine/gamebuild/Placements.cpp`, `planner/placements.json`):

- **Buildings found**: each building of the built places (a painted place's where the vision model saw it, an assembled
  one's where its plan put it), its door, and the interior scene a gateway by its door leads into; the well, gate,
  graves and fields as places to stand by.
- **One AI call decides** (`/planner/placements`): each character's home, work, spot (inside a drawn interior, else at
  the door; outdoors where their part is: the guard at the gate, the gravedigger by the graves, the boss at what it
  guards; never the arrival by default) and whereabouts over time (from the start / a quest stage / an event, until the
  next), and each story prop's and quest item's spot (the family seal by the cracked grave marker, the record board in
  the square) and when it moves (picked up). Checked against the real ids; the rules decide what it leaves out, and all
  of it without the AI. A crowd the cast timeline starts at one spot is the story's gathering: met at home or work,
  brought there by a whereabouts when its scene's stage begins.
- **⑪ places by it**: the characters' home markers, the quests' item and spot aliases, the requirements' props, and the
  story's scene markers (where each one's whereabouts has them then, else where the scene happens).
- **When**: before ⑪ (unless a painted place's buildings aren't found yet), again once they are, after Mark again of a
  Planner place, and on **Replace Characters** (the AI Planner build page's Placements tab, before Implement; `build:replaceCharacters`); entries
  the user edited (`build:setPlacement`, locked) stay.
- **Requirements**: one `whereabouts` item per character per span ("Elder Sella is at village-boundary in Bellwether
  from q-main stage 80"), checked against the scene (the marker; at the start, standing on it) and the quest (its
  move_actor, or a package, by that stage) and in Test (in that scene, near the marker, as the stage began); built by
  rule (the stage's move_actor) or by the AI panel (a move on an event). Props: `prop-at`.
- **AI panel**: the `placements` tool, the open scene's placements in the context, the `placement.set` command.
- **build_scenes** `--replace-characters`: the same by the rules, without the editor.

### 34.12 Placements II: where one can stand, the roads, the gateways, the props

- **Walkable and visible**: a spot is a cell one can walk to from the arrival, not blocked, not a gateway's, not in a
  building's or a painted thing's box, and not hidden behind a painted building (the cells its picture covers behind
  its ground, across no further than its ground: `HiddenCells`); no two characters on one cell. The plan's land the
  painter built over (crop beds under a painted house) is dropped.
- **Anchors where the painting has them**: a square's anchors at the painted square (its stalls and wells), a house
  anchor ("record-table" at "lysa-house") at the door of the character's painted home.
- **Markers**: `start-<slug>` (where they stand when the story starts), `home-<slug>` (their home's door, or inside it),
  `work-<slug>`; Build ▸ Cast's packages run at them (sleep at home, work at work, a marker the timeline names).
- **Props**: each faces what the AI says (a building, a marker, a character, n/e/s/w; mirrored when that's nearer);
  each asset prop stands in its placed scene as an instance of `prefabs/props/<name>.prefab` (its asset's picture),
  plain copies taken out ("Record Table" is record-table).
- **Paths**: each outdoor place's roads (a painted place's: the ones its painting keeps) as `road-<id>` paths, and a
  `to-the-<side>` marker where one leaves the scene; the AI puts each gateway between outdoor places on the way out it
  belongs at (kept in `planner/gateways.json`), the story anchors by it moved with it.
- **The editor**: the clicked cell painted (a building's box and door too); Other (squares, graveyards, fields, anchors),
  By stage (where everyone is while a quest stage plays, and where they go once it completes), Paths tabs; the player
  is never taken out by Replace Characters (its prefab put back when it was).

### 34.13 Routines and the cast timeline's reasons

④ Characters have a **routine** (their usual day: a summary, the places they keep to and when, where they go once a
scene is over); ⑥b gives every state a **why** and is checked that nobody stays at a scene's anchor after its stage.
A repair answer the model gave with only what it fixed is merged into the answer it repairs (`_merged`), and ⑥b refuses
a timeline that leaves a cast member out.

### 34.14 Play: objectives, quests at the start, Esc

Every open objective of the shown stage is marked on the ground under its target (off screen: at the screen's edge,
`HudData::markers`); a quest can start with the game (`"atStart"`, Project ▸ Story ▸ Quests; the build ticks it on a main
quest nothing starts); Esc in the editor's Play is the game's (closes the open window, else the pause menu).

### 34.15 The Planner's pages, Revise, Input, Visuals

- **Pages grouped**: ① Concept · ② Story (Outline, Acts & scenes) · ③ World (Characters, Places, Props & objects) ·
  ④ Quests · ⑤ Life (Cast timeline, Small scenes) · ⑥ Systems (Design, Input) · ⑦ Visuals · ⑧ Validate · Build; each
  description has its implementation on a Build tab of the same name.
- **Revise** at the bottom of every step and of Build's Requirements, Quests, Placements, Cast and Implement: the AI
  finds which documents and fields the words are about, then edits only those (`_revise_step`); each keeps its history
  (`planner/revisions.json`, `planner/build-revisions.json`).
- **⑦ Systems ▸ Input**: the game's controls designed whole (every action, its keys and pad buttons, context, screen;
  the scripts' names for the same thing as one), checked for clashes, written into input.json by the build.
- **⑧ Visuals**: the interface's look (palette, materials, typography, ornaments, motion) and every panel designed in full
  (layout, components, look, images, RCSS); **Build ▸ Visuals** draws its images and writes each panel as an RML document
  (`/planner/uipanel`), in the project's screens manifest.

### 34.15b The places laid out by the Planner

⑥ Places gives each place its layout: its zones, each a component of its Brief with what it's like (`feel`, which was
`character`), where it lies (words, and relations of the place layout's vocabulary: in-north, east-of a zone...), its
share of the place, its buildings by type and count (and whose) and its features. They go into the Brief as `layout`
(with the exits); `plan_root` makes a zone for each where it says and as big as its share, `plan_zone` its buildings as
its parcels.

### 34.16 The world: props & objects, small scenes

- **World ▸ Props & objects** (stage `world`): every thing someone uses, its purpose, states, what characters do at it in
  their day (SmartObject) and every use (by whom, casual or for a quest, when, why) with the engine's Reaction for it.
  **Build ▸ Props & objects** makes each by rule: its entity and prefab, the SmartObject, a Reaction per use (a companion
  entity for each after the first), each state drawn as the prefab it becomes, its users' quest uses as requirements.
- **Life ▸ Small scenes**: casual or quest-related scenes that play round the player (where, what starts them, how often,
  the quests' stages, the cast, the beats). **Build ▸ Small scenes** writes each as an ambient cutscene and a
  `player-near` Reaction where it plays, one per quest stage.
- **Animations**: each thing describes every clip its Reactions play (who: the thing, the player or a character; what it
  looks like), a Reaction's clip one of its own and its user clip one of its users'; each small scene the clips its beats
  play. The build draws the thing's from its picture (its Animator, the clip under the name its Reactions use) and adds
  the users' to their art; the art step keeps the name each clip was drawn under (the animator's), and `ReconcileClips`
  writes it into the Reactions and the small scenes' cutscenes.
- **The engine**: ambient cutscenes (`"ambient": true`: the player keeps control, lines over heads, camera keys skipped,
  its cast held from their packages; one at a time beside a cutscene), the `player-near` trigger.

### 34.17 The Build page as the implementation

The Build page is the implementation: each tab builds one thing, to the requirements, in this order (the order
**Continue building** runs too):
1. **Requirements**, first, from the plan alone, before anything is built (each place's scene file is known from the
   build plan): who is where and when, what each character is animated doing (from its class and whether it fights),
   the quests' steps, props, cutscenes, systems; the AI reads each story scene for what the rules miss. Each
   requirement's implementation items belong to one of the tabs below. **Make the requirements** / **Make them again**
   while the build is stopped makes only them, then stops; Continue building goes on from where the build was.
2. **Places** (⑩): the scenes, roads, gateways.
3. **Characters**: character art and animations only (each drawn once, then animated for what the requirements ask;
   clips already drawn kept), enemy classes too. Item icons moved to Props & objects.
4. **Props & objects**: their art (item icons, doors and ways through too) and animations, each made in its scene with
   its SmartObject and Reactions, and the requirements' prop and asset items.
5. **Quests**: the game's quest files (`quests/*.json`): stages, objectives, stage effects, journal text, objective
   markers (guidance).
6. **Placements**: characters, props and objects placed: the rules first, then the AI decides where exactly each is,
   given the where-requirements and naming the requirement each spot meets; the quests' roles bound to what's placed;
   the game's files written.
7. **Behaviour** (was Cast): AI packages, routines, per-quest-stage changes (⑥b as requirements), dialogue (one per
   character the player can talk to).
8. **Scenes** (was Small scenes): story scenes' triggers, cutscenes (tracks and triggers), the Life page's small scenes.
9. **Systems**: ⑦'s scripts, through the AI panel.
10. **Visuals**: ⑧'s panels (HUD, inventory, dialogue, menus), their styles and images, and the interface art.
11. A final pass for anything left, then **Validate**, **Story & Test**, **Result**.

There is no Implement tab: its work is split across these tabs, and the AI panel is the tool a tab uses for what its
rules can't build. After each tab the requirements that already hold are marked verified (never failed there);
Validate checks them all at the end, and **Fix** / **Build what's left** builds the failed ones again. Each tab shows
its own implementation items.

## Later

Phases after 5 are planned as development goes (2026-10-17): each new phase is written
when we start it, pulling in what it needs from this list and from testing. Nothing here
is scheduled or numbered. (Phases 12–24 above, added 2026-10-21, are planned ahead because
they define the finished product; their milestones will still be refined when each starts.)

### AIGE asset library

A curated, pre-analysed asset collection, organised in style families, with catalog v2
metadata, ontology links (roles, concepts), licences and retrieval embeddings, installable
per project. Phase 7 defines its interface (`place-assets.md` §2); building the library
itself is separate work.

### Engine expansion

The long-term direction: local models, mobile, and the first real 3D rendering path
(`technical.md` §8's "path to 3D"), moving the engine from "isometric indie engine"
toward the general-purpose ambition in `product.md` §6.

**Local models** *(was 4.7)*
- An OpenAI-compatible provider, which covers Ollama, llama.cpp server and LM Studio,
  chosen per pipeline: writing, checking and embeddings. Example: proposals on
  Mistral, contradiction checks on a local model.
- Tests: the provider against a fake OpenAI-compatible server, and configuration.

**Platform layer and mobile groundwork** *(was 5.2)*
- Input, windowing, file paths and the game loop go behind `platform/` interfaces,
  so a mobile front end can plug in. There's a touch-input mapping (virtual stick and
  buttons) usable on desktop for testing, and an Android CMake preset.
- **Blocked here:** building and verifying an Android or iOS export needs the Android
  SDK/NDK or Xcode, which this machine doesn't have. The milestone delivers the
  groundwork and documents the setup steps; the export itself waits for the
  toolchains.

**3D rendering path v1** *(was 5.3)*
- `renderer3d` on the same RHI: a mesh pipeline with depth, a perspective camera,
  directional and ambient light, and textured meshes from glTF 2.0 (cgltf).
- `Transform3D`, `MeshRenderer`, `Camera3D` and `Light` components are registered
  (Inspector, scenes, AI).
- A 3D sample (`apps/sample3d`, `projects/sample3d`) with an orbit camera, a lit
  textured model and simple primitives.
- Tests: transform and camera maths, glTF loading (meshes, materials, a bad-file
  error), components in a scene round trip, and a headless render smoke test.

**3D in the editor (first step)** *(was 5.4)*
- The editor's viewport renders 3D scenes, with orbit, pan and zoom and picking by
  ray. The Inspector and AI work on 3D entities unchanged, through reflection.
- Tests: the ray-picking maths, plus the checklist.

Done when: the AI can run on a local model; a 3D scene can be built, edited and lit;
mobile is ready to be finished when the toolchain is available.

### Editor and content (from testing)

**Rotating prefabs** *(deferred 2026-10-17; now Phase 6.6)*
- Like resizing: Shift+wheel, R and Shift+R turn the brush (and its preview) while one
  is armed, else the selection.
- Flat (top-down) images turn a quarter. Upright ones mirror, since an upright picture
  only has one side.
- Shown on the toolbar next to the brush size.

**Multi-cell tiles and objects** *(proposed 2026-10-16, not decided)*
- **Footprints:** a prefab covers W×H cells. Painting or placing claims them all, a flat
  image stretches across the area, collision covers every cell, and erasing removes it
  as one piece. The preview shows the whole footprint.
- **Stamps:** "Slice into tiles" cuts an image into cell-sized tile prefabs, with a
  stamp brush that paints all of them in one click.

---

## Where we start

Phase 0 is unambiguous and doesn't depend on any open question being resolved except the
two called out in `technical.md` §17 (isometric rendering approach, ECS library choice)
— both are short spikes, not blockers to starting. Suggested first concrete step:
milestone 0.1 (build skeleton + window), since everything else in Phase 0 stacks on it.
