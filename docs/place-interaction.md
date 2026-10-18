# Place interaction — what the user sees, and how the AI is asked

Status: **draft for review (2026-10-18)**. Part of the Phase 7 design.
- **Part A** defines everything the user is shown during a build, including the final
  answer.
- **Part B** defines how each AI call is made, including a **thinking step**.

---

## Part A — What the user sees

### A1. Principles

- **Show decisions, not machinery.** The user sees counts, choices, thumbnails, the plan
  view and the result, never JSON, IR or the model's raw thinking (that is in the log).
- **Numbers with words.** "Large village (60–150 buildings)", never just "large".
- **Every claim is checkable.** A count in a report links to the parts it counts; a
  failed check links to the part that failed.
- **One place per stage.** The AI panel shows the stage's card; the viewport shows the
  plan or the build; the Inspector shows the selected part.
- **The user can stop at any time.** Nothing is applied to the scene until the build
  stage. Each built part is one undo step, and the whole build can be undone.

### A2. The stages, as the user sees them

| # | Stage | Where | What is shown | User actions |
|---|---|---|---|---|
| 1 | **Request received** | AI panel | the query, and "Reading your request…" | Stop |
| 2 | **Confirm, round 1** | AI panel: **Brief card** | build type and name; **size** (class plus numbers); **layout** (choice plus small sketches); **components** (rows: on/off, count or density, thumbnails of matching assets); style; location (the area is outlined in the viewport); **assumptions** (labelled); **questions** highlighted where the AI's drafts disagreed | edit any field · Continue · Cancel |
| 3 | **Confirm, round 2** | Brief card, step 2 | densities and asked module fields for the chosen components; the **asset plan** table (`place-assets.md` §6) with sources and a summary line | edit · change sources · Back · **Build** |
| 4 | **Plan preview** | viewport overlay + AI panel | the **plan view** drawn over the area (zones in colour with labels, main roads, walls and gates, landmarks); a **plan summary** table (per zone: kind, plots, buildings, key qualities); any plan problems | Approve · Adjust (chat or edit a zone's fields) · Back. Shown for *large village* and above; for smaller places it is skipped unless the user turns it on (project setting) |
| 5 | **Asset work** | AI panel: progress | needs being generated or fetched, with live thumbnails as they finish; failures and fallbacks | Stop · replace an asset |
| 6 | **Build** | viewport + AI panel: progress | the current stage and part highlighted in the viewport; per-stage progress (*networks ✓, wards 3/5, plots 41/88…*); live counts; repairs as they happen | Pause · Stop (keeps what is built, as undo steps) |
| 7 | **Result (the answer)** | AI panel: **Result card** | see A3 | Keep · Undo all · Regenerate a part · Open the plan |
| — | **Afterwards** | Inspector | for any clicked entity: its part path (*Riverford › Old town › Plot 14 › Garden*), the part's fields (editable), **Why?**, Regenerate, Lock | edit · regenerate · lock |

### A3. The result card (the final answer)

The answer to the user's query is a single card:

1. **Headline:** one sentence, e.g. *"Built Riverford: a large village of 84 buildings in
   4 wards, walled, on the river, in 6 min."*
2. **Asked vs built:** the Brief's key fields and components, each with the requested
   and the built value, and ✓ or ⚠ against the tolerance. Each row links to the parts.

   | | Asked | Built | |
   |---|---|---|---|
   | Buildings | 70–90 | 84 | ✓ |
   | Gardens | common (≈60% of plots) | 47 of 78 plots (60%) | ✓ |
   | Flowers in gardens | common | 0.27 / cell | ✓ |
   | Walls | stone, 2 gates | stone, 2 gates | ✓ |
3. **Assets:** counts by source (project, library, web, generated), with thumbnails of
   new ones. Generated and fetched assets are listed with their licences. Missing assets
   and placeholders are flagged.
4. **Checks:** passed / total. Each failure or kept compromise is a line ("2 gardens
   lack fences: no corner pieces fit"), linked to the part.
5. **Repairs made:** a short list ("moved 3 barrels blocking doors").
6. **What next:** suggested follow-ups as buttons ("Add villagers", "Build the tavern's
   interior", "More trees in the outskirts").

The card stays in the AI panel's history. *Undo all* reverts the build; *Regenerate a
part* asks which part, or uses the selection.

### A4. "Why?"

Every part has a short **rationale**: one or two sentences written by the AI when it
decided that part (Part B, the `rationale` field), plus the ontology facts it used. For
example: *"Craftsmen ward downstream of the old town: tanneries pollute (rule:
noisome-trades-downstream); on the main road for trade."* This is what is shown, never
the raw thinking.

### A5. Messages and tone

- Short, plain and specific. No apologies, no filler.
- Problems name the part, the reason and the fix that was tried.
- Long waits show what is happening, with counts, never a bare spinner.

---

## Part B — How the AI is asked

### B1. Every AI call has the same shape

```
inputs  →  THINK (free text, private)  →  ANSWER (strict schema)  →  validate  →  (repair once)
```

1. **Inputs**, assembled by the builder:
   - the step's **task description** and **output schema**;
   - a **one-shot example** (Holodeck's prompt structure);
   - the **relevant ontology subgraph** (`place-ontology.md` §3);
   - the documents so far (the Brief, the parent Spec node);
   - **verified examples** of similar nodes.
2. **Think:** the model reasons in free text first:
   - what the request implies;
   - which rules from the subgraph apply;
   - the numbers, worked through (areas, counts, shares);
   - the alternatives, and the choice made.
3. **Answer:** the model writes the step's document in its strict schema. It includes a
   short **`rationale`** per decided part (≤ 2 sentences, for "Why?").
4. **Validate:** schema, structure and aggregate constraints (`place-schema.md` §4).
5. **Repair:** one retry with the exact violations listed. If that also fails, the step
   reports a problem; it never guesses silently.

### B2. Why think first

- Planning a place is multi-step reasoning: areas must add up, shares must match counts,
  and rules interact. Reasoning written out before answering helps with this.
- Forcing a strict format **during** reasoning hurts it. Tam et al. (*Let Me Speak
  Freely?*, EMNLP 2024) found reasoning degrades under format restrictions, and that the
  order of keys in structured output interferes. Free text first, then the format,
  avoids this.
- The thinking also helps debugging and repair (it is logged with the step).

### B3. How the thinking is produced (provider-independent)

The builder supports three modes. The provider adapter picks one, and the setting can
override it:

| Mode | How | When |
|---|---|---|
| **native** | a reasoning model returns its thinking separately from the answer (e.g. Mistral's Magistral models return reasoning apart from content) | the configured model supports it |
| **two-pass** | call 1: think in free text (no format), with all inputs; call 2: write the answer in the strict schema, given the inputs **and** the thinking | default for steps that think when the model has no native reasoning; most robust for schema adherence |
| **one-pass** | one call; the schema's **first** field is `"thinking"` (a string), followed by the answer fields | cheapest; a fallback, used only where tests show it works as well |

Whether a given reasoning model also accepts schema-constrained output is checked per
provider. The design doesn't depend on it, because the two-pass mode always works.

### B4. Which steps think

| Step | Thinks | Why |
|---|---|---|
| Interpret (Brief drafts) | **yes** | resolves ambiguity; maps words to concepts and scale |
| Plan: the root and zone levels | **yes** | the hardest reasoning: areas, shares, relations, design rules |
| Plan: a lower node that needs judgment (a named landmark) | **yes** | |
| Plan: lower levels from expansion rules | no AI call | rules and defaults, no model |
| Compile: relations for the upper levels | **yes** | spatial reasoning |
| Asset needs: search queries, generation prompts | short | |
| Verify: visual critique | **yes** | compares an image with the Spec and checklist |
| Repair: re-plan a node | **yes** | must reason about the failed rule |
| Formatting-only steps (the window's text, report wording) | no | |

### B5. Budget and logging

- **Thinking length:** set per step (default: generous for Interpret and Plan, short
  elsewhere). The model budget is unlimited by decision, but each step still has a
  ceiling so that a runaway answer is cut off.
- **Logging:** each call's inputs (with ontology fact ids), thinking, answer, validation
  result and repair go into the place's log (`places/<place>/log.jsonl`). Reviewers and
  tests can replay a step from it.
- **Caching:** saved documents are reused. Re-solving or rebuilding never calls the AI
  again unless a node is re-planned.
- **Never shown to the user:** the raw thinking. Shown: the rationale (A4).

### B6. Self-check before answering (in the thinking)

Each step's task description ends with a short checklist the model goes through while
thinking, before it answers. For the Plan step:
- Do the children's areas fit within 85% of the parent?
- Do the counts sum to the Brief's numbers?
- Is every enabled component placed?
- Does each zone follow the ontology's site preferences?

The validators then check the same rules for real. Their job is to make the first answer
more often right.
