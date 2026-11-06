# What the AI panel sees and the tools it uses

What the AI panel sends to the model with every request (the context), what it is told about the engine, and the read-only tools it can use to look things up, look at the scene and playtest it.

## What the AI sees with every request

Each request sends the model a summary of the open project as it is now:
- the open scene's entities (up to 200), each with its id, name, prefab and every visible component's values, and its script properties; tiles only as counts per prefab and their bounds;
- your prefabs and their components; the asset list (with each analysed asset's description, kind, tags, direction, size in cells, whether it blocks, and ground tiles' edge terrains);
- script paths and their sources (each up to 6,000 characters, 24,000 in all);
- project settings;
- the World Bible: every entry's key, name and summary (the full text of the entries most related to the request, and every style entry, are added by the AI service);
- dialogue and quests in full while they fit (16,000 characters), records in full while they fit (16,000), then by id only;
- the scene's markers and the schedules; cutscenes; every scene with its markers and the start scene; the World Bible place each scene shows;
- the selection (every selected entity when several are), and the Inspector field you asked about;
- difficulty levels, game UI files and themes, animation sheets and their clips, particles and effects, abilities and magic effects, physics layers and triggers, the story graph and its issues, the plan (when the project has one), shipping (achievements, accessibility, version);
- what the Build panel has open (its place, step, and for a painted place its marks);
- the last 8 turns of the conversation.

What doesn't fit, it reads with tools.

Questions: what does the AI know about my project? does the AI see my scripts? how many entities does the AI see?

## What the AI is told about the engine

The model is also given the engine's full schema, from the editor at start-up: every component and field with its type, range and options, every project setting, every command with its parameters, and every record type. It may only use what the schema lists and ids and paths that are in the context.

Its instructions also explain the engine's systems so it uses them rather than writing Lua: dialogue and quest formats, routines, cutscenes, gameplay systems (items, inventories, Interactable, Reactions, recipes and crafting, animated parts and doors, interiors), AI packages and paths, difficulty, game UI and designs, animation and effects, magic, physics, the story graph, the Planner, records, scenes and gateways, art and roads, and the complete Lua API. It is told to prefer what exists over new code, to make the smallest change that does what was asked, and, when something can't be done, to make no changes and explain why in its notes.

Questions: how does the AI know which components exist? why didn't the AI write a script?

## The tools: looking before answering

When the context doesn't show what it needs, the AI asks for tools instead of guessing. The editor runs them (they only read; nothing changes) and asks again with the results, up to 8 rounds. The chat shows "Looked at: ..." with the tools used.

- **find_entities**: entities by name, prefab, component, or near a spot (with a radius): ids, names, prefabs, positions.
- **read_entity**: one entity's every component and field, and its script properties.
- **find_assets**: art by what it shows (its analysis), name or folder. **find_prefabs**: prefabs.
- **read_file**: a record, script, dialogue, quest, cutscene, schedule, UI file, World Bible entry or input.json.
- **list_scenes**: the scenes, their types, the start scene.
- **read_settings**: project settings. **read_input**: the input actions and their keys.
- **search_world**: World Bible entries about something.
- **what_is_at**: what stands at a spot (world cells), solid or not.
- **buildings**: the buildings one can go into: footprint, kind (cutaway or scene), up to 8 cells inside to stand someone (inside the walls, off furniture and stairs, not where someone already is), the smart objects there, how many people are in it; a scene interior's own scene.
- **check**: the project's problems: gateways, records, animation sheets, and script errors from a 2-second headless run.
- **look**: a picture of the scene (around an entity, an area, or the whole of it up to 96 cells), drawn by the CPU renderer and attached to the next message so the model sees it.
- **playtest**: the open scene played headless for 1 to 30 seconds (5 by default), pressing the input actions asked for in turn (`move_x+` walks right): its events, script errors, where the player went, its pockets and health.

Questions: what tools does the AI have? can the AI see the scene? can the AI playtest my game? what does "Looked at" mean?

## What the AI can and can't do

It can:
- edit entities, prefabs (fields and components, every copy following), settings and script properties;
- write Lua scripts, dialogue, quests, routines, cutscenes, game UI (themes, designs, screens, notifications), animation sheets and any record type, and delete records;
- run the engine's commands: spawn and delete entities, turn and mirror them, create prefabs (from scratch, from an entity, or a character from its sheet), paint terrain and roads, generate dungeons (`level.generate`) and villages (`level.village`), add markers and paths, create scenes with a type, set a scene's type, add and remove input actions, move or rename files with every reference following, delete assets and scenes, import a file from your computer, write and delete World Bible entries;
- change the story graph;
- have characters, animations and art made first.

It can't:
- change the project without your review (except small tweaks if you chose to apply them directly);
- use components, fields, commands, entity ids or paths that don't exist;
- change something it spawned in the same answer (it has no id yet: that is a later task step);
- animate doors or parts itself (Build ▸ Animate or right-click ▸ Animate a part…);
- do the marking of a painted place for you: it pre-fills and checks marks, but never says they're done;
- run while the game is playing.

If a village or road command fails, it doesn't build one by hand; it finishes and quotes the error.

Questions: what can't the AI do? can the AI delete files? can the AI create a new scene? can the AI animate doors?

See also: ai-panel.md, ai-panel-review.md, world-bible.md.
