# The World Bible

The World Bible is your game's canon: its characters, factions, locations, items, events, lore and tone guide. This covers where it is, its entries, kinds and relations, searching it, checking an entry against the canon, and how the AI uses it.

## What the World Bible is and where it is

The World Bible holds what is true in your game. Each entry is a file in the project, `world/<kind>/<id>.md`, so you can also edit them in any text editor. It belongs to the whole project, not to one scene.

Open it in the **Project** panel (**P**) ▸ **World** tab ("World Bible: the game's canon (characters, places, factions…)").

Everything the AI writes is grounded in it: dialogue uses its names and the voice its style entries describe, Talk components and quests refer to characters by their World Bible key (`character/tamsin`), and a scene can show a World Bible location.

### Example: where Old Tamsin lives on disk

1. Press **P** and click the **World** tab. Under **Characters**, click "Old Tamsin".
2. The same entry is the file `world/character/tamsin.md` in the project. Open it in a text editor and it reads:

```
---
name: Old Tamsin
summary: The herb-woman of Riverford, who remembers the old war.
tags: healer, elder
rel: lives_in location/riverford
---
Tamsin has lived by the mill pond for sixty years. She sells remedies, trusts no soldier, and keeps her late
husband's war medal hidden in a tea tin.
```

A dialogue the AI writes for her uses the speaker `character/tamsin` and her voice; a quest she gives names her by the same key.

Questions: what is the World Bible? where is the World Bible? where are World Bible entries stored?

## Kinds of entries

Entries are grouped by kind, each a collapsible group with a count and **+** to add one:
- **Characters** (`character`)
- **Factions** (`faction`)
- **Locations** (`location`)
- **Items** (`item`)
- **Events** (`event`)
- **Lore** (`lore`)
- **Tone & style** (`style`): the tone guide. Every style entry is always sent with AI requests, so the AI writes in that voice.

An entry's key is `<kind>/<id>`, for example `character/villager` or `location/riverford`.

### Example: a tone guide every AI line follows

1. In Project ▸ **World**, click **+** on **Tone & style**.
2. Set **Id** `voice`, **Name** `Voice of the game`, **Summary** "Dry, wry, low fantasy; nobody speaks in prophecies".
3. In the body write: "People talk plainly and briefly. Humour is understated. No modern slang. Villagers call coins 'bits'. Nobody says 'chosen one'."
4. Click **Create**.

The entry is `style/voice`. Every style entry goes with every AI request, so the next "write a greeting for the baker" comes back dry and brief, with prices in bits.

Questions: what kinds of World Bible entries are there? what is a style entry? what is a World Bible key?

## Creating and editing an entry

Pick an entry, or **+** on a group for a new one. The form:
- **Kind** and **Id** (the file name: `world/<kind>/<id>.md`);
- **Name** (shown in the game, e.g. "Old Tamsin");
- **Summary**: one sentence (this is what the AI sees for every entry);
- **Tags**: comma separated;
- **Scene** (locations): the scene that shows this place; building or editing that scene follows this entry;
- **Relations**: a type ("lives_in") and a target entry, with → to go to it and × to remove; **+ Relation** adds one. **Referenced by:** lists entries whose relations point here;
- the body: description, history, personality, secrets.

**Create** / **Save** (Ctrl+S), **Revert** / **Cancel**, "Unsaved changes", and **Delete…**. Problems (a relation to a missing entry, a missing scene) are listed under the form and marked in the list.

### Example: the miller Bren, who owes the smith

1. In Project ▸ **World**, click **+** on **Characters**.
2. **Id** `bren`, **Name** `Bren the Miller`, **Summary** "Riverford's miller, cheerful in public and deep in debt to the smith". **Tags** `riverford, debtor`.
3. Under **Relations**, click **+ Relation**: type `lives_in`, target `location/riverford`. Add another: type `owes`, target `character/garrick`.
4. In the body, write his history and the secret: he borrowed to rebuild the mill after the flood.
5. Click **Create** (Ctrl+S).

Bren appears under Characters. Garrick's entry now shows Bren under **Referenced by:**. If `character/garrick` didn't exist yet, the relation would be listed as a problem under the form until you create Garrick.

### Example: tie a location to its scene

1. Open the location `location/riverford` and set **Scene** to `riverford`. Click **Save**.

Building or editing the `riverford` scene now follows this entry, and the AI is told which World Bible place the scene shows.

Questions: how do I add a character to the World Bible? how do I link a location to a scene? how do I add relations between entries?

## Searching the World Bible

The search box ("Search the World Bible") filters entries by their words (**Matches**). While AI is on, **Related by meaning** also lists entries whose meaning is close to your search, from the AI service's index (embeddings with a Mistral key; otherwise it ranks by keywords). With AI off it says "Needs AI to be on."

The index is kept in the project's `.cache/` and refreshed as entries change.

### Example: find everyone tied to the old war

1. In Project ▸ **World**, type `war` in the "Search the World Bible" box.
2. **Matches** lists the entries with the word: the lore entry "The Border War" and Old Tamsin, whose body mentions it.
3. With AI on, **Related by meaning** also lists entries that never use the word but are about it: the faction "The Iron Company" (mercenaries who fought it) and the item "Tamsin's medal".

Click one to open it. With AI off, **Related by meaning** reads "Needs AI to be on."; Matches still works.

Questions: how do I search the World Bible? what is Related by meaning?

## Check against canon

On a saved entry, **Check against canon** asks the AI whether it contradicts the rest of the World Bible (save first). It shows "Checking against the rest of the World Bible…", then "✓ Consistent with the rest of the World Bible" or each issue with its severity (contradiction or possible), the quoted words, the entry it conflicts with and what that entry says, and an explanation. "Couldn't check: AI is off" when AI isn't on.

The same check runs automatically on AI panel proposals that write dialogue, quests or World Bible entries. A contradiction needs evidence copied from the canon; things the canon doesn't mention aren't contradictions. Which model checks: File ▸ Preferences… ▸ AI ▸ Checks ▸ Canon check.

### Example: a new event that clashes with Tamsin's past

1. Create an event `event/flood-of-riverford` whose body says "The flood of last spring drowned the mill pond and everyone living beside it." Click **Create**.
2. Click **Check against canon**. It shows "Checking against the rest of the World Bible…", then one issue:
   - **contradiction** "drowned ... everyone living beside it" — `character/tamsin` says "Tamsin has lived by the mill pond for sixty years."
3. Change the body to "...flooded the mill pond; the houses beside it were badly damaged", click **Save**, and check again.

It now reads "✓ Consistent with the rest of the World Bible".

Questions: how do I check an entry for contradictions? what is a canon check?

## How the AI uses the World Bible

- Every AI panel request carries every entry's key, name and summary, plus the full text of the entries most related to the request (up to 6, within a size budget) and every style entry.
- The AI treats everything in it as true, uses its names, places, factions and history, and never contradicts it.
- A new named character, place or faction the AI writes is also added to the canon (`world.write`) in the same proposal; it can delete entries too (`world.delete`). Undo on the proposal puts the entries back.
- Its `search_world` tool finds entries about something.
- The Planner creates entries for its characters, places and factions and writes characters' biographies as their bodies.
- Relations written in entries (`rel:` lines) and the story graph are kept in step by **Sync World Bible** in Project ▸ Story graph.

Ask things like "add the miller Bren to the World Bible, he owes the smith money" or "write lore about the old war".

See also: story-graph.md, ai-panel-review.md.

### Example: add a character and lore from the AI panel

1. Type in the AI panel:

   > Add the miller Bren to the World Bible, he owes the smith money

2. The card has a row `▸ World Bible · character/bren (Bren the Miller)` with a **file** badge and the entry's text under ▸, with an `owes` relation to the smith's entry. The World Bible check runs on it ("✓ Consistent with the World Bible").
3. Click **Apply 1**. Bren shows in Project ▸ **World** at once.
4. Then ask "write lore about the old war". The card writes a `lore/...` entry using the names already in the canon (Tamsin, the Iron Company).

**Undo** on either card puts the World Bible back as it was.

Questions: does the AI read the World Bible? can the AI add World Bible entries? why does the AI use these names?

## Dialogue, quests and routines (the Story tab)

**Project ▸ Story** lists and edits the project's dialogue (`dialogue/`), quests (`quests/`) and daily routines (`schedules/`), one file at a time as a draft, saved through the engine's reader. The format is in the dialogue and quest topics. Dialogue speakers, quest givers and talk targets are World Bible keys.

The AI panel writes these too: a dialogue for each person (with a Talk component making them talkable), quests with stages, objectives, aliases and rewards (written before the dialogue that starts them), and routines with markers. To change an existing dialogue it writes it again whole with the same id. Applied changes show in the Story tab at once.

### Example: a quest from Tamsin, written by the AI and touched up by hand

1. In the AI panel ask:

   > Write a quest where Old Tamsin asks the player to fetch moonwort from the marsh, with a dialogue that starts it

2. The card has `! Quest · quests/moonwort.json (...)` first, then `“ Dialogue · dialogue/tamsin.json` (rewritten whole if she already had one), and a Talk component on Tamsin if she had none. Click **Apply**.
3. Open Project ▸ **Story**: the quest and the dialogue are listed. Open `dialogue/tamsin.json`, change one of her lines to sound grumpier, and save.

Your edit is saved through the engine's reader like any draft. If you later ask the AI to change that dialogue, it writes the whole file again with the same id, starting from your version.

Questions: where are the AI's dialogues? can the AI write a quest? how do I edit what the AI wrote?
