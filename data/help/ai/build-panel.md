# The Build panel: building places with AI

The Build panel turns a description ("a large village by a river, with gardens and a market") into a finished place in the scene: it reads the request, lets you confirm what the place holds, lays out its zones, plans what goes in each, finds or makes the art, and builds it. This covers the panel, its steps, stopping, the builds list and the ontology.

## What the Build panel is and how to open it

The Build panel is a dockable panel (bottom by default) for building whole places: villages, towns, dungeons, interiors, farms, camps, castles, wildernesses, regions. It isn't modal: the viewport stays in view for the site outline, the plan and the build.

Open it:
- **U** (show or hide the Build panel);
- by asking the AI panel to build a new place ("build a large village by a river"): the editor recognises a build request (a verb like build, make, create, generate, add, place with a kind of place) and starts a build. The AI panel shows a **build card** ("Building Riverford", its stage) with **Open build panel**;
- from the Planner (⑥ Places ▸ **Build…**, or ⑩ Build).

Requests about changing things (move, delete, tougher...) or about a place's people and story (story, populate, NPCs, dialogue, gossip, lore, villagers) are not builds; they stay in the AI panel. On a build card that is still reading the request, **Ask as a change instead** cancels the build and sends your words to the AI panel as a change.

Questions: how do I build a village with AI? where is the Build panel? why did my request start a build?

## Starting a build

The Build panel's first page: "Describe a place to build. You'll confirm the details before anything is made."

1. Type the description (e.g. "a large village by a river, with gardens and a market").
2. **Build method**: Assembled or Paint (the open scene's type; choosing the other sets the scene's type too). See build-methods.md.
3. Optional **picture**: a picture of the place, used **as the painting** (Paint: skips Layout, Plan and Paint; you mark where one can walk and what one goes behind) or **as a reference for the painting** (laid out and planned as usual, painted after this picture: its look, or the picture edited as your words say).
4. Optional choices beside the words: **Place** (what kind: settlement, dungeon, interior...; "Decided by the AI" unless chosen), **Size**, **Asset variety** (Low: one look each; Normal: as the ontology says; High: about twice as many), how many buildings (from / to), **Features**, and **Parts** (click once to have it, again to leave it out, again to let the words decide).
5. Press **Start**. Then ① Confirm opens: what the place holds, and its layout to adjust.

**Builds** reopens earlier builds or starts another; **Remove older builds** deletes every build but the open one (asks first).

Questions: how do I start a build? what is asset variety? can I build from a picture? how do I reopen an earlier build?

## The steps of a build

The stepper shows the steps for the build's method:

- **Assembled**: ① Confirm · ② Layout · ③ Plan · ④ Assets · ⑤ Result
- **Paint**: ① Confirm · ② Layout · ③ Plan · ④ Paint · ⑤ Mark · ⑥ Animate · ⑦ Assets · ⑧ Result (numbered in order on screen)
- **From a picture used as the painting**: Confirm, Mark, Assets, Result.

Behind the steps:
1. **Read the request**: the AI reads it three times and the readings are merged into a **Brief** (build type, size with numbers, style, components); your choices always win. Without AI, the Brief comes from the words alone.
2. **Questions** (optional): the AI asks where readings disagree; you answer or not.
3. **Plan the top**: the AI plans the place's zones and networks with their placement in words and relations.
4. **Plan each zone**: one AI call per zone for its parcels and features.
5. **Check and repair**: the ontology finds problems; the AI or the rules repair them.
6–8. **Compile, solve, draw**: code only, seeded, so the same plan always gives the same layout.

Every AI step has a rule-based fallback. "Nothing is applied to the scene until the build step"; you can look at the other steps meanwhile.

Questions: what are the Build panel's steps? what does the Brief contain? does building need AI?

## ① Confirm and ② Layout

**① Confirm** shows what the place holds: every field of the Brief (size, components on or off and how many, their fields, style) and **To decide**: each decision with its assumed answer.

**② Layout** shows the zones solved and drawn (no parcels yet). Beside the map, each zone with what it is, where it was meant to go, where it lies, its size (cells²) and how many buildings of each kind (empty: the plan decides). You can:
- change the **Layout form** (the shape of its zones, streets, blocks and lots: a change lays it out again) and the patterns per zone (organic, grid, radial, along a road or river);
- **Paint the zones…**: a brush (1–16 cells, and Erase) paints a zone's exact cells, with ↶ Undo / ↷ Redo and **Done**;
- **Lay out the zones again** after changing what the zones depend on;
- **Revise** in words ("move the market to the crossroads; the residential ward along the river, south of the docks"). See revising-places.md.

"Short of room" warns about a zone you painted smaller than its buildings need. Changing the contents after the plan was made asks **Discard the plan?** (**Keep the plan** / **Discard it and make changes**).

Questions: how do I change the layout of a place? how do I paint zones? what does Lay out the zones again do?

## ③ Plan

**③ Plan** shows the solved layout with what each colour means, a table per part (Part, Area, Holds, Character, with each part's rationale on hover), and **Layout checks and counts** (What, Asked, Planned, Built). The sketch shows sizes only; where each part goes is worked out when it's built, and the site is outlined in the viewport.

You can change a part's fields and lay that part out again ("the rest stays as it is"), lock parts, and **Revise** inside the zones in words ("add 10 more houses to ward 1"). The **Module brush** paints a module over an area. **AI review of the map**: describe a problem ("put houses around the commons; the merchant ward needs townhouses; a bigger market"), or let the AI look at the map and fix what looks wrong. A region's plan has **Sites**: each site is built as a place of its own (**Build this site**).

Questions: how do I see the plan? can the AI fix the map? how do I add houses to a ward?

## Assets: how each piece's art is found

The Assets step (④ Assembled, later for Paint) has tabs **How each is found**, **Background**, **The assets** and **Animate**.

**How each is found**:
- the default for the build: **Search (project, library, web, then make)**, **Generate (the Art tool)**, **Library (else made by the Art tool)**, **The project (else made by the Art tool)**, or **Auto (by each piece's importance)**; with web search and **Allow share-alike art (CC-BY-SA)**;
- **made by**: who makes this build's art, and **Background painted by** (Project Settings ▸ AI by default);
- **Interiors**: for each building type, **Cutaway** (drawn cut away in place), **Scene (no interior)** (a scene of its own, entered through its door) or **None**, and its **Floors** (Auto: counted from its picture, or 1–4). Interiors are always made by BFL (an edit of the exterior picture);
- a table of needs (Needed, For, Gets, From, Why) with candidates, a source per need and **Pick…** to choose art yourself from the project or your library.

For a Planner build: "Art strategy: set in the AI Planner's Game Brief (① Concept ▸ Plan ▸ Art for the places)".

**Background**: the whole ground (roads, paths, floors, grass) painted as one picture from a description you can edit.

**The assets**: each piece made or found, with what the check saw. Per piece: **Keep** (it's right even if the check rejected it), **Refine** (from the project or your library), **Make it again** with what to change ("a thatched roof, warmer colours, smaller"), **Reject**, **Generate**, **Search the web**, **Placeholder**. **■ Stop** stops making the art; what was made is kept and what's missing is a placeholder.

**Animate**: doors of buildings and things that are used (chests, gates, levers), animated before the build.

Questions: where does the Build panel get its art? how do I remake one asset? what are cutaway interiors? how do I stop making art?

## Result: building into the scene

The Result step: **Build into the scene** puts the place into the scene, verifies it and repairs what it can, as one undo step. The vision model then looks at the built place as the game draws it ("a few minutes for a big place") and says what's wrong; **Fix what Verify found** asks the AI to fix it. The page lists what was repaired and the assets used with their licences.

- **Undo (Ctrl+Z)** takes the whole build out of the scene in one step.
- Revise and Build again: the place is replaced, nothing else in the scene is touched.
- **Finish** saves the scene with the place in it, closes the build and shows it in the viewport (reopen it from Builds).

Once built, the place is the scene's: ask the AI panel to add or change things in it ("add a well to the village") like any part of the scene.

Questions: how do I put the built place in the scene? how do I undo a build? how do I change a place after building it?

## Stopping and cancelling a build

You can stop at any time: **Stop** while it works, **Cancel**, or **■ Stop** while art is made (what was made is kept). A cancelled build shows "The build was cancelled." and can be reopened from Builds and resumed. Nothing reaches the scene until Build into the scene, and that is one undo step.

Questions: how do I stop a build? can I resume a cancelled build?

## The ontology: what can be built

**File ▸ Ontology…** opens the place ontology: "What can be built. The engine's is read-only; yours is used by every project." The Build panel and the AI work from the whole.

- **Build types** and their modules, with **Find a module**.
- **New module**: an id (lowercase, dashes), Name, Level, Description ("What it is: the AI reads this"), Classes, **Goes in** (where it can go, min, max, how typical), **Fields** (quantity or quality, type, default, values or vocabulary, **Asked** in ① Confirm) and **Slots** (what art it shows: role, needed, main).
- **Import…** adds modules and extensions from a file; **Export…** saves what you added as one file.

Questions: what is the ontology? how do I add a new kind of building? can I share my modules?

See also: build-methods.md, revising-places.md, planner.md, ai-providers-and-keys.md.
