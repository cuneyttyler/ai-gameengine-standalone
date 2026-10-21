# Planner schemas — the Game Brief and the Planner's documents

Status: **draft for review (2026-10-21)**. The input schema of the AI Planner
(`ai-planner.md`) and the documents each stage writes. Field types reuse
`place-schema.md` §2 (quantities with numbers, qualities from closed vocabularies);
vocabularies for story terms come from GSO (`story-ontology.md` §8).

---

## 1. The documents

Everything lives in the project under `planner/`:

| File | Schema | Written by | Read by |
|---|---|---|---|
| `brief.json` | `aige.game-brief/1` | ① Concept | every stage |
| `outline.json` | `aige.outline/1` | ② Outline | ③–⑥ |
| `kg.json` | `aige.story-kg/1` (`story-ontology.md` §6) | ②–⑤, edits | every stage from ③ |
| `story/acts.json`, `story/scenes/<id>.json` | `aige.story/1`, `aige.scene/1` | ③ Story | ④–⑥, ⑨, ⑫ |
| `characters/<id>.json` | `aige.character/1` | ④ Characters | ⑤–⑦, ⑪, ⑫ |
| `quests/<id>.json` | `aige.quest-design/1` | ③ (skeleton), ⑤ | ⑥, ⑨, ⑪, ⑫ |
| `places/<id>.json` | `aige.place-desc/1` | ⑥ Places | ⑩, ⑪ |
| `systems.json` | `aige.systems/1` | ⑦ Systems | ⑨, ⑫ |
| `ui.json` | `aige.ui-plan/1` | ⑧ Visuals | ⑫ |
| `report.json` | `aige.plan-report/1` | ⑨ Validate, ⑫ playtest | panel |
| `buildplan.json` | `aige.build-plan/1` | ⑥ (rows), ⑩ (status) | ⑩, ⑪ |
| `bindings.json` | `aige.bindings/1` | ⑪ | ⑫, editing |
| `decisions.jsonl` | `aige.decision/1` | every stage, every user action | Why?, history |
| `log.jsonl` | the AI call log (as `places/<id>/log.jsonl`) | every AI call | debugging, replay |
| `state.json` | `aige.plan-state/1` | the job engine | resume |
| `history/` | revisions of the above (commits, the baseline) | each stage's commit | history, restore |
| `defaults.json` | answers kept as project defaults | ① | ① |

Rules for all of them:
- Every document has `"schema"`, `"revision"` and `"madeBy"` (a decision id).
- Every node with an id has `madeBy`, optional `locked`, and optional `rationale`
  (≤ 2 sentences, for Why?).
- Ids are stable slugs (`s-14`, `character/aldric`, `q-moon-amulet`); renaming goes
  through a rename that rewrites references (as World Bible renames do).
- Each schema has a validator in the engine (`engine/planner/`, Phase 22) and a mirror in
  the AI service, as `story.py` mirrors `Story.cpp` today.

## 2. The Game Brief (`aige.game-brief/1`)

The confirmed request: what game to make. Fields are grouped by the confirmation window's
tabs. `*` = asked in round 1; the rest are asked in round 2 or left at the profile's
default and shown as assumptions.

### 2.1 Game

| Field | Type | Values / range | Notes |
|---|---|---|---|
| `title`* | text | | the AI proposes three |
| `logline`* | text | ≤ 40 words | |
| `genre`* | enum | `game-types.md` ids: `arpg`, `rpg`, `action-adventure`, `life-sim`, `platformer`, `metroidvania`, `shmup`, `twin-stick`, `puzzle-falling`, `puzzle-grid`, `tactics`, `roguelike`, `visual-novel`, … | sets the **profile** (§2.10) |
| `subgenres` | enum[] | same ids | "roguelike platformer" |
| `perspective`* | enum | `top-down`, `isometric`, `side`, `three-quarter` | must be supported (`game-types.md` §3) |
| `grid` | enum | `isometric`, `top-down`, `none` | from perspective; `none` for side-view physics games |
| `camera` | enum | `follow`, `fixed-screen`, `scrolling-auto`, `rooms` | `scrolling-auto` for shmups |
| `playTime`* | quantity (hours) | 0.25–100 | with a **scope class**: micro (< 1 h), short (1–5), medium (5–15), long (15–40), epic (40+) |
| `scope` | object | `places`, `scenes`, `mainQuestStages`, `sideQuests`, `characters` as ranges | filled from the scope table (§2.11), editable |
| `audience` | enum | `everyone`, `teen`, `mature` | bounds content (violence, language) for every generator |
| `artStyle` | object | `fromProject` (bool), `style` (pixel-16, pixel-32, painted, flat-vector…), `palette` | from the project's catalog style profile when it has art |
| `players` | enum | `single` | local co-op later |

### 2.2 Story

| Field | Type | Values / range |
|---|---|---|
| `setting`* | object | `world` (text), `era` (enum: prehistoric, ancient, medieval, renaissance, industrial, modern, near-future, far-future, timeless), `genreFlavour` (fantasy, dark fantasy, sci-fi, post-apocalyptic, historical, horror, mystery, slice-of-life…) |
| `tone`* | enum[] | GSO `Tone` |
| `themes` | text[] | ≤ 3 |
| `structure` | enum | GSO `StructureTemplate`; default by genre |
| `pattern` | enum | GSO `BranchingPattern`; default `branch-and-bottleneck` for RPGs, `linear` for action |
| `endings` | int | 1–6 |
| `choicePoints` | quantity | number of major choices; 0–12 |
| `playerCharacter`* | object | `kind` (`fixed`, `created`), `name`, `description`, `classes` (if created) |
| `mustInclude` | text[] | the user's must-haves ("a dragon", "a betrayal") |
| `avoid` | text[] | content to avoid |

### 2.3 Characters

| Field | Type | Values / range |
|---|---|---|
| `cast` | object | counts by importance: `major` 2–12, `supporting` 0–40, `minor` 0–200; from the scope table |
| `companions` | int | 0–8 |
| `factions` | int | 0–12 |
| `behaviour`* | enum | `simple` (stand, follow, fight), `scheduled` (daily packages), `rich` (schedules, smart objects, perception, reactions) |
| `romance` | bool | |
| `portraits` | bool | dialogue portraits generated |

### 2.4 Places

| Field | Type | Values / range |
|---|---|---|
| `worldShape`* | enum | `single` (one place), `hub` (a hub and spokes), `linear` (a chain), `open-region` (a region of connected sites), `levels` (numbered levels/stages) |
| `placeKinds`* | object | per GBO build type: count range (settlement 0–10, interior 0–60, dungeon 0–20, castle, farmland, wilderness, camp, region) |
| `interiors` | enum | `none`, `key` (story places only), `most` (every enterable building) |
| `size` | enum | per settlement: hamlet … city (the GBO scale classes, with numbers) |
| `travel` | enum | `walk`, `walk+fast-travel`, `world-map` |

### 2.5 Systems

Each is `{on: bool, ...options}`. The profile sets defaults; turning one off removes the
stages' work for it.

| System | Options |
|---|---|
| `combat` | `style` (real-time melee, real-time ranged, mixed, turn-based*, none), `damageTypes` (bool), `blocking`, `dodging` |
| `abilities` | `magic` (bool), `schools` (count), `resource` (mana, stamina, cooldown-only), `acquisition` (level, trainers, items, skill use) |
| `inventory` | `model` (list, grid, weight-limited, slots), `equipment` (slots set) |
| `crafting` | `kinds` (alchemy, smithing, cooking, enchanting) |
| `economy` | `currency` (name), `merchants` (bool), `barter` |
| `progression` | `model` (levels, skill-use like Elder Scrolls, none), `levelCap`, `perks` |
| `dialogue` | `style` (trees, hub-topics), `skillChecks`, `voice` (none, generated) |
| `stealth` | bool |
| `time` | `dayNight`, `minutesPerSecond`, `calendar` |
| `factions` | `reputation`, `crime` |
| `save` | `model` (anywhere, checkpoints, save points), `autosave` |
| `death` | `respawn` (last checkpoint, last save, town), `penalty` |

*Turn-based combat is listed because the schema should not need to change for it; the
engine doesn't have it yet (`game-types.md`).

### 2.6 Difficulty (the Difficulty tab)

The questions of `difficulty.md` §8, as fields:

| Field | Type | Values / range |
|---|---|---|
| `levels`* | object[] | count 1–7, each `{id, name, description}`; default: Story, Normal, Hard |
| `default` | id | the level a new game starts on |
| `scales`* | enum[] | `enemy-health`, `enemy-damage`, `player-damage`, `enemy-count`, `enemy-ai`, `resources`, `prices`, `xp`, `checkpoints`, `puzzle-hints`, `timers` |
| `curve` | enum | `flat`, `gentle`, `steady`, `steep`, `sawtooth` (rising with dips after milestones) |
| `levelScaling` | enum | `fixed-zones` (Gothic-like: regions have levels), `scaled` (Oblivion-like: follows the player), `hybrid` (Skyrim-like: zones with min/max) |
| `adaptive` | bool | adaptive difficulty (`difficulty.md` §5) |
| `assists` | enum[] | `invincibility`, `damage-reduction`, `skip-encounter`, `slow-motion`, `aim-assist`, `extra-hints` |
| `permadeath` | enum | `off`, `option` (an Ironman level), `always` |
| `changeAnytime` | bool | allowed during play |

### 2.7 Visuals (the Visuals tab)

| Field | Type | Values |
|---|---|---|
| `theme`* | object | `fromLibrary` (theme id) or `generate` with `style` (parchment, stone, metal, sci-fi glass, minimal flat, pixel frame, hand-drawn) |
| `hudDensity`* | enum | `minimal`, `standard`, `full` |
| `hud` | enum[] | widget ids (`game-ui.md` §6), defaulted from systems and density |
| `windows` | enum[] | screen and window ids (`game-ui.md` §5), defaulted from systems |
| `notifications` | enum | `subtle`, `standard`, `prominent` |
| `font` | object | from the library; `readability` (bool: a dyslexia-friendly default) |
| `mapStyle` | enum | `none`, `minimap`, `full-map`, `both` |

### 2.8 Plan

| Field | Type | Values |
|---|---|---|
| `autonomy` | map stage → `ask` / `review` / `auto` | defaults: ask for concept, outline; review for the rest |
| `building` | enum | `manual`, `automatic`, `ask-per-place` |
| `model` | provider choice | as the Build panel |
| `budget` | object | optional ceilings: calls, tokens, time |
| `assetSources` | policy | `auto` / `project` / `library` / `web` / `generate` (`place-assets.md`) |
| `language` | enum[] | languages to write strings for; the first is the source |

### 2.9 Decisions, assumptions, confidence

As the place Brief: `decisions: [{question, assumed, path, options}]`, `assumptions:
[text]`, and per field `confidence` (0–1) and `differ` (the other readings' values) from
the merge.

### 2.10 Genre profiles

A profile (`data/planner/profiles/<genre>.json`) gives each field's default, which fields
are asked, which stages run, and which systems are on:

```json
{ "schema": "aige.genre-profile/1", "id": "arpg",
  "stages": { "story": "full", "characters": "full", "quests": "full", "places": "full" },
  "defaults": { "perspective": "isometric", "structure": "three-act", "pattern": "branch-and-bottleneck",
    "systems": { "combat": { "on": true, "style": "real-time-melee" },
                 "inventory": { "on": true, "model": "grid" }, "progression": { "on": true, "model": "levels" },
                 "abilities": { "on": true, "magic": true } },
    "difficulty": { "levels": ["story", "normal", "hard", "nightmare"], "levelScaling": "fixed-zones" },
    "hudDensity": "standard" },
  "asked": ["genre", "perspective", "playTime", "setting", "tone", "playerCharacter", "behaviour",
            "worldShape", "placeKinds", "levels", "scales", "theme", "hudDensity"] }
```

### 2.11 Scope table

Scope classes give numbers (the GBO scale tables' approach: words always come with numbers):

| Scope | Play time | Places | Engine scenes | Main stages | Side quests | Characters (major / supporting / minor) |
|---|---|---|---|---|---|---|
| micro | < 1 h | 1–3 | 1–3 | 3–6 | 0–2 | 1–3 / 0–4 / 0–10 |
| short | 1–5 h | 3–10 | 3–12 | 8–20 | 3–12 | 3–5 / 5–12 / 10–40 |
| medium | 5–15 h | 8–25 | 10–30 | 20–40 | 12–40 | 4–8 / 10–25 / 30–100 |
| long | 15–40 h | 20–60 | 25–80 | 30–60 | 40–120 | 6–12 / 20–40 / 80–250 |
| epic | 40 h + | 50+ | 60+ | 50+ | 100+ | 10+ / 40+ / 200+ |

These are for story genres (from published RPG sizes, to be refined from verified plans);
non-story profiles have their own (levels per world, waves per stage).

### 2.12 The confirmation window

Generated from the schema like the Build panel's (`BuildWindow`): one tab per group, the
asked fields first, the profile's defaults shown as assumptions, *differ* and *guessed*
marks, a "To decide" list, and **Continue** (to ② Outline). Changing `genre` reloads the
profile's defaults for fields the user hasn't touched.

## 3. Stage documents (abridged schemas)

### 3.1 `aige.outline/1`

```json
{ "schema": "aige.outline/1",
  "story": { "logline": "", "synopsis": "", "structure": "three-act",
    "acts": [{ "id": "act-1", "purpose": "setup", "summary": "", "beats": ["inciting-incident"] }],
    "storylines": [{ "id": "main", "kind": "main", "summary": "" }],
    "endings": [{ "id": "end-a", "summary": "", "conditions": "" }] },
  "cast": [{ "id": "character/aldric", "name": "Aldric", "importance": "major",
    "narrativeRoles": ["shapeshifter"], "gameplayRoles": ["boss"], "line": "" }],
  "places": [{ "id": "location/riverford", "kind": "settlement", "parent": "location/vale", "line": "" }],
  "connections": [{ "from": "location/riverford", "to": "location/crypt", "kind": "stairs",
    "gate": { "quest": "q-main", "stage": 30 } }],
  "structure": null }
```

`structure` replaces the pillars for non-story games: `{modes, worlds: [{levels}], progression}`.

### 3.2 `aige.scene/1`

```json
{ "schema": "aige.scene/1", "id": "s-14", "act": "act-2", "chapter": "ch-4", "order": 14,
  "branch": null, "card": { "goal": "", "conflict": "", "outcome": "", "value": "trust-betrayal",
    "chargeBefore": 1, "chargeAfter": -1, "mode": "cutscene", "kernel": true,
    "location": "location/riverford-temple", "participants": [{ "who": "character/aldric", "role": "agent" }] },
  "beats": [{ "function": "betrayal", "text": "" }],
  "events": ["e-0091"], "facts": { "assert": ["f-1210"], "retract": ["f-1003"] },
  "newEntities": ["character/ferryman"], "questStage": { "quest": "q-main", "stage": 40 },
  "summary": "", "rationale": "", "madeBy": "d-0377" }
```

### 3.3 `aige.character/1`

```json
{ "schema": "aige.character/1", "id": "character/tamsin", "name": "Old Tamsin", "importance": "supporting",
  "biography": { "backstory": "", "personality": [["agreeable", 0.8]], "need": "", "desire": "",
    "weakness": "", "motivations": ["protection"], "secrets": [""], "arc": { "from": "", "to": "", "scenes": [] } },
  "appearance": { "description": "", "palette": [], "sprite": null, "portrait": null },
  "voice": { "description": "", "sample": "" },
  "gameplay": { "roles": ["quest-giver", "healer"], "class": "records/classes/villager-elder",
    "level": 3, "factions": [{ "id": "faction/wardens", "disposition": 40 }],
    "inventory": "records/loot/tamsin", "abilities": [] },
  "behaviour": { "package": "records/packages/tamsin", "script": null, "combatStyle": null, "barks": [] },
  "dialogue": { "topics": [""], "gives": ["q-moon-amulet"] } }
```

### 3.4 `aige.quest-design/1`

```json
{ "schema": "aige.quest-design/1", "id": "q-moon-amulet", "name": "The Moon Amulet", "type": "main",
  "storyline": "main", "motivation": "protection", "strategy": "recover-stolen-item",
  "giver": { "alias": "giver" }, "level": [3, 6],
  "aliases": [
    { "id": "giver", "kind": "character", "fill": "character/tamsin" },
    { "id": "thief", "kind": "character", "fill": "character/aldric" },
    { "id": "hiding-spot", "kind": "spot", "in": "location/crypt",
      "where": { "anchor": "altar" }, "storyAnchor": true } ],
  "stages": [
    { "index": 10, "journal": "", "advancesScene": "s-03",
      "objectives": [{ "id": "talk-tamsin", "action": "talk", "target": "giver" }] },
    { "index": 20, "objectives": [{ "action": "reach", "target": "hiding-spot" }] },
    { "index": 100, "terminal": "completed" } ],
  "start": { "conditions": [{ "scene": "s-03" }] },
  "rewards": [{ "xp": 400 }, { "item": "records/items/warden-blade" }],
  "relations": [{ "unlocks": "q-the-ferryman" }] }
```

### 3.5 `aige.place-desc/1`

```json
{ "schema": "aige.place-desc/1", "id": "location/riverford", "kind": "settlement", "parent": "location/vale",
  "description": { "purpose": "", "culture": "", "era": "medieval", "mood": "", "residents": [] },
  "contains": { "zones": [{ "kind": "old-town", "character": "" }], "named": [{ "id": "location/riverford-inn",
    "kind": "interior", "line": "" }] },
  "cells": [],
  "storyAnchors": [{ "id": "notice-board", "module": "notice-board", "relation": { "in": "market-square" },
    "for": { "quest": "q-bounties" } }],
  "actors": [{ "character": "character/tamsin", "anchor": "home" }],
  "exits": [{ "to": "location/old-road", "edge": "east", "kind": "road" }],
  "style": { "palette": [], "materials": [] },
  "brief": { "...": "aige.brief/1 for the build type, with storyAnchors and exits" } }
```

### 3.6 `aige.build-plan/1`

```json
{ "schema": "aige.build-plan/1", "mode": "automatic",
  "rows": [{ "place": "location/riverford", "scene": "scenes/riverford.aige", "buildType": "settlement",
    "prompt": "", "dependsOn": ["location/vale"], "mode": "automatic",
    "status": "built", "build": "places/riverford-01", "checks": { "passed": 41, "failed": 0 } }] }
```

### 3.7 `aige.bindings/1`

```json
{ "schema": "aige.bindings/1",
  "aliases": [{ "quest": "q-moon-amulet", "alias": "hiding-spot", "how": "story-anchor",
    "scene": "scenes/crypt.aige", "marker": "crypt/chamber-3/altar", "candidates": 1, "madeBy": "d-0912" }],
  "actors": [{ "character": "character/tamsin", "scene": "scenes/riverford.aige", "entity": "Tamsin",
    "package": "records/packages/tamsin" }] }
```

`how`: `fixed`, `story-anchor`, `query` (with the candidates and scores), `ai-choice` (with
the rationale), `made` (a local build edit, with the build revision), `failed`.

## 4. The report (`aige.plan-report/1`)

```json
{ "schema": "aige.plan-report/1", "kind": "validate",
  "summary": { "errors": 0, "warnings": 3, "accepted": 1 },
  "findings": [{ "id": "f-12", "rule": "R-K4", "severity": "warning", "subject": "character/aldis",
    "message": "Aldis and Aldric start alike.", "fix": { "rename": "character/aldis", "to": "Maren" },
    "status": "open" }] }
```

`status`: `open`, `fixed` (with the decision), `accepted` (with the reason). The playtest
report (`kind: playtest`) lists per quest: `completed`, `stuck` (stage, place, reason), or
`skipped`.

## 5. The decision record (`aige.decision/1`)

| Field | Meaning |
|---|---|
| `id` | `d-<n>`, ordered |
| `time` | ISO time |
| `stage` | the stage |
| `actor` | `ai`, `user`, `rule`, `planner` (the automatic driver) |
| `kind` | `interpret`, `merge`, `write-outline`, `write-scene`, `detail-character`, `design-quest`, `describe-place`, `write-records`, `design-ui`, `critic`, `fix`, `accept`, `edit`, `lock`, `regenerate`, `build-step`, `bind`, `choose-candidate`, `playtest`… |
| `subject` | the node or document decided about |
| `inputs` | document revisions, graph revision, fact ids, candidate lists |
| `call` | the AI call's id in `log.jsonl` (AI decisions) |
| `chosen`, `alternatives` | what was decided and what else was on the table |
| `rationale` | ≤ 2 sentences, shown as Why? |
| `checks` | validation result |
| `outputs` | what it wrote: documents at revisions, graph facts, engine files |
| `reason` | for user actions: the user's reason, if given |

The log is append-only. Undo and restore are new decisions that point at the revision they
restore, so the history is never rewritten.

## 6. The plan state (`aige.plan-state/1`)

```json
{ "schema": "aige.plan-state/1", "stage": "characters", "revision": 31, "baseline": null,
  "jobs": [{ "id": "j-201", "kind": "detail-character", "subject": "character/tamsin",
    "status": "done", "attempts": 1, "decision": "d-0451" },
    { "id": "j-202", "kind": "detail-character", "subject": "character/aldric",
    "status": "running", "attempts": 2 }],
  "stale": ["story/scenes/s-05.json"], "paused": false }
```

Job statuses: `queued`, `running`, `done`, `failed` (with the error and attempts),
`waiting` (for the user), `skipped`.
