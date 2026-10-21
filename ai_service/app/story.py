"""Checks for write_dialogue / write_quest operations (development_plan.md 3.2d),
mirroring engine/gameplay/Story (ValidateDialogue, ValidateQuest) and
engine/world/StoryReferences, so the model gets the same feedback the editor
would show — before anything reaches the review panel. Format: docs/story.md."""

from __future__ import annotations

import re
from typing import Any

EFFECT_OPS = {"set_flag", "clear_flag", "start_quest", "complete_quest", "complete_objective", "fail_quest",
              "set_stage", "set_var", "add_var", "give_item", "take_item", "give_gold", "give_xp", "reputation",
              "play_cutscene"}
QUEST_OPS = {"start_quest", "complete_quest", "complete_objective", "fail_quest", "set_stage"}
OBJECTIVE_TYPES = {"defeat", "talk", "collect", "reach", "deliver", "use", "examine", "have", "survive", "defend",
                   "escort"}
QUEST_STATES = {"inactive", "active", "completed", "failed"}
# What a world condition may ask (15.2); the game answers them.
WORLD_KEYS = {"item", "gold", "level", "attribute", "time", "day", "faction", "difficulty"}
MAX_CHOICES = 9
TEAMS = {"Enemy", "Neutral"}
SAFE_ID = re.compile(r"^[a-z0-9_-]+$")


class StoryScope:
    """What a check can refer to: the project's quests and World Bible, plus
    what the same proposal adds (quests it writes, entries it writes)."""

    def __init__(self, quests: dict[str, dict], world_keys: set[str], prefabs: set[str],
                 scene_markers: dict[str, set[str]] | None = None):
        self.quests = quests          # id -> quest JSON
        self.world_keys = world_keys  # "<kind>/<id>"
        self.prefabs = prefabs
        # Each scene of the project -> its markers (the open one with any added
        # in this proposal); None: scenes aren't checked.
        self.scene_markers = scene_markers

    def objectives_of(self, quest: str) -> list[str] | None:
        q = self.quests.get(quest)
        if q is None:
            return None
        return [o.get("id", "") for o in quest_objectives(q)]

    def stages_of(self, quest: str) -> list[int] | None:
        q = self.quests.get(quest)
        if q is None:
            return None
        if isinstance(q.get("stages"), list):
            return [st.get("index", (i + 1) * 10) for i, st in enumerate(q["stages"]) if isinstance(st, dict)]
        return [(i + 1) * 10 for i, _ in enumerate(q.get("objectives", []) or [])]

    def known_character(self, who: Any) -> bool:
        if not isinstance(who, str) or not who or who == "player" or "/" not in who:
            return True  # plain names ("A guard") are allowed
        return who in self.world_keys


def quest_objectives(q: dict[str, Any]) -> list[dict]:
    """Every objective of a quest, v1 ("objectives") or v2 ("stages")."""
    if isinstance(q.get("stages"), list):
        return [o for st in q["stages"] if isinstance(st, dict)
                for o in (st.get("objectives") or []) if isinstance(o, dict)]
    return [o for o in (q.get("objectives") or []) if isinstance(o, dict)]


def _check_effects(effects: Any, where: str, scope: StoryScope) -> list[str]:
    errors = []
    if effects is None:
        return errors
    if not isinstance(effects, list):
        return [f"{where}: \"do\" must be a list"]
    for effect in effects:
        if not isinstance(effect, dict) or effect.get("op") not in EFFECT_OPS:
            errors.append(f"{where}: unknown effect {effect!r} (ops: {', '.join(sorted(EFFECT_OPS))})")
            continue
        op = effect["op"]
        if op in ("set_flag", "clear_flag"):
            if not effect.get("flag"):
                errors.append(f"{where}: {op} needs a \"flag\"")
            continue
        if op in ("set_var", "add_var"):
            if not effect.get("var"):
                errors.append(f"{where}: {op} needs a \"var\"")
            continue
        if op in ("give_item", "take_item"):
            if not effect.get("item"):
                errors.append(f"{where}: {op} needs an \"item\" (a record id)")
            continue
        if op == "reputation":
            if not effect.get("faction"):
                errors.append(f"{where}: reputation needs a \"faction\"")
            continue
        if op == "play_cutscene":
            if not effect.get("cutscene"):
                errors.append(f"{where}: play_cutscene needs a \"cutscene\" id")
            continue
        if op in ("give_gold", "give_xp"):
            if not isinstance(effect.get("amount", effect.get("value")), (int, float)):
                errors.append(f"{where}: {op} needs an \"amount\"")
            continue
        quest = effect.get("quest", "")
        objectives = scope.objectives_of(quest)
        if objectives is None:
            errors.append(f"{where}: no quest {quest!r} (write it with write_quest first)")
        elif op == "complete_objective" and effect.get("objective") not in objectives:
            errors.append(f"{where}: quest {quest!r} has no objective {effect.get('objective')!r}")
        elif op == "set_stage" and effect.get("stage") not in (scope.stages_of(quest) or []):
            errors.append(f"{where}: quest {quest!r} has no stage {effect.get('stage')!r}")
    return errors


def _check_conditions(conditions: Any, where: str, scope: StoryScope) -> list[str]:
    errors = []
    if conditions is None:
        return errors
    if not isinstance(conditions, list):
        return [f"{where}: \"if\" must be a list"]
    for c in conditions:
        if not isinstance(c, dict):
            errors.append(f"{where}: a condition must be an object")
        elif "flag" in c:
            continue
        elif "var" in c:
            if c.get("op", "==") not in ("==", "!=", "<", "<=", ">", ">="):
                errors.append(f"{where}: a variable's op is one of == != < <= > >=")
        elif "quest" not in c:
            if not WORLD_KEYS & set(c):
                errors.append(f"{where}: a condition is a flag, a quest, a var or one of {', '.join(sorted(WORLD_KEYS))}")
        elif scope.objectives_of(c.get("quest", "")) is None:
            errors.append(f"{where}: condition on unknown quest {c.get('quest')!r}")
        elif c.get("is", "active") not in QUEST_STATES:
            errors.append(f"{where}: quest state should be inactive, active, completed or failed")
        elif "stage" in c and c["stage"] not in (scope.stages_of(c["quest"]) or []):
            errors.append(f"{where}: quest {c['quest']!r} has no stage {c['stage']!r}")
    return errors


def check_dialogue(dialogue_id: str, d: dict[str, Any], scope: StoryScope, where: str) -> list[str]:
    errors: list[str] = []
    if not SAFE_ID.match(dialogue_id or ""):
        errors.append(f"{where}: id must be lowercase letters, digits, - and _")
    nodes = d.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        return errors + [f"{where}: needs a \"nodes\" list"]
    ids = [n.get("id", "") for n in nodes if isinstance(n, dict)]
    if len(ids) != len(nodes) or "" in ids:
        errors.append(f"{where}: every node needs an \"id\"")
    if len(set(ids)) != len(ids):
        errors.append(f"{where}: node ids must be unique")
    by_id = {n.get("id"): n for n in nodes if isinstance(n, dict)}
    start = d.get("start") or (ids[0] if ids else "")
    if start not in by_id:
        errors.append(f"{where}: start {start!r} isn't a node")
    if not scope.known_character(d.get("speaker")):
        errors.append(f"{where}: speaker {d.get('speaker')!r} isn't in the World Bible (add it with world.write)")

    roots = [start]
    for s in d.get("starts", []) or []:
        if not isinstance(s, dict) or s.get("node") not in by_id:
            errors.append(f"{where}: a start goes to a node that doesn't exist")
            continue
        roots.append(s["node"])
        errors += _check_conditions(s.get("if"), f"{where} start {s['node']!r}", scope)

    reached: set[str] = set()
    stack = list(roots)
    while stack:
        node = by_id.get(stack.pop())
        if node is None or node["id"] in reached:
            continue
        reached.add(node["id"])
        choices = node.get("choices") or []
        if not choices and node.get("next"):
            stack.append(node["next"])
        stack += [c.get(k) for c in choices if isinstance(c, dict) for k in ("next", "fail") if c.get(k)]

    for node in nodes:
        if not isinstance(node, dict):
            continue
        at = f"{where} node {node.get('id')!r}"
        if not node.get("text"):
            errors.append(f"{at} has no text")
        if node.get("id") not in reached:
            errors.append(f"{at} can't be reached")
        if not scope.known_character(node.get("speaker")):
            errors.append(f"{at}: speaker {node.get('speaker')!r} isn't in the World Bible")
        choices = node.get("choices") or []
        if not choices and node.get("next") and node["next"] not in by_id:
            errors.append(f"{at} continues to {node['next']!r}, which isn't a node")
        if len(choices) > MAX_CHOICES:
            errors.append(f"{at} has more than {MAX_CHOICES} choices")
        errors += _check_effects(node.get("do"), at, scope)
        for i, choice in enumerate(choices, start=1):
            c_at = f"{at} choice {i}"
            if not isinstance(choice, dict) or not choice.get("text"):
                errors.append(f"{c_at} has no text")
                continue
            if choice.get("next") and choice["next"] not in by_id:
                errors.append(f"{c_at} goes to {choice['next']!r}, which isn't a node")
            if choice.get("fail") and choice["fail"] not in by_id:
                errors.append(f"{c_at} fails to {choice['fail']!r}, which isn't a node")
            check = choice.get("check")
            if check is not None and (not isinstance(check, dict) or not WORLD_KEYS & set(check)):
                errors.append(f"{c_at}: a check is like {{\"attribute\": \"speech\", \"difficulty\": 5}}")
            if choice.get("fail") and check is None:
                errors.append(f"{c_at} has a \"fail\" node but no \"check\"")
            errors += _check_conditions(choice.get("if"), c_at, scope)
            errors += _check_effects(choice.get("do"), c_at, scope)
    return errors


CUTSCENE_OPS = {
    "actor": {"move", "face", "show", "hide", "place"},
    "dialogue": {"line"},
    "camera": {"follow", "move", "shake", "zoom"},
    "audio": {"music", "sound"},
    "screen": {"fade_out", "fade_in", "title", "letterbox"},
    "logic": {"do", "emit", "wait_input"},
    "effect": {"play"},
}


def check_cutscene(cutscene_id: str, c: dict[str, Any], scope: StoryScope, markers: set[str] | None,
                   where: str) -> list[str]:
    """Mirror of engine ValidateCutscene (engine/gameplay/Cutscene), plus markers and
    speakers. `markers`: every scene's markers with those added here (None: not checked)."""
    errors: list[str] = []
    if not SAFE_ID.match(cutscene_id or ""):
        errors.append(f"{where}: id must be lowercase letters, digits, - and _")
    tracks = c.get("tracks")
    if not isinstance(tracks, list) or not tracks:
        return errors + [f"{where}: needs a non-empty \"tracks\" list"]
    for i, track in enumerate(tracks, start=1):
        if not isinstance(track, dict):
            errors.append(f"{where}: tracks must be objects")
            continue
        kind = track.get("type")
        at = f"{where} track {i} ({kind})"
        if kind not in CUTSCENE_OPS:
            errors.append(f"{at}: type must be one of {', '.join(CUTSCENE_OPS)}")
            continue
        if kind == "actor" and not track.get("actor"):
            errors.append(f"{at} needs an \"actor\"")
        for key in track.get("keys", []) or []:
            if not isinstance(key, dict):
                errors.append(f"{at}: keys must be objects")
                continue
            op = key.get("op", "line" if kind == "dialogue" else "do" if kind == "logic" and "do" in key else "")
            k_at = f"{at} at {key.get('at', 0)}s"
            if op not in CUTSCENE_OPS[kind]:
                errors.append(f"{k_at}: op must be one of {', '.join(sorted(CUTSCENE_OPS[kind]))}")
                continue
            if op in ("move", "place") and "marker" not in key and not ("x" in key and "y" in key):
                errors.append(f"{k_at}: {op} needs a \"marker\" (or x and y)")
            if "marker" in key and markers is not None and key["marker"] not in markers:
                errors.append(f"{k_at}: no marker called {key['marker']!r} (add it with scene.addMarker)")
            if op == "line":
                if not key.get("text"):
                    errors.append(f"{k_at}: a line needs \"text\"")
                if not scope.known_character(key.get("speaker")) and not str(key.get("speaker", "")).startswith("@"):
                    errors.append(f"{k_at}: speaker {key.get('speaker')!r} isn't in the World Bible")
            if op in ("music", "sound") and "path" not in key:
                errors.append(f"{k_at}: needs a \"path\"")
            if op == "emit" and not key.get("event"):
                errors.append(f"{k_at}: emit needs an \"event\"")
            if op == "follow" and not key.get("actor"):
                errors.append(f"{k_at}: follow needs an \"actor\"")
            if op == "do":
                errors += _check_effects(key.get("do"), k_at, scope)
    return errors


ACTIVITIES = {"stay", "wander", "sleep"}
CLOCK = re.compile(r"^([01]?\d|2[0-3]):[0-5]\d$")


def check_schedule(schedule_id: str, s: dict[str, Any], markers: set[str], where: str,
                   scene_markers: dict[str, set[str]] | None = None) -> list[str]:
    """Mirror of engine ValidateSchedule (engine/gameplay/Schedule). `markers`
    are the open scene's; an entry naming another scene uses that scene's."""
    errors: list[str] = []
    if not SAFE_ID.match(schedule_id or ""):
        errors.append(f"{where}: id must be lowercase letters, digits, - and _")
    entries = s.get("entries")
    if not isinstance(entries, list) or not entries:
        return errors + [f"{where}: needs a non-empty \"entries\" list"]
    starts = []
    for e in entries:
        if not isinstance(e, dict):
            errors.append(f"{where}: entries must be objects")
            continue
        start = e.get("from", "")
        at = f"{where} entry {start!r}"
        if not isinstance(start, str) or not CLOCK.match(start):
            errors.append(f"{at}: \"from\" must be a time like 06:30")
            continue
        starts.append(start.zfill(5))
        if e.get("activity") not in ACTIVITIES:
            errors.append(f"{at}: activity must be stay, wander or sleep")
        scene = e.get("scene", "")
        if scene and scene_markers is not None and scene not in scene_markers:
            errors.append(f"{at}: no scene {scene!r}")
        else:
            known = scene_markers[scene] if scene and scene_markers is not None else markers
            if e.get("place") not in known:
                errors.append(f"{at}: no marker called {e.get('place')!r}" + (f" in {scene}" if scene else "")
                              + " (add it with scene.addMarker)")
        if e.get("activity") == "wander":
            radius = e.get("radius", 2)
            if not isinstance(radius, (int, float)) or isinstance(radius, bool) or not 0 < radius <= 20:
                errors.append(f"{at}: wander radius must be between 0 and 20 cells")
    if len(set(starts)) != len(starts):
        errors.append(f"{where}: two entries start at the same time")
    return errors


def _check_objective(o: dict, at: str, scope: StoryScope, aliases: dict[str, str]) -> list[str]:
    errors: list[str] = []
    if not o.get("text"):
        errors.append(f"{at} needs \"text\" (shown on screen)")
    kind, scene = o.get("type"), o.get("scene", "")
    target = o.get("target", "")
    if isinstance(target, str) and target.startswith("@"):
        if target[1:] not in aliases:
            errors.append(f"{at}: no alias {target[1:]!r}")
            return errors
        target = aliases[target[1:]]
    scenes = scope.scene_markers
    if scene and scenes is not None and scene not in scenes:
        errors.append(f"{at}: no scene {scene!r} (the context's \"scenes\" lists them)")
    if kind not in OBJECTIVE_TYPES:
        errors.append(f"{at}: type must be one of {', '.join(sorted(OBJECTIVE_TYPES))}")
    elif kind == "reach":
        if not target and not scene:
            errors.append(f"{at}: a reach objective needs a marker (\"target\") or a \"scene\" to arrive in")
        elif target and scenes is not None:
            known = scenes.get(scene, set()) if scene else (set().union(*scenes.values()) if scenes else set())
            if target not in known:
                errors.append(f"{at}: no marker called {target!r} in " + (scene or "any scene"))
    elif kind in ("talk", "deliver") and "/" in str(target) and target not in scope.world_keys:
        errors.append(f"{at}: {target!r} isn't in the World Bible")
    elif kind == "talk" and target not in scope.world_keys:
        errors.append(f"{at}: {target!r} isn't in the World Bible")
    elif kind in ("defeat", "collect") and target not in scope.prefabs and not (kind == "defeat" and target in TEAMS):
        errors.append(f"{at}: {target!r} isn't a prefab" + (" or a team (Enemy)" if kind == "defeat" else ""))
    elif kind not in ("survive", "reach") and not target:
        errors.append(f"{at} has no target")
    if kind == "deliver" and not o.get("item"):
        errors.append(f"{at}: deliver needs an \"item\"")
    if kind == "escort" and not o.get("to"):
        errors.append(f"{at}: escort needs \"to\" (the marker to bring them to)")
    if kind in ("survive", "defend") and not (isinstance(o.get("seconds"), (int, float)) and o["seconds"] > 0):
        errors.append(f"{at}: {kind} needs \"seconds\"")
    count = o.get("count", 1)
    if not isinstance(count, int) or isinstance(count, bool) or count < 1:
        errors.append(f"{at}: count must be a whole number ≥ 1")
    return errors


def check_quest(quest_id: str, q: dict[str, Any], scope: StoryScope, where: str) -> list[str]:
    errors: list[str] = []
    if not SAFE_ID.match(quest_id or ""):
        errors.append(f"{where}: id must be lowercase letters, digits, - and _")
    if not q.get("name"):
        errors.append(f"{where}: needs a \"name\"")
    if not scope.known_character(q.get("giver")):
        errors.append(f"{where}: giver {q.get('giver')!r} isn't in the World Bible")
    aliases: dict[str, str] = {}
    for a in q.get("aliases", []) or []:
        if not isinstance(a, dict) or not a.get("id") or a.get("id") in aliases:
            errors.append(f"{where}: alias ids must be present and unique")
            continue
        if not a.get("fill"):
            errors.append(f"{where}: alias {a['id']!r} isn't filled")
        aliases[a["id"]] = a.get("fill", "")
    stages = q.get("stages")
    if stages is not None:
        if not isinstance(stages, list) or not stages:
            return errors + [f"{where}: \"stages\" must be a non-empty list"]
        indices = [st.get("index") for st in stages if isinstance(st, dict)]
        if len(set(indices)) != len(indices) or None in indices:
            errors.append(f"{where}: every stage needs a unique \"index\" (10, 20, ...)")
        for st in stages:
            if not isinstance(st, dict):
                continue
            st_at = f"{where} stage {st.get('index')!r}"
            for target in [st.get("next")] + [b.get("stage") for b in st.get("branches", []) or [] if isinstance(b, dict)]:
                if target not in (None, 0) and target not in indices:
                    errors.append(f"{st_at}: goes to stage {target!r}, which isn't one")
            for b in st.get("branches", []) or []:
                if isinstance(b, dict):
                    errors += _check_conditions(b.get("if"), st_at, scope)
            if st.get("ends") not in (None, "", "complete", "fail"):
                errors.append(f"{st_at}: \"ends\" is complete or fail")
            errors += _check_effects(st.get("onEnter"), f"{st_at} onEnter", scope)
        objectives = quest_objectives(q)
    else:
        objectives = q.get("objectives")
        if not isinstance(objectives, list) or not objectives:
            return errors + [f"{where}: needs a \"stages\" list (or a non-empty \"objectives\" list)"]
    ids = []
    for o in objectives:
        if not isinstance(o, dict):
            errors.append(f"{where}: objectives must be objects")
            continue
        ids.append(o.get("id"))
        errors += _check_objective(o, f"{where} objective {o.get('id')!r}", scope, aliases)
    if None in ids or "" in ids or len(set(ids)) != len(ids):
        errors.append(f"{where}: objective ids must be present and unique")
    rewards = q.get("rewards")
    if rewards is not None and (not isinstance(rewards, dict) or (rewards.get("reputation") and not rewards.get("faction"))):
        errors.append(f"{where}: rewards are {{experience, gold, items, faction, reputation}}; reputation needs a faction")
    errors += _check_effects(q.get("onComplete"), f"{where} onComplete", scope)
    errors += _check_effects(q.get("onFail"), f"{where} onFail", scope)
    return errors
