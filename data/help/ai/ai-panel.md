# The AI panel

What the AI panel is, where it is, how to turn it on, what you can ask it, how it decides how big a request is, and how the conversation is kept per project.

## What the AI panel is

The AI panel is the editor's chat column: you describe a change to your game in plain words and the AI proposes the edits. It never edits the project directly. Every answer is a proposal: a list of operations (set a field, add a component, write a script, spawn an entity, write a dialogue or quest, write a record...) that you review and apply, and the editor applies the accepted ones through the same code as manual edits, as one undo step.

Almost anything you do by hand, the AI panel can do from words: change values on entities and prefabs, add or remove components (also on prefabs, every copy following), change project settings, write Lua scripts, dialogue, quests, routines, cutscenes, game UI, animation sheets and records (items, packages, recipes, difficulty levels...), edit the World Bible and the story graph, create scenes and prefabs, add input actions, move or rename files with their references, delete assets and scenes, import a file, lay out villages, dungeons and roads with the engine's generators, and generate characters and animations.

Show or hide the panel with **I** (Panels group in the shortcuts, F1). The **›** button in its header collapses it.

### Example: make the skeletons in the crypt tougher

1. Press **I** to show the AI panel if it is hidden.
2. Type in the box at the bottom:

   > Make skeletons tougher but slower

3. Press **Enter**. After "Thinking…", a proposal card comes back with a one-line summary and a list of changes, for example `◈ Skeleton (prefab) · Health.max  20 → 35` and `◈ Skeleton (prefab) · Stats.moveSpeed  3 → 2.4` (◈ marks a prefab field, so every skeleton copy follows).
4. Leave both boxes ticked and click **Apply 2**.

The card's footer turns to "Applied ✓" with an **Undo** button; every skeleton in the scene now has 35 health and walks slower. Ctrl+Z undoes it as one step, like a manual edit.

Questions: what can the AI panel do? where is the AI panel? how do I open the AI chat?

## Turning the AI on or off

The first time, the AI panel asks **Use AI in this editor?** and lists what is sent to the provider with each request: the open scene's entities, their components and values and tile counts; your prefabs, project settings and asset file names; the source of your Lua scripts; and the earlier messages of the conversation. Nothing is sent until you press **Turn on AI**; **Not now** keeps it off. Everything else in the editor works without AI.

To turn it off later: the AI panel's **⚙** (AI settings) ▸ **Turn AI off**, or File ▸ Preferences… ▸ AI ▸ **Use AI**. When off, the AI service isn't started and nothing is sent to a model; the panel shows "AI is turned off" with a **Turn on AI** button.

The header shows the AI's status: "Starting the AI service…", "AI ready (model name)", or an error such as "AI service running, but no model provider is configured (set MISTRAL_API_KEY in ai_service/.env)".

See also: ai-providers-and-keys.md.

### Example: turn the AI on for a new project, then off for a demo

1. Open the project and press **I**. The AI panel shows **Use AI in this editor?** and the list of what is sent.
2. Click **Turn on AI**. The header shows "Starting the AI service…", then "AI ready (model name)", and the example requests appear.
3. Before showing the game at a meeting with no network, click **⚙** (AI settings) in the AI panel's header, then **Turn AI off**.

The panel now reads "AI is turned off. The AI service isn't running and nothing is sent to the model." with a **Turn on AI** button to bring it back; the rest of the editor works as before. File ▸ Preferences… ▸ AI ▸ **Use AI** does the same.

Questions: how do I turn on AI? how do I turn off the AI? what data is sent to the AI provider?

## Sending a request

Type in the box at the bottom and press **Enter** (Shift+Enter for a new line) or **Send**. While the AI works the panel shows "Thinking…" and a progress strip under the header (see "The progress strip"). You can't send while the AI is busy, while the service isn't ready, or while the game is playing: requests during Play mode get "Stop Play mode before asking for changes."

When the conversation is empty, the panel offers example requests you can click to fill the box, such as "Give the hero a sword that burns enemies for 3 seconds", "Make skeletons tougher but slower", "Add two more skeletons near the courtyard entrance" and "Make the background a dark blue night sky".

**Ask AI from a field.** An Inspector field's "Ask AI" focuses the request on that field: a chip "About Skeleton.Health.max" shows above the box, and the AI is told the request is about that field. Click × on the chip to ask about the whole project instead.

**Selection.** The AI is sent what is selected. With several entities selected, every one of them is sent, so "make these faster" means all of them.

### Example: ask about one field from the Inspector

1. In the Scene panel, select the entity **Skeleton**.
2. In the Inspector, use **Ask AI** on its Health component's **max** field. A chip "About Skeleton.Health.max" appears above the AI panel's box.
3. Type:

   > Double it, but only for this one

4. Press **Enter**.

The proposal card has one change, `✎ Skeleton · Health.max  20 → 40`, on this entity only. Click **Apply 1**.

### Example: change several selected guards at once

1. In the Scene panel's tree, Ctrl+click the three entities **Gate Guard**, **Gate Guard (2)** and **Gate Guard (3)**.
2. In the AI panel type "make these faster" and press **Enter**.

All three are sent with the request, so the card lists a speed change for each of the three; **Apply 3** applies them as one undo step.

Questions: how do I send a request to the AI? why can't I send while playing? what is the "About ..." chip?

## How the AI sizes a request

For every request the AI first decides how big it is (its `scope`) and answers one of six ways:

- **small**: one reviewable set of operations. Most requests, including changing how something that already exists behaves. You get a proposal card to review.
- **task**: a new feature with parts that build on each other (a new enemy with its behaviour, its prefab and copies placed). It is carried out step by step on a working copy; you review the whole result at the end. See ai-panel-tasks.md.
- **split**: too big for one go (many unrelated features, or more than about twenty steps). No changes; it suggests 2 to 6 smaller requests. **Do them all, step by step** runs them as one task, or click one to put it in the message box.
- **tools**: it needs to read or look first (an entity's fields, a script, which assets fit, a picture of the view). The editor runs its tools and asks again with the results. A line "Looked at: find_entities, read_entity..." shows what it read. See ai-panel-tools-and-context.md.
- **route**: the request is another panel's work: building a NEW place from nothing (the Build panel) or designing a whole game from a brief (the Planner). A card "This is the Build panel's work" / "This is the Planner's work" opens that panel with the request filled in (**Open the Build panel with it**, **Open the Planner with it**).
- **ask**: the request is unclear in a way that changes what it would do. It asks one question with 2 to 5 answers to click, or you answer in your own words.

### Example: a request too big for one go

1. In the AI panel type:

   > Add a fishing minigame, a day-night cycle, a shop in the harbour and a boss fight in the sea cave

2. Press **Enter**.

The answer is a split card: a summary, then **Do them all, step by step** and, under "Or one at a time:", the smaller requests, such as "Add a fishing minigame at the pier" and "Add a shop to the harbour with a shopkeeper". Click one to put it in the message box and send it on its own, or click **Do them all, step by step** to run them as one task you review at the end.

### Example: the AI asks before guessing

1. Type "Make the door locked" with nothing selected, in a scene with several doors.
2. Press **Enter**.

An ask card comes back with one question, such as "Which door?", and buttons for the doors it found ("The tavern's front door", "The cellar door"...). Click one, or type "the one by the well" in the box; the proposal follows from your answer.

Questions: what does split mean? why did the AI ask me a question? why did it send me to the Build panel? what is a task?

## Requests the editor routes before the AI sees them

Some requests are recognised by the editor and sent straight to another panel:

- **Designing a whole game**: requests containing phrases like "plan a game", "design a game", "make a game", "make me a game", "create a game", "make an RPG", "design an RPG", "make a life-sim" start a plan in the Planner panel, and a plan card appears in the chat.
- **"Build the game"** (also "build my game", "build it all", "build the plan", "build everything") when the project has a plan: the Planner's ⑩–⑬ build starts if the plan is validated, otherwise you're told to validate it first (⑨ in the Planner panel).
- **A change to the open plan** ("make Aldric the mentor's son") goes to the Planner as a revision.
- **Building a place** ("build a large village by a river"): the Build panel starts a build and a build card appears. On the card, while it's still reading the request, **Ask as a change instead** cancels the build and sends the words to the AI panel as an ordinary change.
- **A change to the place the Build panel has open**, while it's at its layout or plan, goes to the Build panel as a revision ("That's about Riverford, so it went to the Build panel as a revision").
- For a painted place: "paint it again ...", "repaint", "pre-fill the marks" and "check the marks" are done by the editor.

Requests about a place's people and story (populate, NPCs, villagers, residents, gossip, dialogue, lore, story) always stay in the AI panel, even when the place is in a plan or being built.

### Example: a build request that was really a change

1. In the AI panel type "build a fence around the farm" and press **Enter**.
2. The editor takes it for a place to build: a card "Building ..." appears and the Build panel starts reading the request.
3. You meant a change to the farm already in the scene, so click **Ask as a change instead** on the card while it is still reading.

The build is cancelled and the same words go to the AI panel as an ordinary request; a proposal card for the fence comes back to review.

### Example: populating a village stays in the AI panel

1. With the village Riverford open in the Build panel at its layout, type in the AI panel:

   > Populate Riverford with villagers who gossip about the miller

2. Press **Enter**.

Because it is about people and gossip, it is not sent to the Build panel as a revision; the AI panel answers it (usually as a task with an outline of who lives where, see ai-panel-tasks.md).

Questions: why did my request open the Planner? why did my message go to the Build panel? how do I stop a request being treated as a build?

## Editor actions in an answer

Any answer can also carry editor actions, done as soon as it arrives with no review because nothing in the project changes: select entities, frame an entity in the view, open a scene (the open one is saved first if it has changes), open a panel (Build, Planner, Generate, Assets, Project, Scene), start Play, or open **Animate the interior** for a building with a cutaway interior. Asking to animate an interior ("make the inside come alive", "animate its fireplace") is answered this way: the window opens and lists what was found to check and animate.

### Example: find and frame the innkeeper

1. In the AI panel type:

   > Show me Vara Bell, the innkeeper

2. Press **Enter**.

There is no card to apply: the editor selects the entity Vara Bell and frames it in the view straight away. If she is in another scene, the answer opens that scene (saving the open one first if it has changes).

### Example: bring the tavern's interior to life

1. Select the tavern building, which has a cutaway interior.
2. Type "make the inside come alive" and press **Enter**.

The **Animate the interior** window opens for the tavern and lists what was found to check and animate, such as the fireplace.

Questions: can the AI select things for me? can the AI open a scene? how do I ask the AI to animate an interior?

## Conversation history per project

The conversation is kept with each project, in `.cache/ai_conversation.json`, so saving, quitting and reopening brings the chat back (up to 400 entries). Follow-ups work because the last 8 turns are sent with each request ("now make them faster").

Proposals from before a restart can't be applied any more: an undecided one shows "Not applied — from before the editor restarted; ask again to redo it". A task that was working when the editor closed shows "Stopped: The editor was closed while it was working."

**Clear the conversation**: the **⌫** button in the header asks "Forget this conversation? The AI starts fresh. Changes already applied stay." and clears it.

**Your own edits are noticed.** If you delete a record, remove a component from a prefab or delete an entity by hand between requests, the next request tells the AI what changed since, so it goes by the project as it is now, not by its earlier answers.

Every proposal and what was decided (applied, rejected, auto-applied, task finished or stopped) is also written to the project's `ai_log.jsonl`, an audit trail.

### Example: a follow-up the next day

1. Ask "Add two more skeletons near the courtyard entrance" and click **Apply 2** on the card.
2. Save and quit. The next day, reopen the project: the chat is back as you left it.
3. Type "now make them faster" and press **Enter**.

The last 8 turns are sent with the request, so "them" means the two new skeletons; the card proposes a speed change for those two only.

### Example: start fresh after deleting a record by hand

1. In Project ▸ Records ▸ Items, delete the item "Rusty Key" that the AI wrote earlier.
2. In the AI panel ask "give the cellar chest a key". The AI is told the record was deleted since, so it doesn't refer to Rusty Key.
3. To drop the whole conversation, click **⌫** in the header and confirm "Forget this conversation? The AI starts fresh. Changes already applied stay."

The chat empties and the example requests show again; everything applied earlier stays in the project.

Questions: is the AI conversation saved? how do I clear the AI chat? why can't I apply an old proposal? where is the AI log?

## The progress strip

Under the AI panel's header, a strip shows each AI call in flight: its name ("AI panel request", "Consistency check", "World Bible search"), its phase (waiting for the model, thinking, writing, finishing, done, failed), the step it's on when it has several, the tokens written so far (hover for thinking, answer and read tokens) and the time taken. A bar shows how far when the steps are counted, else it runs while the call works. Nothing shows when nothing runs. The Build panel and the Planner have the same strip for their own calls.

Each finished proposal shows its tokens used as "input / output tok" in its footer.

### Example: watch a long request work

1. Type "Write a quest where Vara Bell asks you to find her stolen ledger" and press **Enter**.
2. Under the header, the strip shows "AI panel request" moving from waiting for the model to thinking to writing, with the tokens written so far climbing and the seconds counting. Hover over the token count to see thinking, answer and read tokens.
3. When the card arrives, a second line "Consistency check" may run while the card shows "Checking against the World Bible…".

When both finish the strip empties. The card's footer shows what it cost, for example "14210 / 1830 tok".

Questions: how do I see what the AI is doing? how many tokens did a request use? is the AI still working?

See also: ai-panel-review.md, ai-panel-tasks.md, ai-panel-tools-and-context.md, ai-requests-tips.md.
