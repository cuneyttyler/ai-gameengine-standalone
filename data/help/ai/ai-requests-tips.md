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

Questions: how do I make an NPC do something with the AI? how do I ask for a patrol? how do I give a character a daily routine?

## Asking for people, story and dialogue

"Populate this village", "put people in each house", "make a story about this village" are AI panel tasks that start with an outline (see ai-panel-tasks.md). The AI:
1. looks at the buildings (who can live where and cells to stand them), the World Bible, and character prefabs to reuse;
2. writes the story and each named person into the World Bible (`world.write`);
3. places the people at cells inside their buildings, named;
4. makes each one talkable (Talk with their dialogue and World Bible key), writes a dialogue each (a greeting, their trade, gossip as choices), and their idle speech lines;
5. optionally their day (a routine), when asked.

Gossip names neighbours and rivalries, and two people's lines agree. Quests are written only if asked. People for a building whose inside is its own scene go in that scene, as another request. A new named character, place or faction is always added to the canon in the same proposal.

Questions: how do I populate a village with the AI? can the AI write dialogue for NPCs? how do I make NPCs gossip?

## It didn't do everything I asked

- **Check the notes**: the AI says what it assumed, reused, or couldn't do, and why.
- **Some rows were greyed**: those changes failed validation; the rest can still apply. Ask again for the missing part.
- **A task stopped early**: at 40 steps, at the token budget (raise it in ⚙: "A bigger task may use up to ... million input tokens"), or because the next step couldn't be had. What was done is up for review; ask for the rest as a new request.
- **More than four characters or animations**: these go four a step in a task; it shouldn't stop at four. If it did, ask again ("all of them, step by step").
- **It split the request**: press **Do them all, step by step**.
- **It changed one entity instead of all**: say "all skeletons" or "the skeleton prefab".

Questions: why did the AI only do part of my request? why did it stop at four characters? the AI ignored part of my request

## AI is off, not ready, or there's no key

- **"AI is turned off"**: press **Turn on AI** in the AI panel, or File ▸ Preferences… ▸ AI ▸ Use AI.
- **"AI not set up yet"**: the first-run question hasn't been answered; turn it on in the AI panel.
- **"AI service running, but no model provider is configured (set MISTRAL_API_KEY in ai_service/.env)"**: add a key for your provider to `ai_service/.env` (see ai-providers-and-keys.md) and restart the AI.
- **"The AI service didn't start within 45s (is uv installed? see ai_service/README.md)"**: the service needs uv; install it and run `uv sync` in `ai_service` once.
- **"Couldn't start the AI service: ..."** or **"AI service isn't running and no service folder is configured"**: the editor couldn't launch the Python service.
- A provider error shows under File ▸ Preferences… ▸ AI ▸ Model.

Questions: why is the AI not working? AI says no model provider is configured; how do I fix "AI service didn't start"?

## Other common problems

- **"Stop Play mode before asking for changes."**: stop playing (F5) first.
- **"The AI kept asking to look things up without answering; ask again, more precisely."**: name the entity, file or place you mean.
- **"The AI couldn't produce a valid change."**: its answer failed validation twice; the reasons are listed. Rephrase, or ask for less at once.
- **A proposal says it's from before the editor restarted**: ask again.
- **My request opened the Build panel or Planner**: that's routing (building a new place, designing a game). On a build card press **Ask as a change instead** if you meant a change.
- **The script fails when the game runs**: press **Ask AI to fix** under the script check.
- **Contradictions with the World Bible**: **Ask AI to fix**, or apply anyway if the canon should change (then edit the World Bible).

See also: ai-panel.md, ai-panel-review.md, ai-panel-tasks.md.
