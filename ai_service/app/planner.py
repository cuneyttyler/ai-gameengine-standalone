"""The AI Planner's calls (development_plan.md 22, docs/ai-planner.md §5): one step per
endpoint, each the protocol of place-interaction.md Part B — think in free text, answer in
a strict JSON shape, check the answer, repair once with the exact problems, then report a
problem rather than guess. The engine (engine/planner/GamePlan) assembles the inputs from
the plan's documents and the story graph, validates every answer again, applies it, and
records it as a decision; these checks mirror the engine's so most answers pass first time.
"""

from __future__ import annotations

import json
from typing import Any, Callable

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


def _step(provider: Provider, task: str, inputs: dict, shape: str, key: str,
          check: Callable[[dict], list[str]], problems: list[str] | None = None) -> dict:
    """One planner step: think, answer, check, repair once. Returns {ok, <key>, rationale, usage, errors}."""
    usage = Usage()
    text_inputs = _dump(inputs)
    if problems:
        text_inputs += ("\n\nYour last answer had these problems; fix exactly them:\n- " + "\n- ".join(problems))
    result: StepResult = think_then_answer(provider, f"{PLANNER_ROLE}\n\n{task}", text_inputs, shape, think_instructions=THINK)
    usage.add(result.usage)
    if result.answer is None:
        return {"ok": False, "errors": result.errors or ["no answer"], "usage": usage.model_dump()}
    answer = result.answer
    found = check(answer)
    if found:
        repair = think_then_answer(provider, f"{PLANNER_ROLE}\n\n{task}",
                                   text_inputs + "\n\nYour answer:\n" + _dump(answer) +
                                   "\n\nIt has these problems; answer again with them fixed:\n- " + "\n- ".join(found),
                                   shape, think=False, think_instructions=THINK)
        usage.add(repair.usage)
        if repair.answer is not None:
            answer = repair.answer
            found = check(answer)
    value = answer.get(key, answer if key == "*" else None)
    if value is None:
        return {"ok": False, "errors": [f"the answer has no '{key}'"], "usage": usage.model_dump()}
    out = {"ok": True, key: value, "rationale": str(answer.get("rationale", ""))[:600], "usage": usage.model_dump()}
    if found:
        out["problems"] = found  # the engine checks again and sends it back once more if they matter
    return out


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

CONCEPT_TASK = """① Concept: read a request for a game and fill its Game Brief.
{reading}
Rules: genre is one of the genres given (its id); every enum field takes one of its options; numbers stay in
range; playTime is in hours; tone is a list of tone words from its options; write a title and a logline (one
sentence, at most 40 words) even if the request has none; setting.world is a short description of the world.
Fill only what the request decides or strongly implies; the engine fills the rest from the genre's profile.
confidence: per field path, how sure you are (0-1). decisions: questions the user should answer, each with the
answer you assumed and the field path it sets."""

CONCEPT_SHAPE = """{"brief": {"title": "", "logline": "", "genre": "<genre id>", "perspective": "isometric|top-down",
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
  gameplayRoles from the vocabularies, and one line. Exactly one is the player ("player": true). Every narrative
  role the template needs (template.roles) is someone's. Match the Brief's cast counts roughly.
- factions (if the Brief has any): id faction/<slug>, name, line; a character may name its faction.
- places: every location with id location/<slug>, name, kind (one of buildTypes), parent (a place in the list,
  for an interior inside a settlement), one line; the first is where the game starts. Match the Brief's place kinds.
- connections: how places join (road, door, stairs, portal), every place reachable from the first; a gate names
  what opens it (a quest stage, a key), if anything.
Respect the Brief's audience, tone, setting, mustInclude and avoid."""

OUTLINE_SHAPE = """{"outline": {
  "story": {"logline": "", "synopsis": "", "structure": "<template id>",
    "acts": [{"id": "act-1", "purpose": "setup", "summary": "", "beats": ["inciting-incident"]}],
    "storylines": [{"id": "main", "kind": "main", "summary": ""}],
    "endings": [{"id": "end-a", "summary": "", "conditions": ""}]},
  "cast": [{"id": "character/aldric", "name": "Aldric", "importance": "major", "player": false,
    "narrativeRoles": ["antagonist"], "gameplayRoles": ["boss"], "faction": "", "line": ""}],
  "factions": [{"id": "faction/wardens", "name": "", "line": ""}],
  "places": [{"id": "location/riverford", "name": "Riverford", "kind": "settlement", "parent": "", "line": ""}],
  "connections": [{"from": "location/riverford", "to": "location/crypt", "kind": "stairs", "gate": ""}]},
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
    return problems


def plan_outline(provider: Provider, inputs: dict) -> dict:
    return _step(provider, OUTLINE_TASK, inputs, OUTLINE_SHAPE, "outline", lambda a: check_outline(a.get("outline"), inputs),
                 inputs.get("problems"))


# --- ③ Story --------------------------------------------------------------------------------

ACT_TASK = """③ Story, one act: split the act into chapters (each with one dramatic question) and give each chapter
its scenes as scene cards, in the order they're played. About sceneTarget scenes in the act.
A scene card: goal, conflict, outcome; value (a McKee value from vocab.Value) with chargeBefore and chargeAfter
(-1 to 1; a kernel scene turns its value); mode (vocab.SceneMode: cutscene, dialogue, gameplay, environmental,
discoverable, ambient); kernel (true for the main line's turning points); beat (the template beat it carries, if
any, e.g. "inciting-incident", "climax"); location (a place id); participants [{who: character id, role: agent |
patient | witness}] (the player's character takes part in most); needsParts (what the place must contain, e.g.
"altar", "cellar"); branch (only for scenes on a branch after a choice: the branch name; such scenes come between
two main-line scenes). Use the cast's and places' ids; a scene may need a new minor character or place: give it
an id (character/<slug>, location/<slug>) and the engine adds it. Earlier acts' chapters are given: continue them."""

ACT_SHAPE = """{"act": {"chapters": [{"id": "ch-1", "question": "", "summary": "",
   "scenes": [{"id": "s-1", "mode": "dialogue", "kernel": true, "beat": "", "goal": "", "conflict": "", "outcome": "",
     "value": "truth-lie", "chargeBefore": -1, "chargeAfter": 1, "location": "location/x",
     "participants": [{"who": "character/x", "role": "agent"}], "needsParts": [], "branch": ""}]}]},
 "rationale": ""}"""


def plan_act(provider: Provider, inputs: dict) -> dict:
    modes = set(_vocab(inputs, "SceneMode"))

    def check(answer: dict) -> list[str]:
        act = answer.get("act")
        if not isinstance(act, dict) or not act.get("chapters"):
            return ["'act' needs chapters"]
        problems = []
        for ch in act["chapters"]:
            if not ch.get("scenes"):
                problems.append(f"chapter {ch.get('id')} has no scenes")
            for s in ch.get("scenes", []):
                if modes and s.get("mode") not in modes:
                    problems.append(f"scene {s.get('id')} mode {s.get('mode')!r} isn't one of {', '.join(sorted(modes))}")
        return problems

    return _step(provider, ACT_TASK, inputs, ACT_SHAPE, "act", check, inputs.get("problems"))


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
        for f in scene.get("facts", []):
            if properties and f.get("p") not in properties:
                problems.append(f"fact property {f.get('p')!r} doesn't exist")
            if not f.get("s"):
                problems.append(f"fact {f.get('p')} has no subject 's'")
        for n in scene.get("newEntities", []):
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


# --- ④ Characters ---------------------------------------------------------------------------

CHARACTER_TASK = """④ Characters, one character: detail it from its outline line, the scenes it takes part in and
the facts about it. Write its biography (backstory, personality as [trait, 0-1] pairs from vocab.Trait, need,
desire, weakness, motivations from vocab.Motivation, secrets, arc {from, to}), appearance (description and a few
palette colours), voice (how it speaks, a sample line), relationships to other characters of the cast
({with, type from vocab.RelationshipType, valence -1 to 1}), gameplay (roles from vocab.GameplayRole, class: a
short stat-class id like "villager-elder" or "bandit-leader", level, stock for vendors, skills for trainers,
abilities), behaviour (packages: [{template from packageTemplates, place: a location id}] — what it does day to
day; idle: true for someone who just stands), dialogue (topics, the quests it gives), home (a location id) and
line (one line). The player's character has no packages. Keep it consistent with every scene it is in."""

CHARACTER_SHAPE = """{"character": {"name": "", "importance": "", "line": "", "home": "location/x",
   "biography": {"backstory": "", "personality": [["brave", 0.6]], "need": "", "desire": "", "weakness": "",
     "motivations": ["protection"], "secrets": [""], "arc": {"from": "", "to": ""}},
   "appearance": {"description": "", "palette": ["#7a5230"]}, "voice": {"description": "", "sample": ""},
   "relationships": [{"with": "character/y", "type": "friend", "valence": 0.6}],
   "gameplay": {"roles": ["quest-giver"], "class": "", "level": 3, "stock": "", "skills": "", "abilities": []},
   "behaviour": {"packages": [{"template": "", "place": "location/x"}], "idle": false},
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
        return problems

    return _step(provider, CHARACTER_TASK, inputs, CHARACTER_SHAPE, "character", check, inputs.get("problems"))


# --- ⑤ Quests -------------------------------------------------------------------------------

QUESTS_TASK = """⑤ Quests: the main quest in full, and side quests. The main quest (id q-main, type main) keeps
every stage of its skeleton (each plays a kernel scene: keep its index and advancesScene) and gets objectives,
aliases, journal lines and rewards. Side quests (about sideQuests of them; mode tells whether the genre wants
main-only or side-only quests) grow from characters' motivations: pick a giver whose motivation and situation
suggest a need, a strategy for that motivation (strategies), and turn the strategy's actions into stages
(e.g. recover-stolen-item: goto, kill or steal, take, goto, give). Quests in "keep" are locked: don't change them.
A quest: id (q-<slug>), name, type, motivation, strategy, giver {alias}, level [low, high], aliases (each {id,
kind: character | item | location | spot | container, and fill: an id from the story, or query: a description,
or in: the location it's in; spots also where: {anchor: a named spot}}), stages [{index (10, 20, ...), journal,
advancesScene (a scene id, if it plays one), objectives [{action from vocab.ObjectiveAction, target: an alias
id, count}], terminal: "completed" on the last}], rewards [{xp}, {gold}, {item: "<item id>"}], relations
[{requires | excludes | unlocks: quest id}]. Every character a quest needs must be alive at that point."""

QUESTS_SHAPE = """{"quests": [{"id": "q-main", "name": "", "type": "main", "motivation": "protection",
   "strategy": "recover-stolen-item", "giver": {"alias": "giver"}, "level": [1, 4],
   "aliases": [{"id": "giver", "kind": "character", "fill": "character/x"},
               {"id": "hiding-spot", "kind": "spot", "in": "location/crypt", "where": {"anchor": "altar"}}],
   "stages": [{"index": 10, "journal": "", "advancesScene": "s1-02",
               "objectives": [{"action": "talk", "target": "giver"}]}, {"index": 100, "terminal": "completed"}],
   "rewards": [{"xp": 300}], "relations": []}],
 "rationale": ""}"""


def plan_quests(provider: Provider, inputs: dict) -> dict:
    actions = set(_vocab(inputs, "ObjectiveAction"))

    def check(answer: dict) -> list[str]:
        quests = answer.get("quests")
        if not isinstance(quests, list) or not quests:
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
        return problems

    return _step(provider, QUESTS_TASK, inputs, QUESTS_SHAPE, "quests", check, inputs.get("problems"))


# --- ⑥ Places -------------------------------------------------------------------------------

PLACE_TASK = """⑥ Places, one place: describe it from everything the story needs of it — what it is (purpose,
culture, era, mood, residents), what it contains (zones, named buildings and features, interiors as places of
their own), the story anchors the scenes and quests need (each {id, module: a component or module from the
interpret context, relation, for: {quest} or {scene}} — every quest anchor given must be there), the characters
who live or work there (actors {character, anchor}), its exits matching its connections ({to, edge, kind}),
style (palette, materials). Then write its draft Brief for the Build panel, exactly as the interpret context
describes for its build type (schema aige.brief/1: buildType, name, scale, components with their fields, features,
style), with the anchors' modules on."""

PLACE_SHAPE = """{"place": {"kind": "<build type>",
   "description": {"purpose": "", "culture": "", "era": "", "mood": "", "residents": []},
   "contains": {"zones": [{"kind": "", "character": ""}], "named": [{"id": "", "kind": "", "line": ""}]},
   "storyAnchors": [{"id": "altar", "module": "<module>", "relation": {}, "for": {"quest": "q-main"}}],
   "actors": [{"character": "character/x", "anchor": "home"}],
   "exits": [{"to": "location/y", "edge": "east", "kind": "road"}],
   "style": {"palette": [], "materials": []},
   "brief": {"schema": "aige.brief/1", "buildType": "<build type>", "name": "", "scale": {}, "components": {}, "features": []}},
 "rationale": ""}"""


def plan_place(provider: Provider, inputs: dict) -> dict:
    kind = inputs.get("kind", "")
    anchors = [a.get("where", {}).get("anchor") or a.get("alias") for a in inputs.get("anchors", []) if a.get("kind") == "spot"]

    def check(answer: dict) -> list[str]:
        p = answer.get("place")
        if not isinstance(p, dict):
            return ["'place' must be an object"]
        problems = []
        brief = p.get("brief")
        if not isinstance(brief, dict):
            problems.append("the place needs its brief")
        elif kind and brief.get("buildType") not in (kind, None):
            problems.append(f"brief.buildType must be {kind}")
        have = {a.get("id") for a in p.get("storyAnchors", [])} | {a.get("module") for a in p.get("storyAnchors", [])}
        for anchor in anchors:
            if anchor and anchor not in have:
                problems.append(f"the story anchor {anchor!r} a quest needs is missing")
        return problems

    return _step(provider, PLACE_TASK, inputs, PLACE_SHAPE, "place", check, inputs.get("problems"))


# --- ⑦ Systems and ⑧ Visuals ----------------------------------------------------------------

SYSTEMS_TASK = """⑦ Systems: the game's data, from the Brief's systems and what the characters and quests use.
Write a summary (attributes and progression, the economy and its prices, loot, abilities and schools, factions,
enemies per region and level band, the difficulty curve) and the records the game needs, each {type, id,
record} with only the record types and fields given (recordTypes): the unique items quests and characters name
(every quest reward item), a small generic set of weapons, armour and consumables, enemy classes, loot tables,
the characters' stat classes. Keep values within sensible bands for the levels."""

SYSTEMS_SHAPE = """{"systems": {"summary": {"progression": "", "economy": "", "loot": "", "abilities": "", "enemies": "",
   "difficulty": ""}, "records": [{"type": "item", "id": "moon-amulet", "record": {"name": ""}}]},
 "rationale": ""}"""


def plan_systems(provider: Provider, inputs: dict) -> dict:
    types = set((inputs.get("recordTypes") or {}).keys())

    def check(answer: dict) -> list[str]:
        s = answer.get("systems")
        if not isinstance(s, dict):
            return ["'systems' must be an object"]
        return [f"record type {r.get('type')!r} doesn't exist" for r in s.get("records", [])
                if types and r.get("type") not in types]

    return _step(provider, SYSTEMS_TASK, inputs, SYSTEMS_SHAPE, "systems", check, inputs.get("problems"))


VISUALS_TASK = """⑧ Visuals: the game's interface from the Brief's visuals and the systems that are on. Choose the
UI theme (one of themes) to fit the art style and tone; the screens and windows the game needs (from windows:
title, pause, settings, save-load, game-over always; inventory if items, journal if quests, map if more than one
place, spellbook if magic, character if progression, shop if merchants, dialogue if conversations); the HUD
widgets by density (from widgets); notification style; map style."""

VISUALS_SHAPE = """{"ui": {"theme": "", "screens": ["title"], "hud": ["health"], "notifications": "standard", "mapStyle": "minimap"},
 "rationale": ""}"""


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
        return problems

    return _step(provider, VISUALS_TASK, inputs, VISUALS_SHAPE, "ui", check, inputs.get("problems"))


REVIEW_TASK = """⑨ Validate, the critic: read the Brief, the story outline, the cast and the places, and the issues
the engine found. Judge what rules can't: tone and theme kept, the world's own rules respected, the genre's
promise met, the audience respected, names distinct, nothing dull or contradictory. Few findings, each {subject
(an id), severity (warning, or error only for something that breaks the game), message, fix (what to change)}."""

REVIEW_SHAPE = """{"findings": [{"subject": "character/x", "severity": "warning", "message": "", "fix": ""}], "rationale": ""}"""


def plan_review(provider: Provider, inputs: dict) -> dict:
    return _step(provider, REVIEW_TASK, inputs, REVIEW_SHAPE, "findings",
                 lambda a: [] if isinstance(a.get("findings"), list) else ["'findings' must be a list"], inputs.get("problems"))


REVISE_TASK = """A change to the plan, in the user's words: turn it into edits of the plan's documents. Each edit is
{doc (a document from index or documents), pointer (a JSON pointer into it, e.g. "/biography/need",
"/relationships/0/type", "/card/goal"), value (the new value), reason}. Change only what the words ask, in the
documents that hold it (a character's family in its relationships; a scene's events in its card); the engine marks
what depends on the changed documents for regenerating. If the words ask for nothing the documents hold, answer
no edits and say why in the rationale."""

REVISE_SHAPE = """{"edits": [{"doc": "characters/aldric", "pointer": "/relationships/0", "value": {"with": "character/tamsin",
   "type": "child", "valence": 0.7}, "reason": ""}], "rationale": ""}"""


def plan_revise(provider: Provider, inputs: dict) -> dict:
    known = {d.get("doc") for d in inputs.get("index", [])} | set((inputs.get("documents") or {}).keys())

    def check(answer: dict) -> list[str]:
        edits = answer.get("edits")
        if not isinstance(edits, list):
            return ["'edits' must be a list"]
        problems = []
        for e in edits:
            if known and e.get("doc") not in known:
                problems.append(f"no document {e.get('doc')!r}")
            if not str(e.get("pointer", "")).startswith("/"):
                problems.append(f"pointer {e.get('pointer')!r} must start with /")
        return problems

    return _step(provider, REVISE_TASK, inputs, REVISE_SHAPE, "edits", check, inputs.get("problems"))


STEPS: dict[str, Callable[[Provider, dict], dict]] = {
    "concept": plan_concept, "outline": plan_outline, "act": plan_act, "scene": plan_scene, "critic": plan_critic,
    "character": plan_character, "quests": plan_quests, "place": plan_place, "systems": plan_systems,
    "visuals": plan_visuals, "review": plan_review, "revise": plan_revise,
}
