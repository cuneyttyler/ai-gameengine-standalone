TO-DO (2026-10-26, queued)
- AI action auditing: A panel for seeing AI actions in a list. Maybe we can have a panel for auditing all actions with showing it's type (manual, AI)
- git style commit and simple branch mechanism. A panel for this. Works together with auditing.

Ontology (2026-10-27)
- Relative sizes (asked 2026-10-27): in a generated build some props come out at a size that doesn't match the rest. A
  size trait on every role, relative to what it appears with (a bush 1/10 of a tree; a tree as tall as a one-storey
  building), so each piece is scaled against its neighbours in the scene (and painted to it on the sheets)
- Go through all of data/places (roles.json, relations.json, modules, vocabularies) for the new traits and relations
  (docs/places/ontology-in-use.md): each was set only on the roles the dungeon and city tests needed. Check every role for
  orient, views, axis, faces, turns, run, variety, found, purpose, and every role for its relations (set-in, mounted-on,
  against, leads-to, aligned, lights, part-of, used-with, debris-of, coloured-by); e.g. fences/hedges/railings as runs,
  windows/signs/awnings set in or mounted on buildings, benches/boats/trains aligned with paths/water/rails, more roles
  coloured by the background (floors' rugs, cliffs, rubble), found for every role a relation can name
  First pass (2026-10-27): against a wall (sofa, tv, sink, toilet, shower, filing cabinet, whiteboard, copier, water
  cooler, vending machine, nightstand, workbench, weapon rack, desk); used-with (school desk → whiteboard, sofa → coffee
  table); faces the road (billboard, bus stop; `faces` now has a facing use); faces the door (reception desk, checkout
  counter); part-of (fuel pump → gas station, drawbridge → gatehouse); gate set in a wall; coloured by the background
  (rock, boulder, mound, cliff and bank pieces); headstone made with its grave; vehicles no longer `vary`. Still to do:
  water and rails as `found` targets (boats, trains aligned), awnings/signs/windows on a building's face (needs building
  faces, not wall runs), runs beyond walls (fences, hedges, railings in the scene)
- Layout rules for the other zones (modules' "layout", docs/places/ontology-in-use.md): only the parking lot has one
  (bays). Shops (shelves in aisles), market squares (stalls in rows facing a walkway), orchards (grid), graveyards (rows),
  dining halls (tables in rows with room for benches), car showrooms, bus depots, libraries, classrooms; new patterns as
  needed (grid, ring, along-edge). Then an access check in verify: everything used reachable from its zone's entrance.
  Later, optionally, the LLM adjusting a zone's rule for one place
  First pass (2026-10-27): the orchard, graveyard, field and market square patterns moved from Compile.cpp into their
  modules' layout (op grid/rows); parking lot bays. Still: shops, dining halls, depots, libraries, classrooms; access check
- Which art is made with which (made-with, docs/places/ontology-in-use.md): only stairs are made with the walls. Go
  through every role: what must match what it stands with (gates with fences and walls, bridges with paths and roads,
  fireplaces and wall shelves with the walls, market stalls with each other, a building's annex with the building)
  First pass (2026-10-27): pillars, fireplaces, altars, towers, wells, crypts and sarcophagi made with the walls (acts
  only where a place has a wall block). Still: things made with fences, paths and buildings (made-with to a non-wall piece)

Planner: animation and effects in ⑧ Visuals (2026-10-27; the "Animations step" designed in docs/ai/ai-planner.md §5.8
and docs/systems/effects-animation.md §11, left for after Phase 18, which is now built). Phase 28, with its AI panel part.
- Today ⑧ Visuals covers only the UI (theme, screens, HUD, notifications, map) and ⑦ Systems only names effects for
  abilities: nothing plans animations or effects
- Decided: no separate Effects step, and animation and effects go under ⑧ Visuals, which becomes "how the game looks
  and moves":
  - Effects are mostly chosen (a preset per ability or hit type, recoloured to its school), too little for a step and a
    confirmation page of their own
  - Effects and animation are tied: clip events fire effects (an attack frame the hit, a cast the spell, a death a
    dissolve); one page keeps them consistent instead of cross-checking two documents
  - ⑧ already comes after what it reads: ④ Characters (the classes to animate), ⑥ Places (their moods), ⑦ Systems (the
    abilities); and it's the same kind of decision as the UI theme (the look, from the Brief's art style and tone),
    confirmed once
- ⑧ Visuals gets four tabs, each its own job and document, stale only from what it reads (an ability changed redoes
  Effects, not Animation; a place's mood redoes only that place's Mood):
  - Interface: today's step (theme, screens, HUD, notifications, map)
  - Animation: per character class, the animator (humanoid, quadruped, flyer, prop, or an animator record) and the
    clips it needs (idle, walk, attack, hurt, death, cast…), directions, and the style ("the warden: humanoid, idle,
    walk, slash, cast, hurt, death; 8 directions")
  - Effects: per ability and hit type, an effect from the presets (data/fx/effects) or composed from them in the
    ability's school colours (magic.md §9): impacts, projectiles, auras; tied to the clips' events
  - Mood: per place, its lighting (ambient, lights), weather and colour grade, from its mood
- ⑨ Validate checks across them: every clip event names an effect that exists, every ability has its effect, every
  class has the clips its abilities need (a caster has cast)
- Plan in ⑧, make in the build: ⑧ decides only what's needed and in what style; the sprites and frames are made or found
  in ⑫ through the asset plan (project, library, web, or generated with PixelLab animate), checked frame to frame by the
  vision model. Making animation is slow and costly, so it never runs while the user is still shaping the design.
  Cutscene camera work and effects stay in ⑫, where the cutscenes are written

v1.0: what's left (2026-10-27; roadmap.md: Phases 0–24 + the Planner; done when a short isometric action RPG and a
top-down life-sim are each made by hand and by the Planner, exported and played through)
- Trying what's built: Phases 12–24 are "built, to be tried", 25 "partly tried", 26 untried; their checklists
  (docs/testing/phase12–25-test-checklist.md) have nothing ticked; write a Phase 26 checklist
- The done-when test: the two games by hand and by the Planner, exported, played through (and the fixes it turns up)
- Release: macOS and Linux builds (only Windows is built); user documentation (to check how much exists); the template
  projects exist (arpg, rpg, action-adventure, life-sim)
- The phases' "Not yet" items (decide per item from the test games whether v1 needs it):
  - Saves and input: a thumbnail per save slot; rebinding keys inside the game
  - Character AI: the Behaviour section's day timeline in the Inspector, "simulate a day"
  - Narrative: the cutscene timeline editor; bark lines in dialogue
  - Game UI: pop-ups, the map and other system screens, world-space UI, HUD adjustments
  - Effects: skeletal animation, additive particles and collisions, shadows, colour grading
  - Magic: hold and charge, beams and homing, transforms, scripted effects, surfaces, a test arena
  - Physics: draggable collider gizmos, one-way tiles, characters as Box2D bodies against each other
  - Planner: module anchors and actor slots, AI choice among query candidates, local build edits
  - Shipping: full-picture colour filters, sound captions, loading chunks from disk, store platforms
  - Places: painting big places in tiles (Phase 26); the Ontology items above
- Backgrounds: FLUX.2 klein ignores the colour map's scale (builder_tests/background_tests/README.md); say the scale in
  words or use its rough-draft recipe if it's to be offered
- Suggested order: try the Phase 25–26 build flow on a real project; then the action RPG end to end with the Planner
  (with animation and effects in ⑧ Visuals), which shows which "Not yet" items v1 really needs


Paper
This part looks bad, can you revise it?

III. METHOD
A. Methodology
Every build runs the same procedure (Fig. III-A). Each step
reads only documents written before it and writes one typed
document, kept with the place, so any step can be re-run alone
and every built entity can be traced back to the plan node that
made it.
The building procedure. Each step writes a typed document;
every step queries the GBO ontology. Verification failures are
repaired locally at the node that caused them.

Also, take small screenshots from the app from the build panel and put them into article use at most 2 screenshots. If you can make a nice looking visual for describing our algorithm with several sections, covering 1/4th of a page (with colors) it would be great. If you did anything already I can't see it. These will make the article 8 pages already leaving no space for evaluation so you also need to shorten the text. Current length is ok, it leaves 1 page for evaluation but adding screenshots and a figure will use it.

Please check docs again they're changed. Also check @docs/place-layout.md

use generated png for a big village layout and final generated content for a small village

you can mention our design principles. AI actions should be tracable, revertable, schema and ontology use (adhering to strict definitions), 
confirmation of ai suggestions (such as in Confirm/contents fields are shown before proceeding and Confirm/layout tab and plan tab there are images for showing the design, user sees, makes adjustments through structured input and also
through natural language input and confirms) - this is important
also, what are the design principles behind our prompt design you can check sources for these.
determining locations and rotations/directions of the assets

make mention of multi-hop spatial reasoning (chain of though) mechanism that is used for revise of layout and plan. the system calculates relative positions of zones or artefacts (in layout and plan steps) against each other to determine where to place the requested element. it works in chain of though mechanism as described in docs/place-layout.md, meaning: prompt: put A to south of B, reasoning: if B is east of C, A must be east of C
