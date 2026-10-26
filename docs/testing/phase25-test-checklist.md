# Phase 25 — Manual Test Checklist

The user-test checkpoints from Phase 25 (`development_plan.md`, "Build improvements"), for one
end-of-phase pass. Automated tests cover the modern city's layout (`BuildTypesTest.AModernCityBlockFromItsRequest`:
districts, asphalt streets with sidewalks, traffic, every door reachable), the build types and their
choices, the published ontology, the build's robustness (stop, continue, revise, inspect), and the
source rules. The live build tests (`LiveBuildTest.*`, with the AI service) produce the quality runs
in `tests/quality/`. This list covers the editor and what a build looks like.

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor`.
- **Keys:** `ai_service/.env` has `MISTRAL_API_KEY`, `PIXELLAB_API_KEY` and `BFL_API_KEY`.

## Building from the Planner

- [ ] Planner ▸ Build: start a build of the project's places; Stop, close the project, reopen it and
      press Continue: it goes on from the place and step it stopped at, without planning again.
- [ ] Rebuild a built place: it is planned and built again, its old scene replaced.
- [ ] A place with two levels (a dungeon with a lower level) becomes two scenes joined by stairs.
- [ ] The AI panel shows each check's scores (layout, plan, build) while a place is built.

## The build method and the Paint Provider

- [ ] File ▸ New Project asks Assembled or Painted; Project Settings ▸ Places shows the choice.
- [ ] After a place is built, Project Settings ▸ Places ▸ method can't be changed (it says why).
- [ ] Preferences ▸ AI ▸ Paint Provider: Black Forest Labs, and the five models; without a BFL key
      it says so under the selects.

## Modern places

- [ ] Build panel: "a modern city downtown block with office towers, apartments and shops": the Brief
      reads it as a modern city (not a medieval settlement); "a walled city" stays a settlement.
- [ ] The Brief's choices for a modern city show the layout form: zone shape, street grid, block size,
      block fill, coverage, arrangement (blocks, shared, full, full, ordered by default).
- [ ] The built city: rectangular districts, straight streets running on across districts with
      sidewalks, towers at the centre, cars in the lanes, street lights evenly spaced.
- [ ] Change the layout form to grown, per-zone, frontage, natural and build again: the city lays out
      like a village (organic wards, open land between).
- [ ] A village keeps its look (grown wards, organic streets): nothing about it changed.

## Painted

- [ ] With Places ▸ method Painted, a built place's block-out (`places/<id>/blockout.png`) is painted
      by the chosen model into `places/<id>/painting.png`; the build's log shows the model and prompt.
- [ ] `tests/quality/bfl_results/`: compare the models' paintings and `comparison.md` tables; the
      FLUX.3 Image ones keep the block-out's layout.
