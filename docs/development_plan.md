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
        `docs/story.md`) and `StoryLibrary`, which loads and validates them: parse
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
    `lantern.setAll`, `lantern.scaleRadius`). Guide: `docs/modules.md`.
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

## Later

Phases after 5 are planned as development goes (2026-10-17): each new phase is written
when we start it, pulling in what it needs from this list and from testing. Nothing here
is scheduled or numbered.

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

**Rotating prefabs** *(deferred 2026-10-17)*
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
