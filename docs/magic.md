# Abilities and magic

Status: **draft for review (2026-10-21)**. Phase 19 in `development_plan.md`. The AI
Planner designs a game's magic system in its Systems stage (`ai-planner.md` §5.7).

---

## 1. Goal

Spells, special attacks, potions, scrolls, enchanted weapons, auras, shouts, gadgets: all
of them *do something to someone, at a cost, delivered somehow*. Today the engine has
`MeleeAttack` with one optional status effect (burn, poison, slow, regen) and nothing
else. We want one system that covers magic and every other active or passive ability, so
the editor, the AI and the Planner learn one model.

Terms in this document:
- **magic effect**: what changes (damage, heal, a buff, a stun, a summon) — a gameplay
  record;
- **effect** (visual): how it looks and sounds (`effects-animation.md` §6) — called a
  **cue** here when attached to a magic effect or ability;
- **ability**: how magic effects are activated and delivered (a spell, a power attack, a
  potion's use).

## 2. How others do it

| System | Model | What we take |
|---|---|---|
| **Creation Kit** (Skyrim) | **Magic effects** with an **archetype** (value modifier, peak value modifier, absorb, script, summon, light, paralysis, invisibility, cloak, bound weapon, calm, frenzy…), magnitude, duration, area; **spells** are lists of effects with a **casting type** (fire and forget, concentration, constant effect) and a **delivery** (self, contact, aimed, target actor, target location); spell kinds: spell, power, lesser power, ability (passive), disease; the **same effects** are reused by enchantments, potions, ingredients, scrolls and shouts; cost computed from magnitude, duration and area; perks modify | the effect/spell split, archetypes, casting types, deliveries, reuse across items, the cost formula |
| **Unreal Gameplay Ability System** | **attributes**; **gameplay effects** (instant, duration, infinite) with **modifiers** on attributes, stacking rules and periods; **abilities** with costs and cooldowns (themselves gameplay effects), activation and tasks; **gameplay tags** for states, requirements and immunities; **gameplay cues** for visuals and sound | attributes and modifiers, durations and stacking, **tags**, cues, costs and cooldowns as data |
| Diablo III / IV | skills with resource types and rune variants | variants of one ability |
| Path of Exile | skill gems modified by support gems | **modifiers by tag** (perks and supports) |
| Divinity: Original Sin 2 | elemental surfaces and combinations (water + lightning) | **interactions** through tags and surfaces (later) |
| Magicka, Noita | combining elements, spells as programs | not in scope; possible with scripted effects |

## 3. Model

```
Attributes  ◄── modified by ── Magic effects ◄── applied by ── Abilities ◄── used by ── characters, items, perks
 (health, mana, ...)            (what changes)                 (how: cost, delivery)        (who has them)
        ▲                              │ tags (stunned, burning, wet, immune-fire)
        └──── states and immunities ───┘
```

Attributes are Phase 13's (`engine-gaps.md` §2.2). Tags are short names on an entity
(`state.stunned`, `element.fire`, `immune.poison`) with counts, added and removed by
effects; abilities and effects require or are blocked by them.

## 4. Magic effects (`aige.magic-effect/1`)

| Field | Meaning |
|---|---|
| `archetype` | `damage`, `heal`, `modify` (an attribute for a duration: buffs, debuffs), `over-time` (damage or heal per period), `absorb` (take from target, give to caster), `state` (adds tags: stun, root, slow, silence, invisible, paralyse), `summon`, `spawn` (an object or area), `displace` (knockback, pull, teleport), `dispel` (removes effects by tag), `light`, `transform` (swap prefab or animator), `reveal`, `script` (Lua) |
| `attribute`, `op` | which attribute and how (`add`, `mul`, `override`) |
| `magnitude` | a number or a formula (`10 + 2 * caster.skill.destruction`) |
| `duration`, `period` | 0 = instant; period for over-time |
| `area` | radius (0 = single target) |
| `damageType` | physical, fire, frost, shock, poison, holy, … (the game's list); with the target's resistance attribute |
| `stacking` | refresh, stack up to n, strongest wins, separate per source |
| `tags` | granted while active; `requires` / `blockedBy` on the target |
| `cue` | the visual effect on apply, while active, on remove |
| `conditions` | when it applies (target is undead, target health below 30%) |

The four existing status effects become magic effects (burn = over-time fire damage;
poison = over-time, can't kill; slow = modify move speed; regen = over-time heal), so
`MeleeAttack.effect` keeps working.

## 5. Abilities (`aige.ability/1`)

| Field | Meaning |
|---|---|
| `kind` | `spell`, `skill` (martial), `power` (long cooldown), `passive` (always on), `item-use`, `shout` — labels for UI and AI; same mechanics |
| `school` | for spells: the game's schools (destruction, restoration…) or none |
| `activation` | `press` (fire and forget), `hold` (concentration: cost per second while held), `charge` (power grows while held), `toggle`, `passive` (constant effect) |
| `delivery` | `self`, `touch` (the melee hit), `projectile` (speed, homing, pierce, bounce, gravity), `beam`, `cone`, `area` (at a target point or around the caster), `aura` (around the caster for a duration), `summon` (at a point) |
| `targeting` | direction (facing), cursor point, nearest enemy, chosen target |
| `cost` | attribute and amount (mana, stamina, health, charges, an item), per second for `hold` |
| `cooldown`, `castTime`, `recovery` | seconds |
| `effects` | magic effects with magnitude scaling (by caster level, skill, charge) |
| `animation`, `cue` | the caster's animator trigger and the visual effects (cast, travel, impact) |
| `requires` | tags, level, skill, an equipped item kind |
| `ai` | hints for character AI: `use: "heal-self" when "health < 0.4"`, range, priority (`ai-packages.md` §8.2) |

**Items** reuse abilities and effects:
- a **potion** or food is a consumable whose use applies effects to self;
- a **scroll** is a one-use ability;
- an **enchantment** is effects on equipment: constant while worn, or on hit;
- **ingredients** carry effects for alchemy (if crafting is on).

**Perks** (progression) and equipment add **modifiers by tag**: `+25% magnitude for
abilities tagged element.fire`, `−20% cost for school.restoration`.

**Interactions** (optional, later): effects can leave **surfaces** on tiles (oil, water,
ice, fire) and react through tags (wet + shock → stun), as in Divinity: Original Sin.

## 6. Cost and balance

A base cost from the effects, as the Creation Kit does, so generated and hand-made spells
are comparable:

```
cost = base × (magnitude ^ 1.1) × (max(duration, 1) / 10) ^ 0.35 × (1 + area / 4) ^ 0.5   (per effect, summed)
```

The constants are per game (`records/magic/balance.json`). The editor shows each ability's
formula cost next to its set cost, and its damage (or healing) per resource and per second;
outliers are flagged. The shape follows the Creation Kit's (a base cost scaled up by
magnitude, duration and area); the exponents are starting values, tuned per game.

## 7. Runtime

- **`Abilities` component:** known abilities, assigned slots, cooldowns, active magic
  effects (with source, time left, stacks), tags.
- **Projectiles** are entities with a sweep against hurtboxes and solid cells (the current
  collision); with Phase 20 they use sensors and layers (`physics.md`).
- **Order per step:** activation and costs → delivery (spawn projectiles, query areas) →
  effects applied → periodic ticks → expiry → tags updated → events emitted
  (`ability_used`, `effect_applied`, `effect_removed`, `damaged`).
- **Save:** active effects and cooldowns.
- **Lua:** `cast(ability, target)`, `apply_effect(target, effect, magnitude, duration)`,
  `remove_effects(target, tag)`, `has_tag(id, tag)`, `add_tag`, `cooldown_left(ability)`,
  `on("effect_applied", fn)`; `script` archetype effects call `on_apply`, `on_tick`,
  `on_remove`.

## 8. Editor

- **Records panel:** magic effects, abilities, schools, balance constants, perks.
- **Ability editor:** the fields, with the cost and output figures (§6), and **Try** — cast
  it in a test arena (a generated scene with a target dummy) in Play.
- **Inspector ▸ Abilities** on characters: known abilities and slots.
- **Spellbook** and quick slots in the game UI (`game-ui.md` §5).
- **Checks:** an effect with no cue (warning), a cost far from the formula, an ability no
  one has, an AI hint referring to an unknown attribute, a cyclic summon.

## 9. How the Planner designs magic

From the Brief's systems (`abilities.magic`, schools, resource, acquisition) and the story's
**world rules** (`story-ontology.md` §4.4):
1. **The system:** schools from the setting and tone (a dark-fantasy world: blood, shadow,
   bone; a cozy one: growth, weather); the resource and its rule (a world rule "magic costs
   memories" might become health cost and a story variable); how spells are learned.
2. **Spells per school and level band** to a count from the scope, each from archetypes and
   deliveries, costed by the formula, with AI hints.
3. **Enemy and character abilities** from their classes and gameplay roles (a healer heals;
   a boss has phases with abilities).
4. **Items:** potions, scrolls and enchantments for loot tables and merchants, in level
   bands.
5. **Cues:** visual effects from the library in each school's colours (until the
   Animations step can do more).
6. **Checks:** every school has abilities in each band the player passes through; costs
   within the formula's band; every enemy fighter's abilities have AI hints; world rules
   respected (the critic).
