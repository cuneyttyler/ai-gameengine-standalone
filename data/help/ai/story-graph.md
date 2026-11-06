# The story graph

The story graph is the structured record of your game's story: characters, factions, locations, items, events, story scenes and quests, and the facts between them over time, checked by the story rules. This covers Project ▸ Story graph and asking the AI to change it.

## What the story graph is

The story graph is a knowledge graph stored in `planner/kg.json`. Its nodes are characters, factions, locations, items, events, story scenes, quests and more (41 classes in six layers: plot structure, events, agents, world, quests, presentation). Its facts link them: relationships, membership, who knows what, who has what, where someone is, their state, their goals.

Facts that change hold from a story scene until a later one, by the scenes' order, and can belong to a branch. A death is a fact too: a character's state "dead" from a scene on.

Characters, places and items use World Bible keys as ids (`character/tamsin`), so the graph and the World Bible describe the same things.

The AI Planner writes the graph as it designs the story; the AI panel can change it too.

Questions: what is the story graph? where is the story graph stored? how are facts over time recorded?

## Project ▸ Story graph

Open the **Project** panel (**P**) ▸ **Story graph** tab.

- **Nodes**: a table of every node and its class, with a filter and a layer select ("all layers"). Click a node to see its fields, who made it ("made by"), its issues, **Facts over time** (each fact with when it holds; click to go to the other node), **Depends on it**, and **World state here** for a story scene.
- **Issues (N errors, N warnings)**: the story rules' findings ("Every rule holds." when none).
- **Sync World Bible**: brings hand-written `rel:` lines from World Bible entries into the graph, and writes the graph's end-of-story relations out as `rel:` lines.
- **Refresh**, and the graph's revision.

With no graph yet: "No story graph yet. The AI Planner writes planner/kg.json as it designs the story; you can also ask the AI panel."

Questions: how do I see the story graph? what does Sync World Bible do? how do I see a character's facts over time?

## The story rules

The engine checks the story's rules on the graph: the dead don't take part in later scenes, someone is in one place at a time, a unique item has one owner, quests need their characters alive, kernel scenes are played by the main quest, relationships agree both ways, and more (structure, causality, continuity, characters, quests, places). Each finding is an error or a warning, shown in the Issues view and on the node.

An AI change that would break the graph's shapes or add a rule error is refused.

Questions: what rules does the story graph check? why was my story graph change refused?

## Asking the AI to change the story graph

In the AI panel, describe story facts: "add Tamsin and Aldric to the story graph, Tamsin trusts Aldric until scene 4", "Aldric dies in scene 12", "fix the story graph's issues". The AI answers with one story graph change (nodes, facts, facts ended at a scene, and removals), using only the classes, properties and vocabularies of the story ontology, which it's given with the graph's nodes, scenes and current issues.

The change is reviewed and applied like any proposal. A project can extend the ontology in `story/ontology/`.

See also: world-bible.md, planner.md.

Questions: can the AI edit the story graph? how do I say a character dies with the AI? how do I fix story graph issues?
