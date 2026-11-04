# Interior objects and Animate the interior

Status: **built (2026-11-04)**, not yet run live.

A building's cutaway interior ([interiors.md](interiors.md)) is a painted picture per floor. The things in it that
would move (a fireplace burning, a clock's pendulum, a stove, a ceiling fan, a lab's tesla coil) are animated where
they stand, as parts of the building on their floor (AnimatedPart `floor` k, [animations.md §4](../assets/animations.md)).
What such things are, where they're found and how they move is in the ontology. Animating them is three steps:

1. **Find**: the vision model looks at each floor for the ontology's interior objects such a building holds.
2. **Describe**: the LLM writes how each one moves, from the ontology's animations, fitted to what is drawn.
3. **Animate**: the user checks the list, then each part is animated (PixelLab) and put on its floor.

## 1. The ontology: interior objects

`data/places/roles.json` (exported to GBO as `gbo:foundIn` and `gbo:animation` on each role, `ontology/gbo.ttl`).

- **The branch.** `interior-object` (under `prop`) has categories, each with its objects:

  | Category | Objects (examples) |
  |---|---|
  | `furnishing` | bed, chair, sofa, desk, throne, pew, counter, rocking-chair, bunk-bed |
  | `storage` | shelf, bookshelf, cupboard, wardrobe, filing-cabinet, locker, dresser |
  | `hearth` | fireplace, stove, forge, oven, cauldron, brazier, kiln |
  | `interior-light` | candle, candelabra, chandelier, oil-lamp, lantern, floor-lamp, neon-sign, lava-lamp |
  | `appliance` | fridge, microwave, washing-machine, coffee-machine, ceiling-fan, dishwasher |
  | `electronic` | tv, copier, computer, server-rack, arcade-machine, jukebox, control-panel, radio |
  | `water-fixture` | sink, toilet, shower, water-cooler, bathtub, aquarium, indoor-fountain |
  | `craft-equipment` | workbench, anvil, grindstone, bellows, spinning-wheel, loom, potters-wheel, printing-press, lathe, conveyor-belt, gear-works, millstone |
  | `lab-equipment` | alembic, potion-rack, crystal-ball, tesla-coil, specimen-tank, centrifuge, microscope, lab-bench, orrery, hourglass |
  | `timepiece` | grandfather-clock, wall-clock, cuckoo-clock |
  | `musical-instrument` | piano, harp, organ, gramophone |
  | `decoration` | wall-decor, potted-plant, birdcage, curtain, globe, incense-burner, tapestry |

  The furniture that was already there (bed, fireplace, stove, fridge…) moved under its category. The categories have
  no look of their own, so each object still takes `prop`'s look, size and solidity where it doesn't set its own.
  New buildings: `laboratory` (modern) and `alchemy-lab` (a kind of workshop).
- **`found_in`**: the building roles an object is found in. A narrower building counts too: a workshop's grindstone is
  in a smithy, and a house's fireplace is in a suburban house. `building` means any. When an object doesn't set it, it
  inherits its category's: `appliance` is in modern homes and public buildings, `lab-equipment` in a laboratory or an
  alchemy-lab. Outside the branch, `chest`, `banner`, `wall-torch`, `table` and `bench` also have one.
- **`animate`**: how it moves, a list of `{clip, motion, opens, prompt, duration}`:
  - `motion` is the animator's description ("the flames in the fireplace flicker and dance…, the stones stay still").
  - `opens` is what plays it, as AnimatedPart's: `always` (a loop), `use` (the player uses it) or `auto` (as someone
    comes near).
  - `prompt` and `duration` apply to `use`.
  - It's inherited like `found_in`. A stove has two motions: `burn` (always) and `cook` (use).
- **Checks** (`Ontology::Check`): every `found_in` names a known building role, and every animation has a clip and a
  motion and a known `opens`.
- **In code**: `Role::foundIn`, `Role::animate`, `Ontology::RoleFoundIn`, `RoleAnimations`, `IsKindOfRole`,
  `InteriorObjects(building, animated)`. The last returns the objects such a building holds, not the categories.
- **Adding objects**: a project adds or replaces roles in `<project>/places/roles.json`, like any role. An object needs
  `broader` (a category), `found_in` and `animate`.

## 2. Find and describe (`/interiors/parts`, `ai_service/app/interior_parts.py`)

The editor sends:
- each floor's picture and the building's frame in it;
- the building's role label and description;
- the candidates: `InteriorObjects(role, animated)` with their animations. When the building's kind isn't known,
  every interior object that moves is a candidate.

1. **Find** (`find_objects`):
   - Each floor is looked at in parallel, at most four floors.
   - The vision model is told what the building is and the candidates by id ("- fireplace: fireplace (…)"), and asked
     for each one it clearly sees: its kind, a label of what it looks like here, a tight box round what moves, and a
     confidence.
   - Kinds not asked for and tiny boxes are dropped. The same thing seen twice (same kind, boxes overlapping by 60%)
     is kept once. At most 12 a floor are kept.
2. **Describe** (`write_motions`): one LLM call covers every part found. For each it gets the kind, the floor, the
   label and the ontology's suggested animations. It answers the motion fitted to what's drawn (in the thing's own
   terms, the rest staying still, nothing new, no people), plus the clip, `opens`, the prompt and duration for `use`,
   and `animate: false` when nothing would visibly move (an unlit candle). When the LLM can't answer, each part keeps
   its kind's first animation (`source: ontology`).

## 3. In the editor: Animate the interior…

Open it from right-click on a building with a cutaway ▸ **Animate the interior…**, or from Inspector ▸ Building Plan ▸
**Animate the interior…**. It's in `components/InteriorPartsDialog.vue` and `apps/editor/InteriorAnimate.cpp`, with
the messages `interiorParts:open|find|set|add|remove|run|stop` and the reply `interiorParts:state`.

- **Find the moving things** runs steps 1 and 2. Each floor has a tab with its picture and the boxes found (yellow to
  animate, grey left still, green animated, red failed). Each part has a row with:
  - a tick (animate it);
  - its kind and label;
  - its motion, editable;
  - its clip;
  - what plays it (always, when used, as someone comes near), with the prompt and seconds for "when used";
  - **Animate** (this one now) and **✕** (remove it from the list).
- **Drawing a box** round a thing it missed, then picking its kind, adds it with that kind's ontology motion.
- The parts the building already has on its floors are listed as "animated before".
- **Animate N ticked** animates them one at a time:
  - its box is cut from its floor's picture (`assets/textures/generated/parts/`);
  - `/animations/generate` animates it with its motion as the thing's animator (Preferences ▸ AI ▸ Non-humanoid
    animation: PixelLab or FLUX video), looping unless its clip is open, close or break;
  - it becomes a part of the building: an AnimatedPart with `floor` k, `door` false and `opens` as chosen. On a prefab's
    copy, it goes on the prefab, so every copy has it (`PutPart`, shared with Animate a part…). A part redone (a box
    over one it has) replaces it.
- A part plays only while its floor shows. Each one's animation plays over its box in the window as it's done.
- One building is worked on at a time. Closing the window doesn't stop the work, and opening it on that building
  shows where the work has got to.

## 4. AI panel

- An editor action `{"do": "animateInterior", "entity": <id>}` opens the window on that building and starts finding.
  The prompt tells the model to use it when the user asks to animate an interior, without operations.
- The prompt's AnimatedPart entry says what such parts are: `floor` k and `door` false.

## 5. Tests

- `tests/places_test.cpp` `PlacesOntologyTest.InteriorObjectsByBuilding` checks:
  - which objects each building holds (a smithy's forge and grindstone, a lab's tesla coil, a modern home's fan);
  - that categories aren't objects;
  - that animations and `found_in` are inherited;
  - that moved furniture keeps its look and solidity.
- `ai_service/tests/test_interior_parts.py` checks:
  - each floor is looked at with the ontology's kinds;
  - boxes are scaled back and duplicates and unknown kinds dropped;
  - the LLM's motions are used and kept in range;
  - the ontology's motion stands when the LLM can't answer;
  - a rect's offset is added back;
  - no candidates is reported.
- The published ontology is regenerated with `AIGE_WRITE_ONTOLOGY=1` (`ontology/README.md`).
