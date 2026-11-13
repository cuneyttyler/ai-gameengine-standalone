# The Planner: designing and building a whole game

The Planner panel designs a whole game from a short description (its brief, story, cast, quests, places, systems and interface), checks the design, and then builds every place, places the characters, binds the quests, writes the game's files and playtests it.

## What the Planner is and how to open it

The Planner is a dockable panel for designing a whole game with the AI. Where the AI panel makes changes and the Build panel builds one place, the Planner works from a brief ("A short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, three towns and a dungeon, about four hours") to a designed and then built game.

Open it:
- **File ▸ New Game with the Planner…**;
- **Y** (show or hide the Planner panel);
- by asking the AI panel to design a whole new game ("plan a game about...", "make me an RPG where...", "design a game..."): the AI panel asks first, with **Open the Planner with it** (the Planner starts a plan from your words) and **Do it here in the AI panel**. Once a plan is going the AI panel shows a **plan card** (the stage, progress, the headline) with **Open planner panel**.

The Planner needs AI on for most stages; without AI the Game Brief is made from the request's words alone.

### Example: start a game from the AI panel

1. In a new, empty project, type in the AI panel:

   > Make an RPG where a young ferrywoman uncovers a smuggling ring along the river, two villages and a sea cave, about three hours

2. Press **Enter**. The editor recognises "make an RPG" and asks: a card "This reads like the Planner's work" with **Open the Planner with it** and **Do it here in the AI panel**.
3. Click **Open the Planner with it**. A plan card "Planning ..." shows the stage, its progress and what it is doing; **Open planner panel** on it (or **Y**) shows the Planner.

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

① Concept · ② Outline · ③ Story · ④ Characters · ⑤ Quests · ⑥ Places · ⑥b Cast timeline · ⑦ Systems · ⑧ Visuals · ⑨ Validate · ⑩–⑬ Build

- **① Concept**: the request is read three ways and merged into a **Game Brief** shown as a confirmation window with tabs (Game, Story, Characters, Places, Systems, Difficulty, Visuals, Plan). Fields are marked **differ** (the readings disagreed: hover for the others), **guessed** (low confidence), **assumed** (from the genre profile or scope table) or **from the story**. "To decide" lists open points. The window always waits for **Continue**.
- **② Outline**: story outline, cast list and place list drafted together; seeds the story graph and World Bible stubs.
- **③ Story**: each act's scene cards, then every scene written in order against the story graph; a critic reviews each act and may improve scenes ("improvement queued").
- **④ Characters**: biography, relationships, class, AI packages, and a **routine**: their usual day in a sentence or two, the places they keep to (home, work, the inn, the square, each with when and what they do there) and where they go once a story scene that brought them somewhere is over (home, work, a haunt). ⑥b Cast timeline and Placements use it to put each character where it makes sense when the story doesn't need them. Each character's routine shows under its line on the ④ board (**No routine** when it has none); regenerating a character in ④ writes it again.
- **⑤ Quests**: the main quest in full and side quests from characters' motivations.
- **⑥ Places**: a description per place with its story anchors, and a draft Brief for the Build panel. Its **zones** are the place's layout, as a level designer lays it out: each part of the place (a residential ward, the market square, the craftsmen's ward, a graveyard; a dungeon's levels, a building's floors) with what it's like (feel), where it lies (in words, and as relations: in the north, east of the square, along the road), its share of the place (a percent), its buildings by type and how many (and whose: the smith's forge, Nima's apothecary) and what else stands in it (a well, stalls, graves). They go into the Brief as its **layout**, and the builder lays the place out by them: a zone for each, where it says, as big as its share, its buildings as its plots.
- **⑥b Cast timeline**: each character's state before the first quest stage and what each stage changes. A state is where they are (place and spot), the AI packages they run, how they behave (friendly, hostile, follows the player, hidden, absent, dead) and what they talk about. Each state gives its reason, the start too: why the character is there at that point in the story (a scene that needs them, or their routine). What doesn't change is shown as —. It's made from the story, the characters (with their routines) and the places read together: a merchant stays in their shop unless an event takes them out, and a scene's gathering lasts only while that scene plays. Villagers who meet in the square to share rumours at the first scene go back to their routine once its stage is over, in a change whose AI packages are set at their home or work, so they walk there. The step checks this: anyone left at a scene's anchor after its stage, with nothing sending them on, is sent back to the AI to fix (unless the scene is the quest's last). It also plans the **cutscenes**: every moment the player watches (each scene that is a cutscene, and the watched moments of others: an arrival, an attack seen before the fight), with the story scene it comes from, the stage it plays at, where, how it starts: the one moment in the game that sets it off, said in full on **Life ▸ Cutscenes** ("Starts: when the player comes out of Reed Apothecary into Bellwether, while q-main stage 20 is on"). A start is one of: the stage beginning (or completing: what completes a stage is its objectives, so "after Odran is defeated" is the stage whose objective is defeating him), the player reaching a spot (one of the place's anchors), coming out of a building into the place, the player's conversation with someone ending, someone being defeated, or the player using a thing in the place. Also listed: who is in it and what each does, its beats, and what the game is like after it. When two quests hold one character at once, the higher-ranked one decides. The rank is the change's priority (story, normal, ambient), then the main quest, then the side quests in their order. Packages are all kept, stacked by rank: each runs while its quest is at its stages, and the first that holds runs. Dialogue and cutscenes are all kept too. Where they are and how they behave is the higher-ranked quest's. ⇅ marks a cell where this applies, and its detail says which quest wins. It's shown as a tab per quest (the main quest, then each side quest), a column for each of its stages and a horizontal scrollbar; a row of cutscenes on top; each cell short (▶: they're in a cutscene then), and a click shows it in full below. **Regenerate** makes it again (it goes stale when a character, place or quest changes). The build stages its moves and behaviours through the quests and checks every cell (⑩–⑬ Build ▸ **Behaviour**).
- **The pages, grouped:** ① Concept · ② Story (Outline, Acts & scenes) · ③ World (Characters, Places, Props & objects) · ④ Quests · ⑤ Life (Cast timeline, Small scenes) · ⑥ Systems (Design, Input) · ⑦ Visuals · ⑧ Validate · Build. A step with several pages shows them as subtabs. Every thing in the game has two sides: its description here, and its implementation in Build, on a tab of the same name (Life's Cast timeline is built on **Behaviour**, its Small scenes on **Scenes**). Every page has **Regenerate <page>** in its bar: the AI makes that page's documents again (every character, every quest, the cast timeline...; on Systems the features, then everything designed from them), the locked ones kept, and what was made from them later shows as stale. On Concept it reads the request again: the plan is made anew (it asks first).
- **World ▸ Props & objects** (stage ⑥c): every thing in the places someone uses — a story prop (the record board), a quest's object (the east gate), a working thing (the forge, the well), something the player can use (a notice board to read). For each: its place and where in it, its purpose, how it looks, its states (the gate broken, repaired), what characters do at it in their day (a SmartObject: its type and affordances: sit, eat, work, read, pray...), and every use: by whom (the player, named characters, the villagers), casual or for a quest (which stage), when, why, and the engine's **Reaction** that makes it so (its trigger, the input action, range, prompt, what it requires, takes and gives, the clip its user plays and its own, how long it takes, the state it becomes, once or its cooldown, its conditions and effects, the flag it sets, the event it sends). And its **animations**: every clip its Reactions play, each with its name, who plays it (the thing itself: a gate swinging open; or its user: the player reading, a villager hammering) and what it looks like. A Reaction's clip is one of the thing's own, its user clip one of its users' (one for each who plays it).
- **Life ▸ Small scenes** (stage ⑥c, after the props & objects): the little scenes that play round the player as they come by, the player keeping control — two villagers in the square trading rumours, the smith cursing the cold, the elder reading a name at the record board. Casual (the life of the place) or about a quest active then (which quests and stages). For each: where (within so many cells of an anchor or an object, or entering a room), at what time of day, how often (once, each visit, after a cooldown), who's in it (by the cast timeline), the props & objects it uses, and its beats: each one says something, walks somewhere, turns, uses a thing, plays an animation or waits; the animations its beats play (each one's name, who, what it looks like); and what they do after.
- **Build ▸ Props & objects**: each described thing made in its scene: its entity (the one there by its name, else one stood where it's said to be), a SmartObject for what characters do at it in their day, a Reaction for each use (a second use on a companion standing on it: an entity has one), each state's picture drawn as a prefab its Reactions become, its animations (the thing's own drawn from its picture, put on it under the name its Reactions play; its users' added to their art and drawn with the rest), and a character's quest use as a requirement the Behaviour tab builds (the package that sends them to use it). This tab also draws every type of item's icon and the art of doors and ways through. The animator names a character's clip from its action's words, so the build writes the name each was drawn under into the Reactions and the small scenes that play it. Make one, or Build all.
- **Build ▸ Scenes** (was Small scenes): each story scene's trigger (what starts it), each cutscene (its tracks and its trigger), and each small scene written as an ambient cutscene (cutscenes/small-<id>.json: its lines said over the speakers' heads, the player keeping control, the camera untouched) and a trigger in its place (a Reaction set off only by the player coming within its range: player-near), one for each quest stage it plays in, at its time of day, once, each visit or after its cooldown; the clips its people play added to their art (their drawn names written into it).
- **⑦ Systems**: gameplay features, each system worked out, each character's behaviour plan, and the game's records. Its **Input** tab is the game's controls, designed as a whole once the features are: every action (the engine's, the keys the features ask for, every name the game's scripts and screens use), its keys and gamepad buttons, its context (gameplay, dialogue, menu, cutscene, any), the screen it opens, and why. Two names the scripts use for the same thing are one action ("also"). It's checked: each key belongs to one action in a context, every key is a real key, nothing the game uses is left without an action. The build writes it into input.json (Replace Characters, or the build's files step); **Design the controls again** asks the AI again.
- **⑧ Visuals**: the interface designed as a UI designer designs a web app: its theme and font, its screens and HUD widgets, its one look ("style": mood, palette, materials, typography, ornaments, motion, icons), and every panel (the HUD, inventory, journal, map, character, shop, dialogue, title, pause, settings, and the game's own) each in full: its purpose, the action that opens it, its layout region by region, its components (a character figure items are dragged onto to wear them, slot grids, lists, tabs, bars, trackers...: what each shows, what one does with it, how it looks), its look, the images it needs (with prompts and sizes, nine-piece frames) and its own RCSS. **Redesign Visuals** designs it again. **Build ▸ Visuals** makes each panel: its images drawn into ui/art/, then laid out as a design for the UI designer (ui/designs/<id>.json: each element anchored and sized on a 1920 x 1080 canvas, its pictures placed, frames cut in nine pieces, the screen's data and choices bound as the engine's own document has them; a HUD keeps the engine's notifications, markers and what's over people's heads), and its document written from that design (ui/screens/<id>.rml, in the project's screens manifest). Open it in **Project ▸ UI ▸ Designer** to move or resize anything. Build or Build again one panel, or Build all.
- **Revise** (the bottom of every step, and of the Build tabs Requirements, Quests, Placements and Behaviour): a change in your words. The AI first finds which of the step's documents and which of their fields the words are about ("make Jory a fisherman" is Jory's document alone), then edits only those, keeping each document's structure (a list of stages stays a list of stage objects; a change points at the part that changes, such as one stage's journal, or adds one item to a list; an edit that would turn a field into another kind, a list into text, is refused, and each edit keeps the value it replaced); each step keeps its own history (what was asked, what it was found to be about, each edit, why, or what went wrong). **Revert** on a revision in the history takes back every edit it made, the newest first: each value put back as it was, an item it added to a list taken out (a Build tab's the tab's own way: placed again, saved); it shows as reverted. A Build tab's revision is applied its own way: placements edited are locked and everyone is placed again; requirements and implementations are saved; quests' game files are written.
- **⑨ Validate**: checks the whole design.

The stepper shows each stage's status (✓ done, … working, ? waiting, ✗ failed, Ⅱ paused), failures, a stale count and a "review" badge when it waits for you.

### Example: confirming the Game Brief

1. After **Plan**, ① Concept shows the Game Brief window. On the **Game** tab, the length field is marked **differ**: hover over it to see the other readings (say, "4 hours" and "6 hours").
2. On the **Places** tab the dungeon's name is marked **guessed**: change it to "The Hollow Vault".
3. On the **Difficulty** tab a value is marked **assumed** (from the genre profile): leave it.
4. Read "To decide" for open points, then click **Continue**.

The stepper moves on to ② Outline (… working), then ③ Story, each turning ✓ as it finishes. A stage that needs you shows ? and a "review" badge.

Questions: what are the Planner's stages? what does differ mean in the Game Brief? what does assumed mean?

## Open-world games

When the game is an open world ("a small open-world RPG with several villages and dungeons"), the outline (②) writes
a **world**: its name, landform, how far apart its places are (spacing 1-10) and the size most of them have. Every
outdoor place (a village, a port, a castle, a camp, farmland, a forest) is a **section** of it, with its size (small,
medium, big, huge) and where it is when the story says (by the sea, near another village). Sections are joined by the
world's roads, so there's no connection between two of them. Every dungeon, cave, crypt or interior stays a place of
its own, entered from a section through a **gateway**: its kind (cave mouth, dungeon stairs, crypt door, mine shaft...)
and where it is, in words ("in the hills north of the village").

⑨ Validate checks it: the sections reach each other by the world's roads, and every other place has an entrance from a
section with its gateway. ⑩ Build builds the world as one place: its Brief made from the plan (each section's kind,
size, where and its own parts from ⑥, the spacing, the gateways), the world planned, its sections planned, then
painted or assembled as one scene; then each dungeon and interior, and the gateways are linked to them.

### Example: a small open-world RPG

*"A small open-world RPG with several villages and dungeons"* gives one world (coastal, spacing 4) with three village
sections and a camp, and two dungeons: the Wolf Cave behind a cave mouth in the hills north of the first village, the
Sunken Crypt behind a crypt door by the second village's graveyard. ⑩ builds the world scene with both gateways in it,
then the two dungeons, and walking into the cave mouth takes the player into the Wolf Cave.

Questions: can the Planner make an open world? how are dungeons connected in an open world? where does the cave entrance go? why are the villages one scene?

## Working with the plan: Continue, Pause, Regenerate, Why?

The bar under the stepper shows the status, and "N AI calls · tokens written · time generating · N decisions" (hover for tokens read). Its buttons:
- **Continue**: confirm the Game Brief or approve a stage that waits for you;
- **Pause** / **Resume**;
- **Retry failed (N)**: redo the failed jobs (each failure also has its own **Retry** and **Open**);
- **Regenerate stale (N)**: redo exactly the documents whose inputs changed.
- **Restart from <step>** (for the step on view): that step and every step after it are made again, from the plan as the step before left it; what came before stays, with your edits. Restart from ① Concept reads your request again and makes the whole plan anew.

On every item (a scene, a character, a quest, a place):
- **Why?**: the decision behind it, its rationale, inputs and history;
- **Regenerate**: make it again;
- **Edit JSON** / **Save changes**: edit its document (**Back to the readable view**);
- **Lock**: never regenerated; dependents that conflict are reported instead.

② Outline reads as before; each card (the story, each character, each place, each connection, the world) has **Edit**, which opens its fields (**Done** closes them): the story's logline, synopsis and acts; each character's name, importance, roles, line and who is the player (**Add a character**, **Remove**); each place's name, kind, what it's inside, line and, in an open world, whether it's a section, its size and where it lies (**Add a place**); the connections between places, their kind, gateway and what opens them; the world's name, landform, spacing and its places' size. The story graph follows each change, and the steps after ② show as stale: **Regenerate stale**, or **Restart from ③ Story**.

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

The **Places** tab's table lists each place, its scene, build type, mode and status (building, built, needs you, failed, placeholder) with **Open build**, **Built**, **Retry**, **Rebuild** and **Placeholder** (a simple room so the rest can go on). **Stop building**, **Continue building**, **Restart builds** and **Retry failed** apply to all.

**The Build page is the implementation.** Each tab builds one thing, to the requirements, in this order. It's also the order **Continue building** goes in, from wherever the build stopped:
1. **Requirements**: made first, from the plan alone, before anything is built (each place's scene file is already known from the build plan). What the game must have: who is where and when, what each character is animated doing (from its class and whether it fights), the quests' steps, the props, the cutscenes, the systems. The AI also reads each story scene for what the rules miss. A requirement is something the build makes: a thing that must exist, be placed, be drawn, react or be said. The player is played, not built: what the story has the player do (fight, use an item, open a gate, say a line, work something out) becomes what the game offers for it, about the thing or person it's done to (an enemy that can be beaten, an item the player has, a prop that can be used and what reacts to it, a line offered in a conversation, the journal). The player is never scripted or moved. People are cast: a group of villagers the story mentions is cast members moving, never new art. The AI's answers are checked for these, and for the same requirement written twice, and are sent back to it when they break them. Each requirement gets its **implementation items**, and each item belongs to one of the tabs below. See "Requirements: Make, Check and Fix" below.
2. **Places** (⑩): the scenes, their roads and gateways, as in the table above. Painted places wait for **Approve painting** (its tiles and calls in the table), unless ① Concept ▸ Plan ▸ Building is automatic.
3. **Characters**: character art and animations only. The player, the major characters and every kind of enemy (their classes too) are drawn once (Project Settings ▸ AI ▸ Art), then animated for what the requirements ask (walk; their weapon's attack, held so the weapon is drawn in the hand; hurt and die; casting for casters). A clip already drawn is kept; only the missing ones are drawn. The table shows the calls first: **Draw them**, or **Skip** (they keep their placeholders). Set who is drawn in ① Concept ▸ Plan ▸ Art for the characters (all, the default; main: the player, the major characters and the kinds of enemy; none); **Draw the others too** draws and animates every character not drawn yet.
4. **Props & objects**: the art and animations of props and objects: each type of item gets one picture (every sword shares one; an axe, a bow, a helmet, a healing potion one each), used as the inventory's icon and drawn on whoever wields or wears it; doors and ways through get theirs (a gateway's art stands on the gateway itself, so the way there can be seen). Each described object is made in its scene with its SmartObject and Reactions (see Build ▸ Props & objects above), and the requirements' prop and asset items are built here.
5. **Quests**: the game's quest files (`quests/*.json`): stages, objectives, stage effects (a stage can send a character elsewhere, hide or show one, turn one hostile or friendly, make one follow you, spawn enemies, give an NPC an item), journal text naming where to go, and objective markers (guidance).
6. **Placements**: characters, props and objects placed into the scenes. The rules place everyone first: each character at its anchor in its home (the smith by the forge, the innkeeper at the counter), with its numbers from its class and level (⑦'s character-class records), what it carries and wears, a merchant's stock and what killing it gives; the player from its own character (class, level, abilities in its quick slots, starting kit); the enemies a place's plan put in its dungeon rooms (their class from what they show), and loot in its chests. Then the AI decides where exactly each is: it's given the requirements of where people and things are, and names which requirement each spot meets. The quests' roles (each quest's giver, places, spots, things and foes) are bound to what's placed, and the game's files are written. See "The Placements tab" below.
7. **Behaviour** (was Cast): each character's AI packages and routine (work, the inn, bed, by the hours ④ gave it), what changes at each quest stage (⑥b Cast timeline, as requirements), and dialogue: one for each character the player can talk to.
8. **Scenes** (was Small scenes): what starts each story scene (its trigger), the cutscenes and the Life page's small scenes (see Build ▸ Scenes above). Each cutscene of Life ▸ Cutscenes gets its tracks written from its beats, and its start made exactly as planned, replacing any start it had before: a stage's start goes in the quest's stage (as it begins or completes); reaching a spot is a trigger at that anchor's marker; coming out of a building is a trigger at its way out; a conversation ending, a defeat or a use is a rule in the scene that waits for that moment, about that character or thing (while the stage is on). Each cutscene shows how it starts, then what was built for it, or why it couldn't start (no marker for the spot, nothing by that name to use), instead of a guessed spot. Cutscenes come only from Life ▸ Cutscenes: the requirements don't add their own.
9. **Systems**: only the Systems page's features (⑦'s steps: their records, such as items, levels, difficulty, combat and vendors, plus their screens, inputs and scripts), built through the AI panel. A script for a story moment goes to the tab of what it's about: a fight or someone's doings to Behaviour, a thing's use to Props & objects, a moment's effect to Scenes. Each of ⑦'s steps is told where the player is (its entity in the starting scene and its prefab; it travels with the player into the other scenes). For each feature: a feature the engine can't do without code gets its Lua (checked that it compiles), put on its character, kind of enemy, thing or the whole game. The actions the features ask for get their keys, moved to a free key when another action has it.
10. **Visuals**: the panels designed in ⑧ Visuals (the HUD, inventory, dialogue, menus), their styles and images, and the interface art (the UI panel's picture in the game's style). The game's windows: the shop (prices beside each item), the quest log (active, then finished), the map (**M**: the place's painting or its plan from above, a dot where you are, what to do now, where its ways lead), the character (**P**: level, attributes, resistances, what's worn) and the controls (Esc ▸ **Controls**); the theme's font is chosen in ⑧ from the engine's (a book serif, a plain sans, an old printed face, carved capitals, two pixel faces) or the project's own (assets/fonts).
11. Then anything left over is built in a final pass, and **Validate**, **Story & Test** and **Result** follow.

Each tab shows the implementation items that are its own. There's no Implement tab: its work is split across the tabs above, and the AI panel is the tool a tab uses for what its rules can't build (scripts, dialogue, a move on an event). After each tab, the requirements that already hold are marked **verified** (a tab never marks one failed). **Make the requirements** (or **Make them again**) while the build is stopped makes only the requirements, then stops; **Continue building** goes on from where the build was.

**Stopped half-way.** If the editor closes or the AI service stops while the build is waiting on the AI, nothing is marked failed. The build stops ("The AI service stopped: start it, then Continue building goes on from where it was"), and what was half-done is made ready to go again: a drawing goes on from the clip it was on, a Visuals panel or a prop shows *paused* and keeps the images already made, a requirements reading or a validation is asked again, and a place being built reopens from its last step. Start the AI service, then click **Continue building**. The plan's own writing (the story, quests and scenes) pauses the same way; **Resume** carries on.

Enemies in dungeons, camps and the wilds come back: each room's enemies are a spawner's, the same number and kinds, another five minutes after one dies (bosses, and enemies in towns, don't).

The **Behaviour** tab also checks the cast timeline as built, characters by stages. Each cell is checked against the files and against what the scenes' tests saw a moment after each stage began:
- where they are: the quests' stage effects followed from where Placements put them;
- how they behave;
- whether they have a package of that kind, and whether it's the one running then;
- whether their dialogue talks about it.

A failed cell's **Fix** has the AI decide how to make it so, and do it: a stage effect (move them, make them hostile, have them follow the player), an AI package that starts at that stage, dialogue offered from it, or a Lua script.

At the end:
- **Validate** checks every requirement against the files (the cast in the scene, the asset drawn and placed, the objective there, the journal naming the place, the gateway shown, the cutscene written) and against what a scene's **Test** recorded (who was where, who hurt whom, what was said, the stages, the cutscenes played, who kept by the player): ✓ holds, ✗ doesn't (and why), ? a test tells. A requirement is **verified** when all hold, **failed** otherwise; **Fix** builds its items again, told what failed (**Fix the failed** for all). It also playtests (⑫): every scene loads, gateways resolve, scenes are reachable, quests and dialogues validate, each quest's targets reachable ("N/M checks");
- **⑬ Result**: the headline, asked vs made, **Gameplay set up** (each feature and where it was set up, or why it failed), and **Play from the start**.

"Build the game" in the AI panel starts this too.

### Example: build the warden game, with one place by hand

1. Open ⑩–⑬ Build. **Art for the places** shows how places will find their art; leave it.
2. Click **Build each place myself**. The Requirements tab is made first; then the Places table lists Ashford, Greywater, Thornwick and The Hollow Vault, each "needs you".
3. On Ashford click **Open build**: its Brief opens in the Build panel. Go through it there, then come back and click **Built**.
4. Greywater fails twice with art errors; click **Placeholder** so the rest can go on with a simple room.
5. Finish the others the same way. The build goes on through the tabs: Characters, Props & objects, Quests, Placements (the characters placed and the quests bound), Behaviour, Scenes, Systems, Visuals. Validate's playtest reports, for example, "41/42 checks".
6. On ⑬ Result read **Gameplay set up**, then click **Play from the start**.

Later, click **Rebuild** on Greywater to build it properly; the confirm asks "Build Greywater again from the start? Its scene is replaced."

### Example: a ghost that comes out of the graveyard

1. With the build stopped, open the build page's **Requirements** tab and click **Make the requirements**. Under s1-02 the AI adds "Grey Lament comes out of the graveyard entrance in Bellwether and attacks Ina Crook"; under **Assets**, "the graveyard gate: an iron gate in the village's north wall, chained shut" and "Grey Lament is animated rising out of the ground". The build stops again.
2. Click **Continue building**. On **Characters**, click **Draw them**: Grey Lament's rising is added to its sheet. On **Props & objects** the gate is drawn and stood on Bellwether's gateway to the graveyard. On **Scenes** the cutscene of the attack is written and its trigger placed on the way out of the apothecary while q-main stage 10 is on.
3. In **Story & Test**, click **Test** on s1-02. The requirement "Grey Lament attacks Ina Crook" shows ✓ damages (played); one that shows ✗ has **Fix**.

Questions: how do I build the whole game? what order does the build go in? what does each Build tab do? where is the Implement tab? what does a placeholder place mean? how do I playtest the planned game? what are the requirements? how do I fix a failed requirement?

## Requirements: Make, Check and Fix

The build page's **Requirements** tab is where the plan becomes a checklist. Each requirement is one sentence that must be true in the game ("Grey Lament comes out of the graveyard entrance in Bellwether and attacks Ina Crook"), with when, where, who and how to tell it's done. Under it, its **implementation items** say how it's built, by whom, and on which Build tab.

**Make the requirements** makes them in four passes:
- by rule: each scene's cast, the parts it needs, every quest objective, each stage moving on, guidance where the player must go, cutscenes, the systems, and what each character is animated doing (from its class and whether it fights);
- by the AI, scene by scene, from its events and beats: arrivals, fights, who follows whom, lines, effects;
- **visual requirements**: the separate assets the game needs and how each looks (merged, in the game's art style: "the graveyard gate, an iron gate in the north wall, chained shut"), and the **animations** each character and creature needs ("Grey Lament rising out of the grave"). **Assets** lists them;
- the **implementation items**: how each requirement is built (art, an animation, a prefab, a marker, stats, a quest effect, an objective, an AI package, guidance, a cutscene and its trigger, a script, dialogue), by whom (by rule, an AI step, the AI panel, the art) and on which tab (Places, Characters, Props & objects, Quests, Placements, Behaviour, Scenes, Systems or Visuals).

Each requirement has a kind: presence (someone is there), arrival, movement, combat, dialogue, cutscene, item, prop, gateway, effect, quest, guidance, system, scene (a scene that must be played), asset (a separate piece of art and how it looks), animation (a character's, or a thing's: a gate opening, spirits drifting), character-state (a character's state at a quest stage, from ⑥b) and whereabouts.

The buttons along the top:
- **Make the requirements** (**Make them again** once made): by rule from the scenes, quests, cast and systems, then the AI reads each scene's events and beats for the rest, then the assets are merged and the implementations planned. Made while the build is stopped, only the requirements are made; **Continue building** goes on from where the build was.
- **Plan the implementations**: plans the items again and keeps the requirements.
- **Build what's left**: builds what isn't built yet, each item on its own tab, in the tabs' order. Items that failed, were skipped, never started, or were cut off by a stop are built again; built ones stay. Their drawings that failed (no credits, the service down) are drawn again.
- **Check**: every requirement checked against the files and against what the scenes' tests recorded (Story & Test ▸ Test). Each check shows ✓ holds, ✗ doesn't (with why) or ? (a played test will tell).
- **Fix the failed (N)**, and **Fix** on each failed requirement: its items go back to be built again, told what failed. A rule item that was built but still fails goes to the AI panel as a task, with the problem.

Filters: status (todo, built, verified, failed), kind, and made by rule or read from the scenes by the AI; **implementations** shows or hides the items. **By scene** lists them by act and scene; **Assets** lists every separate asset the game needs (its name, type, size, look, states, places and scenes) and, under each, its animations and their status. Validate lists failed requirements among its findings.

**Paid art waits for you.** Assets and animations are paid calls: a bar "N assets and animations wait to be drawn" has **Draw them** and **Skip**, unless ① Concept ▸ Plan ▸ Building is automatic.

**Animations.** An animation named in plain words ("the east gate swinging open") finds its drawn asset by name. When nothing drawn is that thing yet, the build adds the asset, draws it, places it at the requirement's marker, then animates it. A thing (a gate, a grave, a glow) is animated from its own picture by the non-humanoid animator (Project Settings ▸ AI ▸ Non-humanoid animation) into an animation sheet its placed copies play: a glow loops; open, close and break play once.

### Example: build what's left after running out of credits

1. The **Props & objects** tab stops half-way: a red banner says "BFL is out of credits", and the Requirements tab reads "items: 31 of 44 built, 6 failed".
2. Top up the account (or choose another model in File ▸ Preferences…), then close the banner.
3. On the Requirements tab click **Build what's left**. The 31 built items stay; the 6 failed ones and the ones the stop cut off are built again, their drawings drawn again.
4. Click **Check**. "the east gate swinging open" now shows ✓ animation; anything still ✗ has **Fix**.

Questions: what does Build what's left do? what happened to Implement what's left? how do I redo only the failed items? why are the assets waiting? how is a gate animated? what does Fix do on a requirement? how do I make only the requirements?

## Characters and props: where everyone stands (placements)

The build goes in the order of the Build tabs. First the **Requirements**, made from the plan alone before anything is built: who is where and when, what each character is animated doing, the quests' steps, the props, the cutscenes. The tabs after them work to them: **Characters** draws and animates what they ask (a clip already drawn is kept, only the missing ones are drawn), **Props & objects** draws and makes the things, **Quests** writes the quests' files, then **Placements**: the rules place everyone first, then the AI is given the requirements of where people and things are and decides where exactly each is, naming which requirement each spot meets; everyone is placed again by it, the quests' roles are bound to what's placed, and it's kept in `planner/placements.json` (the **Placements** tab of AI Planner ▸ Build, after Quests, shows and edits it). Each tab marks the requirements it already meets verified; **Behaviour**, **Scenes**, **Systems** and **Visuals** build the rest, and Validate checks them all. **Make them again** on the Requirements tab, while the build is stopped, makes only the requirements: **Continue building** goes on from where the build stopped. What Placements decides:
- **The buildings.** Every house, shop, workshop, inn, temple and stable of the built places, with its door. A painted place's buildings are where the vision model found them in the painting; an assembled place's are where its plan put them. A building a gateway leads from into an interior scene (the smithy, the apothecary) has that **interior**. The square's well, the gate, the graves and the fields are listed too, as places to stand by.
- **Whose each one is.** Each character's **home** (a house each while there are enough) and **work** (a smith's smithy, a merchant's shop).
- **Where each stands.** A shopkeeper whose shop's interior is drawn stands inside it, at the counter; one whose isn't stands at its door. People who don't belong in a house stand where their part is: the guard at the gate, the gravedigger by the graves, a ghost among them, a boss at what it guards, the elder at the record board. Nobody is put at the place's arrival point because nothing else is known: when nothing says where, they stand by the square's centrepiece, and **Why** says so.
- **Props and quest items** the same way: the family seal by the cracked grave marker, the record board in the square, the hidden stone box where its scene happens.
- **Whereabouts.** Where each one is over the story: from the start, then from a quest stage (or an event) on, until the next. "Elder Sella is at the village record board from the start until q-main stage 30, at the graveyard's east gate from stage 30 until 40, …". A prop's whereabouts end when the player picks it up.

It is decided in **one AI call** for the whole game (the buildings, the characters with their biographies and cast timeline, the quests' stages, the story scenes, the props), checked against the real buildings, markers and quest stages; whatever it leaves out or gets wrong is decided by the rules instead (and without the AI service, the rules decide it all). When the cast timeline starts many people at one spot (seven villagers "in the square"), they're met at home or work, and gather there when the story scene that has them begins.

**When it runs.** On the Placements tab, the first time the characters are placed; again when a painted place's buildings are found in its painting (the characters are placed again at them); after **Mark again** of a Planner place in the Build panel (its buildings may have moved); and whenever you click **Replace Characters**. Replacing finds the buildings again, decides again, places every character and prop again (their markers and the story's markers with them) and makes the whereabouts requirements again. Anything you **Locked** stays as you set it.

**Each whereabouts is a requirement** (kind *whereabouts*, in the Requirements tab): "Elder Sella is at village-boundary in Bellwether from q-main stage 80 (marker village-boundary)". It's checked against the scene (the marker is there; at the start, they stand on it) and the quest (a stage effect moves them there by then, or a package of theirs goes there), and against what a scene's Test saw as the stage began (in that scene, within a few cells of the marker). It's built by rule (the stage's move_actor to the marker); a move on an event goes to the AI panel. Story scenes' markers (`story/<scene>/<who>`) are put where each one's whereabouts has them then, else where the scene happens.

### Example: move the blacksmith out of the square

1. Open AI Planner ▸ Build ▸ **Placements**. Bera Ash's row reads "work: house-9 (Ash Forge) · inside · forge-work".
2. To have her stand at the forge's door instead, click **Change…** and pick the building with **inside** unticked; the viewport frames her new spot. Her row is now **Locked**.
3. Click **Replace Characters**: everyone else is placed again, Bera stays where you put her, and her whereabouts requirement is made again.

Questions: why are all my villagers at the square? how does the Planner decide who lives where? does Replace Characters undo my changes? when are the characters placed again? what is a whereabouts requirement? where does the family seal go?

## Story & Test: Play from here and Test

The build page's **Story & Test** tab lists each act's story scenes, each with its beats and what was built for it.

- **▶ Play from here** starts the game where that scene's quest stage begins. Every quest is set to its stage then, with the effects of its earlier stages applied (who moved, what you were given), and you start where a player would be at that moment. Earlier scenes are not played again. Example: q-main stage 20 is set by talking to Nima Reed, so its scene starts beside her in Reed Apothecary, with the journal reading "Reach the shut east gate…".
- **Test** plays the scene by itself from the same start, with an autopilot doing what the player would: walking to each objective, talking, using, fighting. To reach another scene it walks there through the gateways, by the shortest route; it only jumps there when no way on foot exists. What it records (who was where, who hurt whom, what was said, the stages, the cutscenes played) feeds the requirements' played checks and the Behaviour tab's cast checks.

Without the editor, `build_scenes <project> --start <story scene>` prints where Play from here starts, and `--test <story scene>` runs the autopilot.

### Example: test the scene where the ghost attacks

1. Open ⑩–⑬ Build ▸ **Story & Test**. Find s1-02 under Act 1.
2. Click **▶ Play from here**. The Viewport shows Reed Apothecary with the player beside Nima Reed; the journal says to reach the east gate.
3. Stop Play, then click **Test** on s1-02. The autopilot walks out of the apothecary through its door and on to the gate.
4. Open the **Requirements** tab: "Grey Lament attacks Ina Crook" now shows ✓ damages (played).

Questions: where does Play from here start? does Play from here replay the earlier scenes? how does the scene test travel between scenes? why did the test teleport?

## What the build looks after for you

The build also keeps the game tidy in ways you'd otherwise fix by hand:
- **One copy of each character per scene.** A character a scene already has isn't placed again, and the AI panel's tasks are told who is already placed.
- **Merchants are met in their shops.** A character who works in their own shop is met there; they aren't walked out into the street for a scene.
- **Ways back out.** An interior gets its gateway back out and its arrival marker at its entry door; the vision model looks at the painting for a door one can walk through. A dungeon's way out is at its entrance.
- **Gateways' art** is named plainly ("way-to-haunted-graveyard"), so it's found again later.
- **A vision check that finds a fault doesn't redraw.** The picture is kept and the finding is shown; nothing is paid for again.
- **Painted places wait for approval.** Before painting, the Planner shows how many calls the painting takes and waits for **Approve painting** (unless ① Concept ▸ Plan ▸ Building is automatic).
- **The build method follows the scene.** A place built into a scene that has a type (Assembled or Paint) is built that way, whatever the plan's method.

Questions: why is the shopkeeper still in her shop? why are there no duplicate NPCs? how does the player get out of an interior? why did the Planner stop before painting?

## How the Planner hands work to other panels

- **To the Build panel**: each place's draft Brief opens in the Build panel's confirmation window (⑥ Places ▸ **Build…**, or each row in ⑩). Automatic builds are normal builds you can open and watch; their steps are recorded as the Planner's decisions.
- **To the AI panel**: on each Build tab, the items its rules can't build (scripts on Systems, dialogue on Behaviour, what Fix sends again, a Behaviour cell's fix) go to its machinery and what it proposes is applied without review (the plan was the review), on the place's scene (saved after) or the whole project. Each request names the requirement, how it's built, when, where, who, its checks and what failed before. What was unsaved in the open scene is kept beside it as `.before-gameplay`. A step that fails is reported and the game goes on.
- **From the AI panel**: once a game exists, a change to it (its quests, cutscenes, characters, where they stand, dialogue, gateways, a place's props) is the AI panel's own: it opens the scene the request is about and does it. The Planner designs new games; its own **Regenerate** and Validate's fixes change the plan. Changes made in the scene by the AI panel can be taken into the plan's documents from the Planner panel.

The Planner's documents live in the project's `planner/` folder (brief, outline, story, characters, quests, places, systems, decisions and history); the story graph is `planner/kg.json`.

See also: build-panel.md, story-graph.md, world-bible.md, ai-panel.md.

### Example: a change to the built game from the AI panel

1. With Bellwether built and Reed Apothecary open in the viewport, type in the AI panel:

   > Put a chained iron gate at the north road of Bellwether

2. The request names Bellwether, a scene the project has, so it isn't routed anywhere: the AI panel opens Bellwether (saving the apothecary first) and places the gate there.
3. Review the card and click **Apply**.

Only designing a whole new game asks about the Planner.

## The Placements tab

**Placements**, a tab of the AI Planner's build page after **Quests** and before **Behaviour**, shows where every NPC and every story prop or quest item lives, and where each one is over the story (character placements and movements). Decide and adjust placements here: each whereabouts span becomes a requirement (kind `whereabouts`) that the build makes true, and the tab's count shows how many are verified (or how many failed).

- **Replace Characters** places them all again (homes, workplaces, spots and whereabouts); its progress shows beside the button. Anything **Locked** is kept as it is.
- **Filter by name**, a **scene** list (only what is in that scene at any time of the story), and **Characters / Props / Buildings / Other** pick what the table shows.
- **Characters**: name, scene, **Home** and **Work** (pick any building in the character's scene: its kind, id and owner are shown), the spot it is at now (a building, inside or at its door, a marker, or x, y), **Why** it was put there (hover for all of it), and **Locked**.
- **Props**: name, prefab or item, scene, building, spot, why and locked.
- **Buildings**: id, kind, scene, **box** (the cells it covers, x0, y0 – x1, y1; a painted building's is the ground under it as the painting shows it), **door** (the cell in front of its front door: where its people stand when they are "at the door"; a building with an interior: where its way in is), its interior scene (or "not drawn"), owner and use (home or work).
- **By stage**: each quest's stages on the left (from ⑥b Cast timeline, with their journal line), Start first. Pick one: every character, where they are while it plays (click it to see it in the viewport, painted), since when and why, and where they go once it completes (or "stays"). A span tied to another quest's stages isn't shown against this quest's.
- **Other**: what isn't a building but stories happen at: the land areas of the plan (squares and plazas, graveyards, fields, farmland, pastures and orchards), the story's anchors (the markers its scenes and quests use, such as village-square), wells, market stalls, graves, crop beds and gates, each with its box, the cell one stands at, and its marker. On a freely painted place, the square is where the painting has its stalls and wells (marked "painted"), not where the plan had it, and Replace Characters moves the square's story anchors there.
- **Paths**: each built outdoor place's roads as paths characters' AI packages can walk (`road-<id>`, end `back`; a freely painted place: the roads its painting keeps, its main roads and streets), with the cells where each turns. Where a road leaves the scene, a marker `to-the-west`, `to-the-east`, `to-the-north` or `to-the-south` on it (x 0 is west, y 0 north, as the plan names its entrances). Each gateway between two outdoor places (the village and the graveyard) is put by the AI on the way out it belongs at, from the story and the places' names and lines (the graveyard's east gate is on the village's to-the-west); one arrives two cells in along the road, it's kept in planner/gateways.json, and the story anchors that stood by it (the failing boundary) move with it, so whoever stands at them (the guard) does too. Doors into buildings stay where they are.
- **Props** are prefabs: a story prop made from ⑥ Assets stands in the scene Placements put it in, at its spot, as an instance of `prefabs/props/<name>.prefab` (its picture from its asset; one stood from it if there was none, a placeholder's picture replaced). Copies of it (the same name, "Record Table" being record-table) are taken out, unless they have a script or an action.
- **Facing**: each prop faces what Placements says ("faces": a building, a marker, a character, or n, e, s, w: the record board the square, the table its keeper's door). Its picture's front looks to the lower left; mirrored, to the lower right: whichever is nearer the way to it. The Props table shows what it faces and how it was turned.
- Click **▸** on a row for its **whereabouts**: from (the start, a quest stage or an event) until (a later one, or the end), where they stand (inside or at the door of a building, at a marker such as the record board, or near something, and the cell; then, dimmed, the marker made for them there, such as home-elder-sella, which is put at that spot), why, and the status of the work it needs (todo, built, failed, verified).
- Click a spot, a box, a door or a whereabouts row to frame it in the viewport, painted: red on the cell they stand on, the building's box faintly red, its door orange. A click in the viewport clears the paint.
- Everyone stands on a cell one can walk on and walk from: not a blocked cell, not inside a building's or a thing's box (a painted place's market stalls, wells and gates are left open by its walk marks, so they're kept clear here too), not on a gateway, and joined to where the player arrives (never in a pocket nobody can reach). No two characters share a cell, and nobody stands on a prop's own marker.
- On a freely painted place nobody stands behind a building: the cells its picture covers behind its ground are left out (a character there would be hidden by the house). The plan's land the painter built over (crop beds under a painted house) isn't offered as somewhere to be. A story anchor set at someone's house (record-table "at lysa-house") is put at the door of the house that is that character's home in the painting, its marker and the prop on it with it.
- Each character gets a **start** marker (start-…: where they stand when the story starts, which can be the square), a **home** marker (home-…: their home's door, or inside it when they're met inside) and a **work** marker (work-…: their workplace's door, or inside it). Their AI packages run there: sleep at home, work at their workplace, and a package the cast timeline gives at a named spot (village-square, the record board) at that marker. Build ▸ **Behaviour** shows each package with where it runs ("work — at village-record-board", "sleep — home: the door of the house (bellwether/house-1)"); ⑥b shows only the place, as it's planned before anything is built.
- The spots come from the cast timeline (Life ▸ Cast timeline, ⑥b) and the story: where it starts someone, and each move it makes, are their whereabouts.
- The count on the tab is how many whereabouts are verified. Right after placing, the ones that already hold (where someone starts and isn't moved after) are verified; the rest by the tabs after it, and Validate checks them all.
- **Change…** sets the spot; **Edit…** on the whereabouts lets you change any row, add (**+ Add**), reorder (↑ ↓) and remove (✕) rows, then **Save**. Every change locks that character or prop, so Replace Characters won't undo it; untick **Locked** to let it be placed again.

Questions: how does the Planner use the Build panel? can I edit the plan from the AI panel? where are the Planner's files?
