# The revision language

Status: **implemented, v1 (2026-10-27).**

When a user revises a place in words ("make the residential ward bigger", "add 10 more houses to ward 1"), the
words are no longer turned straight into changes to the plan. They go through a small **intermediate language**
of instructions with parameters:

```
words ──AI──▶ program ──check──▶ lowering ──▶ plan changes ──compile──▶ PLL ──solve──▶ layout ──▶ checks
          (instructions)   (engine)       (Spec ops, and what stays)          (layout-ir.md)          (adjustments)
```

1. **The AI translates.** It is given the request, where things are now (`Where`), the plan, and the
   instructions this step allows. It answers with a **program**: a list of instructions, one for each thing the
   request asks for, each with its parameters. It doesn't write plan changes itself.
2. **The engine checks** each instruction against the language. Is it allowed at this step? Are its required
   parameters there? Do its zone, parcel and kind exist, and may the zone hold that kind? An instruction that
   fails is left out, and the user is told why.
3. **The engine lowers** each instruction to the plan's own changes (the Spec ops: `set`, `zone`, `move`,
   `add`, `remove`, `swap`, `occupy`, `shift`). It also lowers it to what the layout must keep: a zone grown
   where it stands, or a zone held in place. Numbers are applied here, exactly. "Add 10" adds 10 to the count the
   plan has. "Twice as big" doubles the land the zone has on the map.
4. **The plan is compiled to PLL and solved** as before (`layout-ir.md`, `place-layout.md` §6). Only what the
   program touched is laid out again. Every other zone keeps its exact cells.
5. **Adjustments.** The new layout is checked against what was asked. A move's relations are checked as facts,
   and those that fail are pulled harder and solved again. A zone that grew without room makes only the zones
   around it give way. The user sees what was done, and what couldn't be done.

**Why.** Before, the AI answered in plan changes directly, and those carried more than was asked. For "make the
craftsmen ward twice as big", the AI's `set` also restated three relations, one of them
`craftsmen-ward east-of craftsmen-ward`. So the engine read a size change as a move, and the zone was placed
somewhere else, smaller. In the language, `grow` has no relations to restate. The AI picks the instruction and
the number; the engine does the rest the same way every time.

What a revision can do is written in **one place**, [`data/places/revise-language.json`](../../data/places/revise-language.json):
- the engine checks and lowers against it (`engine/places/ReviseLanguage.cpp`);
- the editor sends it with each revision request, and the service writes the AI's instructions from it
  (`ai_service/app/places.py`, `program_shape`);
- the Build panel shows it as **"what can I ask?"** above the revision box.

---

## 1. Programs

The AI's answer:

```json
{
  "now": "where the parts the request is about are now (from WHERE)",
  "summary": "one or two sentences to the user",
  "program": [
    {"do": "grow", "zone": "residential-ward", "by": 1.5},
    {"do": "keep", "zone": "market-ward"}
  ]
}
```

- Each instruction is `{"do": <name>, <param>: <value>...}`.
- The instructions run in order. Each one sees the plan as the one before left it, so two `add`s to one zone are
  added up.
- An **empty program** is a valid answer: nothing can be done with these instructions, and `summary` says why.

## 2. Parameters

| Type | What it is |
|---|---|
| `zone` | a zone of the place, by its id (`residential-ward`) |
| `node` | a part of the plan, by its id: a zone, or a parcel inside one (`ward-1/well`) |
| `kind` | a kind of parcel, a module id that the zone's rules allow it to hold (`house-plot`) |
| `module` | a zone module (`temple-ward`) |
| `count` | a whole number, 1 or more |
| `factor` | how many times its size: above 1 grows, below 1 shrinks |
| `cells` | an area in cells |
| `dx`, `dy` | cells east and south (negative: west and north) |
| `rel` | where it goes: `[{type, to}]` in the closed relation vocabulary (`place-layout.md` §4.2) |
| `side` | `north`, `south`, `east`, `west`, `middle` |
| `field`, `value` | a setting and its new value (`street-style`, `house-kind`…) |
| `look` | settings of a kind, `{house-kind: cottage}` |
| `then` | `swap` or `place` |
| `path` | a Brief setting (`shape.layout`, `scale.class`, `components.<id>.assets.variants`) |

## 3. At ② Layout: the zones

| Instruction | Parameters | What it does | Lowered to |
|---|---|---|---|
| `swap` | `a`, `b` | Two zones change places, each on the other's land exactly. | `swap` (an exact op on the land: no solving) |
| `occupy` | `zone`, `where`, `then`? | A zone goes where another is now. That one takes its old land (`swap`) or is placed again (`place`). | `occupy` |
| `shift` | `zone`, `dx`, `dy` | A zone moved by so many cells, keeping its shape. | `shift` |
| `move` | `zone`, `rel` | A zone placed anew where the words say. | `move` with the relations (none to itself), and each relation as a fact to check |
| `grow` | `zone`, `by`? (1.3), `to`? | A zone made bigger **where it stands**. | `set` area = the land it has × `by` (or `to`); its seed and its own relations kept |
| `shrink` | `zone`, `by`? (0.75), `to`? | A zone made smaller where it stands. It holds fewer plots in proportion. | `set` area and plots; seed kept |
| `add-zone` | `module`, `id`?, `rel`?, `to`? | A new zone. | `add`, its relations checked as facts |
| `remove-zone` | `zone` | A zone taken out; its land is left open. | `remove` |
| `keep` | `zone` | A zone held where it is. | the zone is anchored: it never gives way in this revision |
| `set` | `node`, `field`, `value` | One setting of a zone. | `set` (`area` and `plots` are quantities; the rest are qualities) |
| `set-place` | `path`, `value` | One setting of the whole place. | a Brief change |

**Where a zone stands.** `grow` and `shrink` lay the zone out again from **its own seed**, with its own
relations. Only it is unfrozen: the other zones keep their cells. It takes open land first. If there isn't
enough, the zones around it give way, each from its seed; zones the request names, and `keep`ed ones, never do.
A `move` lets the zone go: it is placed anew, on free land big enough for it (`place-layout.md` §8.2–8.3).

## 4. At ③ Plan and after: inside the zones

At the plan, the zones lie where the layout put them. A program here changes only what is inside them.

| Instruction | Parameters | What it does | Lowered to |
|---|---|---|---|
| `add` | `zone`, `kind`, `count`, `look`? | **Exactly** `count` more parcels of a kind. They join the group of that kind that looks the same (its `repeat` + `count`), or form a new group `zone/kind`. | one `zone` op with all of the zone's parcels; the Brief's building count raised by as many |
| `remove` | `zone`, `kind`, `count`? — or `node` | So many of a kind taken out of a zone (all of them without a count), or one parcel by its id. | `zone` op with the counts lowered; or `remove` (a local edit: nothing is laid out again) |
| `replace` | `zone`, `kind`, `with`, `count`?, `look`? | So many of one kind turned into another kind. | `zone` op |
| `place` | `node`, then one of `side`, `near`, `into` | One parcel moved: to a side of its zone, next to another parcel, or into another zone (one of a group leaves it). | `set` with `in-<side>` / `center` / `near` relations; or both zones' `zone` ops |
| `set` | `node`, `field`, `value` | One setting of a zone or a parcel. | `set` |
| `set-place` | `path`, `value` | One setting of the whole place. | a Brief change |

**Only what was asked.** Every zone the program doesn't touch keeps its land and every parcel where it stood.
The plan is solved **partially**: the solver gets the layout from before the revision and lays out again only the
zones the changes touch (`resolveOnly_`, with the solver's `previous` and `resolve` options). Everything else is
restored as it was, including the site's own things (trees, the main roads). Building counts aren't re-balanced
across zones.

A zone that can't hold what was added takes the open land beside it (`GrowShortZones`), not its neighbours'
land. It is then laid out again on that new land: the frozen layout's cells, not its old region. The site's
things that stood on that land are cleared for it.

A change to something outside every zone (a road of the place) lays out the whole plan again.

## 5. What is checked before anything runs

An instruction is left out, with the reason told to the user, when:
- it isn't in the language ("fly isn't something a revision can do");
- it belongs to the other step. `grow` at the plan says "that's done on the layout: go back to ② Layout"; `add` at
  the layout says "that's done on the plan";
- a required parameter is missing ("add 10 to the ward" with no kind);
- a zone, parcel or kind it names doesn't exist, or the zone's rules don't allow that kind ("a residential-ward
  can't hold a dock");
- it asks for nothing: `grow` that isn't bigger, `shift` with no distance, `move` with nowhere to go, `remove` of
  a kind the zone hasn't got.

The rest of the program still runs. Both guards of `place-layout.md` §8.2 still apply to what is lowered:
- **scope:** a change to a zone the request doesn't name is dropped;
- **plan freeze:** at the plan, no zone moves or changes size.

## 6. What the user sees

- Above the revision box: **"what can I ask?"** lists this step's instructions, what each does, and examples. An
  example clicked goes into the box.
- In the revision's history:
  - **Done:** each instruction that ran, as it reads (`Grow residential-ward ×1.5`, `Add 10 house-plot to ward-1`);
  - **Not done:** each one that couldn't run, and why;
  - the AI's summary, and the checks of any facts as before.

The program, its lowering and the notes go to the build's `log.jsonl` (`"step": "revise program"`).

## 7. Adding an instruction

1. Add it to `data/places/revise-language.json`: `do`, `steps`, `params` (types from `params`), `required`,
   `defaults`, `says`, `shown`, `examples`. The AI and the panel get it from here.
2. Lower it in `LowerRevision` (`engine/places/ReviseLanguage.cpp`): check its parameters, emit Spec ops,
   `inPlace` and `keep`.
3. Test it in `tests/revise_language_test.cpp`. `TheLanguageFileIsWhole` already checks the file's entries.

The free-form changes (§8.2 of `place-layout.md`) remain for the AI's own map review ("Check the map"), which
isn't a user's request.

## 8. Tests

| Test | What it covers |
|---|---|
| `tests/revise_language_test.cpp` | Each instruction checked and lowered, without the AI: exact counts, sizes from the land, notes for what can't run, the language file whole. |
| `tests/layout_tests/` | Revisions at ② Layout, run as a user runs them in the editor (real AI service, a fresh project): swaps, a move, grow, grow ×2, shrink. Each checks the change and that the zones not asked about kept their land. See its README. |
| `tests/plan_tests/` | Revisions at ③ Plan, run the same way: 10, 5 and 3 more buildings in a zone. Each checks the exact count, and that every other zone kept its land and its things exactly. See its README. |
| `tests/place_flow_test.cpp` | The steps with the AI's answers given (the older free-form changes). |

## 9. Next

- **The words alone, for the common requests.** "Swap A and B" already runs without the AI. "Add N <kind> to
  <zone>" and "make <zone> bigger / smaller / N times as big" could be read the same way: instant, free, exact.
  The AI would only be asked when the words aren't one of these.
- **Shown before it runs.** A program could be shown for the user to confirm ("Grow residential-ward ×1.3 —
  OK?") before anything changes, and edited there: the numbers, the zone.
