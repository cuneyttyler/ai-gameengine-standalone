# Tips and common problems with the AI panel

How to word requests so the AI panel does what you mean, how to ask for NPC behaviour and story content, and fixes for the common problems (it didn't do everything, AI is off, no key, it keeps asking).

## Writing good requests

- **Say what, on what.** "Make skeletons tougher but slower" changes the skeleton prefab, so every copy follows. Select entities first to mean "these".
- **One feature at a time** gets the cleanest review. A list of unrelated features gets a split; use **Do them all, step by step** if you want them in one go.
- **Name things by their names**: a building's name ("house-4"), a character's name, a World Bible entry. The AI places things by name, not by guessed coordinates.
- **Ask from the Inspector** ("Ask AI" on a field) for a question about one value.
- **Follow up** in the same conversation: "now make them faster" works, since the last turns go with each request.
- **Say when you want something new**: the AI reuses art, prefabs and character sheets the project has. Say "a new look" or "regenerate" to have it made again.
- **Base values, not difficulty**: "make skeletons tougher" changes the base; "Hard is too easy" changes the difficulty level.
- **Read the notes** on the card: assumptions and how to test it.

### Example: from a vague request to a clear one

A vague request: "make the town better". It changes nothing specific and usually gets a question or a split.

A clear one, sent as two requests:

1. Select the three market stalls in the Scene panel, then type:

   > Make these stalls sell bread, fish and rope, one each

2. Apply the card, then follow up:

   > Now give the fish stall's trader a gossip line about the smugglers

Each card is small and easy to review, the selection says which stalls, and the follow-up knows which trader "the fish stall's trader" is.

### Example: base value or difficulty?

- "Make the bandits hit harder" changes the Bandit prefab's attack, on every difficulty.
- "Hard is too easy, bandits should hit harder there" changes the Hard difficulty level instead, and Easy and Normal stay as they are.

Questions: how should I word an AI request? how do I get better results from the AI?

## Asking for NPC behaviour

What a character does on its own is its AI packages (records of type "package" listed in its Behaviour component). Ask in plain words and the AI writes the package record and adds its id to the character's Behaviour (adding Behaviour if there is none, keeping the packages it already has unless you say to replace them). It uses a built-in template where one fits:

- "Vara lives in house-4, cooks now and then" → template **live** (wanders round the building and its rooms, uses what you name by weight);
- "the guard patrols the walls" → **patrol** on a named path;
- "the baker sells from her stall" → **vendor**; "sleeps at night" → **sleep** with a schedule;
- "the farmer farms his field" → **tend** on the field's Reaction;
- "stays indoors" → live with outsideWeight 0; "waits longer" → waitMin / waitMax.

For routes it reuses a named Path in the scene or adds one (`scene.addPath`, or `path.around` a building), with forks and conditions, and uses it with the follow-path or patrol templates. Logic the steps can't express (an order of tasks, recipes, counting) becomes a scripted package: a Lua `run()` coroutine using names, never coordinates. "When the alarm sounds this guard chases the player" is a Character Rules response pushing a package.

If the character has no clip for an action ("cook"), the AI can have one made (an animation for its character sheet).

See also: the package records and paths in the systems topics.

### Example: Vara keeps the inn and cooks

1. Type in the AI panel:

   > Vara Bell lives in the Golden Goose, cooks now and then and never goes outside

2. The card has two rows: `• Record · package/vara-live` (with a **file** badge) and `✎ Vara Bell · Behaviour.packages  → vara-live` (or `+ Vara Bell · + Behaviour` if she had none). Under ▸ the record reads:

```json
{
  "template": "live",
  "description": "Vara keeps the inn and cooks.",
  "location": { "building": "golden-goose" },
  "data": { "actions": [{ "action": "cook", "weight": 2 }], "outsideWeight": 0 }
}
```

3. Click **Apply 2**, then press Play.

Vara wanders round the inn's rooms and stops to cook now and then, and never steps out. The package shows in Project ▸ Records ▸ Packages straight away.

### Example: a night patrol on the town wall

1. Ask "The guard Bran patrols the town wall at night and goes to the north gate when the alarm is raised".
2. The card writes a patrol package on the wall's Path (or adds the path first with `scene.addPath` if there is none), with a schedule from 20:00 to 06:00, and a Character Rules response for the alarm.

After applying, Bran walks the wall each night; setting the `alarm_raised` flag from a dialogue or script sends him to the gate.

Questions: how do I make an NPC do something with the AI? how do I ask for a patrol? how do I give a character a daily routine?

## Asking for people, story and dialogue

"Populate this village", "put people in each house", "make a story about this village" are AI panel tasks that start with an outline (see ai-panel-tasks.md). The AI:
1. looks at the buildings (who can live where and cells to stand them), the World Bible, and character prefabs to reuse;
2. writes the story and each named person into the World Bible (`world.write`);
3. places the people at cells inside their buildings, named;
4. makes each one talkable (Talk with their dialogue and World Bible key), writes a dialogue each (a greeting, their trade, gossip as choices), and their idle speech lines;
5. optionally their day (a routine), when asked.

Gossip names neighbours and rivalries, and two people's lines agree. Quests are written only if asked. People for a building whose inside is its own scene go in that scene, as another request. A new named character, place or faction is always added to the canon in the same proposal.

### Example: put people in each house of Millbrook

1. Open the Millbrook scene and type:

   > Put people in each house and make them gossip about the new tax collector

2. The task's outline lists each house with who lives there, what they do and their gossip. Click **Approve and make it**.
3. The final review card has, for each person: a `World Bible · character/<id> (<name>)` row, a spawn inside their house, a Talk component, a `“ Dialogue · dialogue/<name>.json` row with a greeting, their trade and gossip choices, and their idle lines. The story itself is a World Bible row too.
4. Apply it, press Play and talk to two neighbours.

Their gossip names each other and agrees: if the baker says the tax collector lodges at the inn, the innkeeper says so too. No quest was written, because none was asked for; ask "add a quest from the baker about the tax collector" next if you want one.

Questions: how do I populate a village with the AI? can the AI write dialogue for NPCs? how do I make NPCs gossip?

## It didn't do everything I asked

- **Check the notes**: the AI says what it assumed, reused, or couldn't do, and why.
- **Some rows were greyed**: those changes failed validation; the rest can still apply. Ask again for the missing part.
- **A task stopped early**: at 40 steps, at the token budget (raise it in ⚙: "A bigger task may use up to ... million input tokens"), or because the next step couldn't be had. What was done is up for review; ask for the rest as a new request.
- **More than four characters or animations**: these go four a step in a task; it shouldn't stop at four. If it did, ask again ("all of them, step by step").
- **It split the request**: press **Do them all, step by step**.
- **It changed one entity instead of all**: say "all skeletons" or "the skeleton prefab".

### Example: only one skeleton got faster

1. With one skeleton selected, you asked "make the skeleton faster". The card changed `Skeleton · Stats.moveSpeed` on that entity only.
2. Click **Undo** on that card, then ask:

   > Make the skeleton prefab faster, so every skeleton follows

The new card's row is `◈ Skeleton (prefab) · Stats.moveSpeed`, and every copy in every scene gets the new speed.

### Example: a task that ran out of budget

1. "Give every villager a walk and a daily routine" stopped with "Stopped: this task used its token budget (...)".
2. Apply the review card to keep what was done.
3. Raise the budget in ⚙, then ask "the villagers without a routine yet: give them one, step by step".

Questions: why did the AI only do part of my request? why did it stop at four characters? the AI ignored part of my request

## AI is off, not ready, or there's no key

- **"AI is turned off"**: press **Turn on AI** in the AI panel, or File ▸ Preferences… ▸ AI ▸ Use AI.
- **"AI not set up yet"**: the first-run question hasn't been answered; turn it on in the AI panel.
- **"AI service running, but no model provider is configured (set MISTRAL_API_KEY in ai_service/.env)"**: add a key for your provider to `ai_service/.env` (see ai-providers-and-keys.md) and restart the AI.
- **"The AI service didn't start within 45s (is uv installed? see ai_service/README.md)"**: the service needs uv; install it and run `uv sync` in `ai_service` once.
- **"Couldn't start the AI service: ..."** or **"AI service isn't running and no service folder is configured"**: the editor couldn't launch the Python service.
- A provider error shows under File ▸ Preferences… ▸ AI ▸ Model.

### Example: no model provider on a fresh install

1. The AI panel's header says "AI service running, but no model provider is configured (set MISTRAL_API_KEY in ai_service/.env)".
2. Open `ai_service/.env` in a text editor and add your key on its own line:

   ```
   MISTRAL_API_KEY=your-key-here
   ```

3. Save the file and restart the AI (turn it off and on in the AI panel's ⚙, or restart the editor).

The header changes to "AI ready (model name)" and the example requests appear.

Questions: why is the AI not working? AI says no model provider is configured; how do I fix "AI service didn't start"?

## Other common problems

- **"Stop Play mode before asking for changes."**: stop playing (F5) first.
- **"The AI kept asking to look things up without answering; ask again, more precisely."**: name the entity, file or place you mean.
- **"The AI couldn't produce a valid change."**: its answer failed validation twice; the reasons are listed. Rephrase, or ask for less at once.
- **A proposal says it's from before the editor restarted**: ask again.
- **My request opened the Build panel or Planner**: that's routing (building a new place, designing a game). On a build card press **Ask as a change instead** if you meant a change.
- **The script fails when the game runs**: press **Ask AI to fix** under the script check.
- **Contradictions with the World Bible**: **Ask AI to fix**, or apply anyway if the canon should change (then edit the World Bible).

### Example: the AI keeps looking things up

1. "Fix the broken thing in the forest" ends with "The AI kept asking to look things up without answering; ask again, more precisely."
2. Ask again, naming what you mean:

   > The bridge entity "Old Bridge" in the forest scene doesn't let the player cross; make it walkable

With the entity and the problem named, it needs one or two lookups at most and comes back with a card.

### Example: the canon should change, not the dialogue

1. A new dialogue makes the miller Hobb a widower, and the card warns "⚠ 1 possible contradiction with the World Bible": his entry says he is married.
2. You decide the story is better this way: click **Apply 1** anyway.
3. Ask "Update Hobb's World Bible entry: his wife died last winter".

The canon now agrees with the dialogue, and later checks go by the new entry.

Questions: what does "Stop Play mode before asking for changes" mean? how do I fix "The AI couldn't produce a valid change"?

See also: ai-panel.md, ai-panel-review.md, ai-panel-tasks.md.
