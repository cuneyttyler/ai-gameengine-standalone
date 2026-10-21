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
- **One place per stage.** The **Build panel** (A2) shows the stage; the viewport shows
  the location, the plan or the build; the Inspector shows the selected part; the AI panel
  keeps the conversation and a compact card for the build.
- **The user can stop at any time.** Nothing is applied to the scene until the build
  stage. Each built part is one undo step, and the whole build can be undone.

### A2. Where builds are shown: the Build panel

**Why not the AI panel.** The AI panel is a narrow (360 px) chat column. Its proposal
card lists each change as *before → after* with a checkbox, plus Apply / Reject. That
works for small edits, but not for a build:
- The Brief and the asset plan are **wide, editable tables** with selects, sliders,
  sketches and thumbnails. At chat width they would be cramped and scroll sideways.
- Inside the chat history they **scroll away** while the user is still working on them.
- The plan preview and the build happen **in the viewport**, not in a card.
- A town is **thousands of objects**, so a per-change review list is meaningless. The
  user reviews the Brief, the plan and the result instead.

**The Build panel.** A dedicated panel, alongside Scene, Assets and AI, in the same
docking system (5.6). It opens automatically when a building query starts.
- **Docking:** bottom by default (wide, good for tables), or right or left.
- **Not modal:** the viewport stays visible for the location outline, the plan preview
  and the build itself. A modal window, like the pack import dialog, would hide them.
- **A stepper:** ① Request · ② Assets · ③ Plan · ④ Build · ⑤ Result. Each step is one
  view, not a scrolling history. Until something is built, the user can step back freely.
- **One build at a time** is open in the panel. Earlier builds stay reachable from the
  AI panel's cards and from the place's list (*places/*).
- **The AI panel** keeps the conversation. For each build it shows one **build card**:
  the stage, progress and, at the end, the headline, with *Open build panel*.
  Follow-ups ("more trees in the east gardens") still go through the chat.

```
┌ AI (chat) ──────────────┐   ┌ Build — Riverford ─────────────────────────────────────────┐
│ > build a large village │   │ ① Request  ② Assets  ③ Plan  ④ Build  ⑤ Result            │
│   by a river            │   │────────────────────────────────────────────────────────────│
│ ┌ Building Riverford ─┐ │   │ Size  [large village ▾]  buildings [70]–[90]  area 140×140 │
│ │ ② Assets · 3 to     │ │   │ Layout (•organic)(grid)(radial)(along river)  [sketches]   │
│ │   generate          │ │   │ ┌Component──────┬On─┬Amount────┬Assets──────┬Source──────┐ │
│ │ [Open build panel]  │ │   │ │Residential    │ ☑ │ 3–5 wards│ ▣▣▣        │ auto ▾     │ │
│ └─────────────────────┘ │   │ │Gardens        │ ☑ │ common ▾ │ ▣▣ ⚠ rose  │ generate ▾ │ │
└─────────────────────────┘   │ │Walls          │ ☑ │ stone ▾  │ —          │ generate ▾ │ │
                              │ └───────────────┴───┴──────────┴────────────┴────────────┘ │
                              │ Assumptions: … (labelled)          [Back]  [Continue →]    │
                              └────────────────────────────────────────────────────────────┘
```

**Reused from the editor:**
- the multi-step task progress (2.9) becomes the Build step;
- the applied / undone tracking of proposals becomes the result's *Undo all* and per-part
  undo;
- the panel framework and docking (5.6).

The per-change review list remains only as an optional *Details* view of the commands a
build ran.

**Small AI changes are unchanged:** a request that isn't a build ("make skeletons
tougher") still gets the AI panel's proposal card with Apply / Reject.

### A3. The stages, as the user sees them

| # | Stage | Where | What is shown | User actions |
|---|---|---|---|---|
| 1 | **Request received** | AI panel (chat) → the Build panel opens on ① | the query, and "Reading your request…" | Stop |
| 2 | **Confirm, round 1** | Build panel ① **Request** | build type and name; **size** (class plus numbers); **layout** (choice plus small sketches); **components** (rows: on/off, count or density, thumbnails of matching assets); style; location (the area is outlined in the viewport); **assumptions** (labelled); **questions** highlighted where the AI's drafts disagreed | edit any field · Continue · Cancel |
| 3 | **Confirm, round 2** | Build panel ② **Assets** (with round-2 fields) | densities and asked module fields for the chosen components; the **asset plan** table (`place-assets.md` §6) with sources and a summary line | edit · change sources · Back · **Build** |
| 4 | **Plan preview** | viewport overlay + Build panel ③ **Plan** | the **plan view** drawn over the area (zones in colour with labels, main roads, walls and gates, landmarks); a **plan summary** table (per zone: kind, plots, buildings, key qualities); any plan problems | Approve · Adjust (chat or edit a zone's fields) · Back. Shown for *large village* and above; for smaller places it is skipped unless the user turns it on (project setting) |
| 5 | **Asset work** | Build panel ④ **Build** (first part) | needs being generated or fetched, with live thumbnails as they finish; failures and fallbacks | Stop · replace an asset |
| 6 | **Build** | viewport + Build panel ④ **Build** | the current stage and part highlighted in the viewport; per-stage progress (*networks ✓, wards 3/5, plots 41/88…*); live counts; repairs as they happen | Pause · Stop (keeps what is built, as undo steps) |
| 7 | **Result (the answer)** | Build panel ⑤ **Result**; the headline on the AI panel's build card | see A4 | Keep · Undo all · Regenerate a part · Open the plan |
| — | **Afterwards** | Inspector | for any clicked entity: its part path (*Riverford › Old town › Plot 14 › Garden*), the part's fields (editable), **Why?**, Regenerate, Lock | edit · regenerate · lock |

During stages 2–7, the AI panel's **build card** mirrors the current stage in one or two
lines.

### A4. The result (the final answer)

The answer to the user's query is the Build panel's **Result** step:

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

The Result step stays open as the **place's page** after the build. Clicking a row
selects its parts in the viewport and Inspector. The AI panel's build card keeps the
headline and reopens the page. *Undo all* reverts the build; *Regenerate a part* asks
which part, or uses the selection.

### A5. "Why?"

Every part has a short **rationale**: one or two sentences written by the AI when it
decided that part (Part B, the `rationale` field), plus the ontology facts it used. For
example: *"Craftsmen ward downstream of the old town: tanneries pollute (rule:
noisome-trades-downstream); on the main road for trade."* This is what is shown, never
the raw thinking (in the Inspector, and on rows of the Result step).

### A6. Messages and tone

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
  ceiling so that a runaway answer is cut off: for Mistral, about 3,000 tokens of thinking
  and 6,000 of answer per call, and 3 minutes before a call is retried (B7).
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

### B7. Interpret: three readings of the request

The request is read three times, in parallel, each with a different instruction
(`ai_service/app/places.py`, `READINGS`):

| Reading | The AI is told |
|---|---|
| **literal** | decide only what the request says or clearly implies; leave everything else at the build type's defaults and say so in the assumptions |
| **typical** | read it as an experienced level designer would: what a place like this typically has, sized to what was asked |
| **generous** | a rich, lively version of what was asked (more components, livelier densities), never contradicting it |

Each reading writes a whole Brief (build type, size in numbers, components and amounts,
style), with its **decisions** (`{question, assumed, path, options}`: each open point and
the answer it chose) and other assumptions. Then:

1. **Checked** against the ontology's rules (`ValidateBrief`); a reading that breaks them
   goes back to the AI once, with its exact violations (`/places/repair-brief`).
2. **Merged** (`MergeBriefs`): each field takes the value most readings agree on (a tie goes
   to the typical one). The decisions are merged one per point (by the field they set,
   else by the question); the other readings' answers become its options.
3. **Shown** in ① Confirm's Contents: a field the readings disagree on is marked *differ*,
   with the others' values to pick; one a reading guessed (confidence below 0.6) is marked
   *guessed*; "To decide" lists the decisions with their assumed answers.

Why three: their disagreements are what the request leaves open, so the user sees them
before anything is planned (A1).

How it runs (2026-10-20):
- **One request per reading** (`PlaceBuild::SendReadings`, `/places/interpret` with
  `reading`), side by side: the progress page shows each as it comes ("1 of 3 readings").
  A reading that fails after its retries is dropped; only when all three fail does the
  Brief come from the request's words alone (`FallbackDraft`).
- **Only the named build type's options** go with them: the build type the request's words
  name (or the one chosen in the form) and its components, scale and fields; all build
  types only when the words name none (`InterpretContextFor`). For a village that's about
  8 KB of context instead of 36 KB.
- **Cost and time:** each reading is two calls (two-pass, B3), so six calls in all, usually
  30–60 s together. One reading alone would be a third of that, without the *differ* marks.
- **A ceiling on each call** (B5): the thinking pass may write about 3,000 tokens, the answer
  about 6,000 (`MistralProvider.MAX_THINKING_TOKENS`, `MAX_ANSWER_TOKENS`), and a call waits
  3 minutes at most before it's retried. Before these, a reading that ran on could hold the
  step for five minutes.
