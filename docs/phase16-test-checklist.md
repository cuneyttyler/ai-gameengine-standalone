# Phase 16 — Manual Test Checklist

The user-test checkpoints from Phase 16 (`development_plan.md`, "Difficulty"), for one
end-of-phase pass. Automated tests cover the logic: scalable fields and their categories, the
built-in levels, project levels and modifiers, applying and restoring values with health kept in
proportion, overrides, the combat preview, encounter levels, adaptive steps, assists, the session
playing at a level through a save, difficulty conditions, and the menus (`difficulty_test.cpp`,
`test_story_validation.py`). This list covers how it looks and feels. The design is
`difficulty.md` (see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A scene with the player and a few enemies (with Health, MeleeAttack); ideally two scenes joined
  by a Gateway.

## 16.1 Levels and effective values

- [ ] Project ▸ **Difficulty** ▸ Levels: the built-in five (Story … Nightmare) show, read-only.
      **Make them this project's levels**: five files appear in `records/difficulty/`, editable.
- [ ] Change Hard's enemies step to 5 and add a modifier (Health.max, enemies', ×1.2). Save.
- [ ] **Category table**: change enemies/health step 4 to 2.0 and save; `records/difficulty-table/default.json` appears.

## 16.2 Overrides

- [ ] Select an enemy; the Inspector shows ⚖ next to Health.max, MeleeAttack.damage and the
      other scaled fields. Click it: Difficulty ▸ Preview shows each field at every level, with
      the reasons on hover.
- [ ] Type 99 under Hard for Health.max: it's kept (yellow), the others still computed. Ctrl+Z
      undoes it; ↺ also clears it. Save the scene: it carries a DifficultyOverrides component.
- [ ] The **combat preview** below lists, per level, how many hits each side needs.

## 16.3 Level scaling

- [ ] Add a **Zone** (min 5, max 8) and a Spawner (level 1) to a scene; Project Settings ▸
      Difficulty ▸ scaling FixedZones. In Play the spawned enemies are level 5 (F4 or their
      Progression in the Inspector).

## 16.4 Assists and adaptive difficulty

- [ ] In the game (sandbox): New game asks **Choose a difficulty** with each level's line;
      pick Hard and the enemies have Hard's health.
- [ ] Pause ▸ Settings: damage reduction (0/25/50/75%), more protection after each death, game
      speed (100/75/50%), invincible. Each works in play and is remembered next time.
- [ ] With Project Settings ▸ Difficulty.changeAnytime on, Settings has **Difficulty**: changing
      it during play changes the enemies at once. Off: it isn't offered in play.
- [ ] With adaptive allowed and "Adjust difficulty to how I play" on: die three times and the
      enemies get one step weaker; win two fights unhurt and they're back.

## 16.5 The panel, Play at a level, the curve

- [ ] Difficulty ▸ **Play at** Hard, then Play: enemies match the Preview's Hard column. Stop:
      the scene's values are the base ones again (Inspector).
- [ ] During Play, change Play at to Story: enemies change at once.
- [ ] **Curve**: with Zones 1–3 in the first scene and 8–10 in the next one along the gateway,
      the second is flagged "too hard"; an enemy outside its band is flagged too.

## The AI panel

- [ ] Ask the AI "make Hard harder: enemies deal more damage": it writes the hard difficulty
      record (a modifier or the enemies step), not bigger numbers on the prefabs.
- [ ] Ask "make the crypt boss have 200 health on Hard only": a DifficultyOverrides on the boss.
- [ ] Ask "make skeletons tougher": it changes the skeleton prefab's base Health.max.
