"""The AI Planner's calls (development_plan.md 22, docs/ai/ai-planner.md §5): one step per
endpoint, each the protocol of place-interaction.md Part B — think in free text, answer in
a strict JSON shape, check the answer, repair once with the exact problems, then report a
problem rather than guess. The engine (engine/planner/GamePlan) assembles the inputs from
the plan's documents and the story graph, validates every answer again, applies it, and
records it as a decision; these checks mirror the engine's so most answers pass first time.
"""

from __future__ import annotations

import json
import re
from typing import Any, Callable

from . import progress
from .shapes import coerce, parse_shape
from .models import Usage
from .places import StepResult, think_then_answer
from .providers import Provider

PLANNER_ROLE = """You are the AI Planner of a 2D game engine: you design a whole game (its story, characters,
quests, places and systems) for the engine to build. Everything you write is data the engine checks and runs:
use only the ids, classes, properties and values you are given, keep ids as slugs (lowercase, dashes), and
keep every text short and concrete. Never write markdown."""

THINK = """Think it through in plain text first (no JSON yet): what is asked, what the inputs fix, what you
must decide, how the parts fit together, and then check your plan against every rule stated above."""


def _dump(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=1)


def _merged(previous: Any, repaired: Any, key: str, problems: list[str]) -> Any:
    """A repair merged into the answer it repairs: a model asked to fix a few entries often answers with only those. In
    each list of entries with an id ("who" or "id") under the answer's key, an entry the repair left out comes back (in
    its place) unless the problems named it ("props[3]…": it may have been removed on purpose); the repair's entries
    replace theirs, and anything else is the repair's."""
    if not isinstance(previous, dict) or not isinstance(repaired, dict):
        return repaired
    old, new = previous.get(key), repaired.get(key)
    if not isinstance(old, dict) or not isinstance(new, dict):
        return repaired
    named = {(m.group(1), int(m.group(2))) for p in problems for m in re.finditer(r"(\w+)\[(\d+)\]", str(p))}
    out = dict(new)
    for field, before in old.items():
        after = new.get(field)
        if not isinstance(before, list) or not isinstance(after, list) or not all(isinstance(e, dict) for e in after):
            continue
        ident = next((k for k in ("who", "id") if before and all(isinstance(e, dict) and e.get(k) for e in before)), None)
        if ident is None:
            continue
        fixed = {str(e.get(ident)): e for e in after if e.get(ident)}
        merged = []
        for i, e in enumerate(before):
            if str(e[ident]) in fixed:
                merged.append(fixed.pop(str(e[ident])))
            elif (field, i) not in named:
                merged.append(e)
        out[field] = merged + [e for e in after if not e.get(ident) or str(e.get(ident)) in fixed]
    return {**repaired, key: out}


def _step(provider: Provider, task: str, inputs: dict, shape: str, key: str,
          check: Callable[[dict], list[str]], problems: list[str] | None = None) -> dict:
    """One planner step: think, answer, check, repair once. Returns {ok, <key>, rationale, usage, errors}."""
    usage = Usage()
    replay = inputs.get("replay")
    if isinstance(replay, dict):
        # An answer the model already gave (from the engine's call log), re-applied: no model
        # call, only put into shape and checked, as a fresh answer would be.
        answer = coerce(replay, parse_shape(shape))
        value = answer.get(key, answer if key == "*" else None)
        if value is None:
            return {"ok": False, "errors": [f"the answer has no '{key}'"], "usage": usage.model_dump()}
        out = {"ok": True, key: value, "rationale": str(answer.get("rationale", ""))[:600], "usage": usage.model_dump()}
        if found := check(answer):
            out["problems"] = found
        return out
    previous = inputs.get("answer")
    text_inputs = _dump({k: v for k, v in inputs.items() if k not in ("answer", "problems")})
    if problems and isinstance(previous, dict):
        # A quick fix: the answer back with its problems, changed only where they say. No
        # thinking pass and no rewrite: fast, and what was right stays as it was.
        progress.repairing()
        text_inputs += ("\n\nYour answer:\n" + _dump(previous) +
                        "\n\nIt has these problems. Answer with the same JSON, changed ONLY to fix them (remove or correct "
                        "the parts they name; keep everything else exactly as it is):\n- " + "\n- ".join(problems))
        result: StepResult = think_then_answer(provider, f"{PLANNER_ROLE}\n\n{task}", text_inputs, shape, think=False,
                                               think_instructions=THINK)
    else:
        if problems:
            text_inputs += ("\n\nYour last answer had these problems; fix exactly them:\n- " + "\n- ".join(problems))
        result = think_then_answer(provider, f"{PLANNER_ROLE}\n\n{task}", text_inputs, shape, think_instructions=THINK)
    usage.add(result.usage)
    if result.answer is None:
        return {"ok": False, "errors": result.errors or ["no answer"], "usage": usage.model_dump()}
    # Into the shape the step asked for (a list where one text goes, null for an object): a small
    # model's answer is usable as it is, without sending it back.
    example = parse_shape(shape)
    answer = coerce(result.answer, example)
    if problems and isinstance(previous, dict):
        answer = _merged(previous, answer, key, problems)  # a quick fix that answered with only what it fixed
    found = check(answer)
    if found:
        progress.repairing()
        repair = think_then_answer(provider, f"{PLANNER_ROLE}\n\n{task}",
                                   text_inputs + "\n\nYour answer:\n" + _dump(answer) +
                                   "\n\nIt has these problems; answer again with them fixed:\n- " + "\n- ".join(found),
                                   shape, think=False, think_instructions=THINK)
        usage.add(repair.usage)
        if repair.answer is not None:
            answer = _merged(answer, coerce(repair.answer, example), key, found)
            found = check(answer)
    value = answer.get(key, answer if key == "*" else None)
    if value is None:
        return {"ok": False, "errors": [f"the answer has no '{key}'"], "usage": usage.model_dump()}
    out = {"ok": True, key: value, "rationale": str(answer.get("rationale", ""))[:600], "usage": usage.model_dump()}
    if found:
        out["problems"] = found  # the engine checks again and sends it back once more if they matter
    return out


DECOMPOSE_TASK = """You are an AI Planner. Decompose a user's revision request into a sequence of atomic, executable tasks.
Each task should be a single, clear instruction that can be executed independently by the planner's revise step.

Rules:
- Split on logical boundaries: "then", "and then", "after that", "afterwards", "next", "also", ",", ";"
- Each task must be self-contained and executable by the revise step alone
- Preserve the original intent and order
- Don't split atomic actions (e.g., "edit s1-01's goal" is one task)
- Return ONLY the tasks list, no extra commentary

Example:
Input: "Split s1-01 into two beats, then move s1-02 to s1-03, then update the act summary"
Output: ["Split s1-01 into two beats", "Move s1-02 to s1-03", "Update the act summary"]

Input: "Make Jory a fisherman and update his dialogue"
Output: ["Make Jory a fisherman", "Update Jory's dialogue"]

Input: "Fix the east gate and repair the bridge"
Output: ["Fix the east gate", "Repair the bridge"]"""

DECOMPOSE_SHAPE = """{"tasks": ["task 1", "task 2", ...], "rationale": ""}"""


def decompose_request(provider: Provider, instruction: str) -> dict:
    """Decompose a natural language instruction into atomic tasks using LLM."""
    inputs = {"instruction": instruction}
    return _step(provider, DECOMPOSE_TASK, inputs, DECOMPOSE_SHAPE, "tasks",
                 lambda a: [] if isinstance(a.get("tasks"), list) else ["'tasks' must be a list"], None)


def plan_revise_chained(provider: Provider, inputs: dict) -> dict:
    """Decompose a revise request and execute each task as a separate revise step, flattening the
    edits in order (the engine applies them one after another, so the last state is the sum)."""
    instruction = inputs.get("instruction", "")
    if not instruction.strip():
        return {"ok": False, "errors": ["Say what to change"]}

    # Step 1: Decompose into atomic tasks.
    decomp = decompose_request(provider, instruction)
    if not decomp.get("ok"):
        return decomp
    tasks = decomp.get("tasks", [])
    if not tasks:
        return {"ok": True, "edits": [], "tasks": [], "results": [], "rationale": "nothing to change"}

    # Step 2: Run each task in turn; apply its edits to the working copy so the next task sees them.
    all_edits: list[dict] = []
    results = []
    context = dict(inputs)
    context.pop("instruction", None)
    docs = {k: json.loads(json.dumps(v)) for k, v in (context.get("documents") or {}).items()}
    for i, task in enumerate(tasks):
        task_inputs = dict(context)
        task_inputs["instruction"] = task
        task_inputs["documents"] = docs
        result = plan_revise_one(provider, task_inputs)
        edits = result.get("edits") or []
        # Apply this task's edits to the working copy and collect them in order.
        for e in edits:
            all_edits.append(e)
            _apply_edit(docs, e)
        results.append({"task": task, "ok": bool(result.get("ok")), "edits": edits,
                        "error": " ".join(result.get("errors") or []) or None})
        if not result.get("ok"):
            break  # a task that can't be done stops the chain (the ones before it stay)

    return {
        "ok": all(r["ok"] for r in results),
        "edits": all_edits,
        "tasks": tasks,
        "results": results,
        "rationale": " ".join(f"{i + 1}. {r['task']}" for i, r in enumerate(results)),
    }


def plan_revise(provider: Provider, inputs: dict) -> dict:
    """A revise from a panel: first decomposed into tasks, then each run as its own revise pass (the
    edits flattened in order). The build tabs (scope) keep the direct single pass."""
    if inputs.get("scope"):
        return _revise_step(provider, inputs)
    return plan_revise_chained(provider, inputs)


def _apply_edit(docs: dict, e: dict) -> None:
    """Apply one edit to the working copy of the documents between chained tasks: a field edit, or a
    whole document renamed, copied, made or removed."""
    op = e.get("op", "replace")
    doc = e.get("doc")
    if op == "rename":
        to = e.get("to")
        if doc in docs and to:
            moved = docs.pop(doc)
            if isinstance(moved, dict) and "id" in moved:
                moved["id"] = to.rsplit("/", 1)[-1]
            docs[to] = moved
    elif op == "copy":
        to = e.get("to")
        if doc in docs and to:
            import copy as _copy
            copied = _copy.deepcopy(docs[doc])
            if isinstance(copied, dict) and "id" in copied:
                copied["id"] = to.rsplit("/", 1)[-1]
            docs[to] = copied
    elif op == "create":
        if doc and isinstance(e.get("value"), dict):
            docs[doc] = dict(e["value"])
    elif op == "delete":
        docs.pop(doc, None)
    else:
        pointer = e.get("pointer", "")
        if doc in docs and isinstance(pointer, str) and pointer.startswith("/"):
            _apply_pointer(docs[doc], pointer, e.get("value"))


def _apply_pointer(doc: Any, pointer: str, value: Any) -> None:
    """Apply one JSON pointer edit to a document in place (for the working copy between chained tasks)."""
    parts = [p.replace("~1", "/").replace("~0", "~") for p in pointer.split("/")[1:]]
    target = doc
    for p in parts[:-1]:
        target = target[int(p)] if isinstance(target, list) else target[p]
    last = parts[-1]
    if isinstance(target, list):
        if last == "-":
            target.append(value)
        else:
            target[int(last)] = value
    else:
        target[last] = value


def _get(data: dict, path: str):
    """A dotted path into nested dicts ("setting.era"), or None."""
    for key in path.split("."):
        if not isinstance(data, dict) or key not in data:
            return None
        data = data[key]
    return data


def _vocab(inputs: dict, name: str) -> list[str]:
    return list((inputs.get("vocab") or {}).get(name, []))


# --- ① Concept ------------------------------------------------------------------------------

READINGS = {
    "literal": "Read the request literally: decide only what it says or clearly implies; leave the rest at the genre's "
               "usual values and list them in assumptions.",
    "typical": "Read the request as an experienced game designer would: what a game like this typically is, sized to "
               "what was asked.",
    "generous": "Read the request generously: a rich, memorable version of what was asked, never contradicting it.",
}

CONCEPT_TASK = """Set "mechanics" (small, medium or extensive: how deep the game's mechanics are, apart from its length) from the request.
① Concept: read a request for a game and fill its Game Brief.
{reading}
Rules: genre is one of the genres given (its id); every enum field takes one of its options; numbers stay in
range; playTime is in hours; tone is a list of tone words from its options; write a title, a logline (one
sentence, at most 40 words) and a synopsis (one paragraph, 4-6 sentences: who the hero is, what they want, what
stands in their way, the turn in the middle, how it may end) even if the request has none; setting.world is a short
description of the world.
An open world ("open-world", "open world", several villages joined by roads to roam between): worldShape
"open-region", placeKinds counted from the request, and world.spacing (1-10: 1 close neighbours, 3 a walk apart,
7 or more distant, joined by long roads), world.settlementSize (small, medium, big, huge: the size most places are)
and world.landform (inland, coastal, island, archipelago, continent) when the request says or implies them.
Fill only what the request decides or strongly implies; the engine fills the rest from the genre's profile.
confidence: per field path, how sure you are (0-1). decisions: questions the user should answer, each with the
answer you assumed and the field path it sets."""

CONCEPT_SHAPE = """{"brief": {"title": "", "logline": "", "synopsis": "", "genre": "<genre id>", "perspective": "isometric|top-down",
  "playTime": 3, "setting": {"world": "", "era": "", "genreFlavour": ""}, "tone": [""], "themes": [""],
  "playerCharacter": {"kind": "fixed|created", "name": "", "description": ""}, "mustInclude": [""], ...any other
  schema fields the request decides, nested by their dotted paths},
 "confidence": {"<dotted path>": 0.9}, "assumptions": [""],
 "decisions": [{"question": "", "assumed": "", "path": "<dotted path>"}], "rationale": ""}"""


def plan_concept(provider: Provider, inputs: dict) -> dict:
    """The whole answer: the brief, each field's confidence, assumptions, the decisions to ask."""
    genres = [g.get("id") for g in (inputs.get("schema") or {}).get("genres", [])]

    def check(answer: dict) -> list[str]:
        brief = answer.get("brief")
        if not isinstance(brief, dict):
            return ["'brief' must be an object"]
        problems = []
        if genres and brief.get("genre") not in genres:
            problems.append(f"genre must be one of {', '.join(genres)}")
        for key in ("title", "logline"):
            if not str(brief.get(key, "")).strip():
                problems.append(f"{key} is empty")
        # Every enum field takes one of its options ("hub" is a world shape, not a story pattern).
        for field in (inputs.get("schema") or {}).get("fields", []):
            options = field.get("options")
            if not options:
                continue
            value = _get(brief, field.get("path", ""))
            if value is None:
                continue
            values = value if field.get("type") == "enumSet" and isinstance(value, list) else [value]
            bad = [v for v in values if v not in options]
            if bad:
                problems.append(f"{field.get('path')}: {', '.join(map(str, bad))} isn't one of {', '.join(options)}")
        return problems

    task = CONCEPT_TASK.format(reading=READINGS.get(inputs.get("reading", "typical"), READINGS["typical"]))
    result = _step(provider, task, inputs, CONCEPT_SHAPE, "*", check, inputs.get("problems"))
    if not result.get("ok"):
        return result
    answer = result.pop("*")
    return {**result, "brief": answer.get("brief", {}), "confidence": answer.get("confidence", {}),
            "assumptions": answer.get("assumptions", []), "decisions": answer.get("decisions", [])}


# --- ② Outline ------------------------------------------------------------------------------

OUTLINE_TASK = """② Outline: draft the story, the cast and the places together, in one go, so they fit.
- story: the logline, a one-page synopsis, the structure template's acts in order (one paragraph each, with the
  template beats each act holds), the storylines (main, and side lines by name), the endings (one per ending the
  Brief asks for).
- cast: every character with id character/<slug>, name, importance (major, supporting, minor), narrativeRoles and
  gameplayRoles, each only from its own vocabulary (vocab.NarrativeRole, vocab.GameplayRole: "companion" is a
  gameplay role, "mentor" a narrative one), and one line. Exactly one is the player ("player": true). Every narrative
  role the template needs (template.roles) is someone's. Match the Brief's cast counts roughly.
- factions (if the Brief has any): id faction/<slug>, name, line; a character may name its faction.
- places: every location with id location/<slug>, name, kind (one of buildTypes), parent (a place in the list,
  for an interior inside a settlement), one line; the first is where the game starts. Match the Brief's place kinds.
- connections: how places join (road, door, stairs, portal), every place reachable from the first; a gate names
  what opens it (a quest stage, a key), if anything.
- world (an open world: the Brief's worldShape is open-region, or the game has several outdoor places joined by roads, e.g. "a small
  open-world RPG with several villages and dungeons"): its name, landform (inland, coastal, island, archipelago,
  continent), spacing (1-10: how far apart its places are, 1 close neighbours, 9 far apart) and the size most places
  have (small, medium, big, huge): the Brief's world.landform, world.spacing and world.settlementSize when it has them. Then every outdoor place (a settlement, harbour, castle, camp, farmland) is
  a section of it (a forest, a lake or mountains aren't places: the world grows them, and "where" can name them): "section": true, its "size", and "where" when the story places it ({"type": "near|by", "to":
  "sea|lake|river|forest|mountains|<another place's id>"}). Sections are joined by the world's roads: no connection
  between two sections. Every dungeon, cave, crypt or interior is a place of its own with a parent section, entered
  through a gateway: its connection from the section has kind "entrance" (or door/stairs) and a "gateway":
  {"kind": "<one of gatewayKinds' ids (gateway-cave, or just cave): a cave mouth, dungeon stairs, a crypt door, a mine shaft, a tent, a portal, a
  barrow, a ruin, a well, a ship's gangway...; or building-door for an interior entered by one of the place's own
  buildings>", "at": "<where in or near the parent, in words: in the hills north of the village, in the graveyard, at
  the cliff by the sea>"}. Anything that is a scene of its own can be behind a gateway (a fortune-teller's tent, a
  wizard's portal, a smugglers' ship).
  Not an open world: no "world", no "section".
Respect the Brief's audience, tone, setting, mustInclude and avoid."""

OUTLINE_SHAPE = """{"outline": {
  "story": {"logline": "", "synopsis": "", "structure": "<template id>",
    "acts": [{"id": "act-1", "purpose": "setup", "summary": "", "beats": ["inciting-incident"]}],
    "storylines": [{"id": "main", "kind": "main", "summary": ""}],
    "endings": [{"id": "end-a", "summary": "", "conditions": ""}]},
  "cast": [{"id": "character/aldric", "name": "Aldric", "importance": "major", "player": false,
    "narrativeRoles": ["antagonist"], "gameplayRoles": ["boss"], "faction": "", "line": ""}],
  "factions": [{"id": "faction/wardens", "name": "", "line": ""}],
  "places": [{"id": "location/riverford", "name": "Riverford", "kind": "settlement", "parent": "", "line": "",
    "section": true, "size": "small", "where": {"type": "by", "to": "river"}}],
  "connections": [{"from": "location/riverford", "to": "location/crypt", "kind": "entrance", "gate": "",
    "gateway": {"kind": "crypt", "at": "in the graveyard behind the chapel"}}],
  "world": {"name": "", "landform": "inland", "spacing": 3, "settlementSize": "small"}},
 "rationale": ""}"""


def check_outline(outline: Any, inputs: dict) -> list[str]:
    if not isinstance(outline, dict):
        return ["'outline' must be an object"]
    problems: list[str] = []
    cast = outline.get("cast") or []
    places = outline.get("places") or []
    if not (outline.get("story") or {}).get("acts"):
        problems.append("story.acts is empty")
    if sum(1 for c in cast if c.get("player")) != 1:
        problems.append('exactly one cast member is the player ("player": true)')
    for c in cast:
        if not str(c.get("id", "")).startswith("character/"):
            problems.append(f"cast id {c.get('id')!r} should be character/<slug>")
    vocab = inputs.get("vocab") or {}
    for key, scheme in (("narrativeRoles", "NarrativeRole"), ("gameplayRoles", "GameplayRole")):
        allowed = vocab.get(scheme)
        if not allowed:
            continue
        for c in cast:
            for r in c.get(key) or []:
                if r not in allowed:
                    problems.append(f"{c.get('id')} {key}: {r!r} isn't one of {', '.join(allowed)}")
    roles = {r for c in cast for r in c.get("narrativeRoles", [])}
    for role in (inputs.get("template") or {}).get("roles", []):
        if role not in roles:
            problems.append(f"nobody fills the {role} role")
    kinds = set(inputs.get("buildTypes") or [])
    ids = {p.get("id") for p in places}
    for p in places:
        if not str(p.get("id", "")).startswith("location/"):
            problems.append(f"place id {p.get('id')!r} should be location/<slug>")
        if kinds and p.get("kind") not in kinds:
            problems.append(f"place {p.get('id')} kind {p.get('kind')!r} isn't one of {', '.join(sorted(kinds))}")
        if p.get("parent") and p["parent"] not in ids:
            problems.append(f"place {p.get('id')} parent {p['parent']} isn't a place")
    for c in outline.get("connections") or []:
        if c.get("from") not in ids or c.get("to") not in ids:
            problems.append(f"connection {c.get('from')} → {c.get('to')} names a place that isn't listed")
    # An open world: its sections outdoor places; what's not a section is entered from one through a gateway.
    world = outline.get("world")
    if isinstance(world, dict) and world:
        by_id = {p.get("id"): p for p in places}
        sections = {p.get("id") for p in places if p.get("section")}
        if not sections:
            problems.append("an open world needs its outdoor places marked \"section\": true")
        # What's reached from a section: by connections (a dungeon's second level through its first) or as a child.
        reached = set(sections)
        grew = True
        while grew:
            grew = False
            for c in outline.get("connections") or []:
                for a, b in ((c.get("from"), c.get("to")), (c.get("to"), c.get("from"))):
                    if a in reached and b not in reached:
                        reached.add(b)
                        grew = True
            for p in places:
                if p.get("parent") in reached and p.get("id") not in reached:
                    reached.add(p.get("id"))
                    grew = True
        if "world" in {p.get("kind") for p in places}:
            problems.append("'world' isn't a place's kind: the world holds the sections")
        for p in places:
            if p.get("section") and p.get("size") and p["size"] not in ("small", "medium", "big", "huge"):
                problems.append(f"place {p.get('id')} size {p['size']!r} isn't small, medium, big or huge")
            if p.get("section") or p.get("id") not in ids or p.get("id") in reached:
                continue
            problems.append(f"place {p.get('id')} isn't a section: it needs a connection (an entrance with a gateway) from a section or a place inside one")
        for c in outline.get("connections") or []:
            if c.get("from") in sections and c.get("to") in sections:
                problems.append(f"connection {c.get('from')} → {c.get('to')} joins two sections: the world's roads join them, leave it out")
        spacing = world.get("spacing")
        if spacing is not None and (not isinstance(spacing, (int, float)) or not 1 <= spacing <= 10):
            problems.append(f"world.spacing {spacing!r} isn't a number from 1 to 10")
        if world.get("settlementSize") and world["settlementSize"] not in ("small", "medium", "big", "huge"):
            problems.append(f"world.settlementSize {world['settlementSize']!r} isn't small, medium, big or huge")
        # A gateway's kind: one the engine draws (gatewayKinds, with or without "gateway-"), or a building's own door.
        known = {k.get("id") for k in inputs.get("gatewayKinds") or [] if isinstance(k, dict)}
        if known:
            for c in outline.get("connections") or []:
                gw = c.get("gateway")
                if not isinstance(gw, dict) or not gw.get("kind"):
                    continue
                k = str(gw["kind"])
                if k != "building-door" and k not in known and f"gateway-{k}" not in known:
                    problems.append(f"connection {c.get('from')} → {c.get('to')} gateway kind {k!r} isn't one of gatewayKinds (or building-door)")
    return problems


def plan_outline(provider: Provider, inputs: dict) -> dict:
    return _step(provider, OUTLINE_TASK, inputs, OUTLINE_SHAPE, "outline", lambda a: check_outline(a.get("outline"), inputs),
                 inputs.get("problems"))


# --- ③ Story --------------------------------------------------------------------------------

STORY_TASK = """③ Story: write the full story with acts, chapters, and scenes. Include the structure template's acts in order, each split into chapters with dramatic questions, and give each chapter its scenes as scene cards. About sceneTarget scenes total.
A scene card: goal, conflict, outcome; value (a McKee value from vocab.Value) with chargeBefore and chargeAfter
(-1 to 1; a kernel scene turns its value); mode (vocab.SceneMode: cutscene, dialogue, gameplay, environmental,
discoverable, ambient); kernel (true for the main line's turning points); beat (the template beat it carries, if
any, e.g. "inciting-incident", "climax"); location (a place id); participants [{who: character id, role: agent |
patient | witness}] (the player's character takes part in most); needsParts (what the place must contain, e.g.
"altar", "cellar"); branch (only for scenes on a branch after a choice: the branch name; such scenes come between
two main-line scenes). Use the cast's and places' ids; a scene may need a new minor character or place: give it
an id (character/<slug>, location/<slug>) and the engine adds it. Earlier acts' chapters are given: continue them."""

STORY_SHAPE = """{"story": {"acts": [{"id": "act-1", "purpose": "setup", "summary": "", "beats": ["inciting-incident"],
    "chapters": [{"id": "ch-1", "question": "", "summary": "",
    "scenes": [{"id": "s-1", "mode": "dialogue", "kernel": true, "beat": "", "goal": "", "conflict": "", "outcome": "",
      "value": "truth-lie", "chargeBefore": -1, "chargeAfter": 1, "location": "location/x",
      "participants": [{"who": "character/x", "role": "agent"}], "needsParts": [], "branch": ""}]}]}},
 "rationale": ""}"""


def plan_act(provider: Provider, inputs: dict) -> dict:
    modes = set(_vocab(inputs, "SceneMode"))

    def check(answer: dict) -> list[str]:
        story = answer.get("story")
        if not isinstance(story, dict) or not story.get("acts"):
            return ["'story' needs acts"]
        problems = []
        for act in story["acts"]:
            for ch in act.get("chapters", []):
                if not ch.get("scenes"):
                    problems.append(f"chapter {ch.get('id')} has no scenes")
                for s in ch.get("scenes", []):
                    if modes and s.get("mode") not in modes:
                        problems.append(f"scene {s.get('id')} mode {s.get('mode')!r} isn't one of {', '.join(sorted(modes))}")
        return problems

    return _step(provider, STORY_TASK, inputs, STORY_SHAPE, "story", check, inputs.get("problems"))


SCENE_TASK = """③ Story, one scene: write the scene from its card, against the world as it stands (world: who is
alive, where, their relationships, goals and knowledge here; the open threads). Continue from the previous
scene. Write:
- beats: the scene's moments, each {function, text} (one or two sentences each);
- events: what happens in story time, each {id (short, unique in the scene), class (Action if a character does it
  on purpose, with agent and serves: the goal it serves; Happening if nobody intends it; Event otherwise), text,
  kernel (true if the story turns on it), causes / enables (ids of this scene's or earlier events it leads to or
  makes possible), resolves (a conflict id it ends), paysOff (an earlier setup it pays off)};
- facts: what becomes true here, as story-graph facts with the given properties — {p, s, o, ...fields}: e.g.
  {"p": "hasRelationship", "s": "character/a", "o": "character/b", "type": "betrayed-by", "valence": -0.8},
  {"p": "knows", "s": "character/a", "about": "who-stole-the-amulet"}, {"p": "possesses", "s": "character/a",
  "o": "item/amulet"}, {"p": "state", "s": "character/x", "state": "dead"}, {"p": "locatedAt", ...}. Facts that
  change start at this scene unless you give "from";
- end: facts this scene ends ({p, s, o}): a trust broken, an item taken from someone;
- newEntities: characters, places or items the scene introduces, {id, class (Character, Location, Item, Faction),
  name, line};
- questStage: for a main-line gameplay scene, the main quest's stage it plays {index (rising: 10, 20, ...),
  journal (one line the player reads)}; otherwise null;
- summary: two or three sentences.
Rules: the dead don't take part or act; a character is in one place at a time; a unique item has one owner; a
character only mentions what it knows; relationships that change are shown changing."""

SCENE_SHAPE = """{"scene": {"beats": [{"function": "", "text": ""}],
   "events": [{"id": "e1", "class": "Action", "agent": "character/x", "serves": "goal-slug", "text": "", "kernel": true,
     "causes": [], "enables": []}],
   "facts": [{"p": "knows", "s": "character/x", "about": ""}], "end": [],
   "newEntities": [{"id": "character/ferryman", "class": "Character", "name": "", "line": ""}],
   "questStage": {"index": 10, "journal": ""}, "summary": ""},
 "rationale": ""}"""


def plan_scene(provider: Provider, inputs: dict) -> dict:
    properties = {p.get("id") for p in inputs.get("properties", [])}

    def check(answer: dict) -> list[str]:
        scene = answer.get("scene")
        if not isinstance(scene, dict):
            return ["'scene' must be an object"]
        problems = []
        if not str(scene.get("summary", "")).strip():
            problems.append("summary is empty")
        for f in scene.get("facts") or []:
            if not isinstance(f, dict):
                problems.append(f"fact {f!r} must be an object {{p, s, o, ...}}")
                continue
            if properties and f.get("p") not in properties:
                problems.append(f"fact property {f.get('p')!r} doesn't exist")
            if not f.get("s"):
                problems.append(f"fact {f.get('p')} has no subject 's'")
            for key in ("from", "until", "branch"):
                if key in f and not isinstance(f[key], str):
                    problems.append(f"fact {f.get('p')} {key}: {f[key]!r} must be a scene id (text) or left out")
        for e in scene.get("events") or []:
            if isinstance(e, dict):
                for key in ("causes", "enables"):
                    if any(not isinstance(c, str) for c in e.get(key) or []):
                        problems.append(f"event {e.get('id')} {key}: list event ids (text)")
        for n in scene.get("newEntities") or []:
            if not isinstance(n, dict):
                problems.append(f"new entity {n!r} must be an object {{id, class, name, line}}")
                continue
            if "/" not in str(n.get("id", "")):
                problems.append(f"new entity {n.get('id')!r} needs an id like character/<slug>")
        return problems

    return _step(provider, SCENE_TASK, inputs, SCENE_SHAPE, "scene", check, inputs.get("problems"))


CRITIC_TASK = """③ Story, the act review: read the act's scenes (cards and summaries) and the structure and
causality issues the engine found. Judge structure, causality and pacing: a kernel scene that doesn't turn its
value, an event with no cause, a setup never paid off, a slack or rushed stretch. For each real problem give the
scene and a fix as changes to its card's fields (goal, conflict, outcome, value, chargeBefore, chargeAfter, mode,
kernel, participants, location); leave the fix empty if it needs a person to decide. Few, important findings."""

CRITIC_SHAPE = """{"findings": [{"scene": "s1-03", "problem": "", "fix": {"chargeAfter": 1}}], "rationale": ""}"""


def plan_critic(provider: Provider, inputs: dict) -> dict:
    return _step(provider, CRITIC_TASK, inputs, CRITIC_SHAPE, "findings",
                 lambda a: [] if isinstance(a.get("findings"), list) else ["'findings' must be a list"], inputs.get("problems"))


# --- ⑤ Characters ---------------------------------------------------------------------------

CHARACTER_TASK = """⑤ Characters, one character: detail it from its outline line, the scenes it takes part in and
the facts about it. Write its biography (backstory, personality as [trait, 0-1] pairs from vocab.Trait, need,
desire, weakness, motivations from vocab.Motivation, secrets, arc {from, to}), appearance (description and a few
palette colours), voice (how it speaks, a sample line), relationships to other characters of the cast
({with, type from vocab.RelationshipType, valence -1 to 1}), gameplay (roles from vocab.GameplayRole, class: a
short class id like "villager", "blacksmith", "bandit-leader": ⑦ writes its numbers, kit and loot, so characters of
the same sort share one; level; stock for vendors (what it sells, in words); skills for trainers; abilities),
behaviour (packages: [{template from packageTemplates, place: a location id, schedule: {from: "HH:MM", to: "HH:MM",
days: "1,2,3,4,5"}}] — its day: e.g. work at its shop 08:00-18:00, eat at the inn 18:00-21:00, sleep at home
21:00-08:00 (the first package whose schedule holds runs; one without a schedule is its fallback); idle: true for
someone who just stands), routine (where it is when the story doesn't need it somewhere: "summary": its usual day in
a sentence or two, from its work, home and habits; "haunts": the places it keeps to [{place: a location id, spot: where
in it in a few words ("her house", "the forge counter", "the square, by the well"), when: morning | day | evening |
night | any, does: what it does there}], home and work first; "returnsTo": where it goes once a scene that brought it
somewhere is over: home | work | a haunt's spot — a villager who gathers in the square to share rumours goes home or back
to work after it, not stay there all story), dialogue (topics, the quests it gives), home (a location id) and line (one
line). The player's character has no packages and no routine. Keep it consistent with every scene it is in."""

CHARACTER_SHAPE = """{"character": {"name": "", "importance": "", "line": "", "home": "location/x",
   "biography": {"backstory": "", "personality": [["brave", 0.6]], "need": "", "desire": "", "weakness": "",
     "motivations": ["protection"], "secrets": [""], "arc": {"from": "", "to": ""}},
   "appearance": {"description": "", "palette": ["#7a5230"]}, "voice": {"description": "", "sample": ""},
   "relationships": [{"with": "character/y", "type": "friend", "valence": 0.6}],
   "gameplay": {"roles": ["quest-giver"], "class": "", "level": 3, "stock": "", "skills": "", "abilities": []},
   "behaviour": {"packages": [{"template": "work", "place": "location/x", "schedule": {"from": "08:00", "to": "18:00"}}], "idle": false},
   "routine": {"summary": "", "haunts": [{"place": "location/x", "spot": "", "when": "day", "does": ""}], "returnsTo": "home"},
   "dialogue": {"topics": [""], "gives": []}},
 "rationale": ""}"""


def plan_character(provider: Provider, inputs: dict) -> dict:
    motivations = set(_vocab(inputs, "Motivation"))
    relationships = set(_vocab(inputs, "RelationshipType"))
    templates = set(inputs.get("packageTemplates") or [])

    def check(answer: dict) -> list[str]:
        c = answer.get("character")
        if not isinstance(c, dict):
            return ["'character' must be an object"]
        problems = []
        for m in (c.get("biography") or {}).get("motivations", []):
            if motivations and m not in motivations:
                problems.append(f"motivation {m!r} isn't one of {', '.join(sorted(motivations))}")
        for r in c.get("relationships", []):
            if relationships and r.get("type") not in relationships:
                problems.append(f"relationship type {r.get('type')!r} isn't one of {', '.join(sorted(relationships))}")
        for p in (c.get("behaviour") or {}).get("packages", []):
            if templates and p.get("template") not in templates:
                problems.append(f"package template {p.get('template')!r} isn't one of {', '.join(sorted(templates))}")
        entry = inputs.get("entry") if isinstance(inputs.get("entry"), dict) else {}
        if not inputs.get("player") and not entry.get("player"):
            problems.extend(_routine_problems(c.get("routine")))
        return problems

    return _step(provider, CHARACTER_TASK, inputs, CHARACTER_SHAPE, "character", check, inputs.get("problems"))


def _routine_problems(routine: Any, places: set[str] | None = None) -> list[str]:
    """What a character's routine lacks: its summary, its haunts (each with a place and a time of day)."""
    if not isinstance(routine, dict) or not str(routine.get("summary", "")).strip() or not routine.get("haunts"):
        return ["routine needs its summary and haunts (where it is when the story doesn't need it: home, work, ...)"]
    problems = []
    for k, h in enumerate(routine.get("haunts") or []):
        if not isinstance(h, dict) or not str(h.get("place", "")).strip():
            problems.append(f"routine.haunts[{k}] needs its place")
        elif places and str(h["place"]) not in places:
            problems.append(f"routine.haunts[{k}].place {h['place']!r} isn't one of the places ({', '.join(sorted(places))})")
        elif h.get("when", "any") not in ("morning", "day", "evening", "night", "any"):
            problems.append(f"routine.haunts[{k}].when is morning, day, evening, night or any")
    return problems


ROUTINE_TASK = """④ Characters, one character's routine: the character is already designed (its biography, home, work,
AI packages, what it talks about); write only where it is when the story doesn't need it somewhere, from its work, home
and habits and the scenes it is in. "summary": its usual day in a sentence or two; "haunts": the places it keeps to
[{place: one of the places' ids, spot: where in it in a few words ("her house", "the forge counter", "the square, by the
well", one of the place's spots or anchors when one fits), when: morning | day | evening | night | any, does: what it
does there}], home and work first; "returnsTo": where it goes once a scene that brought it somewhere is over: home |
work | a haunt's spot — a villager who gathers in the square to share rumours goes home or back to work after it, not
stay there all story. The haunts are its ordinary day only, two to five of them (home, work, where it eats or prays or
drinks of an evening): not what it does in the story's scenes — the scenes only tell you who it is, where it lives and
works and whom it keeps company with; ⑥b Cast timeline puts it where each scene needs it. Keep it consistent with its
AI packages (their places and hours)."""

ROUTINE_SHAPE = """{"routine": {"summary": "", "haunts": [{"place": "location/x", "spot": "", "when": "day", "does": ""}],
  "returnsTo": "home"}, "rationale": ""}"""


def plan_routine(provider: Provider, inputs: dict) -> dict:
    places = {str(p.get("id")) for p in inputs.get("places") or [] if isinstance(p, dict) and p.get("id")}

    def check(answer: dict) -> list[str]:
        problems = _routine_problems(answer.get("routine"), places)
        haunts = (answer.get("routine") or {}).get("haunts") if isinstance(answer.get("routine"), dict) else None
        if isinstance(haunts, list) and len(haunts) > 5:
            problems.append(f"routine.haunts has {len(haunts)}: keep two to five, its ordinary day only (not the story's scenes)")
        return problems

    return _step(provider, ROUTINE_TASK, inputs, ROUTINE_SHAPE, "routine", check, inputs.get("problems"))


# --- ⑥ Quests -------------------------------------------------------------------------------

QUESTS_TASK = """⑥ Quests: the main quest in full, and side quests. The main quest (id q-main, type main) keeps
every stage of its skeleton (each plays a kernel scene: keep its index and advancesScene) and gets objectives,
aliases, journal lines and rewards. Side quests (about sideQuests of them; mode tells whether the genre wants
main-only or side-only quests) grow from characters' motivations: pick a giver whose motivation and situation
suggest a need, a strategy for that motivation (strategies), and turn the strategy's actions into stages
(e.g. recover-stolen-item: goto, kill or steal, take, goto, give). Quests in "keep" are locked: don't change them.
A quest: id (q-<slug>), name, type, motivation, strategy, giver {alias}, level [low, high], aliases (each {id,
kind: character | item | location | spot | container, and fill: an id from the story, or query: a description,
or in: the location it's in; spots also where: {anchor: a named spot}}), stages [{index (10, 20, ...), journal,
advancesScene (a scene id, if it plays one), objectives [{action from vocab.ObjectiveAction, target: an alias
id, count}], onEnter (what happens as the stage begins, optional), terminal: "completed" on the last}], rewards
[{xp}, {gold}, {item: "<item id>"}], relations [{requires | excludes | unlocks: quest id}]. Every character a quest
needs must be alive at that point.
A stage's onEnter moves the world when the story says so: {op: "move_actor", who: <character alias>, to: <location
alias or place id>, anchor: <a named spot there, optional>} (the smith flees to the capital), {op: "hide_actor" |
"show_actor", who} (gone until the rescue), {op: "set_hostile" | "set_friendly", who} (the betrayal), {op:
"follow_player" | "stop_following", who} (an escort, a companion), {op: "spawn", class: <an enemy class id like
"bandit" or "skeleton-warrior">, at: <a spot alias>, count} (the ambush), {op: "give_to", who, item: <item id>}."""

QUESTS_SHAPE = """{"quests": [{"id": "q-main", "name": "", "type": "main", "motivation": "protection",
   "strategy": "recover-stolen-item", "giver": {"alias": "giver"}, "level": [1, 4],
   "aliases": [{"id": "giver", "kind": "character", "fill": "character/x"},
               {"id": "hiding-spot", "kind": "spot", "in": "location/crypt", "where": {"anchor": "altar"}}],
   "stages": [{"index": 10, "journal": "", "advancesScene": "s1-02",
               "objectives": [{"action": "talk", "target": "giver"}]},
              {"index": 20, "journal": "", "onEnter": [{"op": "spawn", "class": "bandit", "at": "hiding-spot", "count": 3}],
               "objectives": [{"action": "goto", "target": "hiding-spot"}]},
              {"index": 100, "terminal": "completed"}],
   "rewards": [{"xp": 300}], "relations": []}],
 "rationale": ""}"""


QUEST_STAGE_OPS = {"move_actor", "hide_actor", "show_actor", "set_hostile", "set_friendly", "follow_player",
                   "stop_following", "spawn", "give_to", "set_flag", "give_item", "start_quest"}


def plan_quests(provider: Provider, inputs: dict) -> dict:
    actions = set(_vocab(inputs, "ObjectiveAction"))

    def check(answer: dict) -> list[str]:
        quests = answer.get("quests")
        side_slot = bool(inputs.get("only")) and inputs.get("only") != "q-main"
        if not isinstance(quests, list) or (not quests and not side_slot):
            return ["'quests' must be a non-empty list"]
        problems = []
        for q in quests:
            aliases = {a.get("id") for a in q.get("aliases", [])}
            for st in q.get("stages", []):
                for o in st.get("objectives", []):
                    if actions and o.get("action") not in actions:
                        problems.append(f"{q.get('id')}: objective action {o.get('action')!r} isn't one of the actions")
                    if o.get("target") and o["target"] not in aliases:
                        problems.append(f"{q.get('id')}: objective target {o['target']!r} isn't one of its aliases")
                for e in st.get("onEnter", []) or []:
                    op = (e or {}).get("op")
                    if op not in QUEST_STAGE_OPS:
                        problems.append(f"{q.get('id')} stage {st.get('index')}: onEnter op {op!r} isn't one of {', '.join(sorted(QUEST_STAGE_OPS))}")
                    elif op != "spawn" and e.get("who") and e["who"] not in aliases and not str(e["who"]).startswith("character/"):
                        problems.append(f"{q.get('id')} stage {st.get('index')}: onEnter who {e['who']!r} isn't one of its aliases")
                    elif op == "spawn" and not e.get("class"):
                        problems.append(f"{q.get('id')} stage {st.get('index')}: spawn needs an enemy class")
        return problems

    task = QUESTS_TASK
    only = inputs.get("only")
    if only == "q-main":
        task += ("\n\nThis call designs ONE quest: the main quest (id q-main) only. Answer with quests: [that quest].")
    elif only:
        task += ("\n\nThis call designs ONE quest: a single side quest, different in giver and strategy from those in "
                 "\"designed\" (don't repeat them, and don't write the main quest). Answer with quests: [that quest], or "
                 "quests: [] if no character has a need left that makes a good quest.")
    return _step(provider, task, inputs, QUESTS_SHAPE, "quests", check, inputs.get("problems"))


# --- ④ World -------------------------------------------------------------------------------

WORLD_TASK = """④ World: describe a location from everything the story needs of it — what it is (purpose,
culture, era, mood, residents), what it contains (zones, named buildings and features, interiors as locations of
their own), the story anchors the scenes and quests need (each {id, module: a component or module from the
interpret context, relation, for: {quest} or {scene}} — every quest anchor given must be there), the characters
who live or work there (actors {character, anchor}: the anchor is the id of one of its storyAnchors — the forge for
the smith, the counter for the innkeeper, a house for someone who lives there — so they stand there), its exits matching its connections ({to, edge, kind}),
style (palette, materials). Then write its draft Brief for the Build panel, exactly as the interpret context
describes for its build type (schema aige.brief/1: buildType, name, scale, components with their fields, features,
style), with the anchors' modules on. In the Brief, also describe its background (the ground everything stands on,
painted as one picture: docs/places/background.md): a first line saying what the location is for a game and its style
and look, then one line per kind of ground it has, "<ground>: <how it looks, in plain words>" (asphalt: road with white
lane markings; stone-floor: old worn stone slabs, cracks, moss in the joints). Only ground: no buildings or things.
A section of an open world ("world" given; entry.section): its scale class matches entry.size (small: a village or
hamlet, medium: a large village, big: a town, huge: a city; a harbour, castle, camp or farmland its own words for
these), no woods or wilderness round it (the world grows those), its exits are the world's roads (edge "road", no
edge of the map); entry.where (by the sea, near a forest) is the world's to place, mentioned in the description only.
A location entered through a gateway (a connection with "gateway", entered "here": a dungeon behind a cave mouth, a
crypt under a graveyard): its way in is the room or zone the gateway leads to (an entrance hall at the foot of the
stairs), and it has gateways of its own back ("entered" "there": its stairs up), which the engine places.
Its zones are its layout, as a level designer lays the location out for the builder: "contains.zones", one for each part
of the location (each a component of its Brief: a residential ward, the market square, the craftsmen's ward, a graveyard,
farmland; a dungeon's levels, a building's floors), each {"id": a short slug, "kind": its component (one of the Brief's
enabled components), "feel": what it's like in a sentence (its buildings, its people, its sounds), "where": {"words": where
it lies in the location ("in the north-east, beyond the square, along the road to the graveyard"), "rel": the same as
relations — [{"type": near | adjacent | along | far | north-of | south-of | east-of | west-of (of another zone's id, "river"
or "road") | in-north | in-south | in-east | in-west | in-middle (of "site": the whole map)}]}, "share": how much of the
location it takes, a percent (they add up to at most 85: streets and open land take the rest), "buildings": [{"type": house
| cottage | townhouse | shop | smithy | workshop | inn | tavern | temple | chapel | stable | mill | barn | warehouse |
tower | ..., "count", "for": a character id or role when it's someone's (the smith's forge, Nima's apothecary)}], "features":
what stands in it that isn't a building (a well, market stalls, graves, a fountain, crop fields, a notice board)}. Every
building the story or the cast needs is in a zone (each merchant's shop, each resident's home), the counts add up to the
Brief's scale, and the zones' locations agree with the exits (the zone a road leaves by is on that edge) and with the story
(the graveyard on the side its gate is). The builder lays the location out by them."""

WORLD_SHAPE = """{"location": {"kind": "<build type>",
   "description": {"purpose": "", "culture": "", "era": "", "mood": "", "residents": []},
   "contains": {"zones": [{"id": "", "kind": "<component>", "feel": "", "where": {"words": "", "rel": [{"type": "in-north", "to": "site"}]},
                          "share": 25, "buildings": [{"type": "house", "count": 6, "for": ""}], "features": [""]}],
                "named": [{"id": "", "kind": "", "line": ""}]},
   "storyAnchors": [{"id": "altar", "module": "<module>", "relation": {}, "for": {"quest": "q-main"}}],
   "actors": [{"character": "character/x", "anchor": "home"}],
   "exits": [{"to": "location/y", "edge": "east", "kind": "road"}],
   "style": {"palette": [], "materials": []},
   "brief": {"schema": "aige.brief/1", "buildType": "<build type>", "name": "", "scale": {}, "components": {}, "features": [],
             "background": {"description": "<line one: the location and its look>
<ground>: <how it looks>
..."}}},
 "rationale": ""}"""


def plan_place(provider: Provider, inputs: dict) -> dict:
    kind = inputs.get("kind", "")
    anchors = [a.get("where", {}).get("anchor") or a.get("alias") for a in inputs.get("anchors", []) if a.get("kind") == "spot"]

    def check(answer: dict) -> list[str]:
        p = answer.get("location")
        if not isinstance(p, dict):
            return ["'location' must be an object"]
        problems = []
        brief = p.get("brief")
        if not isinstance(brief, dict):
            problems.append("the location needs its brief")
        elif kind and brief.get("buildType") not in (kind, None):
            problems.append(f"brief.buildType must be {kind}")
        # Its layout: each zone where it lies, how much of the location it takes, its buildings by type.
        zones = ((p.get("contains") or {}).get("zones") or []) if isinstance(p.get("contains"), dict) else []
        if kind not in ("interior",) and not zones:
            problems.append("contains.zones: the location's layout, a zone for each part (where it lies, its share, its buildings)")
        share = 0
        relations = {"near", "adjacent", "along", "far", "north-of", "south-of", "east-of", "west-of", "in-north", "in-south",
                     "in-east", "in-west", "in-middle"}
        components = set((brief.get("components") or {}).keys()) if isinstance(brief, dict) else set()
        for k, z in enumerate(zones):
            if not isinstance(z, dict):
                continue
            where = z.get("where") if isinstance(z.get("where"), dict) else {}
            if not str(where.get("words", "")).strip():
                problems.append(f"contains.zones[{k}] ({z.get('id')}) says where it lies (where.words)")
            for r in where.get("rel") or []:
                if isinstance(r, dict) and r.get("type") not in relations:
                    problems.append(f"contains.zones[{k}].where.rel: {r.get('type')!r} is one of {', '.join(sorted(relations))}")
            if components and z.get("kind") not in components:
                problems.append(f"contains.zones[{k}].kind {z.get('kind')!r} is one of the Brief's components ({', '.join(sorted(components))})")
            if isinstance(z.get("share"), (int, float)):
                share += z["share"]
            else:
                problems.append(f"contains.zones[{k}] ({z.get('id')}) needs its share (a percent of the location)")
        if share > 90:
            problems.append(f"the zones' shares add up to {share}%: at most 85% (streets and open land take the rest)")
        have = {a.get("id") for a in p.get("storyAnchors", [])} | {a.get("module") for a in p.get("storyAnchors", [])}
        for anchor in anchors:
            if anchor and anchor not in have:
                problems.append(f"the story anchor {anchor!r} a quest needs is missing")
        return problems

    return _step(provider, WORLD_TASK, inputs, WORLD_SHAPE, "location", check, inputs.get("problems"))


# --- ⑦ Systems and ⑧ Visuals ----------------------------------------------------------------

MECHANICS = """What the engine can do without a script (use these; anything else is "needsScript": true):
- input actions: named buttons with keys; groups sharing a key; only near what they activate; opening a panel
- things that react (component Action, shown as Reaction): on a press, an event, a timer, a touch, someone near; requires,
  takes and gives items; animations and effects; becomes another prefab; conditions ("if") and effects ("do") like dialogue's
- moving parts of a picture (AnimatedPart: doors, levers, fires) and activation spots with no animation
- crafting: "recipe" records, two items combined, more from the Crafting list, stations (a stove)
- using an item in hand on a thing ("accepts"), e.g. seeds on a field, a key on a lock
- farming and growth: a thing that becomes another after a timer; NPCs can work the same things
- combat: melee, ranged, block, dodge, enemies, loot tables
- magic: abilities, spells, effects, scrolls, books, enchantments on equipment
- item enhancement: a recipe from an item and a material to a better item; enchantments
- character growth: attributes, levels, perks; equipment and consumables
- economy: gold, merchants, prices; factions and reputation; day and night; schedules and NPC behaviour packages;
  stealth by sight and sound; dialogue skill checks; achievements; difficulty levels; custom screens"""

GAMEPLAY_TASK = """⑦ Systems, first part: Gameplay. Decide the game's mechanics, from the Brief (its genre, concept and
systems switches) and what the story, characters and quests need. "count" is how many features to design: stay inside
it (the Brief's Gameplay mechanics setting). Each feature:
- id: lowercase-with-dashes; name: what a player would call it; kind: crafting, farming, combat, magic, enhancement,
  economy, progression, stealth, social, exploration or other;
- description: what it is, for the designer, in a sentence or two; howItPlays: what the player does, step by step;
- uses: which of the engine's mechanics it's built from; needs: the places, items and characters it relies on, by name;
- needsScript: true only if the engine can't do it without a script;
- buildRequest: a one-paragraph summary of what setting it up takes.
Prefer features that work together and that the story uses. Don't describe what the engine gives every game (walking).
""" + MECHANICS

GAMEPLAY_SHAPE = """{"gameplay": {"features": [{"id": "cooking", "name": "Cooking", "kind": "crafting", "description": "",
   "howItPlays": "", "uses": ["recipe", "station"], "needs": ["stove"], "needsScript": false, "buildRequest": ""}]},
 "rationale": ""}"""


def plan_gameplay(provider: Provider, inputs: dict) -> dict:
    count = inputs.get("count") or [1, 15]

    def check(answer: dict) -> list[str]:
        g = answer.get("gameplay")
        if not isinstance(g, dict) or not isinstance(g.get("features"), list):
            return ["'gameplay.features' must be a list"]
        problems = []
        features = g["features"]
        if not count[0] <= len(features) <= count[1]:
            problems.append(f"{len(features)} features: the Brief asks for {count[0]} to {count[1]}")
        seen = set()
        for i, f in enumerate(features):
            if not isinstance(f, dict):
                problems.append(f"feature {i} must be an object")
                continue
            for key in ("id", "name", "description", "howItPlays", "buildRequest"):
                if not isinstance(f.get(key), str) or not f[key].strip():
                    problems.append(f"feature {i}: {key} is missing")
            if f.get("id") in seen:
                problems.append(f"feature {i}: the id {f.get('id')!r} is used twice")
            seen.add(f.get("id"))
        return problems

    return _step(provider, GAMEPLAY_TASK, inputs, GAMEPLAY_SHAPE, "gameplay", check, inputs.get("problems"))


SYSTEM_TASK = """⑦ Systems, second part: one system worked out in detail, so it can be built without guessing. For the
feature given, write:
- rules: how it works, every rule, including what happens on failure and at the limits;
- numbers: the values it uses, sensible for the game's length and levels;
- items and recipes: every item and recipe it needs, by id and name;
- actions: the input actions the player uses for it, each with a key, a group if it shares a key, and nearOnly;
- sends: every event it sends, each with when; listens: every event or quest stage it reacts to, each with what happens;
- characters: which characters take part and how;
- steps: how the engine's AI panel sets it up, in order. Each {"place": "" (records, prefabs and input actions, shared by
  every scene) or the name of a place (from places: that scene is opened, with its characters and things in it, so the
  request may name them), "request": a complete request in plain words naming the real things}. Prefer "" for anything
  shared. A step that needs a script says what the script does and which character or thing it goes on.
The quests and the story are already designed: quests (each stage's objectives, the roles they name and the story scene
it plays) and scenes (where each happens, its cast, the parts it needs, what happens). The build makes the quests, their
stages, objectives, items and targets from them, and brings each scene's cast to it. So a system serves them as they are:
- never create a quest, add or change its stages or objectives, or set or advance its stage (no "set q-main to stage 20",
  no counting flags that stand in for an objective); react to a stage ("listens": quest and stage) and to events instead;
- use the quests' and scenes' own names for people, places, items and things; don't invent other ones for the story to need
  (a "trail spot", an "account flag") or move characters the scenes don't;
- what a scene says happens and no objective does (a line said, a gate opening, a light appearing) is yours to make
  happen, when it fits your system: on its stage, with its cast, in its place.
""" + MECHANICS

SYSTEM_SHAPE = """{"system": {"rules": [""], "numbers": {}, "items": [{"id": "", "name": ""}], "recipes": [{"id": "", "inputs": [""], "gives": [""], "at": ""}],
   "actions": [{"name": "cook", "key": "E", "group": "use", "nearOnly": true}], "sends": [{"event": "stew_cooked", "when": ""}],
   "listens": [{"event": "", "does": ""}], "characters": [{"character": "", "role": ""}],
   "steps": [{"place": "", "request": ""}],
   "scripts": [{"path": "scripts/x.lua", "purpose": "", "on": {"kind": "character", "target": "character/x", "place": ""}, "source": ""}]},
 "rationale": ""}"""


def plan_system(provider: Provider, inputs: dict) -> dict:
    places = {str(p).lower() for p in inputs.get("places") or []}

    def check(answer: dict) -> list[str]:
        s = answer.get("system")
        if not isinstance(s, dict):
            return ["'system' must be an object"]
        problems = []
        if not s.get("rules"):
            problems.append("rules is empty: work the system out")
        steps = s.get("steps")
        if not isinstance(steps, list) or not steps:
            problems.append("steps is empty: say how it's set up")
            steps = []
        for i, step in enumerate(steps):
            if not isinstance(step, dict) or not str(step.get("request", "")).strip():
                problems.append(f"step {i}: it has no request")
                continue
            if places and step.get("place") and str(step["place"]).lower() not in places:
                problems.append(f"step {i}: no place named {step['place']!r} (places: {', '.join(sorted(places))})")
            problems += _quest_rewrites(str(step.get("request", "")), f"step {i}", quest_ids)
        problems += check_scripts(s.get("scripts"), needs_script)
        return problems

    quest_ids = [str(q.get("id")) for q in inputs.get("quests") or [] if isinstance(q, dict) and q.get("id")]
    feature = inputs.get("feature") or {}
    needs_script = bool(feature.get("needsScript"))
    task = SYSTEM_TASK
    if needs_script:
        # The engine can't do it without code: the Planner writes the code itself (development_plan.md 32.10).
        from .prompt import LUA_API
        task += "\n\n" + SCRIPT_TASK + "\n\n" + LUA_API
    return _step(provider, task, inputs, SYSTEM_SHAPE, "system", check, inputs.get("problems"))


SCRIPT_TASK = """This feature needs a script: write it yourself, in "scripts": one or more [{"path": "scripts/<feature id>
or a part of it>.lua", "purpose": "what it does, in a line", "on": {"kind": "character" (a character of the story:
target its id, character/x) | "enemy" (every enemy of a class: target the class id) | "thing" (a thing in a place: target
its name, place the place's id) | "game" (runs in every scene, on no one: the rules of the whole game), "target", "place"},
"source": the whole Lua}]. Only the functions below exist; an entity has one script, so one script does all a thing
needs. Keep state in locals at chunk scope; react in update(dt), and to events with on(event, fn), after() and every();
read and change the world through those functions only. The steps then set up everything else (records, actions, prefabs) and say the
script is already written and attached: they don't write it again."""


def _quest_rewrites(request: str, where: str, quest_ids: list[str]) -> list[str]:
    """A step that would build a quest again: creating one, setting its stage, or giving its stages objectives."""
    import re
    text = request.lower()
    ids = [re.escape(q.lower()) for q in quest_ids if q]
    named = r"(?:" + "|".join(ids) + r")" if ids else r"q-[a-z0-9\-]+"
    problems = []
    things = r"(?!\s*(?:xp|tracker|log|items?|markers?|rewards?|panel|tab|screen|state|flags?|records?)\b)"
    if re.search(r"\b(create|make|build)\s+(?:the\s+|a\s+|new\s+)*(?:quest\s+)?" + named + r"\b" + things, text) or \
            re.search(r"\b(create|make|build)\s+(?:the\s+|a\s+|new\s+|its\s+|their\s+)*quests?(?=[\s,.;]|$)" + things, text):
        problems.append(f"{where}: it creates a quest; the quests are built from their design, so react to their stages instead")
    if re.search(r"\b(set|advance|move|put)\b[^.;]{0,30}\b" + named + r"\b[^.;]{0,20}\bstage\b", text) or re.search(r"\bset_stage\b", text):
        problems.append(f"{where}: it sets a quest's stage; the stages move on with their objectives, so react to them instead")
    if re.search(r"\b(add|give|require)\b[^.;]{0,30}\bobjectives?\b(?!\s+markers?)", text):
        problems.append(f"{where}: it adds objectives; the quests' objectives are the story's, so don't add any")
    return problems


def check_scripts(scripts: Any, needed: bool) -> list[str]:
    """A system's scripts: where they're written, what they go on, and that there's code."""
    import re
    problems: list[str] = []
    if scripts is None:
        scripts = []
    if not isinstance(scripts, list):
        return ["scripts must be a list"]
    if needed and not scripts:
        problems.append("the feature needs a script: write it in \"scripts\" (path, purpose, on, source)")
    for i, sc in enumerate(scripts):
        if not isinstance(sc, dict):
            problems.append(f"scripts[{i}] must be an object")
            continue
        if not re.match(r"^scripts/[a-z0-9_\-/]+\.lua$", str(sc.get("path", ""))):
            problems.append(f"scripts[{i}].path must be scripts/<name>.lua (lowercase)")
        on = sc.get("on") or {}
        if on.get("kind") not in ("character", "enemy", "thing", "game"):
            problems.append(f"scripts[{i}].on.kind must be character, enemy, thing or game")
        elif on.get("kind") != "game" and not on.get("target"):
            problems.append(f"scripts[{i}].on.target: whose script it is")
        elif on.get("kind") == "thing" and not on.get("place"):
            problems.append(f"scripts[{i}].on.place: the place the thing is in")
        source = str(sc.get("source", ""))
        if len(source.strip()) < 20 or "function" not in source:
            problems.append(f"scripts[{i}].source: the whole script, with its functions")
    return problems


BEHAVIOUR_TASK = """⑦ Systems, last part: one character's behaviour plan: what they do at every moment that concerns them,
worked out in detail. Go through each quest stage (quests), each system (systems) and what the player can do near them.
Each entry of "behaviour":
- when: exactly one of {"quest": "<quest id>", "stage": <stage index>}, {"event": "<event name>"} (one a system sends
  (systems[].sends), an engine event (engineEvents), or one you send below), {"playerAction": "<action>"} (from
  systems[].actions), with "range": cells for "only with the player this near";
- if: optional Conditions, as dialogue's ([{"flag": "..."}, {"item": "...", "count": 1}, {"time": {"from": 20, "to": 6}}]);
- does: any of {"package": "<id from your packages>" (they start doing this), "stops": "<a package id>", "says": a short
  line in their voice, "clip": a clip they play, "sends": an event, "do": Effects as dialogue's};
- once: true when it happens one time only.
"packages": what they can come to do beyond their daily routine: {"id", "template" (from packageTemplates), "place"}.
Say what's specific to them; don't give them an entry just to have one. An unimportant moment needs no entry. Only use
the quest ids, stages, actions and events given. The quests move on with their objectives: never set a quest's stage
(no set_stage). Where the story's scenes (scenes) put them is the build's to do: don't move them there yourself; what they
say and do in a scene of theirs is yours."""

BEHAVIOUR_SHAPE = """{"plan": {"character": "", "packages": [{"id": "chase-player", "template": "", "place": ""}],
   "behaviour": [{"when": {"quest": "q-main", "stage": 20}, "if": [], "does": {"package": "chase-player", "says": ""}, "once": false}]},
 "rationale": ""}"""


def _named(value: Any, *keys: str) -> Any:
    """A name, whether given as one ("open-journal") or as an object holding it ({"name": "open-journal", "key": "J"})."""
    if isinstance(value, dict):
        for k in keys:
            if isinstance(value.get(k), (str, int)):
                return value[k]
        return None
    return value if isinstance(value, (str, int)) else None


def plan_behaviour(provider: Provider, inputs: dict) -> dict:
    # Stages, actions and events as names (a system design writes its actions and sends as objects, a quest its stages).
    stages = {(q.get("id"), _named(s, "index", "stage")) for q in inputs.get("quests") or [] if isinstance(q, dict)
              for s in q.get("stages") or []}
    actions = {_named(a, "name", "action", "id") for s in inputs.get("systems") or [] if isinstance(s, dict) for a in s.get("actions") or []}
    events = {_named(e, "event", "name") for e in inputs.get("engineEvents") or []}
    events |= {_named(e, "event", "name") for s in inputs.get("systems") or [] if isinstance(s, dict) for e in s.get("sends") or []}
    actions.discard(None), events.discard(None)
    templates = {_named(t, "name", "id") for t in inputs.get("packageTemplates") or []} - {None}

    def check(answer: dict) -> list[str]:
        plan = answer.get("plan")
        if not isinstance(plan, dict) or not isinstance(plan.get("behaviour"), list):
            return ["'plan.behaviour' must be a list"]
        problems = []
        packages = {_named(p, "id", "name") for p in plan.get("packages") or []} - {None}
        for p in plan.get("packages") or []:
            if templates and isinstance(p, dict) and p.get("template") not in templates:
                problems.append(f"package {p.get('id')!r}: template {p.get('template')!r} isn't one of {', '.join(sorted(templates))}")
        own = {_named(e["does"].get("sends"), "event", "name") for e in plan["behaviour"] if isinstance(e, dict) and isinstance(e.get("does"), dict)}
        for i, entry in enumerate(plan["behaviour"]):
            when = entry.get("when") if isinstance(entry, dict) else None
            does = entry.get("does") if isinstance(entry, dict) else None
            if not isinstance(when, dict) or not isinstance(does, dict) or not does:
                problems.append(f"behaviour {i}: it needs when and does")
                continue
            if "quest" in when:
                if (_named(when.get("quest"), "id"), _named(when.get("stage"), "index", "stage")) not in stages:
                    problems.append(f"behaviour {i}: no quest stage {when.get('quest')!r} {when.get('stage')!r}")
            elif "playerAction" in when:
                if _named(when["playerAction"], "name", "action") not in actions:
                    problems.append(f"behaviour {i}: no action {when['playerAction']!r} (actions: {', '.join(sorted(actions))})")
            elif "event" in when:
                if _named(when["event"], "event", "name") not in events and _named(when["event"], "event", "name") not in own:
                    problems.append(f"behaviour {i}: nothing sends the event {when['event']!r}")
            else:
                problems.append(f"behaviour {i}: when must be a quest stage, an event or a playerAction")
            for key in ("package", "stops"):
                if does.get(key) and _named(does[key], "id", "name") not in packages:
                    problems.append(f"behaviour {i}: {key} {does[key]!r} isn't one of its packages")
        return problems

    return _step(provider, BEHAVIOUR_TASK, inputs, BEHAVIOUR_SHAPE, "plan", check, inputs.get("problems"))


SYSTEMS_TASK = """⑦ Systems: the game's data, from the Brief's systems, its gameplay features and their designs (features) and
what the characters, places and quests use. Every item and recipe a feature's design names is written here (recipes:
type "recipe"). Write a summary (attributes and progression, the economy and its prices, loot, abilities and schools,
factions, enemies per region and level band, the difficulty curve) and the records the game needs, each {type, id,
record}, every record exactly in its format (recordFormats: each field's type, choices, bounds and sub-fields; no
other fields, no other values):
- items: the unique items quests and characters name (every quest reward item), and a set of weapons, armour,
  consumables and materials sized by itemVariety (low: about 8 items, few kinds; medium: about 20; high: 40 or more,
  every slot and several damage types). Give each its kind, slot when worn or wielded, value, and what it does:
  weapons damage and a class (sword, axe, spear, dagger, bow, staff...); armour armor; lasting changes as
  "modifiers" [{attribute, op: add|multiply, value}] (attributes: maxHealth, maxMana, maxStamina, attack, defense,
  moveSpeed, healthRegen, resistFire, resistFrost, resistShock, resistPoison; a resistance is a share 0-0.9);
  magic on equipment as "enchantment" {effects: [effect ids], on: "hit" (each hit's target) or "worn" (its wearer)};
  potions, food, scrolls and books as "use" {heal, mana, stamina, effect: burn|poison|slow|regen, duration, attribute,
  value, ability (a scroll casts it), learn (a book teaches it), effects, consumed}. An item's name and description
  match what it does: a lightning or storm weapon deals shock (enchantment {effects: ["shock-damage"], on: "hit"}), a
  flaming one fire, a frost one frost, a venomous one poison.
- magic-effect records when no built-in effect (effectDetails) fits: {archetype: damage|heal|modify|over-time|...,
  magnitude, duration, damageType: physical|fire|frost|shock|poison, ...}; refer to effects and abilities only by
  ids that exist (effectDetails, abilityDetails, or your own records).
- character-class records: one for every gameplay.class a character names (its id exactly that class), and one for
  each kind of enemy the places hold (dungeons, camps, ruins: id like "skeleton-warrior", role fighter, archer,
  caster or beast): attributes at level 1, perLevel, combat (none for civilians), damage and damageType, aggression,
  what it carries and wears (carries, equipment [{slot, item}] using your items), abilities, gold, loot and experience.
  A boss's class role boss, with its own loot.
- loot tables (type "loot"): for enemy classes and bosses, chests in dungeons, and a merchant's stock for every
  character with a stock (id "stock-<the character's id after character/>", entries of items it sells).
- recipes, perks, progression, factions, encounters as the features need.
Keep values within sensible bands for the levels."""

SYSTEMS_SHAPE = """{"systems": {"summary": {"progression": "", "economy": "", "loot": "", "abilities": "", "enemies": "",
   "difficulty": ""}, "records": [
   {"type": "item", "id": "storm-blade", "record": {"name": "Storm Blade", "description": "Lightning runs along its edge.",
     "kind": "weapon", "slot": "weapon", "class": "sword", "damage": 4, "value": 120,
     "enchantment": {"effects": ["shock-damage"], "on": "hit"}}},
   {"type": "item", "id": "warden-mail", "record": {"name": "Warden's Mail", "kind": "armor", "slot": "body", "armor": 3,
     "value": 90, "modifiers": [{"attribute": "resistShock", "op": "add", "value": 0.25}]}},
   {"type": "item", "id": "healing-draught", "record": {"name": "Healing Draught", "kind": "consumable", "stack": 10,
     "value": 15, "use": {"heal": 15, "consumed": true}}},
   {"type": "character-class", "id": "bandit", "record": {"name": "Bandit", "role": "fighter",
     "attributes": [{"attribute": "maxHealth", "value": 12}, {"attribute": "attack", "value": 1}],
     "perLevel": [{"attribute": "maxHealth", "value": 4}], "combat": "melee", "damage": 3, "aggression": "aggressive",
     "equipment": [{"slot": "weapon", "item": "rusty-sword"}], "carries": ["healing-draught"], "gold": 5,
     "loot": "bandit-loot", "experience": 12}},
   {"type": "loot", "id": "stock-mira", "record": {"name": "Mira's stock", "rolls": 6,
     "entries": [{"item": "healing-draught", "weight": 3, "min": 1, "max": 3}]}}]},
 "rationale": ""}"""


def _check_record(fmt: dict, value: Any, where: str, out: list[str]) -> None:
    """A record (or an object field) against its format: unknown fields, wrong kinds, choices and bounds, by path."""
    fields = {f.get("name"): f for f in fmt.get("fields", []) if isinstance(f, dict)}
    if not isinstance(value, dict):
        out.append(f"{where}: expected an object")
        return
    for key, v in value.items():
        f = fields.get(key)
        if f is None:
            out.append(f"{where}.{key}: no such field (fields: {', '.join(fields)})")
            continue
        items = v if f.get("list") else [v]
        if f.get("list") and not isinstance(v, list):
            out.append(f"{where}.{key}: expected a list")
            continue
        for i, one in enumerate(items):
            at = f"{where}.{key}[{i}]" if f.get("list") else f"{where}.{key}"
            kind = f.get("type")
            if kind == "object":
                _check_record(f, one, at, out)
            elif kind == "enum":
                if f.get("options") and one not in f["options"]:
                    out.append(f"{at}: {one!r} isn't one of {', '.join(f['options'])}")
            elif kind in ("float", "int"):
                if not isinstance(one, (int, float)) or isinstance(one, bool):
                    out.append(f"{at}: expected a number")
                elif ("min" in f and one < f["min"]) or ("max" in f and one > f["max"]):
                    out.append(f"{at}: {one} is outside {f.get('min', '')}..{f.get('max', '')}")
            elif kind == "bool":
                if not isinstance(one, bool):
                    out.append(f"{at}: expected true or false")
            elif kind in ("record", "string", "text", "texture", "prefab", "sound", "scene") and not isinstance(one, str):
                out.append(f"{at}: expected text")


ELEMENT_WORDS = {"shock": ("lightning", "storm", "thunder", "shock", "spark"), "fire": ("fire", "flame", "flaming", "ember", "burning", "inferno"),
                 "frost": ("frost", "ice", "frozen", "winter"), "poison": ("venom", "poison", "toxic")}


def check_systems(s: Any, inputs: dict) -> list[str]:
    if not isinstance(s, dict):
        return ["'systems' must be an object"]
    types = set((inputs.get("recordTypes") or {}).keys())
    formats = inputs.get("recordFormats") or {}
    records = [r for r in s.get("records", []) if isinstance(r, dict)]
    problems: list[str] = []
    ids: dict[str, set[str]] = {}
    for r in records:
        ids.setdefault(str(r.get("type")), set()).add(str(r.get("id")))
    effects = {e.get("id") for e in inputs.get("effectDetails") or [] if isinstance(e, dict)} | ids.get("magic-effect", set())
    abilities = {a.get("id") for a in inputs.get("abilityDetails") or [] if isinstance(a, dict)} | ids.get("ability", set())
    abilities |= set(inputs.get("abilities") or [])
    effects |= set(inputs.get("effects") or [])
    items, loot = ids.get("item", set()), ids.get("loot", set())
    for r in records:
        t, rid, rec = r.get("type"), r.get("id"), r.get("record")
        if types and t not in types:
            problems.append(f"record type {t!r} doesn't exist")
            continue
        if t in formats:
            _check_record(formats[t], rec, f"{t}/{rid}", problems)
        if not isinstance(rec, dict):
            continue
        # References that must exist.
        if t == "item":
            for e in (rec.get("enchantment") or {}).get("effects", []) + (rec.get("use") or {}).get("effects", []):
                if effects and e not in effects:
                    problems.append(f"item/{rid}: effect {e!r} doesn't exist (effectDetails, or write a magic-effect record)")
            for k in ("ability", "learn"):
                a = (rec.get("use") or {}).get(k)
                if a and abilities and a not in abilities:
                    problems.append(f"item/{rid}: use.{k} {a!r} isn't an ability")
            # Its name and words agree with its magic.
            text = f"{rec.get('name', '')} {rec.get('description', '')}".lower()
            kinds = set()
            for e in (rec.get("enchantment") or {}).get("effects", []):
                for d in inputs.get("effectDetails") or []:
                    if isinstance(d, dict) and d.get("id") == e:
                        kinds.add(d.get("damageType"))
                kinds |= {k for k in ELEMENT_WORDS if k in str(e)}
            for element, words in ELEMENT_WORDS.items():
                if any(w in text.split() or w in text for w in words) and rec.get("kind") == "weapon" and element not in kinds:
                    problems.append(f"item/{rid}: its name or description says {element}, but it has no {element} effect "
                                    f"(enchantment {{effects: [\"{element}-damage\"], on: \"hit\"}})")
                    break
        if t == "character-class":
            for e in rec.get("equipment", []) or []:
                if isinstance(e, dict) and e.get("item") and e["item"] not in items:
                    problems.append(f"character-class/{rid}: equipment item {e['item']!r} isn't one of the items")
            for c in rec.get("carries", []) or []:
                if str(c).split("*")[0].strip() not in items:
                    problems.append(f"character-class/{rid}: carries {c!r}, not one of the items")
            for k in ("loot", "stock"):
                if rec.get(k) and rec[k] not in loot:
                    problems.append(f"character-class/{rid}: {k} {rec[k]!r} isn't a loot table")
        if t == "loot":
            for e in rec.get("entries", []) or []:
                if isinstance(e, dict) and e.get("item") and e["item"] not in items:
                    problems.append(f"loot/{rid}: item {e['item']!r} isn't one of the items")
    # Every enemy class a quest spawns.
    for q in inputs.get("quests") or []:
        for cls in (q or {}).get("spawns") or []:
            if cls not in ids.get("character-class", set()):
                problems.append(f"{q.get('id')} spawns {cls!r}: write its character-class record")
    # Every class a character names, and every merchant's stock.
    classes = ids.get("character-class", set())
    for c in inputs.get("characters") or []:
        g = (c or {}).get("gameplay") or {}
        cls = g.get("class")
        if cls and classes is not None and cls not in classes:
            problems.append(f"{c.get('id')} is of class {cls!r}: write its character-class record")
        if g.get("stock"):
            stock = "stock-" + str(c.get("id", "")).split("/")[-1]
            if stock not in loot:
                problems.append(f"{c.get('id')} sells things: write its stock as loot {stock!r}")
    return problems[:40]


def plan_systems(provider: Provider, inputs: dict) -> dict:
    def check(answer: dict) -> list[str]:
        return check_systems(answer.get("systems"), inputs)

    return _step(provider, SYSTEMS_TASK, inputs, SYSTEMS_SHAPE, "systems", check, inputs.get("problems"))


VISUALS_TASK = """⑧ Visuals: the game's interface, designed as a UI designer designs a web app's: every panel the
player sees, each its own, from the Brief (genre, art style, tone, the visuals it asks for) and the systems that are on.
1. The base: the UI theme (one of themes) closest to the art style and tone, the font (one of fonts, by name: its style
fits the game; readable at small sizes for text-heavy games), the notification style, the map style, the screens and
windows (from windows: title, pause, settings, save-load, game-over always; inventory if items, journal if quests, map if
more than one place, spellbook if magic, character if progression, shop if merchants, dialogue if conversations), and
the HUD widgets by density (from widgets).
2. "style": the interface's one look, as an art director writes it: "mood" (a sentence), "palette" {background, panel,
panelEdge, ink, inkDim, accent, highlight, danger, good: "#rrggbb"}, "materials" (what panels are made of: weathered oak
and iron studs, parchment over slate, glass and brass...), "typography" (title and body: case, weight, spacing, sizes),
"ornaments" (corners, dividers, seals, rivets), "motion" (how panels open, hovers, presses), "icons" (their style).
3. "panels": every screen and window above and the HUD, each designed in full and different from the others where its
purpose asks — {"id": its screen id (hud, inventory, journal, map, character, shop, dialogue, title, pause, settings,
save-load, game-over, spellbook, and a game's own: a hunt journal, a crafting bench), "name", "layer": hud | window |
screen | popup, "opens": the input action that opens it (or "" for the HUD, title...), "purpose": what the player does
there, "layout": where everything is, region by region, as a designer specifies it (a two-column inventory: the
character's figure on the left with equipment slots round it — head, body, hands, feet, weapon, off hand, ring, amulet —
and a grid of carried things on the right; the details of the picked one below), "components": [{"id", "kind": one of
figure (the character, with slots items are dragged onto to wear them), slot-grid, list, tabs, bar, meter, text, title,
button, image, portrait, minimap, compass, tracker, tooltip, map, page, counter, timer, divider, frame, "shows": the data
it shows (the items carried, health, the tracked quest, the player's gold...), "does": how one uses it (click to use,
drag onto a slot to wear, scroll, hover for details, a tab per category), "look": its own look in detail}], "look": the
panel's look in detail (its frame, background, header, how its parts are set apart, its states), "images": [{"id",
"use": background | frame | header | ornament | icon | figure | divider | button | texture, "prompt": the picture to
generate (what it shows, its style, flat and straight on, no text, plain or transparent around it), "width", "height"
(pixels), "nine": true when it's a frame stretched in nine pieces}], "css": its RCSS (RmlUi's CSS: decorators like
image(), gradient, ninepatch; borders, colours, fonts, spacing; :hover and :active states), written for its own classes,
novel and of this game, not the theme's defaults}. Every screen and window chosen in 1 has its panel, and the HUD;
each has at least four components, the images its look needs (most panels three to six), and its css."""

VISUALS_SHAPE = """{"ui": {"theme": "", "font": "", "screens": ["title"], "hud": ["health"], "notifications": "standard", "mapStyle": "minimap",
  "style": {"mood": "", "palette": {"background": "#000000", "panel": "#000000", "panelEdge": "#000000", "ink": "#000000", "inkDim": "#000000",
            "accent": "#000000", "highlight": "#000000", "danger": "#000000", "good": "#000000"},
            "materials": "", "typography": "", "ornaments": "", "motion": "", "icons": ""},
  "panels": [{"id": "inventory", "name": "", "layer": "window", "opens": "inventory", "purpose": "", "layout": "",
              "components": [{"id": "", "kind": "slot-grid", "shows": "", "does": "", "look": ""}],
              "look": "", "images": [{"id": "", "use": "frame", "prompt": "", "width": 512, "height": 512, "nine": true}], "css": ""}]},
 "rationale": ""}"""

UI_KINDS = {"figure", "slot-grid", "list", "tabs", "bar", "meter", "text", "title", "button", "image", "portrait", "minimap", "compass",
            "tracker", "tooltip", "map", "page", "counter", "timer", "divider", "frame"}


def plan_visuals(provider: Provider, inputs: dict) -> dict:
    themes, windows, widgets = set(inputs.get("themes") or []), set(inputs.get("windows") or []), set(inputs.get("widgets") or [])

    def check(answer: dict) -> list[str]:
        ui = answer.get("ui")
        if not isinstance(ui, dict):
            return ["'ui' must be an object"]
        problems = []
        if themes and ui.get("theme") not in themes:
            problems.append(f"theme must be one of {', '.join(sorted(themes))}")
        problems += [f"screen {s!r} isn't one of the windows" for s in ui.get("screens", []) if windows and s not in windows]
        problems += [f"HUD widget {w!r} isn't one of the widgets" for w in ui.get("hud", []) if widgets and w not in widgets]
        style = ui.get("style")
        if not isinstance(style, dict) or not isinstance(style.get("palette"), dict) or not str(style.get("materials", "")).strip():
            problems.append("style needs its palette and materials (the interface's one look)")
        panels = ui.get("panels")
        if not isinstance(panels, list) or not panels:
            return problems + ["panels: every screen and window, and the HUD, each designed"]
        ids = set()
        for i, p in enumerate(panels):
            if not isinstance(p, dict) or not str(p.get("id", "")).strip():
                problems.append(f"panels[{i}] needs its id")
                continue
            pid = str(p["id"])
            if pid in ids:
                problems.append(f"panels[{i}]: {pid} is designed twice")
            ids.add(pid)
            for k in ("layout", "look", "css"):
                if len(str(p.get(k, "")).strip()) < 40:
                    problems.append(f"panels[{i}] ({pid}).{k} is designed in detail")
            comps = p.get("components") or []
            if len(comps) < 4:
                problems.append(f"panels[{i}] ({pid}) needs at least four components")
            for k, c in enumerate(comps):
                if not isinstance(c, dict) or c.get("kind") not in UI_KINDS:
                    problems.append(f"panels[{i}].components[{k}].kind is one of {', '.join(sorted(UI_KINDS))}")
            images = p.get("images") or []
            if not images:
                problems.append(f"panels[{i}] ({pid}) needs the images its look needs")
            for k, im in enumerate(images):
                if not isinstance(im, dict) or not str(im.get("id", "")).strip() or len(str(im.get("prompt", ""))) < 20:
                    problems.append(f"panels[{i}].images[{k}] needs its id and a prompt")
        wanted = {str(s) for s in ui.get("screens", [])} | {"hud"}
        missing = sorted(wanted - ids - {"save-load", "load", "save"} if "save-load" in ids else wanted - ids)
        if missing:
            problems.append(f"these screens have no panel: {', '.join(missing)}")
        return problems

    return _step(provider, VISUALS_TASK, inputs, VISUALS_SHAPE, "ui", check, inputs.get("problems"))


OBJECTS_TASK = """World ▸ Props & objects: every thing in the game's places that someone uses, with what it's for and how it
works in the engine — the description the build makes it from. Read the story (its scenes' events and outcomes), the
quests' stages and objectives, the characters (their work, routines, the cast timeline: where each is and what each
does), and the places (their purpose, anchors and the things the plan put in them: "objects"). For each thing that
matters — a story prop (the record board, the burial ledger's table), a quest's object (the east gate, the crypt
seal, a lever), a working thing people use (the forge, the well, the stove, a market stall, the bell rope), a thing the
player can use for something (a notice board to read, a bed to rest in) — {"id": a slug, "name", "place": a location
id, "where": its anchor or what it's by ("the square, by the well"), "purpose": why it's in the game, in a sentence,
"look": how it looks, "states": [{"id", "look"}] when it changes (the gate: broken, repaired), "smart": {"type": bed |
chair | table | counter | anvil | fireplace | well | bookshelf | altar | stall | board | workbench | ..., "affordances": what
characters do at it in their day: sleep, sit, eat, drink, work, read, pray, warm} or {} when characters don't use it in
their day, "uses": every way someone uses it: [{"by": "player" | character ids | "villagers" (each named in "who"),
"who": [character ids], "kind": casual | quest, "quest": {"quest", "stage"} when quest, "when": when (always, a time of
day, from a quest stage, once something happened), "why": why they use it, "reaction": the engine's Reaction (component
Action) that makes it so: {"trigger": input | near | event | timer | touch, "action": the input action ("interact", or the
game's own: "repair", "read"), "range": cells, "prompt": shown near it ("Read the notices"), "requires": items carried,
"takes": items taken, "gives": items given, "accepts": an item in hand, "requiresFlag", "userClip": the clip the user
plays (repair, read, pray), "clip": the clip it plays (open, glow), "effect", "userEffect", "seconds": how long it takes,
"becomes": the state it turns into (one of states), "once": true or false, "cooldown": seconds, "if": conditions (as
dialogue's: [{"quest": "q-main", "is": "active", "stage": 10}, {"flag": "x"}]), "do": effects ([{"op": "set_flag",
"name"}, {"op": "complete_objective", "quest", "objective"}, {"op": "send_event", "event"}...]), "emits": an event,
"setFlag", "text": shown when it happens}}]}. Casual uses keep the world alive (villagers drawing water at the well
each morning, the smith at the anvil all day); quest uses are what a quest asks (the player brings the iron brace to the
east gate). Each thing's "animations": every clip its Reactions play — [{"id": the clip's name, one short word (open,
glow, swing, repair, read, draw-water), "who": "object" (the thing itself moves: a gate swinging open, a fire flaring),
"player" or a character id (the user moves: a villager hammering, the player reading), "action": what it looks like, for
the animator to draw ("swinging both hammers at the gate's planks"), "loop": true for one that repeats while it lasts,
"why"}]. A Reaction's "clip" is one of the thing's own ("who": "object"), its "userClip" one of its users' (one entry for
each who plays it: the player and each character id that uses it); a thing that only changes picture (becomes) needs no
clip. Only things the story, the quests, the characters' work or the places name or clearly need. Use the cast's ids,
the places' ids, and the quests' stages as given."""

OBJECTS_SHAPE = """{"objects": [{"id": "", "name": "", "place": "location/x", "where": "", "purpose": "", "look": "",
  "states": [{"id": "", "look": ""}], "smart": {"type": "", "affordances": []},
  "animations": [{"id": "", "who": "object", "action": "", "loop": false, "why": ""}],
  "uses": [{"by": "player", "who": [], "kind": "quest", "quest": {"quest": "q-main", "stage": 10}, "when": "", "why": "",
            "reaction": {"trigger": "input", "action": "interact", "range": 1.5, "prompt": "", "requires": "", "takes": "", "gives": "",
                         "accepts": "", "requiresFlag": "", "userClip": "", "clip": "", "effect": "", "userEffect": "", "seconds": 0,
                         "becomes": "", "once": false, "cooldown": 0, "if": [], "do": [], "emits": "", "setFlag": "", "text": ""}}]}],
 "rationale": ""}"""

REACTION_TRIGGERS = {"input", "near", "event", "timer", "touch"}


def plan_objects(provider: Provider, inputs: dict) -> dict:
    cast = {str(c.get("id")) for c in inputs.get("cast") or [] if isinstance(c, dict) and c.get("id")}
    places = {str(p.get("id")) for p in inputs.get("places") or [] if isinstance(p, dict) and p.get("id")}
    stages = {(str(q.get("id")), int(s.get("index", -1))) for q in inputs.get("quests") or [] if isinstance(q, dict)
              for s in q.get("stages") or [] if isinstance(s, dict) and isinstance(s.get("index"), int)}

    def check(answer: dict) -> list[str]:
        objects = answer.get("objects")
        if not isinstance(objects, list) or not objects:
            return ["'objects': every thing in the places someone uses"]
        problems, ids = [], set()
        for i, o in enumerate(objects):
            if not isinstance(o, dict) or not str(o.get("id", "")).strip():
                problems.append(f"objects[{i}] needs its id")
                continue
            where = f"objects[{i}] ({o['id']})"
            if o["id"] in ids:
                problems.append(f"{where} is described twice")
            ids.add(o["id"])
            if places and str(o.get("place")) not in places:
                problems.append(f"{where}.place {o.get('place')!r} isn't one of the places")
            if not str(o.get("purpose", "")).strip():
                problems.append(f"{where} needs its purpose")
            states = {str(s.get("id")) for s in o.get("states") or [] if isinstance(s, dict)}
            uses = o.get("uses") or []
            for k, a in enumerate(o.get("animations") or []):
                if not isinstance(a, dict) or not str(a.get("id", "")).strip() or not str(a.get("action", "")).strip():
                    problems.append(f"{where}.animations[{k}] needs its id (the clip's name) and its action (what it looks like)")
                elif a.get("who") not in ("object", "player") and cast and str(a.get("who")) not in cast:
                    problems.append(f"{where}.animations[{k}].who is object, player or one of the cast")
            if not uses and not (o.get("smart") or {}).get("affordances"):
                problems.append(f"{where}: who uses it, how and why (uses), or what characters do at it (smart)")
            for k, u in enumerate(uses):
                if not isinstance(u, dict):
                    continue
                w = f"{where}.uses[{k}]"
                problems += [f"{w}.who: {x!r} isn't one of the cast" for x in u.get("who") or [] if cast and str(x) not in cast]
                if u.get("kind") not in ("casual", "quest"):
                    problems.append(f"{w}.kind is casual or quest")
                q = u.get("quest") if isinstance(u.get("quest"), dict) else {}
                if u.get("kind") == "quest" and stages and (str(q.get("quest")), q.get("stage")) not in stages:
                    problems.append(f"{w}.quest: {q.get('quest')} has no stage {q.get('stage')}")
                r = u.get("reaction") if isinstance(u.get("reaction"), dict) else {}
                if r.get("trigger") not in REACTION_TRIGGERS:
                    problems.append(f"{w}.reaction.trigger is one of {', '.join(sorted(REACTION_TRIGGERS))}")
                if r.get("becomes") and str(r["becomes"]) not in states:
                    problems.append(f"{w}.reaction.becomes {r['becomes']!r} is one of its states")
                # Its clips: the thing's own, and its users' (each who plays it).
                anims = [a for a in o.get("animations") or [] if isinstance(a, dict)]
                own = {str(a.get("id")) for a in anims if a.get("who") == "object"}
                if r.get("clip") and str(r["clip"]) not in own:
                    problems.append(f"{w}.reaction.clip {r['clip']!r} is one of its animations with who \"object\"")
                if r.get("userClip"):
                    users = (["player"] if u.get("by") == "player" else []) + [str(x) for x in u.get("who") or []]
                    for user in users:
                        if not any(str(a.get("id")) == str(r["userClip"]) and str(a.get("who")) == user for a in anims):
                            problems.append(f"{w}.reaction.userClip {r['userClip']!r}: an animation of it for {user} (who {user!r})")
                if not str(u.get("why", "")).strip():
                    problems.append(f"{w} needs its why")
        return problems

    return _step(provider, OBJECTS_TASK, inputs, OBJECTS_SHAPE, "objects", check, inputs.get("problems"))


SCENES_TASK = """World ▸ Small scenes: the little scenes that make the game's places feel alive, which play around the player
as they come by (they keep control; nothing stops): two villagers in the square trading rumours about the graveyard, the
smith hammering and cursing the cold, the elder reading out a name at the record board, children running off when the
player comes near. Two kinds: "casual" (the life of the place, any time) and "quest" (about a quest that is active then:
what the villagers say while the graves are disturbed, and something else once the crypt is sealed). Read the story, the
quests' stages, the characters (their routines, relationships and voices) and the cast timeline (who is where at each
stage), the places and their props & objects. For each small scene {"id", "name", "kind": casual | quest, "quests":
[{"quest", "stages": [stage indexes]}] while which it plays (quest scenes: one or more quests; casual: [] for any time),
"place": a location id, "area": {"anchor": a story anchor or marker of the place, or "near": an object's id, "radius":
cells} — where the player has to be for it to start — or {"room": an interior place id} (entering that room starts it),
"trigger": enter-area | enter-room | time ("time": morning | day | evening | night with enter-area), "repeat": once |
each-visit | cooldown, "cooldown": seconds, "cast": the character ids in it (each one who is there then, by the cast
timeline), "objects": the props & objects it uses (their ids), "beats": in order [{"who": a cast id, "do": say | walk |
face | use | animate | emote | wait, "line": what they say (in their voice; short), "to": where they walk or whom they
face (an anchor, an object's id, a character id, "player"), "object": what they use (an object's id), "clip": an
animation, "seconds": how long it takes}], "after": what they do once it's over (back to their routine, stay), "animations": every clip its beats play that
isn't a thing's (a thing's use plays the clip its Reaction has) — [{"id": the clip's name, one short word (wave, laugh,
shrug, sweep), "who": the cast id who plays it, "action": what it looks like, for the animator to draw}] — a beat's "clip"
is one of these (or, using a thing, its user clip), "why": what it shows about the story or the place}. Two to six beats each; lines that sound like people (not exposition), about the
story when it's a quest scene and about their lives when it's casual. Every place the player spends time in gets some;
the square of a village several. Use only the cast, places, quests and objects given."""

SCENES_SHAPE = """{"scenes": [{"id": "", "name": "", "kind": "casual", "quests": [{"quest": "q-main", "stages": [10]}], "place": "location/x",
  "area": {"anchor": "", "near": "", "radius": 6, "room": ""}, "trigger": "enter-area", "time": "", "repeat": "each-visit", "cooldown": 120,
  "cast": ["character/x"], "objects": [""], "beats": [{"who": "character/x", "do": "say", "line": "", "to": "", "object": "", "clip": "", "seconds": 3}],
  "animations": [{"id": "", "who": "character/x", "action": ""}], "after": "", "why": ""}], "rationale": ""}"""

SCENE_DOES = {"say", "walk", "face", "use", "animate", "emote", "wait"}


def plan_scenes(provider: Provider, inputs: dict) -> dict:
    cast = {str(c.get("id")) for c in inputs.get("cast") or [] if isinstance(c, dict) and c.get("id")}
    places = {str(p.get("id")) for p in inputs.get("places") or [] if isinstance(p, dict) and p.get("id")}
    objects = {str(o.get("id")) for o in inputs.get("objects") or [] if isinstance(o, dict) and o.get("id")}
    quests = {str(q.get("id")) for q in inputs.get("quests") or [] if isinstance(q, dict) and q.get("id")}

    def check(answer: dict) -> list[str]:
        scenes = answer.get("scenes")
        if not isinstance(scenes, list) or not scenes:
            return ["'scenes': the small scenes of the game's places"]
        problems, ids = [], set()
        for i, sc in enumerate(scenes):
            if not isinstance(sc, dict) or not str(sc.get("id", "")).strip():
                problems.append(f"scenes[{i}] needs its id")
                continue
            where = f"scenes[{i}] ({sc['id']})"
            if sc["id"] in ids:
                problems.append(f"{where} is written twice")
            ids.add(sc["id"])
            if sc.get("kind") not in ("casual", "quest"):
                problems.append(f"{where}.kind is casual or quest")
            if sc.get("kind") == "quest" and not sc.get("quests"):
                problems.append(f"{where}: a quest scene says while which quests (and stages) it plays")
            for q in sc.get("quests") or []:
                if isinstance(q, dict) and quests and str(q.get("quest")) not in quests:
                    problems.append(f"{where}.quests: {q.get('quest')!r} isn't one of the quests")
            if places and str(sc.get("place")) not in places:
                problems.append(f"{where}.place {sc.get('place')!r} isn't one of the places")
            area = sc.get("area") if isinstance(sc.get("area"), dict) else {}
            if not (area.get("anchor") or area.get("near") or area.get("room")):
                problems.append(f"{where}.area: an anchor, an object it's near, or a room")
            if area.get("near") and objects and str(area["near"]) not in objects:
                problems.append(f"{where}.area.near {area['near']!r} isn't one of the objects")
            people = [str(x) for x in sc.get("cast") or []]
            if not people:
                problems.append(f"{where} needs its cast")
            problems += [f"{where}.cast: {x!r} isn't one of the cast" for x in people if cast and x not in cast]
            beats = sc.get("beats") or []
            if not 2 <= len(beats) <= 8:
                problems.append(f"{where} has two to six beats")
            for k, b in enumerate(beats):
                if not isinstance(b, dict):
                    continue
                if b.get("do") not in SCENE_DOES:
                    problems.append(f"{where}.beats[{k}].do is one of {', '.join(sorted(SCENE_DOES))}")
                if str(b.get("who")) not in people:
                    problems.append(f"{where}.beats[{k}].who {b.get('who')!r} is one of its cast")
                if b.get("do") == "say" and not str(b.get("line", "")).strip():
                    problems.append(f"{where}.beats[{k}] says something: its line")
                if b.get("do") == "use" and objects and str(b.get("object")) not in objects:
                    problems.append(f"{where}.beats[{k}].object {b.get('object')!r} isn't one of the objects")
                if b.get("clip") and b.get("do") in ("animate", "emote") and not any(
                        isinstance(a, dict) and str(a.get("id")) == str(b["clip"]) and str(a.get("who")) == str(b.get("who"))
                        for a in sc.get("animations") or []):
                    problems.append(f"{where}.beats[{k}].clip {b['clip']!r}: an animation of it for {b.get('who')} (animations)")
        return problems

    return _step(provider, SCENES_TASK, inputs, SCENES_SHAPE, "scenes", check, inputs.get("problems"))


UIPANEL_TASK = """Build ▸ Visuals: lay out one panel of the game's interface, exactly as designed ("panel": its purpose,
layout, components and look; "style": the interface's one look and palette), as a design for the engine's UI designer.
The engine writes the document from it, so the layout is what you decide: each element's place and size.

Given: the panel, the style, the theme's tokens, "model": the data the screen binds to (menu | hud | dialogue),
"bindings": what that model has ("texts": the values a text may show as {{name}}; "lists": the lists a list element can
show, and the fields of each row, as {{item.<field>}}), "images": the panel's pictures, drawn already (each {id, use,
path, width, height, nine}: use each by its path), and "canvas": the screen's size in dp (1920 x 1080).

Answer "design": {"root": [element...]}. An element: {"id": unique, "type", "anchor", "x", "y", "w", "h", and as it needs:
"image": a picture's path, "slice": [left, top, right, bottom] (in the picture's pixels: a frame drawn to stretch in nine
pieces, "nine": true, keeps its corners; about an eighth of its smaller side each), "bg": a colour (#rrggbb or #rrggbbaa,
or a theme token: panel, accent, text, highlight), "text" (words, or {{binding}}), "size" (font size in dp), "color",
"align" (left | center | right), "bold", "padding", "children": [elements inside it, placed in it], "list": {"source":
one of bindings.lists, "row": the row's text ({{item.label}}), "rowH": its height in dp, "look": {"bg", "image",
"color"} of a row, "lit": {"bg", "color"} of the highlighted or hovered row}, "bar": {"value", "max", "color"} (a HUD
value and its maximum), "actions" (a button's: [{"do": open | close | event | save | load | quit, "arg": a screen id or
an event}])}.
- type: panel (a box, a frame), image (a picture), text, button, list (the model's rows: a menu's choices are its list,
  never buttons), bar, slots (the inventory's items as a grid of cells).
- anchor: tl t tr l c r bl b br (x, y measured from that point of its parent, inward; w and h its size), or stretch
  (x, y, w, h its margins from the parent's left, top, right and bottom). Positions are in dp, inside the parent.
- Lay it out like a designer: a screen of layer "screen" gets its background picture stretched over the whole canvas
  (anchor stretch, all margins 0); a window is one framed panel (anchor c, a sensible size such as 960 x 680), its
  frame picture sliced; what's in a panel sits inside the frame's border (margins at least its slice, plus 16 dp),
  never overlapping another element; a title 44-64 dp, headings 26-30, text 18-22, rows 40-48 dp high; text in the
  palette's ink on dark, readable. Every picture is used where its "use" says (background, frame, divider, ornament,
  icon, figure, paper).
- Keep what the model offers: a menu screen shows {{title}} and its list of items (the choices), and {{subtitle}},
  {{body}} or {{footer}} where the reference shows them; the HUD its bars and quest lines; dialogue {{speaker}}, {{text}}
  and the choices list.
"notes": what couldn't be done as designed."""

UIPANEL_SHAPE = """{"design": {"root": [{"id": "", "type": "panel", "anchor": "c", "x": 0, "y": 0, "w": 960, "h": 680, "image": "",
  "slice": [32, 32, 32, 32], "children": []}]}, "notes": "", "rationale": ""}"""

UI_TYPES = {"panel", "image", "text", "button", "list", "bar", "slots"}
UI_ANCHORS = {"tl", "t", "tr", "l", "c", "r", "bl", "b", "br", "stretch", "stretch-x", "stretch-y", "stretch-top",
              "stretch-bottom", "stretch-left", "stretch-right"}
UI_ACTIONS = {"open", "close", "event", "press", "flag", "clear", "save", "load", "quit"}


def ui_bindings(reference: str) -> dict:
    """What a screen's data model offers, from the engine's document for it: its texts ({{name}}) and its lists
    (data-for="item, i : items") with each row's fields."""
    lists: dict[str, set] = {}
    for var, source in re.findall(r'data-for="\s*(\w+)\s*(?:,\s*\w+)?\s*:\s*(\w+)', reference):
        fields = set(re.findall(r"\b" + re.escape(var) + r"\.(\w+)", reference))
        lists.setdefault(source, set()).update(fields)
    loop_vars = set(re.findall(r'data-for="\s*(\w+)', reference))
    texts = set()
    for expr in re.findall(r"\{\{([^}]*)\}\}", reference) + re.findall(r'data-(?:if|style-\w+|attr-\w+|class-\w+)="([^"]*)"', reference):
        expr = re.sub(r"'[^']*'", "", expr)  # its quoted words aren't names
        for name in re.findall(r"(?<![.\w])([a-z_][a-z0-9_]*)\b(?!\s*\()", expr):
            if name not in loop_vars and name not in {"i", "true", "false", "px"} and not name.isdigit():
                texts.add(name)
    texts -= set(lists)
    return {"texts": sorted(texts), "lists": {k: sorted(v) for k, v in sorted(lists.items())}}


def plan_uipanel(provider: Provider, inputs: dict) -> dict:
    reference = str(inputs.get("reference") or "")
    found = re.search(r'data-model="(\w+)"', reference)
    model = found.group(1) if found else "menu"
    bindings = ui_bindings(reference)
    images = []
    for im in inputs.get("images") or []:
        if isinstance(im, dict) and im.get("file"):
            path = "ui/art/" + str(im["file"]).rsplit("/", 1)[-1]
            images.append({"id": im.get("id"), "use": im.get("use", ""), "path": path, "width": int(im.get("width") or 0),
                           "height": int(im.get("height") or 0), "nine": bool(im.get("nine"))})
    sizes = {im["path"]: (im["width"], im["height"]) for im in images}
    given = {**inputs, "model": model, "bindings": bindings, "images": images, "canvas": {"w": 1920, "h": 1080}}
    given.pop("reference", None)
    given["reference"] = reference  # its layout, for what the engine's own screen shows

    def check(answer: dict) -> list[str]:
        design = answer.get("design")
        if not isinstance(design, dict) or not isinstance(design.get("root"), list) or not design["root"]:
            return ['answer "design": {"root": [elements]}']
        problems: list[str] = []
        ids: set = set()
        used: set = set()
        has_list = False

        def visit(n, parent_w: float, parent_h: float, where: str) -> None:
            nonlocal has_list
            if not isinstance(n, dict):
                problems.append(f"{where}: an element is an object")
                return
            nid = str(n.get("id") or "")
            at = f"element {nid or where}"
            if not nid:
                problems.append(f"{where}: an element has no id")
            elif nid in ids:
                problems.append(f"{at}: the id is used twice")
            ids.add(nid)
            kind = n.get("type", "panel")
            if kind not in UI_TYPES:
                problems.append(f"{at}: type is one of {', '.join(sorted(UI_TYPES))}")
            anchor = n.get("anchor", "tl")
            if anchor not in UI_ANCHORS:
                problems.append(f"{at}: anchor is one of {', '.join(sorted(UI_ANCHORS))}")
            try:
                x, y, w, h = (float(n.get(k, 0) or 0) for k in ("x", "y", "w", "h"))
            except (TypeError, ValueError):
                problems.append(f"{at}: x, y, w and h are numbers")
                return
            if anchor == "stretch":
                inner_w, inner_h = parent_w - x - w, parent_h - y - h
                if inner_w < 8 or inner_h < 8:
                    problems.append(f"{at}: its margins leave no room in its parent ({parent_w:g} x {parent_h:g})")
            else:
                inner_w, inner_h = w, h
                if w <= 0 or h <= 0:
                    problems.append(f"{at}: give it a size (w, h)")
                elif not anchor.startswith("stretch"):
                    # From a side: its offset plus its size; from the middle: half its size either way of the offset.
                    span_x = abs(x) + w / 2 if anchor in ("t", "c", "b") else x + w
                    span_y = abs(y) + h / 2 if anchor in ("l", "c", "r") else y + h
                    limit_x = parent_w / 2 if anchor in ("t", "c", "b") else parent_w
                    limit_y = parent_h / 2 if anchor in ("l", "c", "r") else parent_h
                    if span_x > limit_x + 1 or span_y > limit_y + 1 or x < 0 and anchor not in ("t", "c", "b") or y < 0 and anchor not in ("l", "c", "r"):
                        problems.append(f"{at}: it doesn't fit in its parent ({parent_w:g} x {parent_h:g} dp) at {anchor} {x:g},{y:g} size {w:g} x {h:g}")
            image = n.get("image")
            if image:
                if image not in sizes:
                    problems.append(f"{at}: its image is one of the panel's: {', '.join(sizes) or 'none'}")
                used.add(image)
            for text in re.findall(r"\{\{\s*([\w.]+)\s*\}\}", str(n.get("text") or "")):
                if "." not in text and text not in bindings["texts"]:
                    problems.append(f"{at}: {{{{{text}}}}} isn't the model's (its texts: {', '.join(bindings['texts']) or 'none'})")
            if kind == "list":
                has_list = True
                src = (n.get("list") or {}).get("source", "items")
                if bindings["lists"] and src not in bindings["lists"]:
                    problems.append(f"{at}: its list.source is one of {', '.join(bindings['lists'])}")
            for a in n.get("actions") or []:
                if not isinstance(a, dict) or a.get("do") not in UI_ACTIONS:
                    problems.append(f"{at}: an action's do is one of {', '.join(sorted(UI_ACTIONS))}")
            if kind == "button" and not n.get("actions"):
                problems.append(f"{at}: a button that does nothing (a menu's choices are its list)")
            for c in n.get("children") or []:
                visit(c, inner_w, inner_h, at)

        for n in design["root"]:
            visit(n, 1920.0, 1080.0, "root")
        for path in sizes:
            if path not in used:
                problems.append(f"use the image {path} where its use says")
        if model in ("menu", "dialogue") and bindings["lists"] and not has_list:
            problems.append("a list element shows the model's choices (" + ", ".join(bindings["lists"]) + ")")
        return problems

    out = _step(provider, UIPANEL_TASK, given, UIPANEL_SHAPE, "*", check, inputs.get("problems"))
    if out.get("ok"):
        answer = out.pop("*", {}) if isinstance(out.get("*"), dict) else {}
        design = answer.get("design") or {}

        def sized(n: dict) -> None:
            # A sliced picture: its size in pixels, for the engine to cut it.
            if isinstance(n, dict):
                if n.get("image") in sizes:
                    n["imgW"], n["imgH"] = sizes[n["image"]]
                    if not n.get("slice"):
                        n.pop("slice", None)
                for c in n.get("children") or []:
                    sized(c)

        for n in design.get("root") or []:
            sized(n)
        if model == "hud":
            # What the HUD shows that isn't laid out (over people's heads, the quest markers, the notifications): the
            # engine's own, kept as they are.
            style = re.search(r"<style>.*?</style>", reference, re.S)
            over = re.findall(r'<div class="(?:over [^"]*|marker)".*?</div>', reference, re.S)
            notes = re.search(r'<div data-model="notes">.*?(?=</body>)', reference, re.S)
            whole = 'style="position: absolute; left: 0; top: 0; right: 0; bottom: 0;"'
            block = (f'<div data-model="hud" class="hud" {whole}>\n' + "\n".join(over) + "\n</div>\n"
                     + (notes.group(0).replace('<div data-model="notes">', f'<div data-model="notes" class="hud" {whole}>', 1).rstrip()
                        if notes else ""))
            if over or notes:
                design.setdefault("root", []).append({"id": "engine-hud", "type": "rml", "name": "Over heads, markers, notifications (the engine's)",
                                                      "rml": block})
            if style:
                design["head"] = "\t" + style.group(0)
        design.update({"schema": "aige.ui-design/1", "id": inputs.get("screen") or "", "model": model, "width": 1920, "height": 1080})
        out["design"], out["notes"] = design, answer.get("notes", "")
    return out


def fix_rcss_decorators(rml: str, images: list) -> str:
    """CSS-style decorators RmlUi doesn't read, written as it does: image(src: "x") as image("x"), and
    ninepatch(src: "x", l: .., t: .., r: .., b: ..) as a @spritesheet of x (its size in pixels from the images
    given, else 256 square) and ninepatch(outer, inner)."""
    rml = re.sub(r'image\(\s*src\s*:\s*("[^"]*"|[^,)\s]+)\s*\)', lambda m: f"image({m.group(1)})", rml)
    # RCSS borders are a width and a colour: no style keyword (border: 2dp solid #657078 isn't read).
    rml = re.sub(r"(\bborder(?:-top|-right|-bottom|-left)?\s*:[^;{}]*?)\s+(?:solid|dashed|dotted|double|groove|ridge|inset|outset)\b",
                 r"\1", rml)
    sizes = {}
    for im in images:
        if isinstance(im, dict) and im.get("file"):
            sizes[str(im["file"])] = (int(im.get("width") or 0), int(im.get("height") or 0))
    sheets = []

    def nine(m: re.Match) -> str:
        inner = m.group(1)
        # Already sprites (ninepatch(a-outer, a-inner[, edges])): as it is.
        if re.fullmatch(r"\s*[\w-]+\s*,\s*[\w-]+\s*(,.*)?", inner, re.S):
            return m.group(0)
        # Its picture: src: x, image(x) or a quoted path first.
        found = (re.search(r'\bsrc\s*:\s*"?([^",)\s]+)"?', inner) or re.search(r'image\(\s*"?([^")\s]+)"?\s*\)', inner)
                 or re.match(r'\s*"([^"]+)"', inner))
        if not found:
            return m.group(0)
        src = found.group(1)
        rest = inner[found.end():]
        w, h = sizes.get(src, (0, 0))
        w, h = w or 256, h or 256
        named = dict(re.findall(r"\b([ltrb])\s*:\s*(-?\d+(?:\.\d+)?)", rest))
        given = [int(float(n)) for n in re.findall(r"(?<![\w.-])(\d+(?:\.\d+)?)\s*(?:px|dp)?", rest)] if not named else []
        edge = max(8, min(w, h) // 8)
        if named:
            l, t, r, b = (int(float(named.get(k, edge))) for k in "ltrb")
        elif len(given) >= 4:
            t, r, b, l = given[:4]  # CSS order
        elif given:
            l = t = r = b = given[0]
        else:
            l = t = r = b = edge
        name = "nine" + str(len(sheets) + 1)
        sheets.append(f"\t\t@spritesheet {name} {{ src: {src}; {name}-outer: 0px 0px {w}px {h}px; "
                      f"{name}-inner: {l}px {t}px {max(1, w - l - r)}px {max(1, h - t - b)}px; }}")
        return f"ninepatch({name}-outer, {name}-inner)"

    rml = re.sub(r"ninepatch\(((?:[^()]|\([^()]*\))*)\)", nine, rml)
    if sheets:
        rml = rml.replace("<style>", "<style>\n" + "\n".join(sheets), 1)
    return rml


INPUT_TASK = """⑦ Systems ▸ Input: the game's controls, designed as a whole. Given the Brief (genre, platform, who plays),
the engine's controls ("defaults": each action, its context and keys), what the game has now ("current"), the keys its
features ask for ("planned"), its screens and features, and every action name its scripts and screens use ("used"),
design every action the game has (the engine's included: each of "defaults", rebound where it clashes): {"name": the action's name (the engine's, or one in "used" / "planned"), "also":
other names the game's scripts or screens use for the very same thing (open-quest-tracker and show_quest_tracker: one
action, the other in also), "description": what it does for the player, in a few words, "context": gameplay | dialogue
| menu | cutscene | any, "keys": keyboard and mouse keys (names from keyNames), "pad": gamepad buttons (Pad...
names), "screen": the screen it opens or closes (inventory, journal, map, character, a game's own screen) or "",
"why": one line}. Keep the genre's conventions (WASD to move, E to interact, I inventory, J journal, M map, Tab or C
character, Esc the menu; the left stick, A to interact, Start the menu), keep each key to one action in a context
(the 'any' context counts in every context), put what's used often under the left hand near WASD, open each screen the
game has with one key, and leave nothing in "used" or "planned" without its action. Movement (move_x, move_y) stays
as the engine has it."""

INPUT_SHAPE = """{"input": {"actions": [{"name": "", "also": [], "description": "", "context": "gameplay", "keys": ["E"], "pad": ["PadA"],
   "screen": "", "why": ""}], "summary": ""}, "rationale": ""}"""

INPUT_CONTEXTS = {"gameplay", "dialogue", "menu", "cutscene", "any"}


def plan_input(provider: Provider, inputs: dict) -> dict:
    keys = {str(k) for k in inputs.get("keyNames") or []}
    used = {str(n) for n in inputs.get("used") or []}
    planned = {str((p.get("action") or {}).get("name")) for p in inputs.get("planned") or [] if isinstance(p, dict)} - {"None", ""}
    engine = {str(a.get("name")) for a in inputs.get("defaults") or [] if isinstance(a, dict) and a.get("name")}

    def check(answer: dict) -> list[str]:
        design = answer.get("input")
        if not isinstance(design, dict) or not isinstance(design.get("actions"), list) or not design["actions"]:
            return ["'input' needs its actions"]
        problems, named, bound = [], set(), {}
        for i, a in enumerate(design["actions"]):
            if not isinstance(a, dict) or not str(a.get("name") or "").strip():
                problems.append(f"actions[{i}] needs its name")
                continue
            name = str(a["name"])
            if name in named:
                problems.append(f"actions[{i}]: {name} is designed twice")
            group = {name} | {str(n) for n in a.get("also") or []}
            named |= group
            context = str(a.get("context") or "gameplay")
            if context not in INPUT_CONTEXTS:
                problems.append(f"actions[{i}] ({name}).context is one of {', '.join(sorted(INPUT_CONTEXTS))}")
            if name in ("move_x", "move_y"):
                continue
            for field in ("keys", "pad"):
                for k in a.get(field) or []:
                    if keys and str(k) not in keys:
                        problems.append(f"actions[{i}] ({name}).{field}: {k!r} isn't a key name")
                        continue
                    for ctx in ([context] if context != "any" else sorted(INPUT_CONTEXTS)):
                        for other_ctx in (ctx, "any"):
                            other = bound.get((other_ctx, str(k)))
                            if other and other != name:
                                problems.append(f"{k} is both {other}'s and {name}'s in {ctx}: one key, one action in a context")
                    bound[(context, str(k))] = name
            if not (a.get("keys") or a.get("pad")):
                problems.append(f"actions[{i}] ({name}) needs a key")
        missing = sorted((used | planned) - named)
        if missing:
            problems.append(f"these names the game uses have no action (as a name or in also): {', '.join(missing)}")
        # The engine's own actions keep their keys unless designed: every one is designed too (or its keys would clash).
        left = sorted(e for e in engine if e not in named)
        if left:
            problems.append(f"the engine's actions are part of the controls too, each with its keys (rebound if they "
                            f"clash): add {', '.join(left)}")
        return sorted(set(problems))

    return _step(provider, INPUT_TASK, inputs, INPUT_SHAPE, "input", check, inputs.get("problems"))


REVIEW_TASK = """⑨ Validate, the critic: read the Brief, the story outline, the cast and the places, and the issues
the engine found. Judge what rules can't: tone and theme kept, the world's own rules respected, the genre's
promise met, the audience respected, names distinct, nothing dull or contradictory. Few findings, each {subject
(an id), severity (warning, or error only for something that breaks the game), message, fix (what to change)}."""

REVIEW_SHAPE = """{"findings": [{"subject": "character/x", "severity": "warning", "message": "", "fix": ""}], "rationale": ""}"""


def plan_review(provider: Provider, inputs: dict) -> dict:
    return _step(provider, REVIEW_TASK, inputs, REVIEW_SHAPE, "findings",
                 lambda a: [] if isinstance(a.get("findings"), list) else ["'findings' must be a list"], inputs.get("problems"))


REVISE_TASK = """A change to the plan, in the user's words: think through the steps agentically, then turn it into edits of the plan's documents. Each edit is
{doc (a document from index or documents), op (add|move|replace), pointer (a JSON pointer into it, e.g. "/biography/need",
"/relationships/0/type", "/card/goal"), value (the new value for add/replace), from (for move), reason}. 
Change only what the words ask, in the documents that hold it (a character's family in its relationships; a scene's events in its card); the engine marks
what depends on the changed documents for regenerating. If the words ask for nothing the documents hold, answer
no edits and say why in the rationale.
"value" is the new value at the pointer, of the same type and shape as the value it replaces (its document's
"structure" says it; "schema" is how the step writes its documents: every field they can have, and what an empty list
holds — a cutscene the words ask for goes into the timeline's "cutscenes" as one item of the schema's shape, not into a
character's changes): a list stays a list of the same kind of items, an object an object with its fields, a text a
text. Point at the smallest part that changes ("/stages/0/journal", "/stages/0/objectives/0") rather than a whole list;
to add an item to a list point at its end ("/stages/0/objectives/-") and give one item of its kind; never write a list
or an object out as text.

For scene splitting, think through the steps first, then use generic edits:
Example: "Split s1-01 into two beats, making four in act 1" →
1. rename s1-03 to s1-04 (shift the last scene up)
2. rename s1-02 to s1-03 (shift the middle scene)
3. copy s1-01 to s1-02 (the new second beat, then trim it)
4. edit s1-01 to only arrival+Sella (trim first)
5. edit s1-02 to only questioning (trim second)

Document operations act on WHOLE documents (use them only when the request says so — renaming a page/item to a new id, copying one, making a new one, deleting one):
- {"op":"rename","doc":"story/scenes/s1-03","to":"story/scenes/s1-04","reason":"..."} — the document (and its file) is renamed; use it ONLY when the request says to rename/move an item to a new id ("move s1-03 to s1-04", "rename the market to the bazaar"). References that list the old id follow.
- {"op":"copy","doc":"story/scenes/s1-01","to":"story/scenes/s1-02","reason":"..."} — a new document that is a copy of `doc`.
- {"op":"create","doc":"story/scenes/s1-05","value":{<the whole new document>},"reason":"..."} — a brand-new document.
- {"op":"delete","doc":"story/scenes/s1-05","reason":"..."} — remove a document.
Field edits (add|replace|move with a pointer) change one field inside a document. Apply document operations in the right order (rename the higher ids first, then copy into the freed id).

Adding a scene to an act needs TWO edits, always together: create the scene document, AND add its id to the act's chapter scene list. A scene not listed in its act's chapters does not appear.
  Example — "Add a scene to act 1":
  {"op":"create","doc":"story/scenes/s1-05","value":{"schema":"aige.scene/1","id":"s1-05","act":"act-1","order":105.0,"card":{"goal":"","conflict":"","outcome":"","mode":"gameplay","location":"location/bellwether","participants":[]},"summary":""},"reason":"new scene"}
  {"op":"add","doc":"story/acts/act-1","pointer":"/chapters/0/scenes/-","value":"s1-05","reason":"list the new scene in the act"}
Removing a scene: delete the document AND take its id out of its act's chapters[].scenes. Renaming a scene: rename the document AND its id is followed in the act (the engine does that), so no act edit is needed for a rename."""

REVISE_SHAPE = """{"edits": [
  {"doc": "story/scenes/s1-03", "op": "rename", "to": "story/scenes/s1-04", "reason": "shift s1-03 to s1-04"},
  {"doc": "story/scenes/s1-02", "op": "rename", "to": "story/scenes/s1-03", "reason": "shift s1-02 to s1-03"},
  {"doc": "story/scenes/s1-01", "op": "copy", "to": "story/scenes/s1-02", "reason": "new second beat, a copy of s1-01"},
  {"doc": "story/scenes/s1-01", "op": "replace", "pointer": "/card/goal", "value": "Arrive and hear Elder Sella", "reason": "trim to arrival only"},
  {"doc": "story/scenes/s1-02", "op": "replace", "pointer": "/card/goal", "value": "Question villagers and shopkeepers", "reason": "trim to questioning"}
 ], "rationale": "split s1-01 into arrival and questioning beats"}"""


REVISE_PICK_TASK = """A change to one step of the game's plan (or of its build), in the user's words. First make a short ordered plan
("steps") of what must happen (think through it agentically), then find what it's about: of this step's documents ("index": each document, its name and its fields; "schema": every field a document of this step can have, the empty ones too), which ones the words change and which of their fields (as JSON pointers, "/biography/need", "/stages/2/journal", "/characters/3/spot"), and why. Only what the words ask: "make Jory a fisherman" is Jory's document alone, not the others; a change that the words make to several (every merchant) names each. None when the words ask for nothing this step holds.
A request to rename, copy or delete a whole item ("move s1-03 to s1-04", "duplicate the inn", "remove the market") targets that item's document.
Adding or removing a scene also targets its act (the act's chapters list the scene ids); include the act document (story/acts/<act>) among the targets.

For scene splitting, the steps should describe the edits in order (renaming scenes, copying a scene, then editing the copies):
Example: "Split s1-01 into two beats" → steps: ["rename s1-03 to s1-04", "rename s1-02 to s1-03", "copy s1-01 to s1-02", "edit s1-01 to arrival only", "edit s1-02 to questioning only"]"""

REVISE_PICK_SHAPE = """{"steps": ["rename s1-03 to s1-04", "rename s1-02 to s1-03", "copy s1-01 to s1-02", "edit s1-01 to arrival only", "edit s1-02 to questioning only"],
 "targets": [{"doc": "story/scenes/s1-01", "fields": ["/card/goal", "/card/conflict", "/card/outcome"], "why": "trim to arrival only, and copy it to s1-02"},
            {"doc": "story/scenes/s1-02", "fields": ["/card/goal", "/card/conflict", "/card/outcome"], "why": "the copied beat, renamed to s1-03"},
            {"doc": "story/scenes/s1-03", "fields": [], "why": "renamed to s1-04"}], "rationale": ""}"""


def plan_revise_one(provider: Provider, inputs: dict) -> dict:
    """One revise pass over the documents: pick the docs the words are about, then edit only those."""
    if inputs.get("scope"):
        return _revise_step(provider, inputs)
    known = {d.get("doc") for d in inputs.get("index", [])} | set((inputs.get("documents") or {}).keys())

    def check(answer: dict) -> list[str]:
        edits = answer.get("edits")
        if not isinstance(edits, list):
            return ["'edits' must be a list"]
        problems = []
        for e in edits:
            if not isinstance(e, dict):
                problems.append("an edit must be an object")
                continue
            op = e.get("op", "replace")
            doc = e.get("doc", "")
            if op in ("rename", "copy"):
                if known and doc not in known:
                    problems.append(f"no document {doc!r}")
                if not str(e.get("to", "")).strip():
                    problems.append(f"op '{op}' needs 'to' (the new document id)")
            elif op == "create":
                if not doc:
                    problems.append("op 'create' needs 'doc'")
                if not isinstance(e.get("value"), dict):
                    problems.append("op 'create' needs 'value' (the whole new document)")
            elif op == "delete":
                if known and doc not in known:
                    problems.append(f"no document {doc!r}")
            elif op in ("add", "replace", "move"):
                if known and doc not in known:
                    problems.append(f"no document {doc!r}")
                if not str(e.get("pointer", "")).startswith("/"):
                    problems.append(f"pointer {e.get('pointer')!r} must start with /")
                if op in ("add", "replace") and "value" not in e:
                    problems.append(f"op '{op}' requires 'value'")
                if op == "move" and "from" not in e:
                    problems.append("op 'move' requires 'from'")
                if isinstance(inputs.get("documents"), dict) and doc in inputs["documents"]:
                    problems += _structure_problems(doc, inputs["documents"][doc], str(e.get("pointer", "")), e.get("value"))
            else:
                problems.append(f"op {op!r} must be one of add, replace, move, rename, copy, create, delete")
        return problems

    return _step(provider, REVISE_TASK, inputs, REVISE_SHAPE, "edits", check, inputs.get("problems"))


def _structure(value: Any, depth: int = 0) -> Any:
    """A document's shape, for an editor to keep: its fields and their kinds, a list by its first item."""
    if isinstance(value, dict):
        return {k: _structure(v, depth + 1) for k, v in value.items()} if depth < 6 else "object"
    if isinstance(value, list):
        return [_structure(value[0], depth + 1)] if value else []
    if isinstance(value, bool):
        return "true/false"
    if isinstance(value, (int, float)):
        return "number"
    return "text" if isinstance(value, str) else "null"


def _at(doc: Any, pointer: str) -> tuple[bool, Any, Any]:
    """(found, the value at a JSON pointer, its parent); "-" at the end: the list it would be added to."""
    parts = [p.replace("~1", "/").replace("~0", "~") for p in pointer.split("/")[1:]]
    cur, parent = doc, None
    for i, p in enumerate(parts):
        parent = cur
        if isinstance(cur, list):
            if p == "-" and i == len(parts) - 1:
                return False, None, cur
            if not p.isdigit() or int(p) >= len(cur):
                return False, None, parent
            cur = cur[int(p)]
        elif isinstance(cur, dict):
            if p not in cur:
                return False, None, parent
            cur = cur[p]
        else:
            return False, None, parent
    return True, cur, parent


def _kind(value: Any) -> str:
    return ("object" if isinstance(value, dict) else "list" if isinstance(value, list) else "true/false" if isinstance(value, bool)
            else "number" if isinstance(value, (int, float)) else "text" if isinstance(value, str) else "null")


def _structure_problems(where: str, doc: Any, pointer: str, value: Any) -> list[str]:
    """An edit that would change a field's kind (a list of stages written out as text) or add the wrong kind of item."""
    found, old, parent = _at(doc, pointer)
    if found and old is not None and _kind(old) != _kind(value) and not (_kind(old) == "number" and _kind(value) == "number"):
        return [f"{where}: {pointer} is {_kind(old)} ({json.dumps(_structure(old), ensure_ascii=False)[:300]}); keep its structure: "
                f"give a {_kind(old)} of the same shape, or point at the part that changes"]
    if found and isinstance(old, list) and isinstance(value, list) and old and value and _kind(old[0]) != _kind(value[0]):
        return [f"{where}: {pointer} is a list of {_kind(old[0])}; keep its items' kind"]
    if not found and pointer.endswith("/-") and isinstance(parent, list) and parent and _kind(parent[0]) != _kind(value):
        return [f"{where}: {pointer[:-2]} is a list of {_kind(parent[0])}: add one {_kind(parent[0])} of the same shape"]
    return []


# Each step's documents, as the step writes them: what a revision may put in them (a field not there yet, an empty
# list's items: a cutscene in the timeline's empty "cutscenes"), by the Revise box's step.
REVISE_SCHEMAS = {
    "concept": "CONCEPT_SHAPE", "outline": "OUTLINE_SHAPE", "story": ("ACT_SHAPE", "SCENE_SHAPE"), "characters": "CHARACTER_SHAPE",
    "quests": "QUESTS_SHAPE", "places": "PLACE_SHAPE", "timeline": "TIMELINE_SHAPE", "cutscenes": "TIMELINE_SHAPE",
    "objects": "OBJECTS_SHAPE", "smallscenes": "SCENES_SHAPE",
    "systems": ("GAMEPLAY_SHAPE", "SYSTEM_SHAPE", "BEHAVIOUR_SHAPE", "SYSTEMS_SHAPE", "INPUT_SHAPE"), "visuals": "VISUALS_SHAPE",
    "build:placements": "PLACEMENTS_SHAPE", "build:requirements": "REQUIREMENTS_SHAPE", "build:cast": "REQUIREMENTS_SHAPE",
    "build:gameplay": "IMPLEMENT_SHAPE",
}


def _schema_of(step: str) -> list[str]:
    names = REVISE_SCHEMAS.get(step, ())
    return [globals()[n] for n in ((names,) if isinstance(names, str) else names) if n in globals()]


def _revise_step(provider: Provider, inputs: dict) -> dict:
    """One step's revision, as an agent would: what the words are about (its documents and fields), then the edits
    to those alone, each checked against the document it changes."""
    documents = inputs.get("documents") or {}
    index = inputs.get("index") or [{"doc": d, "fields": sorted(v.keys()) if isinstance(v, dict) else []} for d, v in documents.items()]
    usage = Usage()
    schema = _schema_of(str(inputs.get("scope") or ""))
    pick_inputs = {"instruction": inputs.get("instruction"), "step": inputs.get("scope"), "index": index,
                   "schema": schema, "brief": inputs.get("brief")}

    def check_pick(answer: dict) -> list[str]:
        targets = answer.get("targets")
        if not isinstance(targets, list):
            return ["'targets' must be a list"]
        return [f"targets[{i}].doc {t.get('doc')!r} isn't one of this step's documents" for i, t in enumerate(targets)
                if not isinstance(t, dict) or t.get("doc") not in documents]

    picked = _step(provider, REVISE_PICK_TASK, pick_inputs, REVISE_PICK_SHAPE, "targets", check_pick)
    usage.add(Usage(**picked.get("usage", {})))
    if not picked.get("ok"):
        return {**picked, "usage": usage.model_dump()}
    targets = [t for t in picked.get("targets") or [] if isinstance(t, dict) and t.get("doc") in documents]
    chosen = {t["doc"]: documents[t["doc"]] for t in targets}
    # The act always lists its scene ids: added, removed or renamed, a scene changes its act too, so the
    # act documents are always editable in a Story revision (a new scene's id goes into chapters[].scenes).
    if str(inputs.get("scope") or "") == "story":
        have = {t["doc"] for t in targets}
        for d, v in documents.items():
            if d.startswith("story/acts/") and d not in have:
                targets.append({"doc": d, "fields": ["chapters"], "why": "the act lists its scenes"})
                chosen[d] = v
    if not chosen:
        return {"ok": True, "edits": [], "targets": [], "rationale": picked.get("rationale", "nothing in this step to change"),
                "usage": usage.model_dump()}
    edit_inputs = {"instruction": inputs.get("instruction"), "step": inputs.get("scope"), "targets": targets, "documents": chosen,
                   "structure": {d: _structure(v) for d, v in chosen.items()},
                   "schema": schema,
                   "brief": inputs.get("brief"), "context": inputs.get("context")}

    def check_edits(answer: dict) -> list[str]:
        edits = answer.get("edits")
        if not isinstance(edits, list):
            return ["'edits' must be a list"]
        problems = []
        for i, e in enumerate(edits):
            if not isinstance(e, dict):
                problems.append(f"edits[{i}] must be an object")
                continue
            op = e.get("op", "replace")
            doc = e.get("doc", "")
            if op in ("rename", "copy"):
                # A whole document renamed/copied: the source must be one of this step's documents.
                if doc not in documents:
                    problems.append(f"edits[{i}].doc {doc!r} isn't one of this step's documents")
                if not str(e.get("to", "")).strip():
                    problems.append(f"edits[{i}] op '{op}' needs 'to' (the new document id)")
            elif op == "create":
                if not doc:
                    problems.append(f"edits[{i}] op 'create' needs 'doc'")
                if not isinstance(e.get("value"), dict):
                    problems.append(f"edits[{i}] op 'create' needs 'value' (the whole new document)")
            elif op == "delete":
                if doc not in documents:
                    problems.append(f"edits[{i}].doc {doc!r} isn't one of this step's documents")
            elif op in ("add", "replace", "move"):
                if doc not in chosen:
                    problems.append(f"edits[{i}].doc must be one of {', '.join(chosen)} (the documents the words are about)")
                elif not str(e.get("pointer", "")).startswith("/"):
                    problems.append(f"edits[{i}].pointer {e.get('pointer')!r} must start with /")
                else:
                    problems += _structure_problems(f"edits[{i}]", chosen[doc], str(e["pointer"]), e.get("value"))
            else:
                problems.append(f"edits[{i}].op {op!r} must be one of add, replace, move, rename, copy, create, delete")
        return problems

    task = (REVISE_TASK + "\nChange only the documents and fields in \"targets\" (what the words were found to be about); keep "
            "everything else in them as it is. A value is the whole new value at its pointer.")
    made = _step(provider, task, edit_inputs, REVISE_SHAPE, "edits", check_edits, inputs.get("problems"))
    usage.add(Usage(**made.get("usage", {})))
    return {**made, "targets": targets, "usage": usage.model_dump()}


DIALOGUE_TASK = """⑫ Dialogue: write one character's conversation tree for the engine, now that the world is
built. In the character's voice (its voice and sample line), about what it knows (knows) and the scenes it talks in
(scenes), never mentioning what it doesn't know. If it gives quests (gives), offer each with a choice whose effects
start it ({"op": "start_quest", "name": "<quest id>"}) and whose conditions say it hasn't started
({"quest": "<quest id>", "is": "inactive"}); add a line for when it's under way ({"quest": id, "is": "active"} in
starts) and when it's done ("completed"). Nodes: {id, text, choices: [{text, next (a node id, or empty to end),
conditions, effects}], next}; starts: [{conditions, node}] checked before start. Short lines; a few nodes."""

DIALOGUE_SHAPE = """{"dialogue": {"start": "greet",
   "starts": [{"conditions": [{"quest": "q-main", "is": "active"}], "node": "progress"}],
   "nodes": [{"id": "greet", "text": "", "choices": [{"text": "", "next": "", "conditions": [], "effects": []}]},
             {"id": "progress", "text": "", "next": ""}]},
 "rationale": ""}"""


def plan_dialogue(provider: Provider, inputs: dict) -> dict:
    quests = {q.get("id") for q in inputs.get("gives", [])}

    def check(answer: dict) -> list[str]:
        d = answer.get("dialogue")
        if not isinstance(d, dict) or not d.get("nodes"):
            return ["'dialogue' needs nodes"]
        ids = {n.get("id") for n in d["nodes"]}
        problems = []
        if d.get("start") not in ids:
            problems.append(f"start {d.get('start')!r} isn't a node")
        for n in d["nodes"]:
            if n.get("next") and n["next"] not in ids:
                problems.append(f"node {n.get('id')}: next {n['next']!r} isn't a node")
            for c in n.get("choices", []):
                if c.get("next") and c["next"] not in ids:
                    problems.append(f"node {n.get('id')}: choice goes to {c['next']!r}, which isn't a node")
                for e in c.get("effects", []):
                    if e.get("op") == "start_quest" and quests and e.get("name") not in quests:
                        problems.append(f"node {n.get('id')}: it starts {e.get('name')!r}, which it doesn't give")
        return problems

    return _step(provider, DIALOGUE_TASK, inputs, DIALOGUE_SHAPE, "dialogue", check, inputs.get("problems"))


BUILDCHECK_TASK = """⑬ Validate: one scene of the story (its card, beats and events) against what the game build made for it
(built: its quest stages with each objective's role and what that role was bound to in the game, its cast with each one's
prefab and home place, its cutscene, packages, gameplay designs and scripts). Say what the player can't see or do that the
scene says happens, or that the game does differently: someone who should be there and isn't (a character's home is
another place and no quest stage brings them), a role bound to "any enemy" or the wrong one when the scene names who, an
event (an attack, a meeting, a gift) that nothing in the game makes happen, an item or place the scene uses that isn't
there, a stage whose objectives don't match what the scene has the player do. Don't repeat alreadyFound; don't report
what the build can't hold (feelings, thoughts) or what a later scene covers. Each finding: severity (error: the scene
can't happen as written; warning: it happens differently), kind (missing|wrong), what (short), detail (the scene's words
against what was built), and fix:
- {"how": "plan", "edits": [{doc, pointer, value, reason}]}: edits of the quests' documents given (doc: their "doc",
  pointer: a JSON pointer, "-" appends to an array). Use stageEffects for what happens as a stage begins (e.g. a character
  brought to the scene's place, made hostile, an enemy spawned) and an alias's fill to bind a role to a named character.
  Prefer this when the documents can say it.
- {"how": "game", "request": "...", "scene": "scenes/x.aige"}: an instruction for the AI that edits the game (a script,
  a trigger, a behaviour) when the plan's documents can't say it: what to make, where, when (which quest stage or event),
  naming the characters and markers.
- {"how": "none"}: when it's for the user to decide.
Never move, show or hide the player (they travel by themselves): put the place's things where they go. Don't move
a vendor, merchant or healer away from their shop: have the player go to them (a talk objective). An edit's value is
what goes there, as it is: a text for a fill, a number for a count, an effect object for an onEnter.
No findings when the build does what the scene says."""

BUILDCHECK_SHAPE = """{"findings": [{"severity": "error", "kind": "wrong", "what": "", "detail": "",
  "fix": {"how": "plan", "edits": [{"doc": "quests/q-main", "pointer": "/stages/1/onEnter/-",
  "value": {"op": "move_actor", "who": "character/x", "to": "location/y", "anchor": "arrive"}, "reason": ""}]}}],
  "rationale": ""}"""


def _clean_fixes(result: dict, key: str) -> dict:
    """Plan edits' values as the model meant them: shaping an answer after the example (whose value is a stage effect)
    wraps a plain value as {"op": value}."""
    found = result.get(key)
    if isinstance(found, dict):
        found = found.get("findings")
    for f in found if isinstance(found, list) else []:
        fix = f.get("fix") if isinstance(f, dict) else None
        for e in (fix.get("edits") or []) if isinstance(fix, dict) else []:
            value = e.get("value") if isinstance(e, dict) else None
            if isinstance(value, dict) and list(value) == ["op"] and not isinstance(value["op"], dict):
                e["value"] = value["op"]
    return result


def plan_buildcheck(provider: Provider, inputs: dict) -> dict:
    docs = {q.get("doc") for q in inputs.get("quests", []) if isinstance(q, dict)}

    def check(answer: dict) -> list[str]:
        found = answer.get("findings")
        if not isinstance(found, list):
            return ["'findings' must be a list"]
        problems = []
        for i, f in enumerate(found):
            fix = f.get("fix") if isinstance(f, dict) else None
            if not isinstance(fix, dict) or fix.get("how") not in ("plan", "game", "none"):
                problems.append(f"findings[{i}].fix.how must be plan, game or none")
                continue
            if fix.get("how") == "plan":
                for e in fix.get("edits") or []:
                    if not isinstance(e, dict) or e.get("doc") not in docs or not str(e.get("pointer", "")).startswith("/"):
                        problems.append(f"findings[{i}]: a plan edit's doc is one of {sorted(d for d in docs if d)} and its pointer starts with /")
            if fix.get("how") == "game" and not str(fix.get("request", "")).strip():
                problems.append(f"findings[{i}]: a game fix needs its request")
        return problems

    return _clean_fixes(_step(provider, BUILDCHECK_TASK, inputs, BUILDCHECK_SHAPE, "findings", check, inputs.get("problems")), "findings")


JUDGETEST_TASK = """⑬ Test: one scene of the story was played by an autopilot, from where the scene begins (each quest at its
stage, the player at the scene's place). The log has the autopilot's steps (what it went to and did: it walks to each
objective of the scene's quest stage and does it; fights are ended by the test, so a defeat says the target was there
and could be fought), the game's events (deaths, talks, quest stages, objectives done, items), the conversations' lines,
script errors, and the quest's stage before and after. Judge it against the scene (its card's goal and outcome, its
beats and events): passed when what the scene says happens did happen in the game (the right characters were there and
did their part, the stage advanced, no script errors broke it); else say what didn't happen. Each finding as in
Validate: severity, kind (missing|wrong), what, detail (the scene's words against the log), and a fix: {"how": "plan",
"edits": [...]} on the quests' documents given (stage effects: a character moved, made hostile, an enemy spawned; an
alias's fill), or {"how": "game", "request", "scene"} for the AI that edits the game (a script error fixed, a line of
dialogue, a trigger), or {"how": "none"}. Don't report the autopilot's own shortcuts (the fight ended for it, being
placed beside a target)."""

JUDGETEST_SHAPE = """{"verdict": {"passed": true, "summary": "", "findings": [{"severity": "error", "kind": "missing", "what": "",
  "detail": "", "fix": {"how": "game", "request": "", "scene": "scenes/x.aige"}}]}, "rationale": ""}"""


def plan_judgetest(provider: Provider, inputs: dict) -> dict:
    def check(answer: dict) -> list[str]:
        verdict = answer.get("verdict")
        if not isinstance(verdict, dict) or not isinstance(verdict.get("passed"), bool):
            return ["'verdict' must be {passed: true|false, summary, findings}"]
        if not isinstance(verdict.get("findings", []), list):
            return ["'verdict.findings' must be a list"]
        return []

    return _clean_fixes(_step(provider, JUDGETEST_TASK, inputs, JUDGETEST_SHAPE, "verdict", check, inputs.get("problems")), "verdict")


CUTSCENE_TASK = """⑫ Stage one scene of the story as a cutscene the player watches: what the scene says happens, played out in
its place with its cast. Given: the scene (its goal, outcome, beats and events), its cast (each one's key, name, whether
it's the player), the place's markers (where things are: its parts, where each of the cast stands for this scene,
story/<scene>/<who>), and the quest stage it plays at. Write {"name", "world": "waits" (everything else holds: the usual)
or "runs" (the world goes on around it: a fight, a crowd), "tracks": [...]}; each track {"type", "actor" (actor tracks:
a cast key or "player"), "keys": [{"at": seconds from the start, "op", ...}]}:
- actor: move {marker, seconds (it takes this long) or speed, path: true to walk round what's in the way}, place {marker}
  (there at once), face {target: a cast key, "player" or a marker}, show, hide, animate {trigger}
- dialogue: line {speaker: a cast key, text, seconds}; talk {with: a cast key} (their real conversation, with the
  player's choices; the cutscene waits for it to end)
- camera: follow {actor}, move {marker, seconds}, zoom {value}, shake {trauma}
- screen: fade_in/fade_out {seconds}, title {text, seconds}
- logic: do {do: story effects, as dialogue's: [{"op": "set_flag", "flag": "..."}]}, emit {event}, wait_until
  {until: {"moves": true} (everyone at their mark) | {"talk": true} (a conversation over) | {"event": name} | {"flag": name},
  max: seconds}
Stage it as a film: open on the place (fade in, the camera on where it happens), bring the cast to their marks, play each
beat (who says what, who goes where, what's shown), and close (fade out). Lines in each character's own voice, short,
from the beats; every beat of the scene shown or said. Only the cast's keys, "player" and the markers given. Don't
change the quests (no set_stage); a flag or event the scene's outcome needs (the haunting ends) is fine. 20 to 90 seconds."""

CUTSCENE_SHAPE = """{"cutscene": {"name": "", "world": "waits", "tracks": [{"type": "screen", "keys": [{"at": 0, "op": "fade_in", "seconds": 1}]},
  {"type": "actor", "actor": "character/x", "keys": [{"at": 1, "op": "move", "marker": "story/s1/x", "seconds": 3, "path": true}]},
  {"type": "dialogue", "keys": [{"at": 4, "op": "line", "speaker": "character/x", "text": "", "seconds": 3}]}]}, "rationale": ""}"""

CUTSCENE_OPS = {
    "actor": {"move", "face", "show", "hide", "place", "animate"},
    "dialogue": {"line", "talk"},
    "camera": {"follow", "move", "shake", "zoom"},
    "screen": {"fade_out", "fade_in", "title", "letterbox"},
    "logic": {"do", "emit", "wait_until"},
    "audio": {"music", "sound"},
}


def plan_cutscene(provider: Provider, inputs: dict) -> dict:
    cast = {str(c.get("key")) for c in inputs.get("cast") or [] if isinstance(c, dict) and c.get("key")} | {"player"}
    markers = {str(m) for m in inputs.get("markers") or []}

    def check(answer: dict) -> list[str]:
        c = answer.get("cutscene")
        if not isinstance(c, dict) or not isinstance(c.get("tracks"), list) or not c["tracks"]:
            return ["'cutscene' must be {name, world, tracks: [...]}"]
        problems = []
        if c.get("world", "waits") not in ("waits", "runs"):
            problems.append("world is waits or runs")
        end = 0.0
        for i, t in enumerate(c["tracks"]):
            if not isinstance(t, dict) or t.get("type") not in CUTSCENE_OPS:
                problems.append(f"tracks[{i}].type is one of {sorted(CUTSCENE_OPS)}")
                continue
            if t["type"] == "actor" and t.get("actor") not in cast:
                problems.append(f"tracks[{i}].actor {t.get('actor')!r} isn't one of the cast ({', '.join(sorted(cast))})")
            for k, key in enumerate(t.get("keys") or []):
                where = f"tracks[{i}].keys[{k}]"
                if not isinstance(key, dict) or not isinstance(key.get("at"), (int, float)):
                    problems.append(f"{where}: needs at (seconds)")
                    continue
                op = key.get("op", "line" if t["type"] == "dialogue" else "")
                if op not in CUTSCENE_OPS[t["type"]]:
                    problems.append(f"{where}: op {op!r} isn't one of {sorted(CUTSCENE_OPS[t['type']])}")
                end = max(end, float(key["at"]) + float(key.get("seconds") or 0))
                if "marker" in key and str(key["marker"]) not in markers:
                    problems.append(f"{where}: no marker {key['marker']!r} in the place (use one given)")
                for who in (key.get("speaker"), key.get("with"), key.get("actor")):
                    if who is not None and who not in cast:
                        problems.append(f"{where}: {who!r} isn't one of the cast")
                if op == "line" and not str(key.get("text", "")).strip():
                    problems.append(f"{where}: a line needs text")
                if op == "do" and "set_stage" in json.dumps(key.get("do", [])):
                    problems.append(f"{where}: don't set a quest's stage")
        if end > 120:
            problems.append(f"it lasts {end:.0f} seconds: keep it to 90")
        return problems

    return _step(provider, CUTSCENE_TASK, inputs, CUTSCENE_SHAPE, "cutscene", check, inputs.get("problems"))


REQUIREMENTS_TASK = """Requirements (the build's checklist): the plan is the game's requirements document; the build implements
it. For one scene of the story, write what must be true in the game for the scene to happen as written: every event and
beat of the scene turned into requirements that are single, concrete and testable. The ones already made by rule are
given ("given": the cast's presence, the parts it needs, the quest objectives, stages moving on, the way there): don't
repeat them; add what they miss, which is usually how things happen. Examples:
- arrival: "Grey Lament comes out of the graveyard entrance in Bellwether" (where from, when);
- combat: "Grey Lament attacks Ina Crook; it does little damage and Ina lasts until Ashen arrives";
- movement: "Elder Sella follows Ashen into the graveyard and waits by the east gate";
- cutscenes are planned in Life ▸ Cutscenes and given (their requirements made by rule): never write a cutscene
  requirement; a watched moment of the scene is the given cutscene's, and what it needs beyond it (a marker, a prop,
  someone being there) is yours;
- effect: "Spirit-light runs from the broken grave toward the family plots"; arrival: "A skeleton rises out of the broken
  grave as stage 30 begins";
- dialogue: "Sella says the family graves should be sealed when Ashen reaches the family plots";
- quest: "q-main moves from stage 20 to 30 when Grey Lament is defeated";
- gateway / guidance / prop: what the player must see to know where to go ("The graveyard entrance in Bellwether is a
  gate that can be seen, north of the square, and the journal says so");
- asset (visual): each thing the scene shows that must be drawn as its own piece of art and isn't given: "The graveyard
  gate: an iron gate in the village's north wall, chained shut", "a broken grave the skeleton rises from", "a spirit-knot
  of pale light". Give it "asset": {"name": what it's called in the game, "type": prop|gateway|item|creature|effect,
  "look": how it looks, from the scene (its state too: chained, broken, glowing)}. Not the characters (they're drawn
  already) and not the place's ground or its buildings;
- animation (visual): each thing someone or something is seen doing that needs its own animation and isn't given:
  "Grey Lament rises out of the broken grave", "the skeleton claws its way up", "Sella beckons". Give it "animation":
  {"who": a cast key, or an asset's name, "action": what it's seen doing, in a few words ("rising out of a grave")}.
  Walking, attacking, getting hurt and dying are given for whoever fights.
Each requirement: {"kind": presence|arrival|movement|combat|dialogue|cutscene|item|prop|gateway|effect|quest|guidance|asset|animation,
"text": the requirement in a sentence, with the real names, "when": {"quest", "stage"} or {"after": "<another requirement's
text>"} or {"event": "..."} or {"reach": "<marker>"} or {"leave": "<scene>"}, "where": {"scene": "scenes/x.aige", "marker":
one of the place's markers or a new one to make, "from": a gateway or marker it comes from}, "who": [cast keys],
"source": {"event": "<the scene's event id>"} or {"beat": index}, "accept": checks that tell it's done, each one of
{"check": "present", "who", "scene", "when"} | {"check": "enters", "who", "scene", "from"} | {"check": "damages", "who",
"target"} | {"check": "says", "who", "words"} | {"check": "cutscene-plays", "id"} | {"check": "stage-advances", "quest",
"from"} | {"check": "event", "name"} | {"check": "prop", "name", "scene"} | {"check": "gateway-visible", "to"} |
{"check": "journal-names", "quest", "stage", "place"} | {"check": "follows", "who", "target"} | {"check": "asset",
"name"} (drawn) | {"check": "placed", "name", "scene"} (in the scene) | {"check": "animation", "who", "action"}}.
Only the cast given, the place's scene and its markers (or a new marker you name in "where"). Be complete: every event
and beat is covered by a given requirement or one of yours. Don't invent events the scene doesn't have. Read the story
with the characters and the places: a cast member whose "metIn" is a building (a merchant in their shop) is met there,
not in the open: the player goes to them; don't require them to come out unless an event of the scene says they do.
A requirement is something the build makes: a thing that must exist, be placed, be drawn, react or be said in the game.
- The player (the cast member with "player": true) is played, not built: what the story says the player does (fights,
  strikes, uses an item, opens a gate, says a line, compares the reports, decides, goes somewhere) is the player's to do.
  Write what the game must offer for it, about the thing or person it's done to: "Grey Lament is hostile at the
  boundary when stage 20 begins and can be beaten" (not "Ashen strikes Grey Lament"), "the player has the chipped sword
  from the start" (item), "the crypt gate opens when the player uses the family seal on it" (prop), "Elder Sella's
  conversation offers the player the line 'Bellwether will read the names'" (dialogue, who: Elder Sella), and for what
  the player works out or decides, the journal or objective that says it (guidance). Never a requirement whose only
  "who" is the player, and never the player moved, made to fight or made to speak.
- People are cast: a group of people the story mentions (villagers, a repair crew, guards, a crowd, mourners) is cast
  members of the scene (their presence or movement, by their keys), never an asset to draw. An asset is a thing, a
  creature that isn't cast (a skeleton, spirits) or an effect.
- No duplicates: one requirement for one thing (not the same fight or line twice in other words)."""

REQUIREMENTS_SHAPE = """{"requirements": [{"kind": "arrival", "text": "", "when": {"quest": "q-main", "stage": 20},
  "where": {"scene": "scenes/x.aige", "marker": "", "from": ""}, "who": ["character/x"], "source": {"event": ""},
  "accept": [{"check": "enters", "who": "character/x", "scene": "scenes/x.aige", "from": ""}],
  "asset": {"name": "", "type": "prop", "look": ""}, "animation": {"who": "", "action": ""}}], "rationale": ""}"""

REQUIREMENT_KINDS = {"presence", "arrival", "movement", "combat", "dialogue", "cutscene", "item", "prop", "gateway", "effect",
                     "quest", "guidance", "asset", "animation"}
REQUIREMENT_CHECKS = {"present", "enters", "damages", "says", "cutscene-plays", "stage-advances", "event", "prop",
                      "gateway-visible", "journal-names", "follows", "objective", "cutscene", "objective-marker", "asset",
                      "placed", "animation"}
ASSET_TYPES = {"prop", "gateway", "item", "creature", "effect", "character"}


PEOPLE_WORDS = re.compile(r"\b(villagers?|crew|crowd|group of (?:people|men|women|villagers|workers)|guards?|soldiers|mourners|"
                          r"workers|townsfolk|townspeople|people|men|women|children|family members|neighbou?rs|helpers|mob)\b", re.I)


def player_keys(inputs: dict) -> tuple[set, list]:
    """The player's cast key(s) and the names they go by (for checks on what's built for them)."""
    keys, names = set(), []
    for c in inputs.get("cast") or []:
        if isinstance(c, dict) and c.get("player"):
            keys.add(str(c.get("key")))
            name = str(c.get("name") or "").strip()
            if name:
                names += [name, name.split()[0]]
    return keys, names


def plan_requirements(provider: Provider, inputs: dict) -> dict:
    cast = {str(c.get("key")) for c in inputs.get("cast") or [] if isinstance(c, dict) and c.get("key")}
    player, _ = player_keys(inputs)

    def check(answer: dict) -> list[str]:
        reqs = answer.get("requirements")
        if not isinstance(reqs, list):
            return ["'requirements' must be a list"]
        problems = []
        seen: dict = {}
        for i, r in enumerate(reqs):
            if not isinstance(r, dict) or not str(r.get("text", "")).strip():
                problems.append(f"requirements[{i}] needs its text")
                continue
            if r.get("kind") not in REQUIREMENT_KINDS:
                problems.append(f"requirements[{i}].kind is one of {sorted(REQUIREMENT_KINDS)}")
            elif r.get("kind") == "cutscene":
                problems.append(f"requirements[{i}] is a cutscene: the cutscenes are planned in Life ▸ Cutscenes and given; "
                                "write only what one needs beyond them (a marker, a prop, someone there), or leave it out")
            for who in r.get("who") or []:
                if cast and who not in cast:
                    problems.append(f"requirements[{i}].who: {who!r} isn't one of the cast ({', '.join(sorted(cast))})")
            who = [str(w) for w in r.get("who") or []]
            if player and who and set(who) <= player and r.get("kind") not in ("item", "guidance", "quest", "asset", "animation"):
                problems.append(f"requirements[{i}] ({r.get('text', '')[:60]}) is about the player alone: the player is played, not "
                                "built. Write what the game offers for it, about who or what it's done to (an enemy that can be "
                                "beaten, a prop that can be used, a conversation's line for the player, the journal), or leave it out")
            key = re.sub(r"\W+", " ", str(r.get("text", "")).lower()).strip()
            if key in seen:
                problems.append(f"requirements[{i}] repeats requirements[{seen[key]}]: one requirement for one thing")
            seen.setdefault(key, i)
            if r.get("kind") == "asset":
                a = r.get("asset")
                if not isinstance(a, dict) or not str(a.get("name", "")).strip() or not str(a.get("look", "")).strip():
                    problems.append(f"requirements[{i}] is an asset: give its asset {{name, type, look}}")
                elif a.get("type") not in ASSET_TYPES - {"character"}:
                    problems.append(f"requirements[{i}].asset.type is one of {sorted(ASSET_TYPES - {'character'})}")
                elif PEOPLE_WORDS.search(f"{a.get('name', '')} {a.get('look', '')}") and a.get("type") in ("creature", "prop"):
                    problems.append(f"requirements[{i}] ({a.get('name')}) is people: people are cast members (their presence or "
                                    "movement, by their keys), not an asset to draw")
            if r.get("kind") == "animation":
                a = r.get("animation")
                if not isinstance(a, dict) or not str(a.get("who", "")).strip() or not str(a.get("action", "")).strip():
                    problems.append(f"requirements[{i}] is an animation: give its animation {{who, action}}")
            accept = r.get("accept")
            if not isinstance(accept, list) or not accept:
                problems.append(f"requirements[{i}] needs accept checks (how to tell it's done)")
            else:
                for a in accept:
                    if not isinstance(a, dict) or a.get("check") not in REQUIREMENT_CHECKS:
                        problems.append(f"requirements[{i}].accept: check is one of {sorted(REQUIREMENT_CHECKS)}")
                        break
        return problems

    return _step(provider, REQUIREMENTS_TASK, inputs, REQUIREMENTS_SHAPE, "requirements", check, inputs.get("problems"))


ASSETS_TASK = """Visual requirements (the build's checklist of art): which separate pieces of art the game needs and how
each one looks. Given: the game's art style, tone and world, its places (id, name, what they are), and the assets the
requirements name ("given": each with its id, name, type, look if known, and the scenes and places it's in). Merge the
ones that are the same thing (the "east gate", the "graveyard entrance" and the "graveyard gate" of Bellwether are one
gate), and decide for each whether it's a separate asset (drawn on its own, placed in the place: a gate, a grave, a
ledger, a lantern, a creature, a glow) or part of a place's painted ground ("a village square", "the boundary", "the
family plots": painted with the place, only named there). Each: {"id": a short slug, "name", "type":
prop|gateway|item|creature|effect, "separate": true|false, "look": for the artist, in the art style: shape, material,
colours, condition, what it shows, as seen in the game (a sentence or two); "size": tiny|small|medium|large|huge
(an item in hand, a crate, a cart, a gate in a wall, a building); "states": its looks the story needs (["chained",
"open"], ["whole", "broken"]), "animations": what it's seen doing that must be animated (a creature: ["rising out
of a grave", "clawing"]; an effect: ["pulsing"]; most props: none), "places": the place ids it's in, "covers": the given
ids it is}. Every given id is covered once. Looks fit the world and each other (one village's gates and walls alike).
People are never an asset (the cast are drawn as characters): a given one that is people (a crew, villagers, a crowd)
is still covered, with "separate": false and "type": "prop", so nothing is drawn for it."""

ASSETS_SHAPE = """{"assets": [{"id": "", "name": "", "type": "gateway", "separate": true, "look": "", "size": "medium",
  "states": [""], "animations": [""], "places": ["location/x"], "covers": [""]}], "rationale": ""}"""


def plan_assets(provider: Provider, inputs: dict) -> dict:
    given = {str(g.get("id")) for g in inputs.get("given") or [] if isinstance(g, dict) and g.get("id")}

    def check(answer: dict) -> list[str]:
        assets = answer.get("assets")
        if not isinstance(assets, list):
            return ["'assets' must be a list"]
        problems, ids, covered = [], set(), []
        for i, a in enumerate(assets):
            if not isinstance(a, dict) or not str(a.get("id", "")).strip() or not str(a.get("name", "")).strip():
                problems.append(f"assets[{i}] needs its id and name")
                continue
            if a["id"] in ids:
                problems.append(f"assets[{i}].id {a['id']!r} is used twice")
            ids.add(a["id"])
            if a.get("type") not in ASSET_TYPES - {"character"}:
                problems.append(f"assets[{i}].type is one of {sorted(ASSET_TYPES - {'character'})}")
            if a.get("separate", True) and not str(a.get("look", "")).strip():
                problems.append(f"assets[{i}] ({a['name']}) needs its look")
            covered += [str(c) for c in a.get("covers") or []]
        missing = sorted(given - set(covered))
        if missing:
            problems.append(f"these given ids aren't covered by any asset: {', '.join(missing)}")
        twice = sorted({c for c in covered if covered.count(c) > 1})
        if twice:
            problems.append(f"each given id is covered once: {', '.join(twice)} are covered more than once")
        return problems

    return _step(provider, ASSETS_TASK, inputs, ASSETS_SHAPE, "assets", check, inputs.get("problems"))


IMPLEMENT_TASK = """Implementations (34.2): how the build makes each requirement of one scene true in the engine. Given: the
scene (id, goal, place, its scene file and markers), the cast (keys and names), the quest stages it plays in, the
assets (drawn or to be drawn), and its requirements (each with id, kind, text, when, where, who, accept). For each
requirement, one or more items {"req": its id, "how", "does": what exactly is made, in a sentence with the real names,
markers and stage ("on entering q-main stage 20, Grey Lament is moved to the marker graveyard-entrance and made
hostile to Ina Crook"), "by", "after": the req ids whose items must be built first}. how is one of:
- quest-effect: a stage's onEnter/onComplete ops: move_actor {who, scene, marker}, hide_actor, show_actor, set_hostile,
  set_friendly, follow_player, stop_following, spawn {prefab, scene, marker}, give_to, set_flag (by rule);
- objective: a stage objective (reach, talk, kill, collect, deliver, use, examine) on a target, its journal (by rule);
- spawn-point: a marker in the place and what comes from it (by rule);
- marker: a new marker in the place, where (by rule);
- prefab: a prop or gateway placed in the place, from its asset (by rule, after its art);
- art: a picture drawn for an asset (by art);
- animation: a character's, creature's, prop's or effect's animation drawn (by art: a gate swinging open, a glow
  pulsing, a grave cracking);
- cutscene: tracks written for a watched moment and its trigger (by ai-step);
- trigger: what starts something (a cutscene, a conversation, a stage) on reaching a place, leaving a building, an
  event (by rule);
- dialogue: lines and choices added to a character's conversation (by ai-step);
- script: behaviour no op does (a ghost that flees at half health, a door that opens when the knot is broken): a Lua
  script the AI panel writes, on whom, reacting to what (by ai-panel);
- stats: health, damage, speed so a fight goes as written (by rule);
- guidance: journal text, an objective marker, a signpost (by rule or ai-step).
by is rule | ai-step | ai-panel | art. Prefer what the engine does by rule; a script only for what nothing else can do.
Every requirement given gets at least one item.
The player (the cast member with "player": true) is played, not built: never a script on the player, never an op that
moves, hides or makes the player fight or speak. What the player does is theirs; the build gives them the means and puts
the reaction on what reacts: an enemy hostile with its stats (fighting, hitting and beating it are the engine's), an item
given (give_to), a prop the player uses (its prefab with its use, by rule: using it sends its event) and the stage, the
prop or the character that reacts to that event, a line for the player as a choice in the conversation of the one they
speak to (dialogue), what they work out in the journal (guidance). What's already true in the engine (attacking,
walking, picking up, a door that opens on use) needs no item beyond the stats or the prefab."""

IMPLEMENT_SHAPE = """{"items": [{"req": "", "how": "quest-effect", "does": "", "by": "rule", "after": [""]}], "rationale": ""}"""

IMPLEMENT_HOWS = {"quest-effect", "objective", "spawn-point", "marker", "prefab", "art", "animation", "cutscene", "trigger",
                  "dialogue", "script", "stats", "guidance", "package"}
IMPLEMENT_BYS = {"rule", "ai-step", "ai-panel", "art"}


def plan_implement(provider: Provider, inputs: dict) -> dict:
    reqs = {str(r.get("id")) for r in inputs.get("requirements") or [] if isinstance(r, dict) and r.get("id")}
    _, names = player_keys(inputs)
    on_player = re.compile(r"\b(?:script|lua)\b[^.]*?\b(?:on|for|attached to)\s+(?:the\s+player|" +
                           "|".join(re.escape(n) for n in names) + r")\b", re.I) if names else re.compile(r"\bscript on the player\b", re.I)

    def check(answer: dict) -> list[str]:
        items = answer.get("items")
        if not isinstance(items, list) or not items:
            return ["'items' must be a list, at least one item for each requirement"]
        problems, done = [], set()
        for i, it in enumerate(items):
            if not isinstance(it, dict):
                problems.append(f"items[{i}] must be an object")
                continue
            if str(it.get("req")) not in reqs:
                problems.append(f"items[{i}].req {it.get('req')!r} isn't one of the requirements given")
            done.add(str(it.get("req")))
            if it.get("how") not in IMPLEMENT_HOWS:
                problems.append(f"items[{i}].how is one of {sorted(IMPLEMENT_HOWS)}")
            if it.get("by") not in IMPLEMENT_BYS:
                problems.append(f"items[{i}].by is one of {sorted(IMPLEMENT_BYS)}")
            if not str(it.get("does", "")).strip():
                problems.append(f"items[{i}] needs 'does': what is made")
            elif it.get("how") == "script" and on_player.search(str(it.get("does"))):
                problems.append(f"items[{i}] scripts the player: the player is played, not scripted. Put the reaction on what "
                                "reacts (the enemy, the prop the player uses, the quest stage), or give the player the means "
                                "(an item, a usable prop, a dialogue choice)")
        missing = sorted(reqs - done)
        if missing:
            problems.append(f"these requirements have no item: {', '.join(missing)}")
        return problems

    return _step(provider, IMPLEMENT_TASK, inputs, IMPLEMENT_SHAPE, "items", check, inputs.get("problems"))


TIMELINE_TASK = """⑥b Cast timeline: where each character is, what they do and say, before the story starts and as each
quest stage begins or ends, and the cutscenes: which play at each stage, where, set off by what, and who is in them.
Read the story (its scenes: where each happens, who is in it, its events, whether it's watched), the quests' stages,
the characters (home, roles, their AI packages, what they talk about, and their routine: their usual day, the places
they keep to and where they go once a scene is over) and the places (who stands or works in each, what's inside what,
the anchors each scene is set at) together: a merchant works in their shop and is met there unless an event of a scene
takes them out; a character a scene needs elsewhere goes there when its stage begins and back (or on) when the story
says.
For every state (the start and each change) ask: why is this character here at this point in the story, and where does
it make sense for them to be? Only a scene that needs them, or their routine, puts someone somewhere. A scene's gathering
(villagers sharing rumours in the square at s1-01) lasts while that scene plays: as its stage completes (or the next
begins), each one who isn't needed by the next scene goes back to their routine (home, work, a haunt), in a change of
its own whose packages take them there (stay, sandbox, sleep or work with "place" their home or work place and "spot"
their house or workplace: they walk there), "why" saying the scene is over. Nobody stands at a scene's anchor all
story long unless the place is theirs (the elder at the record board she keeps).
"characters": for each character {"who": its id, "start": its state before the first stage: {"place": a place id,
"spot": where in it (an anchor the place lists, or a few words), "packages": the AI packages it runs (each {"template":
one of the templates, "place"?, "schedule"?: {"from", "to"}}), "behaviour": friendly | hostile | follows-player | hidden |
absent | dead, "dialogue": what it talks about now (a few words), "does": what it's doing, in a sentence ("tends her
counter, uneasy about the graves"), "why": why they are there before the story starts (their routine, or the scene
that opens the story)}, "changes": [{"quest": a quest id, "stage": its stage index, "on": "enter" |
"complete", only what changes then: "place"?, "spot"?, "packages"?, "behaviour"?, "dialogue"?, and "does": what it does
now, "why": the scene and event that makes it ("s1-02: the accounts are gathered; Nima goes back to her shop"),
"priority": story (a scene needs it: it must hold) | normal | ambient (colour: it gives way)}]}. What doesn't change
isn't written (it stays as it was).
When two quests hold one character at once (the main quest's stage 10 and a side quest's stage 10 are both on), the
game decides by rank: the change's priority (story, then normal, then ambient), then the main quest before the side
quests, then the side quests in their order. Packages are all kept, stacked in that order: each runs while its quest is
at its stages and the first that holds runs (a side quest's follow package runs only when no higher one holds). Dialogue
and cutscenes are all kept (each quest's talk is offered while its stage is on). Where they are and how they behave is
the highest-ranked quest's. So give a change the priority it needs: one a scene of the story depends on is story.
"cutscenes": every moment the player watches rather than plays: each scene whose mode is cutscene, and the watched
moments of other scenes (an arrival, a reveal, an attack seen before the fight). Each {"id": a short slug, "name", "scene":
the story scene it's from, "quest", "stage": the stage it plays at, "on": "enter" | "complete", "place": where it happens,
"trigger": how it starts, the one moment in the game that sets it off (the build makes exactly that): {"kind": "stage"
(as the stage begins, or completes when "on" is complete: what moves the stage on is its objectives, so "after Odran is
defeated" is the stage whose objective is to defeat him, completing) | "reach" (the player comes to "spot": one of the
place's anchors, while the stage is on) | "leave" (the player comes out of the building "from", a place inside "place",
into "place") | "talk-end" (the player's conversation with "with", a character, ends) | "defeat" (the character "who"
is beaten) | "use" (the player uses "thing", a thing in the place by its name), and its field}. Pick the moment the
story means: an arrival seen as the player walks in is "reach" at the way in; a warning given on stepping out of the
apothecary is "leave"; what someone says after talking is "talk-end". "cast": [{"who": a
character id or "player", "role": what part they play, "does": what they do in it}], "beats": what happens, in order (a
few short lines), "after": what the game is like once it ends (who stays, who fights)}. Everyone in a cutscene is at its
place by its stage (in the characters' states); a character that only appears for it arrives there in its own change.
Every character in a scene is where that scene happens, or in a building inside it they're met in, at the stage that
plays it; nobody is in two places at once; the player isn't in "characters". Only the characters, places, quest stages
and package templates given."""

TIMELINE_SHAPE = """{"timeline": {"characters": [{"who": "character/x", "start": {"place": "location/x", "spot": "", "packages":
  [{"template": "work", "place": "location/x", "schedule": {"from": "08:00", "to": "18:00"}}], "behaviour": "friendly",
  "dialogue": "", "does": "", "why": ""}, "changes": [{"quest": "q-main", "stage": 20, "on": "enter", "place": "location/y", "spot": "",
  "packages": [{"template": "work"}], "behaviour": "friendly", "dialogue": "", "does": "", "why": "", "priority": "normal"}]}],
 "cutscenes": [{"id": "", "name": "", "scene": "s1-02", "quest": "q-main", "stage": 20, "on": "enter", "place": "location/x",
  "trigger": {"kind": "leave", "from": "location/y"}, "cast": [{"who": "character/x", "role": "", "does": ""}],
  "beats": [""], "after": ""}]}, "rationale": ""}"""

TIMELINE_BEHAVIOURS = {"friendly", "hostile", "follows-player", "hidden", "absent", "dead"}
TIMELINE_TRIGGERS = {"stage", "reach", "leave", "talk-end", "defeat", "use"}
# What each start names: the field it needs.
TRIGGER_FIELDS = {"reach": "spot", "leave": "from", "talk-end": "with", "defeat": "who", "use": "thing"}


def plan_timeline(provider: Provider, inputs: dict) -> dict:
    cast = {str(c.get("id")): c for c in inputs.get("cast") or [] if isinstance(c, dict) and c.get("id") and not c.get("player")}
    everyone = set(cast) | {"player"} | {str(c.get("id")) for c in inputs.get("cast") or [] if isinstance(c, dict) and c.get("player")}
    places = {str(p.get("id")) for p in inputs.get("places") or [] if isinstance(p, dict) and p.get("id")}
    stages = {(str(q.get("id")), int(s.get("index", -1))) for q in inputs.get("quests") or [] if isinstance(q, dict)
              for s in q.get("stages") or [] if isinstance(s, dict) and isinstance(s.get("index"), int)}
    templates = {str(t) for t in inputs.get("templates") or []}
    # The anchors a scene is set at ({place, anchor} -> scene) and the stage that plays each scene.
    anchored: dict[tuple[str, str], str] = {}
    place_anchors: dict[str, set] = {}
    for p in inputs.get("places") or []:
        if isinstance(p, dict):
            place_anchors[str(p.get("id"))] = {str(a.get("id")) for a in p.get("anchors") or [] if isinstance(a, dict) and a.get("id")}
        for a in (p.get("anchors") or []) if isinstance(p, dict) else []:
            if isinstance(a, dict) and a.get("id") and a.get("for"):
                anchored[(str(p.get("id")), str(a["id"]))] = str(a["for"])
    played: dict[str, tuple[str, int]] = {}
    later: dict[str, list[int]] = {}
    for q in inputs.get("quests") or []:
        if not isinstance(q, dict):
            continue
        for s in q.get("stages") or []:
            if isinstance(s, dict) and isinstance(s.get("index"), int):
                later.setdefault(str(q.get("id")), []).append(s["index"])
                if s.get("playsScene") and str(s["playsScene"]) not in played:
                    played[str(s["playsScene"])] = (str(q.get("id")), s["index"])
    names = {str(c.get("id")): str(c.get("name") or c.get("id")) for c in inputs.get("cast") or [] if isinstance(c, dict)}

    def lingers(t: dict) -> list[str]:
        """Someone put at a scene's anchor and never sent on once its stage is over (while the quest goes on)."""
        out = []
        start = t.get("start") if isinstance(t.get("start"), dict) else {}
        states = [(start, None)] + [(c, c) for c in t.get("changes") or [] if isinstance(c, dict)]
        place = ""
        for state, change in states:
            place = str(state.get("place") or place)
            spot = str(state.get("spot") or "")
            if not spot:
                continue
            scene = next((sc for (pl, an), sc in anchored.items() if pl == place and (spot == an or an in spot)), "")
            if not scene or scene not in played:
                continue
            quest, stage = played[scene]
            if change is not None and str(change.get("quest")) == quest and isinstance(change.get("stage"), int) and change["stage"] > stage:
                continue  # put there after its scene's stage: another reason
            if not any(i > stage for i in later.get(quest, [])):
                continue  # its scene is the quest's last
            moved = any(isinstance(c, dict) and str(c.get("quest")) == quest and isinstance(c.get("stage"), int)
                        and (c["stage"] > stage or (c["stage"] == stage and c.get("on") == "complete"))
                        and (c.get("place") or c.get("spot") or c.get("packages")) for c in t.get("changes") or [])
            if not moved:
                out.append(f"{names.get(t['who'], t['who'])} is at {spot} for {scene} ({quest} stage {stage}) and nothing sends them on "
                           f"once it's over: add a change (on its stage's complete, or the next stage's enter) taking them back to "
                           f"their routine (home, work, a haunt) with the packages that walk them there, and why")
        return out

    watched = {str(sc.get("id")) for sc in inputs.get("scenes") or [] if isinstance(sc, dict) and sc.get("mode") == "cutscene"}

    def check_state(where: str, state: dict, problems: list[str]) -> None:
        if "place" in state and state["place"] not in places:
            problems.append(f"{where}.place {state['place']!r} isn't one of the places")
        if "behaviour" in state and state["behaviour"] not in TIMELINE_BEHAVIOURS:
            problems.append(f"{where}.behaviour is one of {sorted(TIMELINE_BEHAVIOURS)}")
        for k, p in enumerate(state.get("packages") or []):
            if templates and isinstance(p, dict) and p.get("template") not in templates:
                problems.append(f"{where}.packages[{k}].template is one of {sorted(templates)}")

    def check(answer: dict) -> list[str]:
        t_all = answer.get("timeline")
        if not isinstance(t_all, dict) or not isinstance(t_all.get("characters"), list):
            return ["'timeline' must be {characters: [...], cutscenes: [...]}"]
        problems, seen = [], set()
        for i, t in enumerate(t_all["characters"]):
            if not isinstance(t, dict) or t.get("who") not in cast:
                problems.append(f"characters[{i}].who {t.get('who') if isinstance(t, dict) else t!r} isn't one of the characters (not the player)")
                continue
            seen.add(t["who"])
            start = t.get("start")
            if not isinstance(start, dict) or not start.get("place"):
                problems.append(f"characters[{i}] ({t['who']}) needs its start: at least its place")
            else:
                check_state(f"characters[{i}].start", start, problems)
                if not str(start.get("why", "")).strip():
                    problems.append(f"characters[{i}].start needs its why (why they are there before the story starts)")
            problems.extend(lingers(t))
            for k, c in enumerate(t.get("changes") or []):
                where = f"characters[{i}].changes[{k}]"
                if not isinstance(c, dict):
                    problems.append(f"{where} must be an object")
                    continue
                if (str(c.get("quest")), c.get("stage")) not in stages:
                    problems.append(f"{where}: {c.get('quest')} has no stage {c.get('stage')}")
                if c.get("on", "enter") not in ("enter", "complete"):
                    problems.append(f"{where}.on is enter or complete")
                if not str(c.get("why", "")).strip():
                    problems.append(f"{where} needs its why (the scene and event that make it)")
                check_state(where, c, problems)
                if c.get("priority", "normal") not in ("story", "normal", "ambient"):
                    problems.append(f"{where}.priority is story, normal or ambient")
        missing = sorted(set(cast) - seen)
        if missing:
            problems.append(f"every character has an entry: missing {', '.join(missing)}")
        cuts = t_all.get("cutscenes") or []
        if not isinstance(cuts, list):
            return problems + ["'cutscenes' must be a list"]
        ids, covered = set(), set()
        for i, c in enumerate(cuts):
            where = f"cutscenes[{i}]"
            if not isinstance(c, dict) or not str(c.get("id", "")).strip():
                problems.append(f"{where} needs its id")
                continue
            if c["id"] in ids:
                problems.append(f"{where}.id {c['id']!r} is used twice")
            ids.add(c["id"])
            covered.add(str(c.get("scene", "")))
            if (str(c.get("quest")), c.get("stage")) not in stages:
                problems.append(f"{where}: {c.get('quest')} has no stage {c.get('stage')}")
            if c.get("place") not in places:
                problems.append(f"{where}.place {c.get('place')!r} isn't one of the places")
            trig = c.get("trigger") if isinstance(c.get("trigger"), dict) else {}
            kind = trig.get("kind")
            field = TRIGGER_FIELDS.get(str(kind))
            if kind not in TIMELINE_TRIGGERS:
                problems.append(f"{where}.trigger.kind is one of {sorted(TIMELINE_TRIGGERS)}: the moment in the game that starts it")
            elif field and not str(trig.get(field) or "").strip():
                problems.append(f"{where}.trigger is {kind}: give its {field!r}")
            elif kind == "leave" and trig.get("from") not in places:
                problems.append(f"{where}.trigger.from is the building one comes out of (one of the places)")
            elif kind in ("talk-end", "defeat") and trig.get(field) not in cast:
                problems.append(f"{where}.trigger.{field} is one of the characters (not the player)")
            elif kind == "reach" and place_anchors.get(str(c.get("place"))) and trig.get("spot") not in place_anchors[str(c.get("place"))]:
                problems.append(f"{where}.trigger.spot is one of {c.get('place')}'s anchors: "
                                f"{', '.join(sorted(place_anchors[str(c.get('place'))]))}")
            if not c.get("cast"):
                problems.append(f"{where} needs its cast (who is in it and what they do)")
            for k, m in enumerate(c.get("cast") or []):
                if not isinstance(m, dict) or m.get("who") not in everyone:
                    problems.append(f"{where}.cast[{k}].who isn't one of the characters or the player")
        missed = sorted(watched - covered)
        if missed:
            problems.append(f"these scenes are watched (mode cutscene) and have no cutscene: {', '.join(missed)}")
        return problems

    return _step(provider, TIMELINE_TASK, inputs, TIMELINE_SHAPE, "timeline", check, inputs.get("problems"))


PLACEMENTS_TASK = """Placements (34.11): where every character and every story prop of the built game stands, and where each
is as the story goes on. Given: the built scenes (each its place, its kind and the markers in it: story anchors with
what they are), the buildings found in them ("buildings": id, scene, kind — house, shop, workshop, inn, temple, stable,
centrepiece, stall, gateway-gate, grave, crop; and the land areas: square, graveyard, field ("area": true, its "box" of
cells, its door the open cell in its middle); and the story anchors ("marker": stand there with {"marker"}) —, its size,
its door's cell, and "interior": the scene of its drawn interior, or null), the places (purpose, residents, who stands at which anchor), the characters (line, class, roles,
home, AI packages, biography, routine (their usual day and the places they keep to: where "his house" or "back to work" is), and their cast timeline: where they start and what each quest stage changes), the quests'
stages (objectives, effects, the story scene each plays), the story scenes (where, who), the props and quest items to
place, and a draft the rules made ("draft"). Decide, for each character {"id", "home": the building they live in (a
house; null when they live nowhere in particular: a ghost, a boss, a traveller), "work": the building they work in (a
shop, a smithy, an inn, a temple; null when none), "scene": the scene they stand in at the start, "spot": where in it —
{"building": an id, "inside": true when its interior is drawn and they are met inside it (a shopkeeper at their counter),
false at its door} or {"marker": a marker of that scene} or {"near": a building id or marker, "note": a few words: "by the
graves", "at the gate"} —, "why": one sentence from what the story, the character or the place says}. A merchant, smith
or innkeeper whose building's interior is drawn is inside it; one whose isn't stands at its door. Someone who belongs
outdoors stands where their work is: a guard at the gate or boundary, a gravedigger by the graves, a farmer in the
fields, a ghost among the graves, a boss at what it guards, the elder at the square or record board. Never put anyone
at the place's arrival point because nothing else is known: choose the most fitting spot and say why. No two people on
one building's door unless they share it (a family). Each character's "whereabouts": where they are over time, in
order, the first {"from": {"start": true}} (their spot above), then one for each time the story moves them: {"from":
{"quest", "stage", "on": "enter" | "complete"} or {"event": a game event}, "until": the next one's from (or {} for to the
end), "scene", "spot" as above, "why": the scene or stage that makes it}. Follow the cast timeline and the quests'
move_actor effects; add a move only when a story scene needs them somewhere they aren't. For each prop {"id", "scene",
"spot" as above (a marker or near a building: the hidden box by the cracked grave marker, the record board in the
square), "faces": what its front looks to — a building id, a marker, a character id, or n | e | s | w (the record board
faces the square, the table faces its keeper's door, a sign faces the road), "why", "whereabouts": the start, then when it moves: {"from": {"quest", "stage", "on"}, "holder": "player" (it's
picked up) or a character id (given to them), "why"}}. Use only the ids, scenes, markers, quests and stages given;
every character and prop given gets an entry; keep the draft where it is right.
"requirements" (when given): what the game's design requires of where people and things are (each its id, kind —
presence, character-state, arrival, movement, prop, item —, its words, when and where, and who): the placements are
how they're met. Each must hold: put the character or prop where and when it says (the right scene and, by its words,
the right building, marker or spot), and give the requirement's id in the "why" of the spot or whereabouts that meets
it ("meets r-12: ...").
The ways between outdoor places ("ways": a gateway in "scene" leading "to" another place, with that place's name and
line, and where it stands now) go where a road leaves the scene ("exits": a marker "to-the-west", "to-the-east", ... on
a road at the scene's edge). For each way, "gateways": {"scene", "to", "exit": the exit marker of that scene it belongs
at, "why"}: the side the other place lies on, from the story, the places' lines and names and the anchors by the
gateway (the graveyard's "east gate" leads out of the village to the west, so it's on the village's to-the-west; and in
the graveyard, the way back to the village is on its east side). A way that has no fitting exit is left out (it stays
where it is). Characters and anchors at a gateway (a guard at the gate, the failing boundary by it) go with it."""

PLACEMENTS_SPOT = """{"building": "", "inside": false, "marker": "", "near": "", "note": ""}"""
PLACEMENTS_FROM = """{"start": false, "quest": "", "stage": 0, "on": "enter", "event": ""}"""
PLACEMENTS_SHAPE = ("""{"placements": {"characters": [{"id": "character/x", "home": "", "work": "", "scene": "scenes/x.aige", "spot": """
                    + PLACEMENTS_SPOT + """, "why": "", "whereabouts": [{"from": """ + PLACEMENTS_FROM + """, "until": """
                    + PLACEMENTS_FROM + """, "scene": "", "spot": """ + PLACEMENTS_SPOT + """, "why": ""}]}],
 "props": [{"id": "", "scene": "", "spot": """ + PLACEMENTS_SPOT + """, "faces": "", "why": "", "whereabouts": [{"from": """ + PLACEMENTS_FROM
                    + """, "holder": "", "scene": "", "spot": """ + PLACEMENTS_SPOT + """, "why": ""}]}],
 "gateways": [{"scene": "scenes/x.aige", "to": "scenes/y.aige", "exit": "to-the-west", "why": ""}]}, "rationale": ""}""")


def plan_placements(provider: Provider, inputs: dict) -> dict:
    buildings = {str(b.get("id")): b for b in inputs.get("buildings") or [] if isinstance(b, dict) and b.get("id")}
    scenes = {str(s.get("scene")): s for s in inputs.get("scenes") or [] if isinstance(s, dict) and s.get("scene")}
    characters = {str(c.get("id")) for c in inputs.get("characters") or [] if isinstance(c, dict) and c.get("id")}
    props = {str(p.get("id")) for p in inputs.get("props") or [] if isinstance(p, dict) and p.get("id")}
    stages = {(str(q.get("id")), int(s.get("index", -1))) for q in inputs.get("quests") or [] if isinstance(q, dict)
              for s in q.get("stages") or [] if isinstance(s, dict) and isinstance(s.get("index"), int)}
    ways = {(str(w.get("scene")), str(w.get("to"))) for w in inputs.get("ways") or [] if isinstance(w, dict)}
    exits: dict[str, set[str]] = {}
    for x in inputs.get("exits") or []:
        if isinstance(x, dict):
            exits.setdefault(str(x.get("scene")), set()).add(str(x.get("marker")))

    def check_spot(where: str, scene: str, spot, problems: list[str]) -> None:
        if not isinstance(spot, dict):
            problems.append(f"{where}.spot must be an object")
            return
        b, m, n = str(spot.get("building") or ""), str(spot.get("marker") or ""), str(spot.get("near") or "")
        if not (b or m or n):
            problems.append(f"{where}.spot needs a building, a marker or near")
        if b and b not in buildings:
            problems.append(f"{where}.spot.building {b!r} isn't one of the buildings")
        elif b and spot.get("inside") and not buildings[b].get("interior"):
            problems.append(f"{where}.spot: {b} has no drawn interior: inside is false (they stand at its door)")
        if m and scene in scenes and m not in (scenes[scene].get("markers") or []):
            problems.append(f"{where}.spot.marker {m!r} isn't a marker of {scene}")
        if n and n not in buildings and scene in scenes and n not in (scenes[scene].get("markers") or []):
            problems.append(f"{where}.spot.near {n!r} is neither a building nor a marker of {scene}")

    def check_from(where: str, f, problems: list[str]) -> None:
        if not isinstance(f, dict) or f.get("start") or f.get("event"):
            return
        if f.get("quest") and (str(f.get("quest")), f.get("stage")) not in stages:
            problems.append(f"{where}: {f.get('quest')} has no stage {f.get('stage')}")

    def check(answer: dict) -> list[str]:
        p = answer.get("placements")
        if not isinstance(p, dict) or not isinstance(p.get("characters"), list):
            return ["'placements' must be {characters: [...], props: [...]}"]
        problems, seen = [], set()
        for i, c in enumerate(p["characters"]):
            if not isinstance(c, dict) or str(c.get("id")) not in characters:
                problems.append(f"characters[{i}].id {c.get('id') if isinstance(c, dict) else c!r} isn't one of the characters")
                continue
            seen.add(str(c["id"]))
            for k in ("home", "work"):
                if c.get(k) and str(c[k]) not in buildings:
                    problems.append(f"characters[{i}].{k} {c[k]!r} isn't one of the buildings (or null)")
            scene = str(c.get("scene") or "")
            if scene and scene not in scenes:
                problems.append(f"characters[{i}].scene {scene!r} isn't one of the scenes")
            spot = c.get("spot") if isinstance(c.get("spot"), dict) else {}
            if spot.get("building") and str(spot["building"]) in buildings:
                scene = str(buildings[str(spot["building"])].get("scene") or scene)
            check_spot(f"characters[{i}]", scene, spot, problems)
            if not str(c.get("why", "")).strip():
                problems.append(f"characters[{i}] ({c['id']}) needs its why")
            for k, w in enumerate(c.get("whereabouts") or []):
                where = f"characters[{i}].whereabouts[{k}]"
                if not isinstance(w, dict):
                    problems.append(f"{where} must be an object")
                    continue
                check_from(where, w.get("from"), problems)
                ws = str(w.get("scene") or "")
                if ws and ws not in scenes:
                    problems.append(f"{where}.scene {ws!r} isn't one of the scenes")
                if not (isinstance(w.get("from"), dict) and w["from"].get("start")):
                    check_spot(where, ws, w.get("spot"), problems)
        missing = sorted(characters - seen)
        if missing:
            problems.append(f"every character has an entry: missing {', '.join(missing)}")
        done = set()
        for i, pr in enumerate(p.get("props") or []):
            if not isinstance(pr, dict) or str(pr.get("id")) not in props:
                problems.append(f"props[{i}].id isn't one of the props")
                continue
            done.add(str(pr["id"]))
            scene = str(pr.get("scene") or "")
            if scene and scene not in scenes:
                problems.append(f"props[{i}].scene {scene!r} isn't one of the scenes")
            check_spot(f"props[{i}]", scene, pr.get("spot"), problems)
            faces = str(pr.get("faces") or "")
            if faces and faces not in ("n", "e", "s", "w") and faces not in buildings and faces not in characters \
                    and not (scene in scenes and faces in (scenes[scene].get("markers") or [])):
                problems.append(f"props[{i}].faces {faces!r} is a building, a marker of {scene}, a character or n, e, s, w")
            for k, w in enumerate(pr.get("whereabouts") or []):
                if isinstance(w, dict):
                    check_from(f"props[{i}].whereabouts[{k}]", w.get("from"), problems)
        missed = sorted(props - done)
        if missed:
            problems.append(f"every prop has an entry: missing {', '.join(missed)}")
        for i, g in enumerate(p.get("gateways") or []):
            if not isinstance(g, dict):
                problems.append(f"gateways[{i}] must be an object")
                continue
            pair = (str(g.get("scene")), str(g.get("to")))
            if pair not in ways:
                problems.append(f"gateways[{i}]: there's no way from {pair[0]} to {pair[1]} (one of the ways)")
            elif str(g.get("exit")) not in exits.get(pair[0], set()):
                problems.append(f"gateways[{i}].exit {g.get('exit')!r} isn't one of {pair[0]}'s exits "
                                f"({', '.join(sorted(exits.get(pair[0], set()))) or 'none'})")
        return problems

    return _step(provider, PLACEMENTS_TASK, inputs, PLACEMENTS_SHAPE, "placements", check, inputs.get("problems"))


# Planner steps in order:
# ① Concept          -> concept
# ② Outline          -> outline
# ③ Story (Acts & Scenes) -> act, scene, critic
# ④ World             -> world (was "place")
# ⑤ Characters       -> character, routine
# ⑥ Quests           -> quests
# ⑦ Systems          -> gameplay, system, behaviour, systems
# ⑧ Visuals          -> visuals
# Other steps:
#    review, revise, dialogue, buildcheck, judgetest, cutscene,
#    requirements, assets, implement, timeline, input, uipanel,
#    objects, smallscenes, placements

STEPS: dict[str, Callable[[Provider, dict], dict]] = {
    "concept": plan_concept, "outline": plan_outline,
    "act": plan_act, "scene": plan_scene, "critic": plan_critic,
    "world": plan_place, "character": plan_character, "routine": plan_routine, "quests": plan_quests,
    "gameplay": plan_gameplay, "system": plan_system, "behaviour": plan_behaviour, "systems": plan_systems,
    "visuals": plan_visuals, "review": plan_review, "revise": plan_revise, "dialogue": plan_dialogue,
    "buildcheck": plan_buildcheck,
    "judgetest": plan_judgetest,
    "cutscene": plan_cutscene,
    "requirements": plan_requirements,
    "assets": plan_assets,
    "implement": plan_implement,
    "timeline": plan_timeline,
    "input": plan_input,
    "uipanel": plan_uipanel,
    "objects": plan_objects,
    "smallscenes": plan_scenes,
    "placements": plan_placements,
    "decompose": decompose_request,
}
