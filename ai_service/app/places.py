"""Building places with AI (development_plan.md 7.4, docs/place-interaction.md
Part B). Every call has the same shape:

    inputs -> THINK (free text, private) -> ANSWER (strict schema) -> validate -> (repair once)

The editor assembles the inputs (from the ontology) and validates the answers
in C++ (engine/places); this module makes the calls. Thinking is produced one
of three ways (B3): `native` (a reasoning model returns it apart from its
answer), `two-pass` (a free-text call, then the answer given the thinking: the
default) or `one-pass` (one JSON call whose first field is "thinking").

Interpret (step 1) drafts several Briefs, each from a different reading of the
request, so that where they disagree is where the request is open."""

from __future__ import annotations

import json
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field

from .models import Usage
from .providers import Provider, ProviderError

MODES = ("auto", "native", "two-pass", "one-pass")

# The readings of a request, one per draft: their disagreements show the user
# what the request leaves open.
READINGS = [
    ("literal", "Read the request literally: decide only what it says or clearly implies; leave everything else at "
                "the build type's defaults and say so in assumptions."),
    ("typical", "Read the request as an experienced game level designer would: what a place like this typically "
                "has, sized to what was asked."),
    ("generous", "Read the request generously: a rich, lively version of what was asked (more components, livelier "
                 "densities), never contradicting it."),
]

THINK_INSTRUCTIONS = """Think it through in plain text before anything is written down. No JSON yet.
Work through: what the request asks for and implies; which build type it is; what size, with numbers (use the
scale table: a size word is never left as a word); which components the place needs and how much of each; the
style; what you must assume because the request doesn't say. Then check yourself:
- does the size class match the numbers?
- is every component one the build type allows?
- does every field use the values its type allows?"""

BRIEF_SHAPE = """The Brief (schema aige.brief/1), as JSON:
{
  "brief": {
    "schema": "aige.brief/1",
    "buildType": "<one of the build types>",
    "name": "<a short place name>",
    "scale": {"class": "<a class of the build type's scale table>", "buildings": {"count": [low, high]}},
    "style": {"era": "...", "culture": "...", "biome": "<biome>", "condition": "<condition>", "tags": ["..."]},
    "shape": {"layout": "<one street pattern>" or {"mix": {"<pattern>": <share>, ...}} (shares add up to 1; a mix
              when the place combines patterns, e.g. a grid core with organic outskirts),
              "description": "<one sentence: where each pattern goes, if the request says>"},
    "walled": "<none|palisade|stone>",
    "features": [{"module": "<e.g. river>"}],
    "components": {
      "<component module id>": {"enabled": true, "count": [low, high] or n, "density": "<none|rare|some|common|abundant>",
                                "assets": {"source": "auto"}, "params": {"<field>": <value of its type>}}
    }
  },
  "confidence": {"<field path, e.g. scale.class or components.garden.density>": <0 to 1>},
  "decisions": [{"question": "<what the user should decide: one short question>",
                 "assumed": "<what this Brief assumes for it, in words: the answer you chose>",
                 "path": "<the Brief path it sets, e.g. scale.class, walled, components.docks.enabled, style.era; empty if none>",
                 "options": ["<other sensible answers, short>"]}],
  "assumptions": ["<anything else you assumed that isn't worth asking>"],
  "rationale": "<one or two sentences: why this reading>"
}
"root" holds the root module's own settings the user chose ({"<field>": <value>}, e.g. a dungeon's levels).
"scale.buildings" counts the build type's unit (its "counts": buildings, rooms, tents, sites...). Pick the build
type by what the request names (its "words": a cave or crypt is a dungeon, the inside of an inn an interior).
"count" is how many of the component itself: for a zone (a ward), how many such zones, usually 1 (a village has
one residential ward, a city several), never its buildings: those go in its "params" ("plots", "farmsteads"...).
Only components listed for the build type. A disabled component is {"enabled": false} and nothing else.
Every question you'd ask is a decision with the answer you assumed for it (the user sees both, side by side, and
changes the answer if they want another); don't repeat a decision's assumption in "assumptions".
"params" only uses the component's fields, with values of their type. Give confidence below 0.6 for anything
you guessed."""


@dataclass
class StepResult:
    answer: dict | None
    thinking: str = ""
    mode: str = ""
    usage: Usage = field(default_factory=Usage)
    errors: list[str] = field(default_factory=list)


def resolve_mode(provider: Provider, mode: str) -> str:
    if mode not in MODES or mode == "auto":
        if getattr(provider, "reasoning", False):
            return "native"
        return "two-pass" if hasattr(provider, "complete_text") else "one-pass"
    if mode == "two-pass" and not hasattr(provider, "complete_text"):
        return "one-pass"
    return mode


def _parse(text: str) -> tuple[dict | None, str]:
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        # Some models wrap JSON in prose or a fence: take the outermost object.
        start, end = text.find("{"), text.rfind("}")
        if start < 0 or end <= start:
            return None, "the answer wasn't JSON"
        try:
            value = json.loads(text[start:end + 1])
        except json.JSONDecodeError:
            return None, "the answer wasn't JSON"
    return (value, "") if isinstance(value, dict) else (None, "the answer wasn't a JSON object")


def think_then_answer(provider: Provider, task: str, inputs: str, answer_shape: str, mode: str = "auto",
                      think: bool = True) -> StepResult:
    """One step (B1): think in free text, then answer in `answer_shape`."""
    mode = resolve_mode(provider, mode) if think else "answer-only"
    result = StepResult(answer=None, mode=mode)
    answer_system = f"{task}\n\n{answer_shape}\n\nAnswer with the JSON object only."
    try:
        if mode == "two-pass":
            thought = provider.complete_text([  # type: ignore[attr-defined]
                {"role": "system", "content": f"{task}\n\n{THINK_INSTRUCTIONS}"},
                {"role": "user", "content": inputs}])
            result.thinking = thought.text.strip()
            result.usage.add(thought.usage)
            reply = provider.complete([
                {"role": "system", "content": answer_system},
                {"role": "user", "content": f"{inputs}\n\nYour reasoning so far:\n{result.thinking}\n\n"
                                            "Now write the answer, following your reasoning."}])
        elif mode == "one-pass":
            reply = provider.complete([
                {"role": "system", "content": f"{task}\n\n{THINK_INSTRUCTIONS}\n\n{answer_shape}\n\n"
                                              "Answer with one JSON object whose FIRST key is \"thinking\" (your "
                                              "reasoning, as a string), followed by the answer's keys."},
                {"role": "user", "content": inputs}])
        else:  # native, or an answer with no thinking (a repair)
            reply = provider.complete([
                {"role": "system", "content": answer_system},
                {"role": "user", "content": inputs}])
            result.thinking = reply.thinking
    except ProviderError as error:
        result.errors.append(str(error))
        return result
    result.usage.add(reply.usage)
    answer, problem = _parse(reply.text)
    if answer is None:
        result.errors.append(problem)
        return result
    if mode == "one-pass":
        result.thinking = str(answer.pop("thinking", ""))
    result.answer = answer
    return result


# --- Interpret ------------------------------------------------------------------------

def _clean_draft(answer: dict, build_types: list[str]) -> tuple[dict | None, list[str]]:
    """The draft's shape, cleaned: the C++ validator checks its contents."""
    brief = answer.get("brief")
    if not isinstance(brief, dict):
        return None, ["the answer has no brief"]
    brief["schema"] = "aige.brief/1"
    if build_types and brief.get("buildType") not in build_types:
        if len(build_types) == 1:
            brief["buildType"] = build_types[0]
        else:
            return None, [f"unknown build type {brief.get('buildType')!r}"]
    if not isinstance(brief.get("components"), dict):
        brief["components"] = {}
    confidence = {}
    for path, value in (answer.get("confidence") or {}).items() if isinstance(answer.get("confidence"), dict) else []:
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            confidence[str(path)] = round(min(max(float(value), 0.0), 1.0), 2)
    strings = lambda key: [" ".join(str(s).split())[:300] for s in answer.get(key) or [] if str(s).strip()][:12] \
        if isinstance(answer.get(key), list) else []
    decisions = []
    for d in answer.get("decisions") or [] if isinstance(answer.get("decisions"), list) else []:
        if not isinstance(d, dict) or not str(d.get("question", "")).strip():
            continue
        decisions.append({"question": " ".join(str(d["question"]).split())[:300],
                          "assumed": " ".join(str(d.get("assumed", "")).split())[:300],
                          "path": str(d.get("path", "") or "").strip()[:120],
                          "options": [" ".join(str(o).split())[:120] for o in (d.get("options") or [])
                                      if isinstance(d.get("options"), list) and str(o).strip()][:6]})
    return {"brief": brief, "confidence": confidence, "assumptions": strings("assumptions"),
            "questions": strings("questions"), "decisions": decisions[:10],
            "rationale": " ".join(str(answer.get("rationale", "")).split())[:400]}, []


def interpret_inputs(query: str, context: dict, reading: str, choices: dict | None = None) -> str:
    chosen = ""
    if choices:
        chosen = ("What the user chose in the form, alongside their words (Brief paths and values). These are "
                  "decided: keep each exactly as given in your Brief, read the words for everything else, and "
                  "where the words seem to contradict a choice, the choice wins (say so in assumptions):\n"
                  f"{json.dumps(choices, ensure_ascii=False)}\n\n")
    return (f"The request: {query.strip() or '(no words: build from the choices)'}\n\n{chosen}Reading: {reading}\n\n"
            f"What can be built (build types, their components with fields, scale tables, vocabularies):\n"
            f"{json.dumps(context, ensure_ascii=False)}")


def interpret(provider: Provider, query: str, context: dict, drafts: int = 3, mode: str = "auto",
              workers: int = 3, choices: dict | None = None, reading: str = "") -> dict:
    """Drafts up to len(READINGS) Briefs for `query`, in parallel."""
    build_types = [b.get("id") for b in context.get("buildTypes", []) if isinstance(b, dict) and b.get("id")]
    task = ("You turn a request for a game place (a village, a town, a castle...) into a Brief: the confirmed "
            "description of what to build, in the build type's own terms.")
    readings = READINGS[:max(1, min(drafts, len(READINGS)))]
    if reading:
        # One reading per request (the editor asks for each apart, and shows them as they come).
        readings = [r for r in READINGS if r[0] == reading] or readings[:1]

    def one(reading: tuple[str, str]) -> dict:
        step = think_then_answer(provider, task, interpret_inputs(query, context, reading[1], choices), BRIEF_SHAPE, mode)
        draft = {"reading": reading[0], "mode": step.mode, "thinking": step.thinking,
                 "usage": step.usage.model_dump(), "errors": list(step.errors)}
        if step.answer is not None:
            cleaned, problems = _clean_draft(step.answer, build_types)
            draft["errors"] += problems
            if cleaned:
                draft.update(cleaned)
        return draft

    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        results = list(pool.map(one, readings))
    usage = Usage()
    for r in results:
        usage.add(Usage(**r["usage"]))
    good = [r for r in results if "brief" in r]
    errors = [f"{r['reading']}: {e}" for r in results for e in r["errors"]]
    return {"ok": bool(good), "drafts": results, "errors": errors if not good else [], "warnings": errors if good else [],
            "usage": usage.model_dump()}


def repair_brief(provider: Provider, query: str, context: dict, brief: dict, problems: list[dict]) -> dict:
    """The one repair round (B1.5): the draft and its exact violations, back once."""
    build_types = [b.get("id") for b in context.get("buildTypes", []) if isinstance(b, dict) and b.get("id")]
    listed = "\n".join(f"- {p.get('where', '?')}: {p.get('message', '')}" for p in problems if isinstance(p, dict))
    inputs = (f"The request: {query.strip()}\n\nYour Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n"
              f"It breaks these rules:\n{listed}\n\nFix exactly these, change nothing else.\n\n"
              f"What can be built:\n{json.dumps(context, ensure_ascii=False)}")
    task = "You correct a Brief for a game place so that it follows its build type's rules."
    step = think_then_answer(provider, task, inputs, BRIEF_SHAPE, think=False)
    out = {"ok": False, "errors": list(step.errors), "usage": step.usage.model_dump()}
    if step.answer is not None:
        cleaned, problems_left = _clean_draft(step.answer, build_types)
        out["errors"] += problems_left
        if cleaned:
            out.update(cleaned)
            out["ok"] = True
    return out


# --- Plan (the Spec), 7.5 -------------------------------------------------------------

ROOT_SHAPE = """The top of the plan, as JSON:
{
  "quantity": {"buildings": {"count": <n>}, "area": {"area": <cells>}},
  "quality": {"layout": <as the Brief>, "condition": "...", "walled": "..."},
  "children": [
    {"id": "<short kebab-case id, unique>", "module": "<a zone, network or site module>",
     "quantity": {"<field>": <value of its type>}, "quality": {"<field>": <value>},
     "rel": [{"type": "<relation>", "to": "<another child's id>"}],
     "placement": "<one sentence: where it lies in the place, e.g. 'along the river's west bank, north of the market'>",
     "rationale": "<one or two sentences: why this zone, here, this size>"}
  ],
  "rationale": "<one or two sentences>"
}
Zones (wards) get "plots" (a count) and "area", and each ward its own "street-style" (a layout pattern: organic,
grid, radial, linear...): the place's main layout for most, another where the ward calls for it (a planned grid
for a merchant ward, radial round a temple, linear along the docks). Include the ground, the main road(s), the
river and walls when the Brief has them.
Other build types count in their own unit (the root's "counts" in the rules; its field replaces "buildings"): a
dungeon's zones are its levels and its parcels rooms ("rooms" on each level); an interior's zones are its floors
("level": ground, upper or cellar) and its parcels rooms; a region's are its biomes and the heartland holding its
sites; a camp counts tents, a wilderness its points of interest. Give each zone its count in its own count field."""

ZONE_SHAPE = """One zone's parcels, as JSON:
{
  "quantity": {"plots": {"count": <n>}, ...the zone's other quantity fields},
  "quality": {...the zone's quality fields},
  "children": [
    {"id": "<zone id>/<short name>", "module": "<a parcel or feature module this zone may contain>",
     "repeat": <how many parcels this template stands for>,
     "quantity": {"<field>": <value or [low, high]>}, "quality": {"<field>": <value, or a mix for variety>},
     "rationale": "<one sentence>"}
  ],
  "rationale": "<one or two sentences>"
}
Group alike parcels into one template with "repeat" (28 cottages are one child, not 28). The repeats of the
building parcels add up to the zone's plots (or, for other build types, the parcels of their unit, rooms or tents,
add up to the zone's own count field). A dungeon level has one entrance room; the last level holds the goal (a
boss arena, a treasure room); a floor of a building gets the rooms its kind needs (an inn: a tavern hall and a
kitchen below, guest rooms above)."""

PLAN_CHECK = """Before answering, check while thinking:
- do the children's areas fit within 85% of the parent's area?
- do the counts add up to the Brief's numbers (buildings, plots; rooms, tents or sites for other build types)?
- is every enabled component placed somewhere?
- does each zone follow its module's site preferences and the design rules?"""


# How the zones are laid out (place-layout.md §11.2): for every build type whose zones share one site.
ROOT_DESIGN = """Design the layout first, while thinking, as a designer of this kind of place would: what stands at
its heart, what lies at its edges, where the water, the routes in and the walls are, what each zone needs beside it
(a dock on the water, fields outside the walls, a camp's fire in its middle, a castle's keep in its inner ward), and
what should be kept apart. Then:
- zones are areas side by side on the site: each one contiguous, none inside another (never a zone that
  "encompasses" or "surrounds" others: name its own part instead);
- one zone per component, unless the Brief's count for it says more; the scale's usual number of zones is a guide,
  not a reason to add or split zones;
- say where each zone lies with relations ("rel"), only of these types: near, adjacent, along (a river, road or
  coast), far, north-of, south-of, east-of, west-of (of another child: a zone, the river, a road), in-north,
  in-south, in-east, in-west (to "site": a part of the whole map). "north-west of the river" is two relations.
  Every direction in the "placement" sentence is also in "rel"."""

SKETCH_SHAPE = """
And, because the site is given as a grid: "sketch": {"rows": [<16 strings of 16 characters, north at the top>],
"legend": {"<one capital letter>": "<a zone's id>"}}: the grid as given, each zone's cells written with its letter.
Each zone one contiguous patch, about as many cells as its area holds (see the cell size), on open land (".") or
over roads ("="), never on water ("~") or outside ("x"); leave some "." between zones where the place has open land."""


def plan_root(provider: Provider, brief: dict, context: dict, mode: str = "auto", examples: list | None = None,
              directions: dict | None = None, grid: dict | None = None) -> dict:
    """The root and its zones, networks and ground (place-interaction.md B4: thinks). `examples`: places this
    project built and the user kept (7.7, verified examples), for its style."""
    task = ("You plan a game place from its confirmed Brief: its zones (wards of a settlement, levels of a dungeon, "
            "floors of a building, a castle's baileys, a region's biomes, a farm's fields, a camp's areas), networks "
            "(roads, river, walls) and ground, each with its quantity and quality fields. " + PLAN_CHECK +
            "\n\n" + ROOT_DESIGN)
    shape = ROOT_SHAPE
    sketch = ""
    if grid and grid.get("rows"):
        shape = ROOT_SHAPE + SKETCH_SHAPE
        sketch = (f"THE SITE as a {len(grid['rows'])}x{len(grid['rows'])} grid, north at the top (each cell is about "
                  f"{grid.get('cell', 1)}x{grid.get('cell', 1)} tiles, so about {grid.get('cell', 1) ** 2} of a zone's area; "
                  f"{grid.get('note', '')}). Symbols: {json.dumps(grid.get('symbols', {}), ensure_ascii=False)}\n" +
                  "\n".join(grid["rows"]) + "\n\nSketch the zones on it (\"sketch\"), and make their relations agree "
                  "with where you sketched them.\n\n")
    shown = ""
    if examples:
        shown = ("Places this project built before and the user kept (follow their choices where this Brief is "
                 f"alike; the Brief decides where it differs):\n{json.dumps(examples, ensure_ascii=False)}\n\n")
    told = ""
    if directions and directions.get("answers"):
        told = ("The user's answers to the questions they were asked, in their words. They decide where things go: "
                "give each zone the relations (\"rel\") and the \"placement\" they ask for, and follow everything else "
                f"they say:\nThe questions: {json.dumps(directions.get('questions', []), ensure_ascii=False)}\n"
                f"The answers: {directions['answers']}\n\n")
    inputs = (f"The Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n" + told + sketch + shown +
              f"The modules and rules (the ontology's relevant part):\n{json.dumps(context, ensure_ascii=False)}")
    return _plan_result(think_then_answer(provider, task, inputs, shape, mode))


QUESTIONS_SHAPE = """The questions, as JSON:
{
  "questions": [
    {"text": "<one short question, answerable in a few words, with a suggestion: 'Where should the market be? (e.g. at the crossroads, in the middle)'>",
     "about": "<placement|size|parts|style|other>", "zone": "<the zone module it's about, if one>"}
  ]
}"""


def ask_questions(provider: Provider, query: str, brief: dict, context: dict, choices: dict | None = None) -> dict:
    """② Assets: the questions whose answers most change the layout, before the zones are planned.
    First where each main zone goes; then what the request leaves open that matters."""
    task = ("You confirm a game place with its user before it is planned. Ask 3 to 7 short questions whose answers "
            "most change how it is laid out. First, for the main zones the Brief turns on, where each should go: "
            "relative to the river, the roads, the compass and each other (one question may cover two zones). Then "
            "anything the request leaves open that changes the layout (what stands at the centre, how dense, what's "
            "outside the walls). Never ask what the Brief or the user's choices already decide. Each question gives "
            "a suggestion the user can accept.")
    chosen = f"What the user chose in the form:\n{json.dumps(choices, ensure_ascii=False)}\n\n" if choices else ""
    inputs = (f"The request: {query.strip()}\n\n" + chosen + f"The Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n"
              f"What the place can have (zones, networks):\n{json.dumps(context, ensure_ascii=False)}")
    step = think_then_answer(provider, task, inputs, QUESTIONS_SHAPE, think=False)
    out = {"ok": False, "errors": list(step.errors), "usage": step.usage.model_dump()}
    answer = step.answer
    if isinstance(answer, dict) and isinstance(answer.get("questions"), list) and answer["questions"]:
        out["ok"] = True
        out["answer"] = {"questions": [q for q in answer["questions"] if isinstance(q, (dict, str))][:8]}
    elif not out["errors"]:
        out["errors"] = ["the answer had no questions"]
    return out


def plan_zone(provider: Provider, brief: dict, zone: dict, siblings: list[dict], context: dict, mode: str = "auto") -> dict:
    """One zone's parcels (thinks: per-zone totals and qualities)."""
    task = ("You plan one zone of a game place: which parcels it holds, how many of each (as templates with "
            "\"repeat\"), and their fields, following the zone's own fields and the Brief. " + PLAN_CHECK)
    kinds = ""
    if isinstance(zone.get("kinds"), dict) and zone["kinds"]:
        kinds = ("The user set how many of these the zone holds; plan exactly that many of each (the repeats of "
                 "that module add up to it), and fill the rest of its count with other kinds as fits:\n"
                 f"{json.dumps(zone['kinds'], ensure_ascii=False)}\n\n")
    inputs = (f"The Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n"
              f"The zone:\n{json.dumps(zone, ensure_ascii=False)}\n\n" + kinds +
              f"The other zones (for balance):\n{json.dumps(siblings, ensure_ascii=False)}\n\n"
              f"What this zone may contain (the ontology's relevant part):\n{json.dumps(context, ensure_ascii=False)}")
    return _plan_result(think_then_answer(provider, task, inputs, ZONE_SHAPE, mode))


def repair_node(provider: Provider, node: dict, problems: list[dict], context: dict) -> dict:
    """The one repair round for a plan node: its violated rules, back once."""
    listed = "\n".join(f"- {p.get('where', '?')}: {p.get('message', '')}" for p in problems if isinstance(p, dict))
    task = "You correct one node of a game place's plan so that it follows its module's rules."
    inputs = (f"The node:\n{json.dumps(node, ensure_ascii=False)}\n\nIt breaks these rules:\n{listed}\n\n"
              f"Fix exactly these and keep everything else.\n\nIts module and rules:\n{json.dumps(context, ensure_ascii=False)}")
    shape = "The corrected node, as JSON, in the same form as given (its quantity, quality and children)."
    return _plan_result(think_then_answer(provider, task, inputs, shape, think=False))


REVISE_SHAPE = """The changes, as JSON:
{
  "now": "<where the parts the request is about are now, from WHERE (not from the plan's relations)>",
  "summary": "<one or two sentences to the user: what you changed, or why it can't be done>",
  "expect": [{"node": "<a zone id>", "type": "<relation>", "to": "<a zone id, or river / a road's id>"}],
  "brief": {"<a Brief path, e.g. scale.buildings.count>": <its new value>},
  "changes": [
    {"op": "move", "node": "<id>", "rel": [{"type": "<relation>", "to": "<a part's id, or river / road>"}]},
    {"op": "set", "node": "<id>", "quantity": {"<field>": <value>}, "quality": {"<field>": <value>}, "rel": [...]},
    {"op": "zone", "node": "<zone id>", "quantity": {...}, "quality": {...},
     "children": [<all of the zone's parcels afterwards, as in the plan: id, module, repeat, quantity, quality>]},
    {"op": "add", "node": {"id": "<new id>", "module": "<a zone, network or site module>", "quantity": {...},
                           "quality": {...}, "rel": [...], "children": [<its parcels, for a zone>]}},
    {"op": "remove", "node": "<id>"}
  ]
}
"move" says where a part goes, and nothing else: use it when the request is about place ("put X south of Y", "near
the river"). The relations there are: near, adjacent, along, far, north-of, south-of, east-of, west-of (from another
part, "to" its id or river / a road's id), and in-north, in-south, in-east, in-west (in that part of the map itself,
"to": "site"). Nothing else: "below" on the map is south-of, "above" north-of, "left" west-of, "right" east-of; a
compound direction is two relations ("north-west of the market" is north-of market-ward and west-of market-ward; "in
the north-west of the map" is in-north and in-west, both to "site"). "Where X is now" (put the farmland where the
residential ward is) is X's place in WHERE, said in these relations (its side of the river, its part of the map). To take one part out (the inn north of the market), "remove" its id and change nothing else: not its zone's
parcels, sizes or counts. "set" changes only the fields given. "zone" re-plans a zone's parcels: list all of them,
kept ones too, and only when the request is about what that zone holds.
"expect" lists every spatial fact the request asks for, one relation each ("the residential ward along the river on
its left side, the farmland to its left" is: residential-ward along river, residential-ward west-of river,
farmland-ring west-of residential-ward). A request about "the others" or "the rest" is about every zone it doesn't
name: give each of them its "move" and its facts ("the residential ward east of the river, the others west of it": the
residential ward along river and east-of river; every other zone west-of river). They are checked on the new layout;
one that doesn't hold is reported.
The request may change the settings too, as the user would in the Contents and Layout tabs:
- the place's main layout pattern: "brief": {"shape.layout": "<organic|grid|radial|linear-along-road|...>"};
- a zone's own pattern: "set" its quality "street-style"; its size: "set" its quantity "area" ({"area": cells}) or
  "plots" ({"count": n}, how many buildings it holds);
- the place's size: "brief": {"scale.buildings.count": [low, high]} or {"scale.class": "..."};
- a part turned on or off, or how many of it: "brief": {"components.<id>.enabled": true|false,
  "components.<id>.count": n, "components.<id>.density": "<none|rare|some|common|abundant>"}; a zone turned on
  also needs its "add" (a new zone in the plan), one turned off its "remove".
Leave out what the request doesn't ask for. An empty "changes" list (and no "brief"), with the reason in "summary",
when the request can't be met with these modules."""


WHERE_GUIDE = """WHERE says how the place is laid out now: each zone's area, centre and extent in cells, its
river bank and how many rows of it meet the river, where each other zone lies from it (north, south-west...), and
whether they touch; and a map, a character per few cells: a letter per zone (the legend), ~ the river, = a road,
. open land. x grows east (right), y grows south (down): "left" is west, "right" east, "above"/"up" north,
"below"/"down" south. "Along the river" is a strip beside it over the zone's length; "on the left side" of the river
is its west bank. Read the request against WHERE: a placement the request asks for that WHERE doesn't show isn't
there yet, whatever the plan's relations say."""


# Where a moved zone goes, reasoned step by step (place-layout.md §8.3): relations compose, so a
# zone placed by another inherits that zone's place on the axes the relation doesn't move along.
MOVE_REASONING = """Reason it out step by step in your thinking, for each zone the request moves:
1. The zone that moves, and what it is placed by (its references: zones, the river, a road, the map's edge).
   A zone named only as a reference stays where it is.
2. Where each reference is NOW, from WHERE (not from the plan's relations): its part of the map, its bank of the
   river, its side of the main road, what it touches.
3. Compose: a relation moves the zone along one axis only. "South of the temple ward" moves it south; across,
   east-west, it stays where the temple is: the temple on the river's east bank puts it on the east bank too,
   and in the east of the map if the temple is. "East of the docks" keeps the docks' side of an east-west road
   and their north or south part. "Near" or "beside" keeps the reference's side of everything. A compound
   ("south-east") inherits only where it points the way the reference already lies. The moved zone's own old
   place doesn't count: it is moving. What the request says itself ("across the river", "on the west bank")
   wins over what is inherited.
4. Check the land: is there free land there (the map in WHERE)? If not, say which zones the request didn't name
   give way.
5. Then write every relation that follows: the asked ones and the composed ones (e.g. east-of river), in the
   "move" and in "expect", and one "placement" sentence that says them. Give a zone one "move" and no other
   relations for it.

"""


def revise_plan(provider: Provider, brief: dict, plan: dict, result: dict, instruction: str, context: dict,
                mode: str = "auto", where: dict | None = None, stage: str = "", room: list | None = None) -> dict:
    """The user's change to a plan they've seen (place-interaction.md A3: revise before building)."""
    task = ("You revise a game place's plan as its user asks. Change only what the request is about; everything "
            "else stays as planned: never re-plan, resize or move a zone the request doesn't name. Use only the modules, fields and values the rules allow (a zone may hold only "
            "the parcels its rules list): when the request asks for something a module can't hold, do the nearest "
            "thing that is allowed and say so in the summary. The Brief's building count bounds the total: when "
            "adding buildings, take them from elsewhere or raise scale.buildings.count. " + PLAN_CHECK +
            "\n\n" + WHERE_GUIDE)
    where_text = ""
    if where:
        layout_map = "\n".join(where.get("map", []))
        rest = {k: v for k, v in where.items() if k != "map"}
        where_text = f"WHERE (the layout now):\n{json.dumps(rest, ensure_ascii=False)}\n\nThe map:\n{layout_map}\n\n"
    at_layout = ""
    if stage == "plan":
        at_layout = ("The zones are laid out and fixed (the user sets where zones lie, their borders and sizes on the "
                     "layout page, not here). Change only what's inside the zones: their parcels, counts and styles. "
                     "Don't move, resize, add or remove a zone, and give no relations for zones; if the request asks "
                     "for that, say in the summary that it's done on the layout page.\n\n")
        if room:
            at_layout += ("Each zone's land and about how many parcels it holds (\"holds about\"; \"has\": placed now):\n"
                          f"{json.dumps(room, ensure_ascii=False)}\n"
                          "When the request asks a zone for more than it holds, first fit more in: smaller parcels or "
                          "building kinds (cottages for houses), a denser street-style (a grid), less open land between "
                          "plots. Ask for no more than it then holds, and say in the summary how many fit and that the "
                          "zone is made bigger on the layout page.\n\n")
    if stage == "layout":
        at_layout = ("The plan is at its layout: only the zones, networks and where they go are planned so far; what's "
                     "inside each zone comes later. Change where zones lie (\"move\" with relations, and their "
                     "\"placement\" sentence), their sizes, add or remove zones; don't plan parcels.\n\n" + MOVE_REASONING)
    inputs = (f"The user asks:\n{instruction}\n\n" + at_layout + where_text +
              f"The Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n"
              f"The plan (each zone with its parcels; id: what to name in a change):\n{json.dumps(plan, ensure_ascii=False)}\n\n"
              f"How it was laid out (buildings placed per zone, and what didn't fit):\n{json.dumps(result, ensure_ascii=False)}\n\n"
              f"The modules and rules (the ontology's relevant part):\n{json.dumps(context, ensure_ascii=False)}")
    step = think_then_answer(provider, task, inputs, REVISE_SHAPE, mode)
    return _revision_result(step)


def _revision_result(step: StepResult) -> dict:
    out = {"ok": False, "mode": step.mode, "thinking": step.thinking, "errors": list(step.errors),
           "usage": step.usage.model_dump()}
    answer = step.answer
    if answer is None:
        return out
    changes = answer.get("changes", [])
    if not isinstance(changes, list):
        out["errors"].append("changes is a list")
        return out
    answer["changes"] = [c for c in changes if isinstance(c, dict) and c.get("op") in ("move", "set", "zone", "add", "remove")]
    expect = answer.get("expect", [])
    answer["expect"] = [e for e in expect if isinstance(e, dict) and e.get("node") and e.get("type") and e.get("to")] if isinstance(expect, list) else []
    if not isinstance(answer.get("brief", {}), dict):
        answer["brief"] = {}
    answer["summary"] = str(answer.get("summary", "")).strip()
    issues = answer.get("issues", [])
    answer["issues"] = [str(i).strip() for i in issues if str(i).strip()] if isinstance(issues, list) else []
    out["answer"] = answer
    out["ok"] = True
    return out


REVIEW_SHAPE = """Your review, as JSON:
{
  "thinking": "<what you see on the map, zone by zone, against the plan and the checks>",
  "issues": ["<one thing that looks wrong or missing, in a short sentence>", ...],
  "summary": "<one or two sentences to the user: what you'd change, or that it looks right>",
  "brief": {...as for a revision, usually empty},
  "changes": [...the revision's changes, as below; empty when it looks right]
}
""" + REVISE_SHAPE.split("\n", 1)[1]


SCENE_TASK = ("You check a game place as it was built into the scene, drawn the way the game draws it (an "
              "isometric view; plain coloured shapes are placeholders where the project has no art yet). Look for "
              "what a player would see as wrong: buildings standing on roads or water, things floating, cut off or "
              "far too big or small for their plot, crowded or empty stretches, roads that end nowhere, a part the "
              "Brief asked for that isn't there. Placeholders themselves aren't a problem to fix in the plan. The "
              "checks below say what the build verified. Propose plan changes only for real problems; when it "
              "looks right, say so and change nothing.")


def review_layout(provider: Provider, image: dict, brief: dict, plan: dict, result: dict, verify: list,
                  context: dict, kind: str = "map") -> dict:
    """The AI looks at the solved map (7.6) or at the built scene (7.7, kind "scene") and proposes changes."""
    task = SCENE_TASK if kind == "scene" else (
            "You check a game place's layout, drawn as a map with a legend, against its Brief and its plan. Look "
            "for what a player would notice: a zone without buildings, a ward whose main building is missing (a "
            "temple ward without its temple), empty stretches, things in odd places, a part the Brief asked for "
            "that isn't there. The checks below say what the layout placed against what was planned. Propose "
            "only changes the rules allow, and only for real problems; when it looks right, say so and change "
            "nothing.")
    inputs = (f"The Brief:\n{json.dumps(brief, ensure_ascii=False)}\n\n"
              f"The plan:\n{json.dumps(plan, ensure_ascii=False)}\n\n"
              f"How it was laid out:\n{json.dumps(result, ensure_ascii=False)}\n\n"
              f"The checks (asked, planned, built):\n{json.dumps(verify, ensure_ascii=False)}\n\n"
              f"The modules and rules:\n{json.dumps(context, ensure_ascii=False)}")
    step = StepResult(answer=None, mode="vision")
    try:
        reply = provider.complete([
            {"role": "system", "content": f"{task}\n\n{REVIEW_SHAPE}\n\nAnswer with the JSON object only."},
            {"role": "user", "content": [{"type": "text", "text": inputs}, {"type": "text", "text": "The map:"}, image]}])
    except ProviderError as error:
        step.errors.append(str(error))
        return _revision_result(step)
    step.usage.add(reply.usage)
    answer, problem = _parse(reply.text)
    if answer is None:
        step.errors.append(problem)
    else:
        step.thinking = str(answer.pop("thinking", "")) or reply.thinking
        step.answer = answer
    return _revision_result(step)


def _plan_result(step: StepResult) -> dict:
    out = {"ok": False, "mode": step.mode, "thinking": step.thinking, "errors": list(step.errors),
           "usage": step.usage.model_dump()}
    answer = step.answer
    if answer is None:
        return out
    if not isinstance(answer.get("children", []), list):
        out["errors"].append("children is a list of nodes")
        return out
    for key in ("quantity", "quality"):
        if key in answer and not isinstance(answer[key], dict):
            answer[key] = {}
    answer["children"] = [c for c in answer.get("children", []) if isinstance(c, dict) and c.get("module")]
    sketch = answer.get("sketch")
    if sketch is not None and not (isinstance(sketch, dict) and isinstance(sketch.get("rows"), list)
                                   and isinstance(sketch.get("legend"), dict)):
        answer.pop("sketch", None)  # a sketch it couldn't write is no sketch: the layout goes by relations alone
    out["answer"] = answer
    out["ok"] = True
    return out
