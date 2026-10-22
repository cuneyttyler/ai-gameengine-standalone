# Phase 21 — Manual Test Checklist

The user-test checkpoints from Phase 21 (`development_plan.md`, "The story ontology"), for one
end-of-phase pass. Automated tests cover the logic: the ontology loading and a project's
extension, the published Turtle, the graph's storage and changes, every competency question on a
hand-written story, each rule's planted error, the World Bible round trip (`story_graph_test.cpp`),
and the AI's story_graph checks (`test_storygraph.py`). This list covers using it. The design is
`story-ontology.md` (see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
- A project with a few World Bible characters and places (World tab).

## 21.1 The ontology

- [ ] `ontology/gso-vocab.ttl` and `gso-shapes.ttl` open in a Turtle viewer (or parse with rdflib).
- [ ] Add `story/ontology/vocabularies.json` to a project with `{"vocabularies": {"Tone": ["noir"]}}`:
      the AI panel accepts a story with tone `noir` after reopening the project.

## 21.2 The graph, through the AI panel

- [ ] AI panel: "Start the story graph: Tamsin (the temple healer) and Aldric (her friend) from the
      World Bible, three scenes: Aldric steals the Moon Amulet (cutscene), Tamsin names him
      (dialogue in the temple), the player recovers it (gameplay in his tower). Tamsin trusts Aldric
      until scene 2, then is betrayed by him." The preview shows one Story graph change with its
      nodes and facts; apply it: `planner/kg.json` exists.
- [ ] World tab: Tamsin's entry now has `rel: betrayed_by character/aldric` (the end-of-story fact).
- [ ] Add a `rel: friend character/odo` line to Tamsin by hand, then Story graph ▸ **Sync World
      Bible**: "1 relation read in"; the graph now has it.
- [ ] Ask: "Aldric dies in scene 2." then "Aldric speaks in scene 3": the second change is refused
      with R-T1 (the dead don't take part) and the AI fixes it or explains.

## 21.3–21.4 Rules and the viewer

- [ ] Project ▸ **Story graph**: nodes listed by layer (world, fabula, plot, play…) with a filter.
- [ ] Click Tamsin: her fields, **facts over time** ("start … before s-02: trusts Aldric", "s-02 …
      end: betrayed-by Aldric"), what depends on her.
- [ ] Click a scene: **World state here** shows its participants, who's alive, where they are,
      what they know, the open threads.
- [ ] **Issues**: warnings like "Kernel scene … doesn't turn its value" or "Major character … has
      no arc"; clicking one opens its node.
- [ ] Ask the AI panel "fix the story graph's issues": the issues list shrinks after applying.

## AI panel

- [ ] "Who is alive in scene 3 if the player kills Aldric?" — the AI answers from context
      storyGraph (and the graph's facts per branch).
- [ ] A change with a made-up class, property or relationship type is caught before applying.
