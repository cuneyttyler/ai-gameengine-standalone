# Difficulty

Difficulty levels and how they scale the game: categories and steps, the category table, modifiers, per-entity
overrides, the Difficulty tab (Levels, Category table, Preview, Curve), level scaling, adaptive difficulty and assists.

## How difficulty works

Designers set **base values** once (an enemy's Health.max, a weapon's damage); a difficulty level is applied on top and
never written back into files.

- A **level** chooses a **step** (1-5; 3 is as designed, higher is harder) for each **category**: player, enemies,
  allies, stealth, resources.
- Scalable fields have a **kind**: health, damage, defense, speed, perception, count, gold, loot, price, xp. A field's
  category comes from its kind and its owner's side: gold, loot, price and xp are always resources; an enemy's
  perception is stealth; the rest are player, enemies or allies by whose it is.
- Scalable fields include Health.max, Stats (attack, defense, moveSpeed), MeleeAttack.damage, RangedAttack.damage,
  Perception (sight, hearing), Spawner.maxAlive, Inventory.gold, Container.gold, ItemPickup.count, Pickup.amount,
  Merchant buyRate and sellRate.
- The **category table** gives the multiplier for each category, kind and step.
- **Modifiers** on a level add to that for one field.

Effective values are applied when play starts, when a scene loads, after spawns and when the level changes; health keeps
its share.

Questions: how does difficulty scale enemies? what are difficulty categories? does difficulty change my files?

## The Difficulty tab: Levels

Open **Project ▸ Difficulty**. A switch at the top shows **Levels**, **Category table**, **Preview** and **Curve**; **Play
at** picks the level Play starts at (default: the project's default level).

**Levels:** with no `records/difficulty` yet, the built-in five apply (Story, Easy, Normal, Hard, Nightmare); **Make them
this project's levels** copies them into records so you can edit them. Then **+ Level** adds one. For each level:

- **Id**, **Name**, **Order** (easiest first), **Description**;
- **Category steps**: buttons 1-5 per category;
- **Modifiers (on top of the steps)**: a field, whose (anyone's, the player's, enemies', allies'), "name contains"
  (e.g. boss: only entities whose Name contains it), an op (×, +, =, at least, at most) and a value; **+ Modifier**;
- **Experience** × rewards, and **one life** (permadeath);
- **Create** / **Save**, **Delete…**.

The default level, changing difficulty during play, adaptive difficulty and level scaling are in Project Settings ▸
Difficulty.

Questions: how do I add a difficulty level? how do I make bosses tougher on Hard only? how do I choose which difficulty
Play uses?

## Category table, Preview, Curve

- **Category table:** "Multipliers per step (1 easiest … 5 hardest)". A row per category and kind with five numbers;
  **Save table** writes `records/difficulty-table/default.json` (rows left out use the built-in table).
- **Preview:** select an entity to see each scalable field at every level, with the base value; type a value to set it
  by hand for one level (saved in the entity's **DifficultyOverrides** component as `{"Health.max": {"hard": 20}}`), ↺ to
  go back. The **combat preview** shows the selection against the player per level: the player's hits to kill it, its
  hits to kill the player, and seconds each way. The Inspector's ⚖ opens the preview for the selection.
- **Curve:** checks scenes in gateway order from the start scene by their **Zone** band (minLevel, maxLevel): a band
  starting more than two levels above the previous one's end is "too hard", a band below the previous one is flagged,
  enemies outside their band are flagged. **Check again** reruns it.

Questions: how do I set a value for one difficulty level only? how many hits does it take to kill an enemy on Hard? how
do I check the difficulty curve?

## Level scaling, adaptive difficulty and assists

- **Level scaling:** the **Zone** component gives a scene's level band; Project Settings ▸ Difficulty sets how enemies
  follow the player (scaling, levelOffset). Spawners use it and set a spawned character's level.
- **Adaptive difficulty:** ±1 step on enemies and stealth, after three deaths, or two fights won unhurt; off unless the
  game allows it and the player turns it on; kept in saves.
- **Assists** (the player's settings): invincible, damage reduction, more protection per death (2% each, up to 80%),
  game speed.
- **Scripts and conditions:** Lua `difficulty()`, `difficulty_step(category)`, `set_difficulty(id)`, the event
  `difficulty_changed`; dialogue conditions `{"difficulty": "story"}` or `{"difficulty": {"enemies": {"atLeast": 4}}}`.
- **The AI panel** knows the levels, table and scalable fields, so "make skeletons tougher" changes base values and
  "Hard is too easy in the crypt" changes a level's modifiers or the zone band.

Questions: do enemies scale with the player's level? what is adaptive difficulty? what assists are there? how do I read
the difficulty in a script?
