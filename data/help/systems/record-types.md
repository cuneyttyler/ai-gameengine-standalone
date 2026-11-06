# Record types

A guide to what each built-in record type in Project ▸ Records is for, how it's used in a game, and an example. The
exact field list of each type is given separately; this explains purpose and use.

## item records: things to carry, wear, use or sell

An `item` record is something that can be picked up, carried, sold or used. Inventories, pickups, containers,
merchants, loot tables, recipes, quests and Reactions all refer to items by id.

- **Basics:** `name` (required, shown to the player), `description`, `kind` (misc, weapon, armor, consumable, key,
  quest, material, currency, book), `icon`, `value` (price), `weight`, `stack` (how many fit in one slot, default 1),
  `unique` (only one exists: story items), `tags`.
- **Wearing and wielding:** `slot` (none, weapon, offhand, head, body, hands, feet, ring, amulet), `damage` (added to
  attack while wielded), `armor` (added to defense while worn), `modifiers` (attribute changes while worn: add or
  multiply). How it looks on the wearer: `sprite`, `point`, `grip`, `pointing`, `size`, `layer`, and `class` and
  `animation` for its attack clips.
- **Using it:** `use` (heal, mana, stamina, a status effect, a timed attribute boost, `consumed` by default, `learn` an
  ability for a book, `ability` for a scroll, `effects` for a potion's magic effects).
- **Enchantment:** magic effects that apply while worn or on each hit.

Example: a healing potion is kind consumable, value 25, stack 10, `use.heal` 30. A rusty key is kind key, used by a
locked door's `key` field.

### Example: a leather cap that adds armour and speed

1. In Project ▸ Records, press **+** on the **item** group; **Id** `leather-cap`.
2. Set **name** `Leather cap`, **kind** `armor`, **value** `15`, **weight** `0.5`, **slot** `head`, **armor** `2`.
3. In **modifiers**, add one row: **attribute** `moveSpeed`, **op** `multiply`, **value** `1.05`.
4. Press **Create**.

```json
{
  "name": "Leather cap",
  "kind": "armor",
  "value": 15,
  "weight": 0.5,
  "slot": "head",
  "armor": 2,
  "modifiers": [{ "attribute": "moveSpeed", "op": "multiply", "value": 1.05 }]
}
```

Put `leather-cap` in the player's Inventory **items**; equipped, it adds 2 to defense and 5% to walking speed.

Questions: how do I make an item? how do I make a weapon or armour? how do I make a potion?

## loot records: what chests, enemies and merchants give

A `loot` table is rolled to decide what a chest, a defeated enemy or a merchant's stock gives. It has `rolls` (how many
picks), gold between `goldMin` and `goldMax`, and weighted `entries`. Each entry is an `item` with a count range
(`min`, `max`), or another loot `table` rolled instead; a `weight` (how likely against the others); a `chance` (0-1,
the chance it's given when picked); and a level band (`levelMin`, `levelMax`, 0 for any).

Used by: a Container's loot (rolled the first time it's opened), an enemy's Rewards loot (dropped where it falls as
pickups with the items' icons), a Merchant's stock (filled again each day), and Lua `drop_loot(x, y, table[, level])`.

Example: "bandit-drops": rolls 2, goldMin 3, goldMax 12; entries: bread (weight 3, 1-2), dagger (weight 1, chance 0.5),
and the table "rare-gems" (weight 0.2).

### Example: a loot table for the old chest in the crypt

1. In Project ▸ Records, press **+** on the **loot** group; **Id** `crypt-chest`.
2. **rolls** `2`, **goldMin** `5`, **goldMax** `20`.
3. Add three **entries**: `healing-potion` (weight 3, min 1, max 2); `bone-charm` (weight 1, chance 0.5); and, with
   **item** empty, **table** `rare-gems` (weight 0.2).
4. Press **Create**.

```json
{
  "name": "Crypt chest",
  "rolls": 2,
  "goldMin": 5,
  "goldMax": 20,
  "entries": [
    { "item": "healing-potion", "weight": 3, "min": 1, "max": 2 },
    { "item": "bone-charm", "weight": 1, "chance": 0.5 },
    { "table": "rare-gems", "weight": 0.2 }
  ]
}
```

5. Select the chest in the crypt scene, add a **Container**, and pick `crypt-chest` in its **loot**.

The first time the player opens it, two picks are rolled and 5-20 gold added; after that it keeps what's left.

Questions: how do I make enemies drop items? how do loot tables work? how do I fill a chest?

## recipe records: crafting items from items

A `recipe` makes items from items. Fields: `name` (shown in the Crafting list), `inputs` (items used up, each with a
count), `keeps` (items needed but not used up: a knife, a pot), `gives` (what it makes), `at` (a station name such as
`stove`: made only at a Reaction or animated part whose `station` names it; empty: anywhere), `seconds` (how long it
takes), `clip` and `effect` on the player, `text` shown when made, and the rule fields `if` (conditions), `do`
(effects when made), `start` (effects when begun) and `interrupted` (effects if cut short; inputs are given back).

The Records form edits `if`, `do`, `start` and `interrupted` with the rule editor rather than raw JSON.

### Example: a stew cooked at the inn's stove

1. In Project ▸ Records, press **+** on the **recipe** group; **Id** `rabbit-stew`.
2. **name** `Rabbit stew`; **inputs**: `raw-rabbit` count 1, `carrot` count 2; **keeps**: `cooking-pot`; **gives**:
   `stew` count 1.
3. **at** `stove`, **seconds** `4`, **text** `The stew smells wonderful.`
4. Press **Create**.

```json
{
  "name": "Rabbit stew",
  "inputs": [{ "item": "raw-rabbit", "count": 1 }, { "item": "carrot", "count": 2 }],
  "keeps": ["cooking-pot"],
  "gives": [{ "item": "stew", "count": 1 }],
  "at": "stove",
  "seconds": 4,
  "text": "The stew smells wonderful."
}
```

At the inn's stove (a Reaction or animated part whose **station** is `stove`), Rabbit stew appears in the Crafting
list; after 4 seconds the rabbit and carrots are gone, the pot stays, and a stew is added.

See also: Crafting and items in hand.

Questions: how do I add a crafting recipe? what is a station? how do I make a recipe that takes time?

## perk and progression records: levelling up

- **progression:** how characters grow. Only the record named `default` is used: `baseExperience` (experience from level
  1 to 2, default 100), `growth` (each next level needs this many times more, default 1.5), `maxLevel` (default 50),
  `perkPointsPerLevel` (default 1), and `perLevel` (attributes added at every level).
- **perk:** taken with a perk point from a `level` on, it changes attributes from then on. `name`, `description`,
  `level`, `requires` (perks that must be taken first), `modifiers` (attribute, add or multiply, value) and
  `abilityModifiers` (scale abilities with a tag, such as `element.fire`, by magnitude or cost: 1.25 = +25%).

A character levels from experience (enemies' Rewards, quests' rewards, Lua `award_xp`); a level up heals, shows
"Level n!" and sends the `level_up` event. Lua `take_perk(id, perk)` takes a perk.

### Example: a faster curve and a Toughness perk

1. In Project ▸ Records, press **+** on **progression**; **Id** must be `default`.
2. **baseExperience** `80`, **growth** `1.4`, **maxLevel** `30`; in **perLevel** add `maxHealth` value `5`. **Create**.
3. Press **+** on **perk**; **Id** `toughness`; **name** `Toughness`; **description** `+20 maximum health.`;
   **level** `3`; **modifiers**: `maxHealth`, `add`, `20`. **Create**.

```json
{ "name": "Toughness", "description": "+20 maximum health.", "level": 3,
  "modifiers": [{ "attribute": "maxHealth", "op": "add", "value": 20 }] }
```

Level 2 now takes 80 experience, level 3 another 112; each level adds 5 health, and from level 3 the player can spend a
perk point on Toughness (or a script can call `take_perk(self_id(), "toughness")`).

Questions: how do I set the experience curve? how do I add perks? where is the level-up curve?

## faction and encounter records: who fights whom, and what spawns

- **faction:** who belongs together and how they stand with others. `name` (required), `description`,
  `playerStanding` (-100..100: how members feel about the player at the start), and `relations` (another faction and a
  standing from -100 war to 100 allies; below 0 they fight). A character joins one with the **FactionMember**
  component (its faction and a disposition to the player; below 0 it's hostile; attacking a member lowers it). Without
  factions, the three teams Neutral, Player and Enemy decide who fights.
- **encounter:** what a **Spawner** makes. Weighted `entries`, each a `prefab`, a count range (`min`, `max`) and a level
  band. The Spawner makes its encounter's enemies within its radius, up to maxAlive, again respawnSeconds after one dies
  (0: once), rolled at its level.

Example: faction "town-guard" with relations bandits -80; a Spawner by the bandit camp with encounter "bandit-camp"
(bandit weight 3, bandit-archer weight 1).

### Example: bandits at war with the town guard, and their camp

1. In Project ▸ Records, press **+** on **faction**: **Id** `bandits`, **name** `Redhand Bandits`, **playerStanding**
   `-50`, **relations**: `town-guard` standing `-80`. **Create**.
2. **+** on **faction** again: **Id** `town-guard`, **name** `Town Guard`, **playerStanding** `20`, **relations**:
   `bandits` standing `-80`. **Create**.
3. **+** on **encounter**: **Id** `bandit-camp`; **entries**: `prefabs/bandit.prefab` (weight 3, min 1, max 2) and
   `prefabs/bandit-archer.prefab` (weight 1, min 1, max 1). **Create**.
4. In the scene, put an entity by the camp's fire and add a **Spawner**: **encounter** `bandit-camp`, **radius** `5`,
   **maxAlive** `4`, **respawnSeconds** `120`.
5. Give each guard a **FactionMember** with **faction** `town-guard`.

```json
{ "name": "Redhand Bandits", "playerStanding": -50, "relations": [{ "faction": "town-guard", "standing": -80 }] }
```

Bandits attack the player and the guards on sight; up to four appear round the fire, a new one two minutes after one dies.

Questions: how do I make two groups hostile to each other? how do I spawn enemies? what is an encounter?

## package, procedure and combat-style records: character behaviour

- **package:** an AI package: what a character does, where and when. A character's Behaviour component lists packages;
  the first whose schedule and conditions hold runs its template (stay, wander, live, work, sleep, follow-path, follow,
  script and more) with the package's data, or its own procedure for template `custom`. See "AI packages".
- **procedure:** a behaviour tree written by hand, `tree` as JSON (`{"type": "sequence", "children": [...]}`). A
  package whose template names a procedure record runs it; a project procedure record with the same name as a built-in
  template replaces that template for the project.
- **combat-style:** how a character fights: `range` (melee closes in; ranged and kiting keep their distance and shoot),
  `keepDistance` (cells a ranged fighter keeps; 0: most of its range), `fleeAt` (runs below this share of its health,
  default 0.2, changed by its confidence), `callForHelp` (cells within which allies join, default 6). Picked in
  Behaviour's combatStyle; empty means melee, flee at 20%.

### Example: an archer that keeps her distance

1. In Project ▸ Records, press **+** on **combat-style**; **Id** `skirmisher`.
2. **name** `Skirmisher`, **range** `kiting`, **keepDistance** `5`, **fleeAt** `0.3`, **callForHelp** `8`. **Create**.

```json
{ "name": "Skirmisher", "range": "kiting", "keepDistance": 5, "fleeAt": 0.3, "callForHelp": 8 }
```

3. Select the bandit archer, and in Inspector ▸ Behaviour pick `skirmisher` in **combatStyle** (she also needs a
   RangedAttack).

She shoots from five cells, backs off when the player closes in, runs below 30% health, and bandits within eight cells
join her fight.

Questions: what is a package record? what is a procedure record? how do I make an archer keep its distance?

## difficulty records: levels and the category table

- **difficulty:** one difficulty level. `name` (required), `description`, `order` (easiest first), `categories` (a step
  1-5 for player, enemies, allies, stealth and resources; 3 is as designed, higher is harder), `modifiers` (on top of
  the steps: a target field like `Health.max`, whose (player, enemy, ally, any), a name `tag` such as boss, an op mul,
  add, set, min or max, and a value), `xp` (experience rewards times this) and `permadeath` (one life).
- **difficulty-table:** the game's multipliers per category, kind (health, damage, defense, speed, perception, count,
  gold, loot, price, xp) and step. Only `records/difficulty-table/default.json` is used; rows it leaves out use the
  built-in table.

Edit both more comfortably in Project ▸ Difficulty. With no difficulty records, the built-in five levels apply (Story,
Easy, Normal, Hard, Nightmare). See "Difficulty".

### Example: a "Veteran" level with tougher bosses

1. In Project ▸ Records, press **+** on **difficulty**; **Id** `veteran`.
2. **name** `Veteran`, **description** `Enemies hit harder; bosses have more health.`, **order** `4`.
3. **categories**: player `3`, enemies `4`, allies `3`, stealth `3`, resources `4`.
4. **modifiers**: target `Health.max`, owner `enemy`, tag `boss`, op `mul`, value `1.5`. **xp** `1.2`. **Create**.

```json
{
  "name": "Veteran",
  "description": "Enemies hit harder; bosses have more health.",
  "order": 4,
  "categories": { "player": 3, "enemies": 4, "allies": 3, "stealth": 3, "resources": 4 },
  "modifiers": [{ "target": "Health.max", "owner": "enemy", "tag": "boss", "op": "mul", "value": 1.5 }],
  "xp": 1.2
}
```

Veteran appears in the game's difficulty choice in its order; enemies use step 4 of the table, any enemy whose Name
contains "boss" gets half as much health again, and experience rewards are 20% larger.

Questions: how do I add a difficulty level? what is the difficulty table?

## animator, particles and effect records: animation and visual effects

- **animator:** states that play clips, and transitions between them. `states` (an id, the clip it plays, `exit: end`
  to go back to the default when the clip ends, a playback speed; the first is the default), `transitions` (from a
  state or `*`, to a state, an `if` such as `speed > 0.1`, `attacking`, `hurt`, `casting`, `using`, `dead`, `!dead`, or
  `a && b`, and a priority), and `default`.
- **particles:** an emitter's settings: rate, burst, duration, lifetime range, shape (point, circle or box), radius,
  speed range, direction, spread, gravity, drag, rise, size, colour and alpha from start to end, a texture, layer and
  the most alive at once. Engine presets are in data/fx.
- **effect:** what something looks and sounds like, as a list of parts: particles, a sound, a screen shake, hit-stop,
  a short light, a flash on the target, slow motion. Actions, input actions, hits, deaths and spells play effects by
  name. Engine presets can be copied into the project from Project ▸ Effects (Customise).

### Example: a door-slam effect with dust and a shake

1. In Project ▸ Records, press **+** on **particles**; **Id** `door-dust`: **burst** `12`, **duration** `0.2`,
   **lifetime** `[0.4, 0.8]`, **shape** `box`, **radius** `0.5`, **speed** `[0.5, 1.5]`, **direction** `-90`,
   **spread** `60`, **color** `["#b8a486", "#7a6a55"]`, **alpha** `[0.8, 0]`. **Create**.
2. Press **+** on **effect**; **Id** `door-slam`; add three **parts**: particles `door-dust`; sound
   `assets/audio/door-slam.wav`; shake `0.3`. **Create**.

```json
{ "parts": [
  { "particles": "door-dust" },
  { "sound": "assets/audio/door-slam.wav" },
  { "shake": 0.3 }
] }
```

A script can play it on the door with `play_effect("door-slam", self_id())`; puffs of dust rise, the slam sounds, and
the screen shakes briefly.

Questions: what is an animator record? how do I make a particle effect? how do I add a hit flash and screen shake?

## magic-effect, ability and magic-balance records: magic and skills

- **magic-effect:** what changes: an `archetype` (damage, heal, modify, over-time, absorb, state, summon, displace,
  dispel, light), the attribute and op, `magnitude` (a number or a formula such as `8 + 2 * caster.level`), duration,
  period, area, damage type, stacking (refresh, stack, strongest, separate), tags it grants, tags it requires or is
  blocked by, a visual `cue`, a `prefab` for summon, and `nonLethal` for over-time effects that can't kill (poison).
- **ability:** how magic effects are used: name, kind (spell, skill, power, passive, item-use, shout), school,
  activation (press, hold, toggle, passive), delivery (self, touch, projectile, area, cone, aura), range, radius,
  speed, cost (mana, stamina or health, and an amount), cooldown, cast time, duration, the effects, tags, the caster's
  animation, cues for casting and impact, a required level, and AI hints (use, when, priority).
- **magic-balance:** the constants of the cost formula. Only `records/magic-balance/default` is used.

### Example: a firebolt spell

1. In Project ▸ Records, press **+** on **magic-effect**; **Id** `fire-damage`: **archetype** `damage`, **magnitude**
   `8 + 2 * caster.level`, **damageType** `fire`, **tags** `element.fire`. **Create**.
2. Press **+** on **ability**; **Id** `firebolt`: **name** `Firebolt`, **kind** `spell`, **school** `destruction`,
   **activation** `press`, **delivery** `projectile`, **range** `8`, **speed** `10`, **cost** mana `12`, **cooldown**
   `1.5`, **effects** `fire-damage`, **tags** `element.fire`, **animation** `casting`, **ai** use `attack`, when
   `distance < 8`. **Create**.

```json
{
  "name": "Firebolt", "kind": "spell", "school": "destruction",
  "activation": "press", "delivery": "projectile", "range": 8, "speed": 10,
  "cost": { "attribute": "mana", "amount": 12 }, "cooldown": 1.5,
  "effects": ["fire-damage"], "tags": ["element.fire"], "animation": "casting",
  "ai": { "use": "attack", "when": "distance < 8" }
}
```

A level-3 caster's bolt does 14 fire damage; a cultist given the ability casts it when the player is within 8 cells, and
a perk with an abilityModifier on `element.fire` scales it.

See "Combat, abilities and magic" and Project ▸ Magic.

Questions: how do I make a fireball? what is a magic effect versus an ability? how is a spell's cost worked out?

## achievement records

An `achievement` is unlocked by a quest reaching a state, a story flag, a number of game events, or a script. Fields:
`name`, `description` (how to get it, or a hint), `icon`, `hidden` (secret until unlocked), `points` (default 10),
`kind` (quest, flag, event, script); for quest: `quest` and `state` (active, completed, failed); for flag: `flag`; for
event: `event` (death, pickup, hit, attack, trigger_enter, talk, quest_completed…), `target` (what it's about: a prefab,
a name, a trigger, a quest id; empty: any) and `count` (default 1). Scripts unlock any with
`unlock_achievement(id)` and ask with `achievement_unlocked(id)` and `achievement_progress(id)`.

Example: "rat-catcher": kind event, event death, target prefabs/rat.prefab, count 20.

### Example: two achievements, one for a quest and one for rats

1. In Project ▸ Records, press **+** on **achievement**; **Id** `cellar-cleared`: **name** `Cellar Cleared`,
   **description** `Help Vara Bell with her rat problem.`, **kind** `quest`, **quest** `rats-in-the-cellar`, **state**
   `completed`, **points** `20`. **Create**.
2. **+** again; **Id** `rat-catcher`: **name** `Rat Catcher`, **description** `Kill 20 rats.`, **hidden** ticked,
   **kind** `event`, **event** `death`, **target** `prefabs/rat.prefab`, **count** `20`. **Create**.

```json
{ "name": "Rat Catcher", "description": "Kill 20 rats.", "hidden": true,
  "kind": "event", "event": "death", "target": "prefabs/rat.prefab", "count": 20 }
```

Finishing the quest unlocks Cellar Cleared at once; Rat Catcher stays secret until the twentieth rat dies. A script can
show progress with `achievement_progress("rat-catcher")`.

Questions: how do I add achievements? how do I unlock an achievement from a script?
