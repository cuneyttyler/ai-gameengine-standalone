# Revising a place in words

How to change a place being built in the Build panel by describing the change ("make the residential ward bigger", "add 10 more houses to ward 1"): the revision language, what each instruction does at ② Layout and ③ Plan, what is kept, and what the panel shows.

## Revising a place: where and how

While a place is at ② Layout or ③ Plan in the Build panel, type a change in the **Revise** box ("e.g. move the market to the crossroads; the residential ward along the river, south of the docks") and press **Revise**. You can also type it in the AI panel: a change about the place the Build panel has open goes there as a revision ("That's about Riverford, so it went to the Build panel as a revision").

Above the box, **what can I ask?** lists this step's instructions, what each does and examples; click an example to put it in the box.

The revision's history shows **You:** your words, then:
- **Done**: each instruction that ran, as it reads ("Grow residential-ward ×1.5", "Add 10 house-plot to ward-1");
- **Not done**: each that couldn't run, and why;
- the AI's summary, and the checks of any placement facts.

Once a place is built into the scene, it's the scene's: ask the AI panel for changes ("add a well to the village") as for any part of the scene.

Questions: how do I change a place in words? where is the Revise box? what can I ask in a revision?

## How a revision runs: the revision language

Your words aren't turned straight into plan changes. The AI translates them into a **program**: a list of instructions with parameters, one for each thing you asked. The engine then:
1. checks each instruction (allowed at this step? required parameters there? do the zone, parcel and kind exist, and may the zone hold that kind?); a failing one is left out and you're told why;
2. applies the numbers exactly ("add 10" adds 10 to the plan's count; "twice as big" doubles the zone's land on the map);
3. lays out again only what the program touched: every other zone keeps its exact cells and every parcel where it stood;
4. checks the new layout against what was asked, pulls failing placements harder, and makes only the zones around a growing zone give way.

An empty program is a valid answer: nothing can be done with these instructions, and the summary says why. "Swap A and B" runs without the AI.

Questions: why didn't my revision move other zones? does the AI change the plan directly? how exact are revision numbers?

## At ② Layout: changing the zones

At ② Layout a revision changes the zones themselves:
- **swap** two zones: each takes the other's land exactly;
- **occupy**: a zone goes where another is now; that one takes its old land or is placed again;
- **shift** a zone by so many cells east/south (negative west/north), keeping its shape;
- **move** a zone to where the words say (it's placed anew, on free land big enough);
- **grow** a zone where it stands (by ×1.3 unless you say), taking open land first, else the zones around it give way (zones you named or kept never do);
- **shrink** a zone where it stands (×0.75 unless you say), with fewer plots in proportion;
- **add-zone** a new zone of a module, **remove-zone** (its land is left open);
- **keep** a zone where it is: it never gives way in this revision;
- **set** one setting of a zone; **set-place** one setting of the whole place.

Asking for something done at the plan (like adding houses) here says "that's done on the plan".

Questions: how do I make a ward bigger? how do I swap two zones? how do I keep a zone in place?

## At ③ Plan: changing what is inside the zones

At ③ Plan the zones stay where the layout put them (no zone moves or changes size); a revision changes only what is inside:
- **add** exactly so many parcels of a kind to a zone ("add 10 more houses to ward 1"); the place's building count rises by as many;
- **remove** so many of a kind (all without a count), or one parcel by its id;
- **replace** so many of one kind with another;
- **place** one parcel: to a side of its zone, next to another parcel, or into another zone;
- **set** one setting of a zone or parcel; **set-place** one setting of the whole place.

A zone that can't hold what was added takes the open land beside it, not its neighbours' land. Building counts aren't re-balanced across zones. A change to something outside every zone (a road) lays out the whole plan again. Asking to grow a zone here says "that's done on the layout: go back to ② Layout".

Questions: how do I add buildings to a zone? how do I remove a parcel? why can't I grow a zone at the plan step?

## Why an instruction wasn't done

An instruction is left out, with the reason, when:
- it isn't something a revision can do;
- it belongs to the other step (grow at the plan, add at the layout);
- a required parameter is missing ("add 10 to the ward" with no kind);
- a zone, parcel or kind it names doesn't exist, or the zone's rules don't allow that kind ("a residential-ward can't hold a dock");
- it asks for nothing (grow that isn't bigger, shift with no distance, remove of a kind the zone hasn't got);
- it changes a zone your request didn't name.

The rest of the program still runs. Name zones as the panel shows them, and say the kind and the number.

Questions: why was my revision not done? what does "not done" mean in the revision history?

See also: build-panel.md, build-methods.md.
