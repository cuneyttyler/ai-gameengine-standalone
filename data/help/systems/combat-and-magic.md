# Combat, abilities and magic

Fighting in games made with the editor: health, melee and ranged attacks, blocking and dodging, damage types and
attributes, inventories and equipment, factions; and abilities and magic (Project ▸ Magic, magic-effect and ability
records, quick slots, character AI casting).

## Combat components

Combat is set up with components in the Inspector:

- **Health**: current and max. Reaching 0 removes the entity (the player stays, marked dead: Game over).
- **Faction**: Neutral, Player or Enemy; no friendly fire within a non-neutral team. **FactionMember** joins a faction
  record instead (see Record types).
- **Hurtbox**: the area attacks can hit, and invulnerability time after a hit.
- **MeleeAttack**: damage, damage type, reach, radius, cooldown, knockback, an optional on-hit status (burn, poison,
  slow, regen). Without it, the `attack` action does nothing for that character.
- **RangedAttack**: damage, damage type, range, speed, cooldown, projectile picture, pierce. The player shoots with
  **F** (pad RB), or with attack when it has no melee weapon; enemies with one shoot when in range and in sight.
- **Stats**: attack (added to damage), defense (subtracted from damage taken, minimum 1), moveSpeed.
- **Block**: hold Left Ctrl (pad LB): a share of a hit is stopped (Combat.blockReduction) for stamina.
- **Dodge**: Left Shift (pad B): a dash of Combat.dodgeDistance cells, untouchable while it lasts, for stamina.

Damage types: physical, fire, frost, shock, poison; resistances take their share away (up to 0.9).

Questions: how do I give the player a sword attack? how do I make an enemy shoot? how do blocking and dodging work? why
does attack do nothing?

## Attributes, progression and equipment

- **Attributes** holds base numbers: maxHealth, maxMana, maxStamina, attack, defense, moveSpeed, health/mana/stamina
  regen, and resistFire, resistFrost, resistShock, resistPoison. Each step: effective = (base + every add) × every
  multiply, from equipment, perks and timed modifiers; results go to Stats, Health.max and Resources.
- **Progression**: level, experience, perk points, perks. **Rewards** on an enemy: experience for its killer, gold and a
  loot table dropped where it falls. The curve is the progression record `default`.
- **Inventory** (`items` as "potion*3, iron-sword", gold, slots, maxWeight), **Equipment** (an item per slot),
  **ItemPickup**, **Container**, **Merchant** (stock loot table refilled daily, buyRate, sellRate).
- In play, **I** (Tab, pad Y) opens the inventory: gold, level, health, attack, defense, what's worn and carried.

Questions: how do attributes work? how do I level up the player? how do I equip items? how do I make a shop?

## Enemies and characters in a fight

Enemies controlled by AI packages fight through their Behaviour component: aggression (Peaceful, Defensive, Aggressive,
Frenzied), confidence (how soon they flee), assistance (who they help), and a combat-style record (melee, ranged or
kiting; keepDistance; fleeAt; callForHelp). They notice the player with Perception (sight cone, hearing), call allies,
flee when hurt and search where they last saw the player. Simple enemies can instead use **Follow** (walk toward the
player within range) plus a script that swings when close.

Difficulty scales health, damage, defense, speed and perception by category (see Difficulty).

See also: Behaviour component (combat, fleeing, searching).

Questions: how do I make an enemy that chases the player? how do I make an enemy flee? how do enemies decide to attack?

## Abilities and magic effects

Magic and special attacks are one system: an **ability** says how something is used (cost, delivery), and **magic
effects** say what changes.

- **magic-effect** records: archetype damage, heal, modify (a buff or debuff on an attribute), over-time (damage or heal
  per period), absorb, state (adds tags: stunned, rooted, silenced, invisible), summon, displace (push, pull), dispel
  (remove by tag), light. Magnitude is a number or a formula (`8 + 2 * caster.level`); duration, period, area, damage
  type, stacking (refresh, stack, strongest, separate), tags granted, required and blocking tags, a visual cue.
- **ability** records: kind (spell, skill, power, passive, item-use, shout), school, activation (press, toggle,
  passive; hold works as press for now), delivery (self, touch, projectile, area, cone, aura), range, radius, speed,
  cost (mana, stamina or health), cooldown, cast time, duration, effects, tags, the caster's animation, cast and impact
  cues, required level, and AI hints (`use`: attack, heal-self, buff, debuff; `when`: `health < 0.4`, `distance < 6`,
  `mana > 0.2`, joined with &&; `priority`).
- Engine presets (data/magic): fire, frost and shock damage, heal, burn, poison, regen, slow, stun, root, silence,
  invisibility, buffs, drain, push, cleanse, light; abilities firebolt, frost nova, heal, flame aura, power strike, drain
  life, stoneskin, light, toughness.

Stunned and rooted can't move (stunned can't swing), silenced can't cast spells, invisible fades.

Questions: how do I make a fireball? what is the difference between an ability and a magic effect? what presets are
there? how do I stun an enemy?

## The Magic tab

**Project ▸ Magic** ("Abilities and magic effects, with their cost and output") has two views:

- **Abilities**: a table of every ability (engine presets and the project's, marked by source) with kind and school,
  delivery, its cost, the **formula** cost (what the cost formula suggests; flagged when far off: "Far from the formula:
  check it"), output **per cost** and **per s**, and ⚠ problems. **Try** plays the game, learns the ability and casts it
  at the nearest enemy (or ahead). **Customise** copies an engine preset into the project's records to change it in
  Records ▸ ability. Selecting a row shows its activation, cooldown, effects and AI hint.
- **Magic effects**: each effect with its archetype, magnitude, duration and tags, and Customise for presets.

A filter box and **Refresh** are at the top. The cost formula's constants are the magic-balance record `default`.

Questions: where do I see all abilities? how do I test a spell? how do I change a built-in spell? why is my ability's
cost flagged?

## Giving abilities to characters and items

- Give a character an **Abilities** component listing known abilities (and quick slots). In play, keys **1-4** use the
  quick slots (aimed at the nearest enemy in front); **K** opens the abilities window (a pick goes to slot 1); the HUD
  shows slots and cooldowns.
- Characters with Abilities cast by their AI hints a few times a second.
- Items: a book's `use.learn` teaches an ability; a scroll's `use.ability` casts it (free, no cooldown); a potion's
  `use.effects` applies magic effects; an `enchantment` applies effects while worn or on hit. Perks'
  `abilityModifiers` scale abilities by tag.
- Lua: `cast`, `apply_effect`, `remove_effects`, `has_tag`, `add_tag`, `cooldown_left`, `learn`; events
  `ability_used`, `ability_learned`, `effect_applied`, `effect_removed`.

Not yet: charge activation, beams, homing or bouncing projectiles, transforms, scripted effects, element interactions.

Questions: how do I give an enemy a spell? how does the player use abilities? how do I make a spell book item? how do I
enchant a weapon?
