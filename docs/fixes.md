# Fixes and changes after Phase 7

Status: **2026-10-20**, after commit `2c29da0` (7.10, the end of Phase 7). These came from
using the Build panel on real projects (Test1, Test2). They change parts built in 7.4–7.10;
each entry names the milestone it belongs to. None of it is committed yet.

---

## 1. The Build panel's steps (7.4, 7.5, 7.9)

The steps were Request → Assets → Plan → Build → Result. They are now:

| Step | What it holds |
|---|---|
| **① Confirm** | Two tabs: **Contents** (what the place holds) and **Layout** (the zones and where they go). Opens straight after the request is read and the zones are planned. |
| **② Plan** | As before: each zone's parcels, the map, Verify, Revise, Approve. |
| **③ Assets** | After the plan: where the art comes from (the asset plan, web search and share-alike switches), then **Build**. |
| **④ Build** | The art found and made (its own page, below), then the scene. |
| **⑤ Result** | As before, with **Finish**. |

- **No Request page.** After the request is read, the zones are planned at once (`Host::layoutStep`,
  on in the editor; tests leave it off and keep the old flow). Everything the Request page had is in
  Contents.
- **A progress page** from the request to the layout: the readings (n of 3), the readings corrected
  and why, comparing them, planning the zones, laying them out; a bar, ✓ per step, the seconds the
  active step has run, and a line when the AI didn't answer and is asked again (attempt n of 3).
- **The Assets step moved after the plan**: the art is chosen for what was planned. Densities and
  each part's fields moved to Contents (they shape the plan).

## 2. ① Confirm: Contents (7.4)

- **Name, size, root settings, walls, features, style** and the **components table**: every component
  the place can have, the ones it has first.
- **"How many"** says what it counts: "1 ward(s) · 25 buildings each", "3 shop(s)", "how common".
- **What each part holds**: each part's own fields, as compact cards.
- **To decide**: each open point as a decision, with the answer that was assumed beside it and a way
  to change it (the field's own control when it sets one, else a text answer that goes to the zones'
  planning). **Also assumed**: only what no decision covers.
  - The interpret prompt asks for `decisions: [{question, assumed, path, options}]`; the merge keeps
    one per point (by field, else by question) and the other readings' answers as options.
- A change to anything the zones depend on marks the layout **out of date**; "Plan the zones" waits
  for **Lay out the zones again**.

## 3. ① Confirm: Layout (7.5, 7.6)

- **The zones laid out alone** (no parcels): a simpler map, bigger; beside it each zone with its
  description, where it was meant to go (the AI's `placement` sentence and relations) and where it lies
  (from `places/Where`: "In the north-west; along the river (its west bank); beside the market (east)").
- **Each zone's size**: its land (cells²) and its count of buildings, editable. Each zone gets at least
  the land its buildings need (a typical lot of its kinds × 1.9, as the compiler does), and the site
  grows to hold them all; a warning only for a zone painted smaller.
- **Layout patterns**: the place's main layout (pattern tiles, with shares for a mix) and **each zone's
  own** (`street-style`, now on every zone with buildings; unset, the main one). The AI chooses a zone's
  pattern when it plans; the residential ward's fixed "organic" default is gone.
- **The painting tool** (click the map): the zones on the right, the site on the left; a brush (1–16
  cells, and Erase) paints a zone. A zone takes exactly its painted cells (taken from any other zone);
  the first stroke adds to the land it has. Once something is painted, the layout keeps its site (the
  cells are the site's); in the plan, painted zones scale with the site. "Let it be placed" clears it.
  - The solver's partition takes a zone's `paint` (cell runs) or `box` first; it doesn't grow beyond it.
- **Revise** works here, on the zones only (the AI is told so). **Plan the zones →** plans each zone's
  parcels inside the zones exactly as laid out (below).
- **The layout is frozen.** The plan came out with zones different from the layout's, and going back to
  the Layout tab changed the layout again (the plan re-solved from seeds on a bigger site, and its
  fitting rounds grew zones). Now each zone's cells, the site's side and the networks' seed are kept on
  the spec (`layoutCells`, `layoutSide`, `layoutSeed`); the plan fills those cells on that site, with no
  growth; the Layout tab shows the same layout when you come back. Only what you do in the Layout tab
  changes it (paint, draw, a size, Revise there, "Let it be placed", "Lay out again"). In the plan,
  Revise and the AI's review can't move, resize, add or remove zones; that part of an answer is dropped
  with a note pointing to ① Confirm → Layout. More buildings than a zone holds: the AI is told each
  zone's room and fits more in first (smaller parcels, a denser pattern); what still doesn't fit is
  capped to what the solver placed, and the plan says so, with where to make the zone bigger.
- **Better layouts** (`place-layout.md` §11): the AI sketches the zones on a 16×16 grid of the site
  with its networks drawn in (water, roads, walls, outside the walls), for every build type whose
  zones share a site; the sketch guides the zones' seeds and growth. The prompt asks for the design
  first, zones side by side (never inside another), one per component, placements as relations.
  A zone of a module the Brief didn't ask twice for is dropped. Each fresh layout is solved with four
  seeds and the best by quality checks kept (one piece, compact, not inside another, room, relations
  met, unique, coverage); what fails shows in the Layout tab with **Fix with the AI**. The site is
  sized for the zones to cover three quarters of it (was 60%).
- The confirming questions are asked in the background; their answers (and decisions answered in
  words) go to the zones' planning as the user's directions.

- **Each zone's kinds of building.** A zone shows the kinds its rules allow (a market ward: shops, inns),
  each with how many: empty is "the plan decides" (shown as about how many it will make, by the rules'
  weights), a number is kept. They're in the Brief (`components.<zone>.kinds`), so Contents and Layout
  show the same; setting them grows the zone's count to at least their sum and lays the zones out again,
  each where it was. The zone's planning is told them and the plan holds exactly that many (added,
  removed or recounted after the AI's answer; rebalancing to the Brief's total leaves them alone). What
  the zone's land can't hold is capped, and the note says which kinds were cut.
- **Confirm and Plan side by side.** Once the plan is made, ① Confirm can be opened again and the plan
  stays: it shows the layout and contents the plan was made from (the zones' own map, kept as
  `zones.png`), with "Back to the plan". Changing anything there asks first: "Discard the plan?"; yes
  goes back to the layout (the same one), and the plan is made again from it.

## 4. Revise (7.5)

- **At the layout, a revision moves only what it moves.** "Put the craftsmen's ward between the temple
  ward and the docks" moved the temple ward too: the AI rewrote the relations of every zone it named,
  the scope guard let them through (the request names them), and every zone was laid out again from
  its seed. Now a zone named before the clause's first word of place ("between", "along", "north of",
  "where"…) is what moves; one named after it is a landmark and keeps its place (the AI's changes to
  where it lies are left out, with a note); "leave / keep / don't move X" pins X. The moved zones are
  laid out anew and every other zone keeps its exact cells; only if a moved zone then lacks room do
  the zones the request didn't name give way. Pulling a failed fact harder re-lays only that zone.
- **A painted zone can be moved by Revise.** Painted cells are exact, and with the layout kept they held
  a painted zone in place whatever a revision said ("put the market ward south-east of the temple" left
  it where it was painted). A zone a revision moves now lets its painting go (noted in the revision).
- **A moved zone's place is composed from what it's placed by** (`place-layout.md` §8.3). "South of the
  temple ward" put the market ward on the west bank (its old one), the temple being on the east. The
  revise prompt now has the model reason step by step (what moves, its references, where they are now,
  what carries over on the axis the relation doesn't move along, then the relations), and the code
  composes it again from the layout: on such an axis the zone takes its reference's side of the river
  and the main roads and its part of the map, unless the request says otherwise; the model's
  contradicting guess is dropped, and the revision's notes say why.
- **A moved zone starts on free land.** Its seed was chosen before the kept zones took their cells; the
  best cell was inside a kept zone, and it moved to the nearest free cell: a pocket shut in by the temple
  ward, the river and a road, so the market ward got a sliver. Now the kept, painted and drawn zones'
  land is theirs first, and a seed goes on the free land left, in a stretch of it big enough for its
  zone (`place-layout.md` §6.3).
- **One placement per zone in an answer.** The AI may move a zone and, in the same answer, restate its old
  relations in a "set"; applied in order, the old ones won. A zone's "move" now decides its relations.
- **Direction words** ("east of the river") were read from the whole request and given to every zone it
  named: one request put every zone east of the river. They're now applied only in a clause that names
  the zone and one other part; the rest is the AI's reading. Their notes ("…, as the request says") are
  gone.
- **"The others", "the rest", "all", "every"**: the request is about every zone (the guard that keeps a
  small request to the zones it names no longer drops them).
- **At the Layout step**, a revision's spatial facts are checked on the new layout and pulled harder,
  twice at most, as in the plan.
- **Revise changes settings too**: the main layout, a zone's pattern, its area and buildings, the place's
  size, parts on or off and how many (the revise prompt's "brief" and "set").

## 5. Interpreting the request (7.4)

- **A zone's count** is how many of that zone. A count far above what the size has in zones (a reading
  wrote 10–40 for the houses) is taken as its buildings (`params.plots`), the count back to 1; the
  prompt says so too.
- **Density words** the AI uses ("medium", "dense", "sparse") map to the five (some, common, rare...),
  instead of the default.
- **The choices form** (start screen): build type, size, the main settings, features, the main parts;
  sent with the words and kept as set (`ApplyChoices`, `BuildChoices`).

- **The three readings** (literal, typical, generous: what they are and how they're merged is
  in `place-interaction.md` B7) are now **a request each**, side by side, shown as they come;
  one that fails doesn't sink the others. They're given **only the named build type's
  options** (about 8 KB for a village, was 36 KB with all eight types).

## 6. Assets, sourcing and the art (7.9)

Why a built place showed placeholders where art had been made:
- the catalog rejected anything not the project's without a licence, and **generated art has none**: it's
  the project's own now;
- **placeholder images** from earlier builds were in the catalog as project art (and chosen); they're
  left out;
- a piece made for a slot takes **the slot's layer, size and role** (the vision model's guesses could
  differ: a crop read as ground, a barrel with no role);
- a role of alternatives (`barrel|pot|bench|sign|lamp`) is **any of them**: the art is asked for as the
  first, the check passes any.

And:
- **Ground**: a terrain the project has no tiles for (dirt, cobble) is made by the Art tool too, as one
  plain tile (a full autotile set isn't made yet).
- **The art's own page** in ④ Build: progress, a card per piece with each try's image and the vision
  check's verdict; click an image for a larger view; **Build with placeholders** to stop waiting.
- **Refine** a piece (in your words): made again and checked again, while sourcing or after the build
  (then the place is built again with it; the old piece leaves the catalog's choices).
- The art panel's state is kept in `session.json`; closed mid-sourcing, it picks up again.
- **Build again** tries again what's still a placeholder.
- **Finish** saves the scene with the place and closes the build.

- **③ Assets finds the art; ④ Build builds.** ③ Assets has two tabs: **How each is found** (the source
  per part, the web and share-alike switches) with **Find the assets**, and **The assets**: the art being
  searched for and made (as it comes, each refinable), then what each part got (the project, the
  library, made, a placeholder). The build waits there (stage `sourced`) for **Build**; ④ Build then shows
  its progress: the build runs a phase a frame (the art chosen, the scene built, checked, repaired, saved
  and drawn), with a bar and what it's doing. ③ Assets stays open during and after the build.
- **No ④ Build step**: the build is quick, so its progress shows in ③ Assets, and the steps are
  ① Confirm, ② Plan, ③ Assets, ④ Result. **④ Result** shows only the built place (the plan is ② Plan's).
- **"How each is found" is what will happen**: it showed an estimate for every part the Brief's components
  could have; it now shows the real decisions for what the plan has (each piece of art, and the ground),
  worked out when the plan is approved and again when a part's source changes.
- **The project's ground was hidden by a placeholder**: a placeholder grass tile from an earlier build counted
  as the project's grass set, so the generated grass was never used (grey ground). Placeholders are left
  out of the terrain set as they are of the catalog.
- **The crash after the art was checked** ("bad function call"): the scene build turned each new image into
  a prefab with no pixels-per-cell function given, and called it. Now an image is sized by its analysis
  ("cells" across), else by its catalog entry. A build that fails now says why and goes back to ③ Assets.

## 7. The editor (7.10's "stays responsive")

- **The build's heavy steps run off the UI thread** (planning, laying out, partial solves), one at a
  time; the panel shows "Working…"; the build's AI answers wait for the job.
- **The AI client runs four requests at a time** (it ran one): a long build request no longer holds up
  the AI panel. Starting the service still runs first and alone.
- **Mistral's calls** wait 180 s at most (was 90), and each may write about 3,000 tokens of thinking and
  6,000 of answer: a call that ran on (with no ceiling) could hold a step for five minutes.
- The questions running beside the plan no longer hold it up (it waited for every request, not its own).

## 8. Solver and compiler fixes (7.6, 7.10)

- **A room's floor stays joined to its doors** as things are placed; the check searched from all doors
  at once and let a room be split between them.
- **Stairs** between dungeon levels and floors count as walkable (their object stands on the cell).
- **Completable**: a key found opens its lock on the next pass (it stopped one pass early).
- **Dangling references** to temporary JSON (the window's choices, the compiler's levels and floors, a
  castle's wall and moat), the same pitfall as before; the source-rule test guards `.value(...).items()`.
- The solver's per-op **timings** are kept beside the solved document, not in it (the same seed gives the
  same document).
- The **zone label** on the map is larger.

## 9. Tests

- `tests/buildtypes_test.cpp`: the layout step (questions, directions, out-of-date, a drawn zone, a
  zone's pattern and size, the plan keeping the zones where they were), painting a zone, the choices,
  building from choices alone.
- The demo project was removed (2026-10-20): the tests that load it (SliceTest, DemoPrefabs,
  ContextBuilder, AiGameplay, EditParity, DemoWorldBible, Story*, LevelBuild, ProjectTemplate, Modules,
  Export, and three Python test files) fail until they're given another project or removed.
- The changes after the layout step (decisions, revise settings, painting append, the AI client's
  workers, the frozen layout, the sketch and quality checks) haven't been run against the tests yet.

## 10. Phase 8 (2026-10-21)

Phase 8's fixes are listed in `development_plan.md` (Phase 8, its status). From testing:
- **Grey ground**: a placeholder tile set hid the generated grass (now left out of the terrain set).
- **Stripes on the ground**: generated tiles weren't seamless at their edges; each is made so
  (`-seamless.png` beside it) before it becomes a tile prefab.
- **One picture for different things**: a slot of alternatives took one art for all its objects.

- **① Confirm and ② Layout are steps** (they were two tabs of ① Confirm): ① Confirm (the contents),
  ② Layout, ③ Plan, ④ Assets, ⑤ Result. ① Confirm's button goes on to ② Layout ("Lay out the zones
  again →" when the contents changed); "Plan the zones →" is the Layout's.
- **A source you choose is searched** (`Decide`, place-assets.md §5): after a build, setting a part's source to
  web found nothing new: the art generated into the project counted as "the project's, fits well" and was
  used again. Now "web" counts only project art that came from the web, and "library" only library art;
  what the chosen source can't give still falls back to the project's (better than a placeholder).
