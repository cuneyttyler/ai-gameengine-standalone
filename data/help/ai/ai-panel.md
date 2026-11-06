# The AI panel

What the AI panel is, where it is, how to turn it on, what you can ask it, how it decides how big a request is, and how the conversation is kept per project.

## What the AI panel is

The AI panel is the editor's chat column: you describe a change to your game in plain words and the AI proposes the edits. It never edits the project directly. Every answer is a proposal: a list of operations (set a field, add a component, write a script, spawn an entity, write a dialogue or quest, write a record...) that you review and apply, and the editor applies the accepted ones through the same code as manual edits, as one undo step.

Almost anything you do by hand, the AI panel can do from words: change values on entities and prefabs, add or remove components (also on prefabs, every copy following), change project settings, write Lua scripts, dialogue, quests, routines, cutscenes, game UI, animation sheets and records (items, packages, recipes, difficulty levels...), edit the World Bible and the story graph, create scenes and prefabs, add input actions, move or rename files with their references, delete assets and scenes, import a file, lay out villages, dungeons and roads with the engine's generators, and generate characters and animations.

Show or hide the panel with **I** (Panels group in the shortcuts, F1). The **›** button in its header collapses it.

Questions: what can the AI panel do? where is the AI panel? how do I open the AI chat?

## Turning the AI on or off

The first time, the AI panel asks **Use AI in this editor?** and lists what is sent to the provider with each request: the open scene's entities, their components and values and tile counts; your prefabs, project settings and asset file names; the source of your Lua scripts; and the earlier messages of the conversation. Nothing is sent until you press **Turn on AI**; **Not now** keeps it off. Everything else in the editor works without AI.

To turn it off later: the AI panel's **⚙** (AI settings) ▸ **Turn AI off**, or File ▸ Preferences… ▸ AI ▸ **Use AI**. When off, the AI service isn't started and nothing is sent to a model; the panel shows "AI is turned off" with a **Turn on AI** button.

The header shows the AI's status: "Starting the AI service…", "AI ready (model name)", or an error such as "AI service running, but no model provider is configured (set MISTRAL_API_KEY in ai_service/.env)".

See also: ai-providers-and-keys.md.

Questions: how do I turn on AI? how do I turn off the AI? what data is sent to the AI provider?

## Sending a request

Type in the box at the bottom and press **Enter** (Shift+Enter for a new line) or **Send**. While the AI works the panel shows "Thinking…" and a progress strip under the header (see "The progress strip"). You can't send while the AI is busy, while the service isn't ready, or while the game is playing: requests during Play mode get "Stop Play mode before asking for changes."

When the conversation is empty, the panel offers example requests you can click to fill the box, such as "Give the hero a sword that burns enemies for 3 seconds", "Make skeletons tougher but slower", "Add two more skeletons near the courtyard entrance" and "Make the background a dark blue night sky".

**Ask AI from a field.** An Inspector field's "Ask AI" focuses the request on that field: a chip "About Skeleton.Health.max" shows above the box, and the AI is told the request is about that field. Click × on the chip to ask about the whole project instead.

**Selection.** The AI is sent what is selected. With several entities selected, every one of them is sent, so "make these faster" means all of them.

Questions: how do I send a request to the AI? why can't I send while playing? what is the "About ..." chip?

## How the AI sizes a request

For every request the AI first decides how big it is (its `scope`) and answers one of six ways:

- **small**: one reviewable set of operations. Most requests, including changing how something that already exists behaves. You get a proposal card to review.
- **task**: a new feature with parts that build on each other (a new enemy with its behaviour, its prefab and copies placed). It is carried out step by step on a working copy; you review the whole result at the end. See ai-panel-tasks.md.
- **split**: too big for one go (many unrelated features, or more than about twenty steps). No changes; it suggests 2 to 6 smaller requests. **Do them all, step by step** runs them as one task, or click one to put it in the message box.
- **tools**: it needs to read or look first (an entity's fields, a script, which assets fit, a picture of the view). The editor runs its tools and asks again with the results. A line "Looked at: find_entities, read_entity..." shows what it read. See ai-panel-tools-and-context.md.
- **route**: the request is another panel's work: building a NEW place from nothing (the Build panel) or designing a whole game from a brief (the Planner). A card "This is the Build panel's work" / "This is the Planner's work" opens that panel with the request filled in (**Open the Build panel with it**, **Open the Planner with it**).
- **ask**: the request is unclear in a way that changes what it would do. It asks one question with 2 to 5 answers to click, or you answer in your own words.

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

Questions: why did my request open the Planner? why did my message go to the Build panel? how do I stop a request being treated as a build?

## Editor actions in an answer

Any answer can also carry editor actions, done as soon as it arrives with no review because nothing in the project changes: select entities, frame an entity in the view, open a scene (the open one is saved first if it has changes), open a panel (Build, Planner, Generate, Assets, Project, Scene), start Play, or open **Animate the interior** for a building with a cutaway interior. Asking to animate an interior ("make the inside come alive", "animate its fireplace") is answered this way: the window opens and lists what was found to check and animate.

Questions: can the AI select things for me? can the AI open a scene? how do I ask the AI to animate an interior?

## Conversation history per project

The conversation is kept with each project, in `.cache/ai_conversation.json`, so saving, quitting and reopening brings the chat back (up to 400 entries). Follow-ups work because the last 8 turns are sent with each request ("now make them faster").

Proposals from before a restart can't be applied any more: an undecided one shows "Not applied — from before the editor restarted; ask again to redo it". A task that was working when the editor closed shows "Stopped: The editor was closed while it was working."

**Clear the conversation**: the **⌫** button in the header asks "Forget this conversation? The AI starts fresh. Changes already applied stay." and clears it.

**Your own edits are noticed.** If you delete a record, remove a component from a prefab or delete an entity by hand between requests, the next request tells the AI what changed since, so it goes by the project as it is now, not by its earlier answers.

Every proposal and what was decided (applied, rejected, auto-applied, task finished or stopped) is also written to the project's `ai_log.jsonl`, an audit trail.

Questions: is the AI conversation saved? how do I clear the AI chat? why can't I apply an old proposal? where is the AI log?

## The progress strip

Under the AI panel's header, a strip shows each AI call in flight: its name ("AI panel request", "Consistency check", "World Bible search"), its phase (waiting for the model, thinking, writing, finishing, done, failed), the step it's on when it has several, the tokens written so far (hover for thinking, answer and read tokens) and the time taken. A bar shows how far when the steps are counted, else it runs while the call works. Nothing shows when nothing runs. The Build panel and the Planner have the same strip for their own calls.

Each finished proposal shows its tokens used as "input / output tok" in its footer.

Questions: how do I see what the AI is doing? how many tokens did a request use? is the AI still working?

See also: ai-panel-review.md, ai-panel-tasks.md, ai-panel-tools-and-context.md, ai-requests-tips.md.
