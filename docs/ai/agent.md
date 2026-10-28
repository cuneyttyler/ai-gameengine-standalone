# The AI panel as an agent

Almost anything a user does by hand, the AI panel does from words: it reads what it needs, plans a big request as
steps, carries them out on a working copy, looks at the result, and hands work that belongs to another panel to that
panel. Tests: [../testing/end-user-tests.md](../testing/end-user-tests.md) §20.

## The answer

One JSON object a turn (`ai_service/app/prompt.py` OUTPUT_FORMAT, `models.py` Proposal). Its `scope`:

| Scope | What happens |
|---|---|
| `small` | its operations, reviewed and applied (most requests) |
| `task` | step by step on a working copy (`TaskRunner`, up to 20 steps), each applied, played, its results sent back; the whole reviewed at the end |
| `split` | too big: smaller requests; **Do them all, step by step** runs them as one task |
| `tools` | it reads or looks first: the editor runs its `tools` and asks again with the results (up to 8 rounds) |
| `route` | another panel's work: a card opens the Build panel or the Planner with the request filled in |
| `ask` | unclear: one question with 2–5 answers to click (or the user answers in words) |

Any answer can also carry:

- `editor`: select, frame, open a scene or a panel, play (done when it arrives: nothing in the project changes);
- `art`: sprites to draw first (`art:<name>` in its operations becomes the path);
- `generate`: characters and animations to make first (`character:<name>`, `animation:<name>`).

## Tools

Run by the editor (`engine/ai_bridge/AiTools.cpp`, `EditorApp::RunAiTools`), reading only:

| Tool | Gives |
|---|---|
| `find_entities` | by name, prefab, component, near a spot: ids, names, prefabs, positions |
| `read_entity` | one entity's every component and field, its script properties |
| `find_assets` / `find_prefabs` | art by what it shows (its analysis), name, folder; prefabs |
| `read_file` | a record, script, dialogue, quest, cutscene, schedule, UI file, World Bible entry, input.json |
| `list_scenes` | the scenes, their types, the start scene |
| `read_settings` / `read_input` | project settings; input actions and keys |
| `search_world` | World Bible entries about something |
| `what_is_at` | what stands at a spot, solid or not |
| `check` | the project's problems: gateways, records, sheets, script errors |
| `look` | a picture of the scene (around an entity, an area, or all of it), drawn by the CPU renderer and attached to the next message |
| `playtest` | the scene played headless for a few seconds, pressing the actions asked for in turn (`move_x+` walks): its events, script errors, where the player went, its pockets and health |

## Actions

Operations (reviewed): entity fields and components, prefab fields and **components on prefabs**
(`add_prefab_component` / `remove_prefab_component`, every copy following), settings, script properties, dialogue, quests,
schedules, cutscenes, UI, animation sheets, records (write and delete), the story graph, and commands — scene.spawn,
scene.paintRect, entity.delete / turn / mirror, prefab.create / createFromEntity / replace, terrain paint / path /
replace, script.write, world.write / delete, scene.addMarker, level.generate / village, **scene.create** (with a
type), **scene.setType**, **input.addAction**, **input.removeAction**, **file.move** (a prefab, asset or scene moved or
renamed, every reference following — a scene's rename too), **asset.delete**, **scene.delete**, **asset.import** (a file
from the user's computer). Gateways are a Gateway component on an entity plus `scene.addMarker` in the scene it leads
to.

**Undo**: the AI panel's Undo on an applied proposal puts back the files it wrote (prefabs, scripts, records, dialogue,
quests, settings, input.json, World Bible entries, new scenes and prefabs deleted) as well as its scene changes; Apply
again writes them anew. Ctrl+Z undoes the scene's side only. A `file.move`'s rewritten references aren't put back.

**Review each step** (the AI panel's settings): a task waits after each step, the step shown, for **Continue** or Stop.

## Context

What's sent with every request (`ContextBuilder`): the open scene's entities (200 at most), tiles in summary, prefabs,
analysed assets, scripts (capped), settings, the World Bible by name, dialogue and quests (capped), records, scenes,
the selection — **every selected entity** when there are several — and what the Build panel has open. More is read
with tools.

## Still to do

- `look` while playing; a playtest holding several keys at once.
- Web search for assets (the Assets panel's web search, from words).
