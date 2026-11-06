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

Questions: how do I make enemies drop items? how do loot tables work? how do I fill a chest?

## recipe records: crafting items from items

A `recipe` makes items from items. Fields: `name` (shown in the Crafting list), `inputs` (items used up, each with a
count), `keeps` (items needed but not used up: a knife, a pot), `gives` (what it makes), `at` (a station name such as
`stove`: made only at a Reaction or animated part whose `station` names it; empty: anywhere), `seconds` (how long it
takes), `clip` and `effect` on the player, `text` shown when made, and the rule fields `if` (conditions), `do`
(effects when made), `start` (effects when begun) and `interrupted` (effects if cut short; inputs are given back).

The Records form edits `if`, `do`, `start` and `interrupted` with the rule editor rather than raw JSON.

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

Questions: how do I add achievements? how do I unlock an achievement from a script?
