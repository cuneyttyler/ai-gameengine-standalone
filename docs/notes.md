TO-DO (2026-10-26, queued)
AI settings / project (in progress)
- Preferences ▸ AI: Prefab art + Background (FLUX.3 Image) app defaults
- Project Settings ▸ AI: provider, model, art, background, each "Use app default (…)" (engine side done: AiSettings; UI dropdown, effective values, PUT /background-provider still to do)
- New Project: one window (name, location + Browse, grid, LLM provider/model, prefab art, background); no starter content, no Assembled/Painted
- Painted method removed (engine/test side done; still: EditorApp paint provider + PlacesMethodLock, Python backdrop/paint endpoints, docs, schema fixture)
- Planner writes the place's background description in an editable field; Build ▸ Assets background row: Generate/Regenerate (POST /places/background, done in the service)
- Background image import: a manual editor feature (e.g. Scene ▸ Background Image…), not in the Build panel
Assets
- ~~"How each is found": choosing find/generate should stay on the page, not start generation/search~~ (done 2026-10-27)
- ~~Generation/search: show rejected art's image (not a placeholder), with Keep; add Pick (user library search) as for not-found art~~ (done 2026-10-27)
- ~~Library tab: top/bottom sections resizable; art grid in a folder shows art badly (rectangles); no "Add to project" for art already in the project~~ (done 2026-10-27)
Viewport / editor
- ~~Clicking a prefab in the viewport selects it in Assets ▸ Prefabs~~ (done 2026-10-27)
- ~~Panel fullscreen: other panels' shortcuts should open them~~ (done 2026-10-27)
- ~~Right-click menu closes on clicking elsewhere~~ (done 2026-10-27)
- ~~Right-click on a prefab/tile: Draw; on a prefab: Replace prefab (also in the Inspector)~~ (done 2026-10-27)
- ~~Selecting a tile: show its prefab image in the Inspector~~ (done 2026-10-27)
- ~~Periodic snapshots for crash recovery; detect a crash and offer recovery at startup~~ (done 2026-10-27)
Builder
- ~~Finish opens the Viewport~~ (done 2026-10-27)
- ~~Token counts and seconds per step (as in the Planner)~~ (done 2026-10-27)
- ~~Next step: overlay with a spinner and what's being done~~ (done 2026-10-27)
- ~~Add a sketch step~~ (dropped 2026-10-27: the painted background and matched art cover it)
AI Planner
- ~~Home page: previous plans; a button on each step back to the home page~~ (done 2026-10-27)
- ~~Confirm window decisions (number of characters, places…) decided during story generation and editable~~ (done 2026-10-27)
- ~~Choices section (story ontology options usable with the prompt), as in the Build panel~~ (done 2026-10-27)
- ~~Concept step: a synopsis of the story~~ (done 2026-10-27)
- ~~Story: scene detail window resizable~~ (done 2026-10-27)
- ~~Total generation time next to total tokens~~ (done 2026-10-27)
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

TO-DO
- AI Plan panel
- On AI panel confirmations - when a change is applied, show Undo button. When the change is reverted using ctrl-z show apply hide undo.
- Mouse click-drag on viewport should move camera
- Incomplete build plans in build plan should be saved between sessions.
- Build history (builds which are made with build panel)
- Shift + wheel should make brush bigger/smaller (1 cell, 4 cells, etc)
TO-DO
- AI action auditing: A panel for seeing AI actions in a list. Maybe we can have a panel for auditing all actions with showing it's type (manual, AI)
- git style commit and simple branch mechanism. A panel for this. Works together with auditing.

DONE
- Prefabs should be resizable with some shortcut on keyboard or mouse, or through ui when adding to the scene
- Remove prefab option
- On empty scene space, we can show tile borders
- Add right click menus with related options (you decide - some options need confirmation), on scene and assets menu.
- Shortcuts refine. Let's think about what kind of shortcuts would be helpful for editing and add them. And add to somewhere shortcut info.
- Add context menu to the top (File (New Project, Open Project, Recent Projects, Save Project, Save as..., Settings), View, Help etc)
- Make left panel collapsable.
- Shortcuts for making each panel visible/invisible.
- Grouping prefabs/assets? I don't know what mechanism they use in other game engines but prefab/asset number might become big and hard to manage.
- Art tab history
- Delete quest pops up aige-editor.local says and does nothing and window gets locked
- Saving, quitting, reopening a project doesn't load earlier conversations
- Multiple scenes in a project, connecting scenes from gateways, Scene manager panel
- At launch, it shows recent project window with buttons new project, open folder etc
- Recent projects in the context menu (File) should be dropdown
- In assets, add multiple select using shift and Ctrl-A
- Viewport visibility should have shortcut too.
- In shortcut list, change F1 description from "This List" to "Shortcuts"
- Can't close project settings - it should be on a separate window.
- Panels should be movable to left, bottom, or right
- Shortcuts should be editable in settings
- Remove ctrl from panel visibility shortcuts
- When multiple assets selected, keep Drawn selectbox and below buttons in side side part





Building
SceneCraft: relations that link any group of items, each with a score, and a render-and-critique loop, which gave its biggest gain.
Constraint Is All You Need: existing and locked things are fixed points. The layout score penalizes both clutter and large empty gaps. Reachability must be tested; their agent found 9.5% of levels unplayable.
Database-driven levels: the AI writes the module library ahead of time. Big places then need few AI calls: the AI plans the upper levels and module rules fill the lower ones.
CityGenAgent: building-density bands, and the AI also critiquing the plan view.
Narrative-to-Scene: per-layer grids as the source of truth. Without real conflict resolution, only 72% of relations held.
GameTileNet: five affordance types for assets; split tilesheets into whole objects and upscale images before the vision model looks at them.
Villages (Emilien et al.): interest maps with vetoes, organic growth along roads, and "growth scenarios" such as an old walled core with newer farms outside.
Parcels (Vanegas et al.): parcel subdivision styles, and keeping customizations when a parent part is regenerated.
Hierarchical WFC: the module tree is a weighted "consists-of" tree, densities are its weights, and selecting or regenerating works by meaning.
ClarEval and Active Disambiguation: batch the questions, always show size words with numbers, and ask where the drafts disagree.

2. An ontology the builder consults?
Yes, it makes sense. I think it's also the strongest research contribution. Module files describe each module on its own, but the builder also needs facts that cross modules, for example:

a water mill requires a river;
a tannery is typically downstream of houses;
"smithy", "forge" and "blacksmith" are the same concept;
an asset tagged rose can fill the flower role;
patricians prefer high ground.
A knowledge graph holds all of these as one queryable structure, and every step uses it:

Interpret: map words to concepts and synonyms.
Confirm: which components exist for this build type, and what "large" means in numbers.
Plan: which parts a part may contain, how typical each is, and what each requires or excludes.
Compile: what each relation means geometrically.
Assets: which assets can fill which role.
Verify: which rules apply to a part.
Explain: every decision can cite the facts it used.
The design is in docs/place-ontology.md:

Storage: an in-memory graph in a simple JSON format, exportable to the standard formats OWL, SKOS and SHACL, so others can reuse and cite it.
Prompts: only the relevant subgraph goes into each prompt.
Growth: reviewed AI-proposed additions, plus weights learned from verified builds.
Provenance: every fact records its source (a paper, a user, or a learned result).
It now comes first in the plan, as part of 7.1.

Prior work affects how far the novelty claim can go:

Tutenel et al. (AIIDE 2009) and SketchaWorld (Smelik et al., 2011) already used semantic object classes to guide layout solvers, and the paragraph cites them.
The claim is narrower: to our knowledge, nothing uses a shared ontology both to ground the AI's planning and to define the constraints that are compiled and checked.
Naming: I've used "GBO" (GenBuilding Ontology) as a placeholder. Other options are Topos (Greek for "place") or PlaceKG. Which do you prefer?

Recent work on language-driven scene and level generation agrees on a central point: large language models should decide what a place contains and how its parts relate, while numerical solvers decide where things go. Holodeck asks an LLM for spatial relations, which a constraint solver then satisfies [1]. SceneCraft represents a scene as a relation graph with differentiable scores and revises it from rendered images [2]. GLDL turns narrative into definition and constraint sentences that are solved as a facility-layout problem [3]. The database-driven framework moves the LLM's knowledge into reviewed databases, so assembly needs no model calls [4]. CityGenAgent splits a city into editable block and building programs [5]. In parallel, procedural modelling has developed strong algorithms for the geometric layer: villages grown from interest maps [6], parcel subdivision with street access [7], hierarchical tile synthesis [8], and mission-and-space graphs for dungeons [9]. Semantic approaches to game worlds attach classes and placement rules to objects [10, 11].

We combine these strands in a single procedure for 2D sprite-based worlds of any scale: query → interpret → confirm → plan → compile → solve → verify. Each step writes a typed, constrained document. The Brief is the confirmed request; the user confirms it in a window generated from its schema, with questions batched and placed where several sampled interpretations disagree [12, 13]. The Spec is a plan tree whose nodes carry quantity fields (counts, densities with numeric meaning, areas) and quality fields (closed vocabularies), with aggregate constraints checked across the tree. The Layout IR is a constraint language that keeps GLDL's core terms [3] and extends them with hierarchy, n-ary group relations [2], linear features, networks, subdivision, scatter and terrain.

The main contribution is an explicit place ontology: a knowledge graph of build types, modules, fields, vocabularies, asset roles, spatial relations and design rules, each fact with its source. Every step queries it. The interpreter maps words to concepts, the planner draws allowed children, typical weights, requirements and interest preferences from it, the compiler takes the meaning of relations from it, asset retrieval matches catalogued sprites (with measured footprint, anchor and perspective) to roles, and verification applies the rules the ontology attaches to each class. Earlier semantic libraries guided layout solvers [10, 11], and LLM systems used ad-hoc prompts or learned programs [1–5]. To our knowledge, no prior system uses a shared ontology both to ground the LLM's planning and to define the constraints that are compiled and checked. Because the ontology is explicit, the system can explain its decisions, and it can grow by reviewed, AI-proposed extensions and by weights learned from verified builds.

The approach adds three further elements. For scale, the LLM plans only the upper levels and ontology-driven expansion rules fill the rest, so a city needs tens of model calls rather than thousands. For verification, each level is checked (hard rules, conformity tolerances, rendered critique [2, 5]) and repaired locally before the next is built inside it. For editing after generation, every built entity keeps its link to its plan node, and an edited or regenerated subtree preserves locked work by matching children across regenerations, as in parcel persistence [7].

Two references need completing before publication: [4] and [12] have no authors, because I couldn't confirm them from what I read. The design documents are committed.

Sources:

Holodeck
SceneCraft
Constraint Is All You Need
Database-Driven 3D Level Generation
CityGenAgent
Procedural Generation of Villages
Procedural Parcel Generation
Hierarchical Semantic WFC
Dormans 2010
Using Semantics to Improve the Design of Game Worlds
A Declarative Approach to Procedural Modeling of Virtual Worlds
ClarEval
Active Task Disambiguation
Narrative-to-Scene Generation
GameTileNet
3D Scene Generation survey



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



The parameters are the layout form from yesterday, root fields any zoned place can use, with the modern city's defaults:

Field	Values	City default	What it controls
zone-shape	grown, blocks	blocks	district shapes: rectangles cut along the avenues
street-grid	per-zone, shared	shared	one street lattice for the whole place, lined up with the avenues
block-size	cells	16–20	the lattice's spacing
block-fill	frontage, full	full	lots back to back, blocks built through
coverage	partial, full	full	districts cover the whole site
arrangement	natural, ordered	ordered	cars, lamps, trees, bins, bicycles evenly spaced in lines and mirrored pairs
