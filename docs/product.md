# Product Overview — AI-Native Game Engine (Built From Scratch)

## 0. Relationship to the Godot-based sibling project

`docs/godot/` (read-only reference in this repo) describes a *different* project: an
AI-native layer built **on top of Godot** — modules and editor plugins that make an
otherwise-unmodified Godot into an AI-native tool. That project ships faster and proves
out AI-native UX patterns (Diff Review, Mechanic Composer, World Bible) on a mature
runtime.

**This project is the ground-up sibling.** Here, we own every layer — renderer, physics,
ECS, scripting, editor, build pipeline — written from scratch in C++. The two projects
share a product thesis (AI as a first-class citizen of authoring and runtime, not a
plugin bolted onto a manual pipeline) and will likely converge in spirit over time, but
this one exists because owning the full stack is the only way to:

1. Make **every single layer** of the engine natively introspectable and AI-editable —
   not just the layers a host engine happens to expose a plugin API for.
2. Remove any long-term ceiling imposed by someone else's engine license, architecture,
   or roadmap, since the ambition here is to eventually stand alongside Unity/Unreal as
   a general-purpose engine, not stay a third-party layer forever.
3. Control the full data model (scene format, component schema, asset pipeline) so the
   "natural language can adjust anything" promise is architectural, not aspirational.

The cost is obvious: we're rebuilding what Godot/Unity/Unreal already have. See
`development_plan.md` for how that cost is sequenced so something playable and
AI-native exists long before the engine has AAA-grade rendering.

## 1. What this is, in one paragraph

An **AI-native game engine**, built from scratch in C++: one editor where a developer
can describe what they want in natural language — and the engine turns that into real,
inspectable project files (scenes, components, scripts, assets) — or edit everything by
hand with the same tools any hand-built engine would offer, mixing the two freely and
choosing how much the AI does on its own. Every module, every
inspector field, every asset, every piece of gameplay logic is exposed through a single
**reflection and command layer** that both the manual UI and the AI service read from —
so natural language can query or modify *anything* the engine knows about, not just a
curated subset of "AI features." The engine starts narrow — desktop, 2D/2.5D isometric
indie games — and is architected from day one to grow into a general-purpose engine
with mobile and full 3D support, without a rewrite.

## 2. Why build a new engine at all — landscape check

The competitive/AI-tooling landscape is the same one described in `docs/godot/product.md`
§2 (Unity Muse, Promethean AI, Inworld, Scenario.gg, Ludo.ai, Roblox Cube, AI Dungeon) —
point tools bolted onto existing pipelines, none of them exposing the *whole* engine to
natural language. That analysis still holds and isn't repeated here. What's specific to
*this* project is the additional axis: none of Unity, Unreal, or Godot were designed with
an internal reflection/command layer built for AI from day one — retrofitting one onto
them means every new engine subsystem (a new renderer feature, a new physics param) has
to be manually wired into the AI layer as an afterthought. Building from scratch means
the reflection layer *is* the engine's extension mechanism — new modules get AI
adjustability for free, by construction, because that's how you add a module at all.

## 3. Core design thesis: AI-native means "reflection-first," not "chatbot-first"

The single most important architectural idea in this product: **every module exposes its
editable state and its available actions through a common reflection registry**, and both
the manual editor UI and the AI Orchestration Service are just two different clients of
that same registry. Concretely:

- A renderer setting, a physics material parameter, a component field, an animation
  graph node, a build/export setting, an editor preference — all of it is registered
  the same way, with a type, a human-readable description, valid ranges/constraints, and
  (where relevant) a live value.
- The manual editor's inspector panels are **generated from** this registry, not
  hand-wired per-field.
- The AI service never has special-cased knowledge of "the renderer" or "the physics
  system" — it queries the registry like any other client, which is what makes "every
  module and every feature must be adjustable using AI" true by construction instead of
  a per-feature promise someone has to remember to keep.

This is the mechanism behind the product requirement that **any field, anywhere in the
engine, can be changed with a natural-language instruction** — see `technical.md` §7 for
how this is actually implemented.

## 4. One editor, adjustable AI

There are no modes. (Early plans had an "AI-Driven" and an "AI-Assisted" mode; in
practice a mode only rearranged panels while the engine, files and workflow stayed the
same, so it was dropped in Phase 2 — see `development_plan.md` 2.6.) Instead there is
one editor, and the developer decides how much the AI does.

### 4.1 Direct editing
Traditional direct-manipulation editing: scene tree, viewport, inspector, script editor,
node-graph editors — the same surfaces a Unity/Unreal/Godot user would expect. The
editor must be **fully usable with AI turned off entirely** — a hard product
requirement, not a nice-to-have, so the engine is a legitimate manual game engine on its
own merits, with AI as an accelerant rather than a dependency. Turning AI off (a per-user
preference) stops the AI service, so nothing is sent to a model.

### 4.2 The AI panel
A chat panel, collapsible to a thin rail, where the developer describes what they want
("add a dodge-roll with i-frames," "make this dungeon 30% bigger and darker," "this NPC
should remember if the player stole from them"). The engine turns each request into a
structured proposal against the reflection registry — new components, modified
resources, generated assets, edited behavior graphs — shown as a diff for review. AI is
also reachable from any field or panel ("Ask AI", see §3), but nothing is proposed
unless asked for.

### 4.3 Automation grows inside the panel, not as a mode
More autonomy comes as choices the developer makes, not a different editor:
- **Review policy** — which changes apply without review (e.g., simple field tweaks)
  and which always need it (scripts, file writes, deletions).
- **Bigger tasks** — one request carried out in several steps (write, playtest, fix,
  repeat), with progress shown in the chat and one reviewable result.
- **Minimal AI** — AI off, or collapsed out of the way and only used through Ask AI.

### 4.4 One set of files
Hand edits and AI edits operate on **exactly the same project files** through the same
write path — there is no "AI project format," no conversion step, and no record of who
made a change other than the AI decision log. The reflection registry and the on-disk
file formats are the single source of truth.

## 5. Core product pillars

### 5.1 Universal Natural-Language Adjustability
Every module (§3) exposes itself to NL editing. This is the differentiator called out
explicitly by the product requirement and is treated as the engine's core architectural
invariant, not a feature of any one panel.

### 5.2 Structured-Data-First Authoring
Wherever plausible, things AI generates are **structured data** (typed component
records, resource files, node graphs) rather than free-form code — structured data is
easier to validate, diff, review, and safely regenerate. Free-form scripting (Lua) exists
for logic that genuinely needs it, but quests, dialogue, behavior trees, and item/enemy
definitions are graphs/records by default. See `technical.md` §8.

### 5.3 AI Diff Review (universal)
Every AI proposal, from any module, from any panel, funnels through one review surface:
propose → preview → accept/edit/reject → commit. Nothing is written to a project file
without becoming a normal, human-editable file at the moment it's accepted.

### 5.4 Runtime Content Synthesis (optional, per-system)
As in the Godot-based sibling project: dynamic quests/dialogue grounded in a World
Bible/lore graph, with contradiction-checking, fallback-to-static as a hard requirement,
and per-system opt-in — a shipped game can run with zero AI calls if every system is
locked to authored content.

### 5.5 Procedural Asset Generation
Text-to-sprite/text-to-3D generation, variant families from a hand-authored "hero" asset,
every generated asset versioned with prompt/seed/model for reproducibility, landing in
the project as ordinary asset files with no runtime AI dependency.

### 5.6 Manual authoring is never a second-class path
Same guarantee as the sibling project: AI-generated artifacts are plain project files
the instant they're accepted, and the product must be fully usable with every AI feature
switched off.

## 6. Target genres (v1) and long-term scope

**v1 focus — same two genres as the sibling project, for the same reason (they reward
rich, varied, hand-craftable content that AI helps fill in at volume/depth):**

1. **Isometric Action RPG** (Diablo/Sacred/Divine Divinity-like) — dungeon layouts,
   itemization, enemy variants, boss encounters, loot tables.
2. **Cozy / Peaceful Life-Sim** (Stardew Valley-like) — daily NPC schedules, relationship
   dialogue, seasonal events, crafting/farming content.

**Long-term scope — this is the key difference from the sibling project's roadmap:** the
engine is architected from the start (rendering abstraction, ECS, reflection layer,
scripting) to grow into a **general-purpose engine**: full 3D, mobile export, and
eventually genres and project types well beyond isometric indie games. Isometric 2.5D is
the *first target*, not an architectural ceiling. See `technical.md` for exactly which
decisions are made "general from day one" vs. "deferred but not designed away."

## 7. Application pages / editor surfaces

| Page | Purpose |
|---|---|
| **Project Dashboard** | Create/open projects; genre templates (Isometric ARPG, Cozy Sim, Blank); recent projects; AI provider/budget status |
| **World Bible / Lore** | Canonical source of truth: characters, factions, locations, timeline, tone/style guide; human-editable, AI-grounded |
| **Scene / Level Editor** | Native viewport + inspector, with the collapsible AI panel beside it, all on the same underlying scene |
| **Reflection Inspector** | The generated, per-field inspector described in §3 — every property of every module, each with an inline "Ask AI" affordance |
| **Mechanic & System Composer** | Chat panel: describe a mechanic/system → structured component/system diff → review → accept/edit/reject |
| **Quest & Dialogue Designer** | Visual node graph for quests/dialogue; "scaffold from prompt"; live lore-consistency checker |
| **Asset Generator Studio** | Text-to-sprite/text-to-3D; batch variants; approve/reject/regenerate gallery |
| **NPC & Behavior Designer** | Personalities, schedules, relationships; behavior-tree/graph editing; dialogue preview |
| **AI Diff Review** | Global feed of every AI proposal across every module — accept/edit/reject, full audit trail |
| **Script Editor** | Lua editor for hand-written or AI-scaffolded gameplay scripts, with hot reload |
| **Playtest / Preview** | Run the game in-editor; toggle live AI runtime synthesis on/off |
| **Build & Export** | Desktop first (Windows/macOS/Linux); mobile export surfaced once the rendering/platform layer supports it |
| **Settings** | AI provider credentials, cloud vs. local model per pipeline, spend limits/usage, telemetry, engine/editor preferences (also reflection-backed, also AI-adjustable) |

## 8. Steps to develop a game with this product

1. **Create a project** from a template (Isometric ARPG, Cozy Sim, Blank).
2. **Seed the World Bible** via prompts; review/edit generated canon.
3. **Block out the world** — hand-paint tiles/levels, or describe them and accept/edit
   an AI-generated first pass.
4. **Author core mechanics** via the Composer or by hand in the Reflection
   Inspector/Script Editor; every generated component reviewed before it's applied.
5. **Populate NPCs** — generate portraits/sprites, define personalities/schedules in the
   NPC Designer.
6. **Write the spine of the story by hand**; let AI scaffold secondary content bounded
   by the World Bible.
7. **Generate asset variety** via the Asset Generator Studio, curating what's kept.
8. **Playtest** with runtime synthesis on/off; fix lore inconsistencies via Diff Review.
9. **Lock content** per-system: dynamic (replayability) vs. frozen (predictability,
   performance, offline play).
10. **Polish by hand** as with any game.
11. **Export** for desktop; mobile once the platform layer is ready (see
    `development_plan.md`).

## 9. Non-goals / guardrails

- **Not** "type a sentence, get a finished game." A human reviews every change that
  matters to the shipped product.
- **Not** required to be online to ship — AI is a development-time and optional
  opt-in runtime feature only.
- **Not** replacing artists/writers — first-draft volume for teams too small to hand
  produce it, not a substitute for taste and final polish.
- **Not** attempting rendering/physics/tooling parity with Unity/Unreal in the near
  term. The long-term ambition is real, but v1-v3 deliberately stay inside the scope a
  small team can actually build and ship (see `development_plan.md` for the phasing that
  makes this honest rather than aspirational).

## 10. Roadmap (product-level; see `development_plan.md` for engineering detail)

| Phase | Product-level focus |
|---|---|
| **0 — Engine Bootstrap** | No AI yet. A window opens, a triangle/sprite renders, an entity moves, a project can be saved/loaded. Proves the from-scratch foundation before any AI investment. |
| **1 — Manual Isometric Editor** | A genuinely usable, manual-only isometric 2.5D editor and runtime: scene editing, sprites, basic 2D physics, Lua scripting, save/load. This must stand on its own as a real (if minimal) game engine. |
| **2 — Reflection Layer & AI Editing v1** | The universal reflection/command registry goes in; AI Orchestration Service connects to it; the AI panel (Mechanic Composer) and AI Diff Review ship. |
| **3 — Content Depth** | World Bible/lore graph, contradiction checking, dungeon layout generation, NPC schedule/behavior generation, cozy-sim content generation. |
| **4 — Polish & Ecosystem** | Editor shell and workflow, performance passes, plugin/module groundwork. |
| **5 — Shipping & Workflow** | Desktop export, top-down art on the isometric grid, multiple connected scenes, a start window, editor refinements from testing. |
| **Later** | Phases after 5 are planned as development goes. On the list: local-model support, mobile export hardening, a full 3D rendering path, broader genre support — the "become a general-purpose engine" direction. |

Phases 0-2 are the load-bearing ones: nothing AI-native is credible until there's a real
engine under it, and nothing "AI-native by construction" (§3) is possible until the
reflection layer exists. `development_plan.md` breaks these into concrete milestones and
testing checkpoints.
