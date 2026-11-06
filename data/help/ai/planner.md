# The Planner: designing and building a whole game

The Planner panel designs a whole game from a short description (its brief, story, cast, quests, places, systems and interface), checks the design, and then builds every place, places the characters, binds the quests, writes the game's files and playtests it.

## What the Planner is and how to open it

The Planner is a dockable panel for designing a whole game with the AI. Where the AI panel makes changes and the Build panel builds one place, the Planner works from a brief ("A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours") to a designed and then built game.

Open it:
- **File ▸ New Game with the Planner…**;
- **Y** (show or hide the Planner panel);
- by asking the AI panel to design a game ("plan a game about...", "make me an RPG where...", "design a game..."): the request starts a plan and the AI panel shows a **plan card** (the stage, progress, the headline) with **Open planner panel**.

The Planner needs AI on for most stages; without AI the Game Brief is made from the request's words alone.

Questions: how do I design a whole game with AI? where is the Planner? how do I open the Planner?

## Starting a plan

The Planner's home page, **Plan a game**:
1. **Build method**: Assembled or Paint, for every place the plan builds (the project's method by default, or "this plan only"). See build-methods.md.
2. Describe the game in the box.
3. Optional main choices (each "Decided by the AI" unless you pick one) and **Other choices** from the story ontology: chips you click to fix a value; **Clear the choices** resets them. What you choose is kept as you chose it.
4. Press **Plan**.

**Your plans** lists the current plan and earlier ones, each with its stage and when it changed: **Open** or **Remove** (deletes the plan's documents and history, asks first; what it already built into scenes stays). Starting a new plan when the project has one asks whether to **Replace** it or **Keep it**. The **⌂** button in the stepper returns to the home page; **Back to the plan** returns.

Questions: how do I start a plan? can I have several plans? how do I delete a plan?

## The stages

The Planner works through stages shown as a stepper; each is a view you can open again later:

① Concept · ② Outline · ③ Story · ④ Characters · ⑤ Quests · ⑥ Places · ⑦ Systems · ⑧ Visuals · ⑨ Validate · ⑩–⑬ Build

- **① Concept**: the request is read three ways and merged into a **Game Brief** shown as a confirmation window with tabs (Game, Story, Characters, Places, Systems, Difficulty, Visuals, Plan). Fields are marked **differ** (the readings disagreed: hover for the others), **guessed** (low confidence), **assumed** (from the genre profile or scope table) or **from the story**. "To decide" lists open points. The window always waits for **Continue**.
- **② Outline**: story outline, cast list and place list drafted together; seeds the story graph and World Bible stubs.
- **③ Story**: each act's scene cards, then every scene written in order against the story graph; a critic reviews each act and may improve scenes ("improvement queued").
- **④ Characters**: biography, relationships, class, AI packages; the World Bible body.
- **⑤ Quests**: the main quest in full and side quests from characters' motivations.
- **⑥ Places**: a description per place with its story anchors, and a draft Brief for the Build panel.
- **⑦ Systems**: gameplay features, each system worked out, each character's behaviour plan, and the game's records.
- **⑧ Visuals**: theme, screens, HUD.
- **⑨ Validate**: checks the whole design.

The stepper shows each stage's status (✓ done, … working, ? waiting, ✗ failed, Ⅱ paused), failures, a stale count and a "review" badge when it waits for you.

Questions: what are the Planner's stages? what does differ mean in the Game Brief? what does assumed mean?

## Working with the plan: Continue, Pause, Regenerate, Why?

The bar under the stepper shows the status, and "N AI calls · tokens written · time generating · N decisions" (hover for tokens read). Its buttons:
- **Continue**: confirm the Game Brief or approve a stage that waits for you;
- **Pause** / **Resume**;
- **Retry failed (N)**: redo the failed jobs (each failure also has its own **Retry** and **Open**);
- **Regenerate stale (N)**: redo exactly the documents whose inputs changed.

On every item (a scene, a character, a quest, a place):
- **Why?**: the decision behind it, its rationale, inputs and history;
- **Regenerate**: make it again;
- **Edit JSON** / **Save changes**: edit its document (**Back to the readable view**);
- **Lock**: never regenerated; dependents that conflict are reported instead.

A change marks what depends on it stale. Editing a scene's card rewrites that scene. Plans are saved and resume after closing the editor.

Questions: how do I regenerate part of the plan? what does stale mean? how do I see why the AI decided something? can I pause the Planner?

## ⑨ Validate: errors, warnings, Fix and Accept

⑨ Validate checks everything together: the story rules over the story graph, every document's schema, cross-document checks (every character in a scene has a document; every place a description; quest anchors exist in their places; reward items have records; systems in use are on; needed windows exist) and an AI critic.

It shows "N errors, N warnings, N accepted". Errors stop the build until they're fixed or accepted; warnings don't. The Planner fixes what has a known remedy on its own once, then checks again.
- **Fix** with a choice of **Errors**, **Warnings** or **Both** and **Fix now**; **Stop fix** stops every fix (what isn't fixed goes back to open).
- **Accept…** keeps a finding as it is, asking for your reason.

With no open errors the design is saved as the **baseline**, and ⑩–⑬ Build becomes available.

Questions: why can't I build the game? how do I fix validation errors? what is the baseline?

## ⑩–⑬ Build: building the game

Available once ⑨ Validate passes (to build one place before that, open it in ⑥ Places and choose **Build…**, which opens its draft Brief in the Build panel).

**Art for the places** shows how places find their art (set in ① Concept ▸ Plan ▸ Art for the places; **change it**). Then:
- **Build automatically**: each place is built in order (parents before the places inside them), its steps driven by the Planner;
- **Build each place myself**: each place's build waits for you: **Open build**, go through the Build panel, then **Built**.

The table lists each place, its scene, build type, mode and status (building, built, needs you, failed, placeholder) with **Open build**, **Built**, **Retry**, **Rebuild** and **Placeholder** (a simple room so the rest can go on). **Stop building**, **Continue building** (from the next place not built), **Restart builds** and **Retry failed** apply to all. Then:
- **⑪ Bindings**: characters placed at home, quest aliases bound to characters, places and markers;
- **⑫ Playtest**: every scene loads, gateways resolve, scenes are reachable, quests and dialogues validate, each quest's targets reachable ("N/M checks");
- **⑬ Result**: the headline, asked vs made, **Gameplay set up** (each feature and where it was set up, or why it failed), and **Play from the start**.

"Build the game" in the AI panel starts this too.

Questions: how do I build the whole game? what does a placeholder place mean? how do I playtest the planned game?

## How the Planner hands work to other panels

- **To the Build panel**: each place's draft Brief opens in the Build panel's confirmation window (⑥ Places ▸ **Build…**, or each row in ⑩). Automatic builds are normal builds you can open and watch; their steps are recorded as the Planner's decisions.
- **To the AI panel**: in ⑫, each gameplay feature's steps go to the AI panel's machinery and what it proposes is applied without review (the plan was the review), on the place's scene (saved after) or the whole project. What was unsaved in the open scene is kept beside it as `.before-gameplay`. A step that fails is reported on the result page and the game goes on.
- **From the AI panel**: with a plan open, a request naming a character, place or quest of the plan ("make Aldric the mentor's son") becomes a revision of the plan's documents. Requests to populate a place with people stay in the AI panel. Changes made in the scene by the AI panel can be taken into the plan's documents from the Planner panel.

The Planner's documents live in the project's `planner/` folder (brief, outline, story, characters, quests, places, systems, decisions and history); the story graph is `planner/kg.json`.

See also: build-panel.md, story-graph.md, world-bible.md, ai-panel.md.

Questions: how does the Planner use the Build panel? can I edit the plan from the AI panel? where are the Planner's files?
