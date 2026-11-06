# The story graph

The story graph is the structured record of your game's story: characters, factions, locations, items, events, story scenes and quests, and the facts between them over time, checked by the story rules. This covers Project ▸ Story graph and asking the AI to change it.

## What the story graph is

The story graph is a knowledge graph stored in `planner/kg.json`. Its nodes are characters, factions, locations, items, events, story scenes, quests and more (41 classes in six layers: plot structure, events, agents, world, quests, presentation). Its facts link them: relationships, membership, who knows what, who has what, where someone is, their state, their goals.

Facts that change hold from a story scene until a later one, by the scenes' order, and can belong to a branch. A death is a fact too: a character's state "dead" from a scene on.

Characters, places and items use World Bible keys as ids (`character/tamsin`), so the graph and the World Bible describe the same things.

The AI Planner writes the graph as it designs the story; the AI panel can change it too.

### Example: Aldric's story as facts over time

In the warden game the Planner designed, the graph holds, among others:

- the nodes `character/aldric`, `character/tamsin`, `location/riverford` and the story scenes in order;
- a fact "Aldric is at Riverford" from scene 1, ended at scene 5 when he leaves for Greywater, and a new fact "Aldric is at Greywater" from scene 5 on;
- a fact "Tamsin trusts Aldric" from scene 1 until scene 4, where she finds out he lied;
- a state "dead" for Aldric from scene 12.

Because Aldric is `character/aldric` in both, his World Bible entry and his node are the same character: a dialogue for him and a fact about him talk about one person.

Questions: what is the story graph? where is the story graph stored? how are facts over time recorded?

## Project ▸ Story graph

Open the **Project** panel (**P**) ▸ **Story graph** tab.

- **Nodes**: a table of every node and its class, with a filter and a layer select ("all layers"). Click a node to see its fields, who made it ("made by"), its issues, **Facts over time** (each fact with when it holds; click to go to the other node), **Depends on it**, and **World state here** for a story scene.
- **Issues (N errors, N warnings)**: the story rules' findings ("Every rule holds." when none).
- **Sync World Bible**: brings hand-written `rel:` lines from World Bible entries into the graph, and writes the graph's end-of-story relations out as `rel:` lines.
- **Refresh**, and the graph's revision.

With no graph yet: "No story graph yet. The AI Planner writes planner/kg.json as it designs the story; you can also ask the AI panel."

### Example: where is Tamsin in scene 8?

1. Press **P** and click the **Story graph** tab.
2. Type `tamsin` in the filter of **Nodes** and click `character/tamsin`.
3. Under **Facts over time**, read each fact with when it holds; click "Riverford" to jump to that location's node.
4. To see the whole world at one moment, click the story scene for scene 8 and read **World state here**: who is where, who has what, who is alive.

### Example: bring hand-written relations into the graph

1. In Project ▸ **World**, you added `rel: owes character/garrick` to Bren's entry by hand.
2. In Project ▸ **Story graph**, click **Sync World Bible**.

Bren's debt is now a fact in the graph, and the graph's end-of-story relations are written back to the entries as `rel:` lines, so both agree.

Questions: how do I see the story graph? what does Sync World Bible do? how do I see a character's facts over time?

## The story rules

The engine checks the story's rules on the graph: the dead don't take part in later scenes, someone is in one place at a time, a unique item has one owner, quests need their characters alive, kernel scenes are played by the main quest, relationships agree both ways, and more (structure, causality, continuity, characters, quests, places). Each finding is an error or a warning, shown in the Issues view and on the node.

An AI change that would break the graph's shapes or add a rule error is refused.

### Example: a dead man at the feast

1. Aldric dies in scene 12, but the story scene "The Harvest Feast" (scene 14) still has him taking part.
2. Project ▸ Story graph ▸ **Issues** lists an error for it, and the scene's and Aldric's nodes show the issue too.
3. Either move his death later, or take him out of the feast. In the AI panel:

   > Fix the story graph's issues: Aldric shouldn't be at the harvest feast, his brother Edric speaks for him instead

After applying, Issues reads "Every rule holds." (or the remaining findings). A change that would add a rule error, such as "Aldric dies in scene 3" while a later quest still needs him alive, is refused rather than applied.

Questions: what rules does the story graph check? why was my story graph change refused?

## Asking the AI to change the story graph

In the AI panel, describe story facts: "add Tamsin and Aldric to the story graph, Tamsin trusts Aldric until scene 4", "Aldric dies in scene 12", "fix the story graph's issues". The AI answers with one story graph change (nodes, facts, facts ended at a scene, and removals), using only the classes, properties and vocabularies of the story ontology, which it's given with the graph's nodes, scenes and current issues.

The change is reviewed and applied like any proposal. A project can extend the ontology in `story/ontology/`.

### Example: Tamsin trusts Aldric, until she doesn't

1. In the AI panel type:

   > Add Tamsin and Aldric to the story graph, Tamsin trusts Aldric until scene 4

2. The card has one row, `• Story graph · planner/kg.json`, with a **file** badge. Under ▸ it shows the change: the two character nodes (`character/tamsin`, `character/aldric`) if they were missing, and a trust fact from Tamsin to Aldric that ends at scene 4.
3. Click **Apply 1**.

In Project ▸ Story graph, Tamsin's **Facts over time** shows the trust holding until scene 4. Follow up with "Aldric dies in scene 12" to add his death as a state from that scene on.

See also: world-bible.md, planner.md.

Questions: can the AI edit the story graph? how do I say a character dies with the AI? how do I fix story graph issues?
