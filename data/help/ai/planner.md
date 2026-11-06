# The Planner: designing and building a whole game

The Planner panel designs a whole game from a short description (its brief, story, cast, quests, places, systems and interface), checks the design, and then builds every place, places the characters, binds the quests, writes the game's files and playtests it.

## What the Planner is and how to open it

The Planner is a dockable panel for designing a whole game with the AI. Where the AI panel makes changes and the Build panel builds one place, the Planner works from a brief ("A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours") to a designed and then built game.

Open it:
- **File ▸ New Game with the Planner…**;
- **Y** (show or hide the Planner panel);
- by asking the AI panel to design a game ("plan a game about...", "make me an RPG where...", "design a game..."): the request starts a plan and the AI panel shows a **plan card** (the stage, progress, the headline) with **Open planner panel**.

The Planner needs AI on for most stages; without AI the Game Brief is made from the request's words alone.

### Example: start a game from the AI panel

1. In a new, empty project, type in the AI panel:

   > Make an RPG where a young ferrywoman uncovers a smuggling ring along the river, two villages and a sea cave, about three hours

2. Press **Enter**. The editor recognises "make an RPG" and starts a plan instead of a proposal: a plan card "Planning ..." shows the stage, its progress and what it is doing.
3. Click **Open planner panel** on the card (or press **Y**).

The Planner opens at ① Concept, reading your request; soon the Game Brief window waits for you.

Questions: how do I design a whole game with AI? where is the Planner? how do I open the Planner?

## Starting a plan

The Planner's home page, **Plan a game**:
1. **Build method**: Assembled or Paint, for every place the plan builds (the project's method by default, or "this plan only"). See build-methods.md.
2. Describe the game in the box.
3. Optional main choices (each "Decided by the AI" unless you pick one) and **Other choices** from the story ontology: chips you click to fix a value; **Clear the choices** resets them. What you choose is kept as you chose it.
4. Press **Plan**.

**Your plans** lists the current plan and earlier ones, each with its stage and when it changed: **Open** or **Remove** (deletes the plan's documents and history, asks first; what it already built into scenes stays). Starting a new plan when the project has one asks whether to **Replace** it or **Keep it**. The **⌂** button in the stepper returns to the home page; **Back to the plan** returns.

### Example: plan a short dark-fantasy RPG

1. Choose **File ▸ New Game with the Planner…**. The **Plan a game** page opens.
2. Leave **Build method** on Assembled ("the project's").
3. In the box type:

   > A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours

4. Leave the main choices on "Decided by the AI". Click **▸ Other choices** and click one chip to fix a value you care about; the line now reads "1 chosen: kept as you chose them".
5. Click **Plan**.

The stepper appears with ① Concept working. Under **Your plans** on the home page (**⌂**), this plan is listed as "current" with its stage.

### Example: a second idea without losing the first

1. With the warden plan open, click **⌂**, type a new description ("a cosy farming sim on a windy island") and click **Plan**.
2. Asked whether to **Replace** the current plan or **Keep it**, choose **Keep it**.

Both are under **Your plans**: the warden plan as "earlier", the island as "current". **Open** switches back; **Remove** deletes a plan's documents after asking (scenes it built stay).

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

### Example: confirming the Game Brief

1. After **Plan**, ① Concept shows the Game Brief window. On the **Game** tab, the length field is marked **differ**: hover over it to see the other readings (say, "4 hours" and "6 hours").
2. On the **Places** tab the dungeon's name is marked **guessed**: change it to "The Hollow Vault".
3. On the **Difficulty** tab a value is marked **assumed** (from the genre profile): leave it.
4. Read "To decide" for open points, then click **Continue**.

The stepper moves on to ② Outline (… working), then ③ Story, each turning ✓ as it finishes. A stage that needs you shows ? and a "review" badge.

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

### Example: change a character and redo what depends on her

1. In ④ Characters, open Maren, the warden's sister. Click **Why?** to see why she was made a priest, with the inputs and history behind it.
2. You want her to be a smuggler instead: click **Edit JSON**, change her class and biography, and click **Save changes**.
3. The stepper shows a stale count on ③ Story and ⑤ Quests: scenes and quests that used Maren now depend on old facts.
4. Click **Regenerate stale (N)** in the bar under the stepper.

Only the documents whose inputs changed are made again. To keep one scene exactly as it is, click **Lock** on it first: it is never regenerated, and a conflict with it is reported instead.

Questions: how do I regenerate part of the plan? what does stale mean? how do I see why the AI decided something? can I pause the Planner?

## ⑨ Validate: errors, warnings, Fix and Accept

⑨ Validate checks everything together: the story rules over the story graph, every document's schema, cross-document checks (every character in a scene has a document; every place a description; quest anchors exist in their places; reward items have records; systems in use are on; needed windows exist) and an AI critic.

It shows "N errors, N warnings, N accepted". Errors stop the build until they're fixed or accepted; warnings don't. The Planner fixes what has a known remedy on its own once, then checks again.
- **Fix** with a choice of **Errors**, **Warnings** or **Both** and **Fix now**; **Stop fix** stops every fix (what isn't fixed goes back to open).
- **Accept…** keeps a finding as it is, asking for your reason.

With no open errors the design is saved as the **baseline**, and ⑩–⑬ Build becomes available.

### Example: clearing the last errors before building

1. ⑨ Validate shows "2 errors, 5 warnings, 0 accepted". One error says a reward item of the quest "The Stolen Amulet" has no record; another that a character in a story scene has no document.
2. Choose **Fix** ▸ **Errors** and click **Fix now**. The Planner writes the missing item record and the character's document, then checks again.
3. One warning says a side quest is long for a four-hour game. You're fine with that: click **Accept…** and give the reason "the side quest is optional".

The page reads "0 errors, 4 warnings, 1 accepted", the design is saved as the baseline, and ⑩–⑬ Build can be opened.

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

### Example: build the warden game, with one place by hand

1. Open ⑩–⑬ Build. **Art for the places** shows how places will find their art; leave it.
2. Click **Build each place myself**. The table lists Ashford, Greywater, Thornwick and The Hollow Vault, each "needs you".
3. On Ashford click **Open build**: its Brief opens in the Build panel. Go through it there, then come back and click **Built**.
4. Greywater fails twice with art errors; click **Placeholder** so the rest can go on with a simple room.
5. Finish the others the same way. ⑪ Bindings places the characters and binds the quests; ⑫ Playtest reports, for example, "41/42 checks".
6. On ⑬ Result read **Gameplay set up**, then click **Play from the start**.

Later, click **Rebuild** on Greywater to build it properly; the confirm asks "Build Greywater again from the start? Its scene is replaced."

Questions: how do I build the whole game? what does a placeholder place mean? how do I playtest the planned game?

## How the Planner hands work to other panels

- **To the Build panel**: each place's draft Brief opens in the Build panel's confirmation window (⑥ Places ▸ **Build…**, or each row in ⑩). Automatic builds are normal builds you can open and watch; their steps are recorded as the Planner's decisions.
- **To the AI panel**: in ⑫, each gameplay feature's steps go to the AI panel's machinery and what it proposes is applied without review (the plan was the review), on the place's scene (saved after) or the whole project. What was unsaved in the open scene is kept beside it as `.before-gameplay`. A step that fails is reported on the result page and the game goes on.
- **From the AI panel**: with a plan open, a request naming a character, place or quest of the plan ("make Aldric the mentor's son") becomes a revision of the plan's documents. Requests to populate a place with people stay in the AI panel. Changes made in the scene by the AI panel can be taken into the plan's documents from the Planner panel.

The Planner's documents live in the project's `planner/` folder (brief, outline, story, characters, quests, places, systems, decisions and history); the story graph is `planner/kg.json`.

See also: build-panel.md, story-graph.md, world-bible.md, ai-panel.md.

### Example: revise the plan in words from the AI panel

1. With the warden plan open in the Planner, type in the AI panel:

   > Make Aldric the mentor's son

2. Because Aldric is a character of the plan, the request goes to the Planner as a revision of its documents instead of a proposal card.
3. In the Planner, the stages that used Aldric show stale counts; click **Regenerate stale (N)**.

By contrast, "populate Ashford with villagers" stays in the AI panel as an ordinary task, even though Ashford is a place of the plan.

Questions: how does the Planner use the Build panel? can I edit the plan from the AI panel? where are the Planner's files?
