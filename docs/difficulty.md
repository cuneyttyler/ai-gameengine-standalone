# Difficulty — levels, scaling, assists and the difficulty curve

Status: **built, to be tried (2026-10-21)**; see "As built" at the end. Phase 16 in `development_plan.md`. The AI
Planner asks about difficulty in the Game Brief's Difficulty tab (`planner-schema.md`
§2.6) and writes the presets in its Systems stage (`ai-planner.md` §5.7).

---

## 1. Goal

- A game has **difficulty levels** (Story, Normal, Hard…) that the player chooses.
- Each level changes **any number of fields**: enemy health and damage, the player's
  damage, resources, prices, AI reaction, checkpoint spacing, puzzle hints, timers.
- Designers set **base values once**. Difficulty is applied on top; values per level are
  computed, and can be overridden by hand where the computed one is wrong.
- The editor shows what each level does, and lets designers **play and compare** levels.
- The difficulty **curve** over the game (how hard each chapter is) is designed and
  checked.
- The AI panel and the Planner understand all of this.

## 2. What games do

| Game | Approach | What we take |
|---|---|---|
| Skyrim | six levels, each a pair of multipliers: damage dealt and taken (Novice ×2 / ×0.5 … Legendary ×0.25 / ×3) | levels as modifier sets; simple multipliers cover most needs |
| The Last of Us Part II | five presets plus **granular sliders per category** (Player, Enemies, Allies, Stealth, Resources), and 60+ accessibility options | **categories**: a level is a value per category; a Custom level |
| Celeste | Assist Mode: game speed, infinite stamina, invincibility, skip chapter, separate from difficulty | **assists**, apart from levels, without judgement |
| Hades | God Mode: damage resistance grows with each death; Heat: opt-in modifiers for more challenge | assists that grow; opt-in **challenge modifiers** |
| Diablo III / IV | world tiers / torment levels: more health and damage, better rewards | rewards that scale with difficulty |
| Resident Evil 4 | hidden adaptive rank from performance | **adaptive difficulty**, bounded and optional |
| Left 4 Dead's AI Director | pacing from player stress | adaptive spawn pacing (later) |
| Gothic / Oblivion / Skyrim | fixed zone levels / everything scales to the player / zones with minimum and maximum levels | **level scaling** as a choice |
| XCOM | Ironman: one save | permadeath as an option |
| Hunicke, *The case for dynamic difficulty adjustment in games* (ACE 2005, Hamlet) | adjust supply and demand (items, enemy strength) within bounds, invisibly | bounds on adaptation |

## 3. The model

### 3.1 Scalable fields

A reflected field can be marked **scalable** with a **category** when it's registered (in
C++, a module's `module.json`, or a record schema):

```cpp
r.Field("max", &Health::max).Scalable("health");           // category from the owner
r.Field("damage", &MeleeAttack::damage).Scalable("damage");
```

The category of a field on an entity depends on its owner: `health` on an enemy is
*Enemies*, on the player *Player*, on a companion *Allies*. The owner is read from the
entity's faction relation to the player (hostile, player, friendly).

### 3.2 Categories

| Category | Typical fields |
|---|---|
| **Player** | damage dealt, damage taken, health regeneration, stamina, i-frame time |
| **Enemies** | health, damage, attack speed, count per encounter, elites share, reaction time, accuracy, aggression, perception range |
| **Allies** | companion health and damage, how often they help |
| **Stealth** | enemy perception speed and range, search time |
| **Resources** | loot amount, ammo, healing items, gold, prices (buy and sell), experience |
| **Timing** | timers, platform speeds, auto-scroll speed (shmups), piece fall speed (puzzle) |
| **Checkpoints** | checkpoint spacing, lives, continues, respawn health |
| **Puzzles** | hints available, hint delay |

A game uses only the categories its systems need; the Planner and the Difficulty panel
hide the rest.

### 3.3 Difficulty levels (records)

`records/difficulty/<id>.json`:

```json
{ "schema": "aige.difficulty/1", "id": "hard", "name": "Hard", "order": 3,
  "description": "Enemies hit harder and resources are scarce.",
  "categories": { "enemies": 4, "player": 3, "resources": 2, "stealth": 4, "checkpoints": 3 },
  "modifiers": [
    { "target": "Health.max", "owner": "enemy", "op": "mul", "value": 1.5 },
    { "target": "records/encounters/*.elites", "op": "add", "value": 0.1 }
  ],
  "rewards": { "xp": 1.2 }, "achievementsAllowed": true }
```

- **Category steps** (1–5, 3 = normal) give modifiers from the game's **category table**
  (`records/difficulty/categories.json`): for each category and step, the multipliers of
  its fields. One table, edited once; levels choose steps.
- **Explicit modifiers** add to or replace a category's effect for one field or selector.
- **Custom:** if allowed, the player sets each category's step in the settings menu.

A modifier's `target` is a field path (`Component.field`, `Attribute:name`,
`records/<type>/<id-or-*>.<field>`, `settings.<path>`), with an optional `owner` (player,
enemy, ally, any), `tags` (only entities with a tag, e.g. `boss`) and `when` (a
condition). Ops: `mul`, `add`, `set`, `min`, `max`, `curve` (a table by the player's level).

### 3.4 Effective values

**Files hold base values.** The effective value is computed:

```
effective = override[level]  if the field has a per-level override
          = base × Π(mul) + Σ(add), clamped by the field's min/max, then set/min/max ops
```

It's applied when an entity spawns, when a record is read, and again for every live entity
when the level changes during play (if allowed). Scaled values are never written back.

### 3.5 Per-entity overrides

In the Inspector, a scalable field has a **by difficulty** toggle. When on, it shows one
column per level with the computed value greyed; typing a value overrides that level only:

```
Health.max   10   [by difficulty ▾]   Story 6  Normal 10  Hard 15  →  [Hard: 20]  Nightmare 25
```

Saved in the entity, prefab or record as `"difficulty": {"Health.max": {"hard": 20}}`. An
override is shown with a marker and a ↺ to go back to the computed value. This is the
"setting character properties according to difficulty for manual editing" path: designers
see every level's value and fix the ones that matter (a boss) without touching the rest.

## 4. Level scaling

Separate from the difficulty level: how strong things are relative to the player.

| Model | Behaviour | Fields |
|---|---|---|
| `fixed-zones` | each region or scene has a level band; enemies spawn at it | `Zone.level` (min, max) |
| `scaled` | enemies take the player's level (± an offset) | encounter `levelOffset` |
| `hybrid` | the player's level, clamped to the zone's band | both |

Enemy classes have attributes per level (`curve` modifiers or class level tables).
Encounter tables choose by level.

## 5. Adaptive difficulty (optional)

Off by default; when the game enables it, the player sees it as a setting ("Adjust
difficulty to how I play").
- **Signals:** deaths per encounter, health left after fights, time to clear, retries at a
  checkpoint, hints used.
- **Response:** moves category steps by at most ±1 from the chosen level (e.g. Enemies 3 →
  2 after three deaths at one encounter; back after two clean fights).
- **Bounds and honesty:** never beyond ±1, never during a fight, logged (the debug overlay
  shows the current adjustment), and off for Ironman and achievements if the game says so.

## 6. The difficulty curve

How hard each part of the game is meant to be, at Normal:
- A **target level band** per chapter or region (from the Brief's `curve`: flat, gentle,
  steady, steep, sawtooth), and the **critical path** order of places from the story.
- **Checks** (the Planner's ⑨ Validate and the editor's checks):
  - enemy levels in each place within its band;
  - bands rise along the critical path (sawtooth allows dips after milestones);
  - quest levels match their place's band (R-Q5 of `story-ontology.md`);
  - expected player level at each chapter (from the experience the critical path gives)
    against the chapter's band;
  - rewards (R-Q6) within the band's economy.
- **Combat preview:** for an enemy class against the expected player at a chapter, per
  level: hits to kill each way, time to kill, damage per second. Shown as a table so a
  designer sees "on Hard, a skeleton in chapter 3 kills the player in 4 hits".

## 7. Assists and accessibility

Assists are separate from levels and can be combined with any:
- invincibility, damage reduction (fixed or growing per death, as Hades' God Mode), skip
  encounter, game speed (Celeste), aim assist, extra hints, longer timers, auto-pickup;
- accessibility settings with gameplay effect (hold-to-toggle, slower QTEs) live with the
  assists; visual ones (text size, colour-blind modes) are in `game-ui.md`.

A game decides which assists exist; the settings menu shows them with a neutral
description (no "easy mode" shaming).

## 8. The Planner's Difficulty tab

The questions (`planner-schema.md` §2.6), with the defaults the genre profile proposes:

| Question | Example answers | Effect |
|---|---|---|
| How many levels, and their names? | Story, Normal, Hard, Nightmare | level records |
| What should difficulty change? | enemy health and damage, resources, checkpoints | the categories in use |
| How hard is the default? | Normal | `default` |
| How should the game get harder over time? | steady, sawtooth | the curve |
| Do enemies follow the player's level? | fixed zones / scale / hybrid | level scaling |
| Should difficulty adapt to the player? | no | adaptive |
| Which assists? | damage reduction, skip encounter | assists |
| Permadeath? | an Ironman level | a level with one save slot |
| Can the player change difficulty during play? | yes | `changeAnytime` |
| Do harder levels give better rewards? | slightly (+20% XP on Hard) | `rewards` |

In ⑦ Systems the Planner writes: the category table (multipliers per step, from the genre
profile, adjusted for the combat model), the level records, zone bands from the curve and
the world graph, encounter tables per level, and assists. Characters' class records hold
base values only (`ai-planner.md` §14, point 5).

## 9. Editor

- **Difficulty panel** (Project ▸ Difficulty…): the levels (add, rename, reorder), the
  category table (steps × categories, editable), each level's category steps and explicit
  modifiers, assists, adaptive settings, level scaling.
- **Preview:** pick an entity, prefab or record and see every scalable field at every level
  (computed, override, why: which modifiers applied).
- **Combat preview** (§6) and the **curve view**: chapters on the critical path with bands,
  enemy levels and expected player level.
- **Play at a level:** a selector by the Play button; the Inspector's values show the
  current level's effective values while playing.
- **Checks:** a level that changes nothing; a field scaled below its minimum; a curve
  violation; an override on a field that isn't scalable.

## 10. Runtime

- Chosen at **New game** and in the settings menu (if `changeAnytime`); stored in the save.
- `difficulty()` and `difficulty_step(category)` in Lua; conditions in dialogue and quests
  can read them (a hint offered on Story only).
- The event `difficulty_changed` re-applies effective values.

## 11. The AI and difficulty

- The AI panel's context includes the difficulty model (levels, category table, scalable
  fields), so "make skeletons tougher" changes **base values**, and "Hard is too easy in
  the crypt" changes the level's modifiers or the zone band, shown as a proposal.
- Proposals never write scaled values into files.
- The Planner's playtest (`ai-planner.md` §11.2) runs at the default level; the combat
  preview checks the other levels.

## 12. As built (Phase 16)

Where the implementation differs from or settles the draft above:
- **Kinds.** A scalable field has a kind: `health`, `damage`, `defense`, `speed`, `perception`,
  `count`, `gold`, `loot`, `price`, `xp`. Its category comes from the kind and the owner's side:
  gold, loot, price and xp are always *resources*; an enemy's perception is *stealth*; the rest are
  *player*, *enemies* or *allies*. Marked in the engine: Health.max, Stats (attack, defense,
  moveSpeed), MeleeAttack.damage, RangedAttack.damage, Perception (sight, hearing), Spawner.maxAlive,
  Inventory.gold, Container.gold, ItemPickup.count, Pickup.amount, Merchant (buyRate as price,
  sellRate as loot). The schema exports `"scalable": <kind>`.
- **Categories in use**: player, enemies, allies, stealth, resources. Timing, checkpoints and puzzles
  wait for the systems that have such fields.
- **The table** is the record `records/difficulty-table/default.json` (`{"rows": [{category, kind,
  steps: [5 numbers]}]}`), not `records/difficulty/categories.json`; rows it leaves out use the
  built-in table. Higher steps are harder in every category.
- **Levels** are `records/difficulty/<id>.json` (name, description, order, categories, modifiers,
  xp, permadeath). With none, the built-in five apply: Story, Easy, Normal, Hard, Nightmare.
  Modifier targets are `Component.field`; `tag` matches the entity's Name. Record and settings
  targets, `when` and `curve` ops are later work.
- **Overrides** are the `DifficultyOverrides` component (`values`: JSON text), edited from the
  Difficulty tab's Preview (the Inspector's ⚖ opens it).
- **Effective values** are applied when play starts, when a scene loads, after spawns and when the
  level changes; base values are kept per entity at runtime and never saved. Health keeps its share.
- **Level scaling**: the `Zone` component and Project Settings ▸ Difficulty (scaling, levelOffset);
  spawners use it and set a spawned Progression's level.
- **Adaptive**: ±1 step on enemies and stealth, after three deaths / two fights won unhurt; off
  unless the game allows it and the player turns it on. Kept in saves.
- **Assists** (the player's settings): invincible, damage reduction, more protection per death
  (2% each, up to 80%), game speed.
- **The curve** checks scenes in gateway order from the start scene by their Zone band: a band
  starting more than two levels above the previous one's end is "too hard"; a band below the previous
  one is flagged; enemies outside their band are flagged. Chapters from the story's critical path
  come with the story ontology (Phase 21).

