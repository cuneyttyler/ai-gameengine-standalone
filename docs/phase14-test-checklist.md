# Phase 14 — Manual Test Checklist

The user-test checkpoints from Phase 14 (`development_plan.md`, "Character AI"), for one
end-of-phase pass. Automated tests cover the logic: the templates, the schedule choosing
work by day and the bed by night, patrols, an aggressive enemy noticing, chasing and
attacking, a wall hiding the player, cowards fleeing, a scripted package's Lua, and actors
across scenes (`character_ai_test.cpp`). This list covers how the characters look and feel
in play. The design is `ai-packages.md`.

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A built village (Build panel) so there are beds, tables and counters (they become smart
  objects); or place entities with **SmartObject** by hand.
- **F4** in Play shows what every character is doing and how aware it is of you.

## 14.1–14.3 Packages and smart objects

- [ ] Records ▸ **package**: "keeper-work" (template work, location marker "shop",
      schedule 08:00–20:00, data object counter) and "keeper-sleep" (template sleep, location
      alias bed). Give an NPC **Behaviour** (packages "keeper-work, keeper-sleep", bed: a
      marker near a bed). In Play with the clock at day, they walk to the counter and stay;
      at night (Project Settings ▸ Time.startHour 22) they walk to a bed and disappear into it.
- [ ] A **sandbox** package in the tavern: they sit, eat, wander and now and then say an
      idle bark (Barks.idle) when you're near.
- [ ] A **patrol** package (data path: two markers): they walk between them, pausing.
- [ ] A **follow** package (target player): they follow at a distance and catch up.

## 14.5 Perception and combat

- [ ] An enemy with **Behaviour** (aggression Aggressive), **Perception** and **Barks**
      (alert "There you are!") notices you in front of it, says its line, chases and attacks;
      from behind or through a wall it doesn't (until you make noise attacking near it).
- [ ] Its friends nearby (same team, assistance Allies) join in.
- [ ] A **combat-style** record (range kiting, keepDistance 5) on an archer with
      RangedAttack: it keeps its distance and shoots.
- [ ] A peasant (aggression Defensive, confidence Cowardly) runs when you hit them.
- [ ] Out of sight for a few seconds, an enemy goes to where it last saw you, then back to
      its package.

## 14.6 Scripted packages

- [ ] A package with template **script** and a Lua file with `function run() move_to(..)
      bark("Boo") end`: the character walks there and speaks.

## 14.4 Actors across scenes

- [ ] Give an NPC **Actor** (key character/x, prefab) in two scenes. Kill them in one: they
      aren't in the other either.
- [ ] A package with location.scene set to another scene: after you leave and come back,
      they're in that other scene, not this one.
- [ ] Save, load: the roster (who's dead, who's where) comes back.
