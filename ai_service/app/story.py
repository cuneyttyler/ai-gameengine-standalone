"""Checks for write_dialogue / write_quest operations (development_plan.md 3.2d),
mirroring engine/gameplay/Story (ValidateDialogue, ValidateQuest) and
engine/world/StoryReferences, so the model gets the same feedback the editor
would show — before anything reaches the review panel. Format: docs/story.md."""

from __future__ import annotations

import re
from typing import Any

EFFECT_OPS = {"set_flag", "clear_flag", "start_quest", "complete_quest", "complete_objective"}
OBJECTIVE_TYPES = {"defeat", "talk", "collect"}
QUEST_STATES = {"inactive", "active", "completed"}
TEAMS = {"Enemy", "Neutral"}
SAFE_ID = re.compile(r"^[a-z0-9_-]+$")


class StoryScope:
    """What a check can refer to: the project's quests and World Bible, plus
    what the same proposal adds (quests it writes, entries it writes)."""

    def __init__(self, quests: dict[str, dict], world_keys: set[str], prefabs: set[str]):
        self.quests = quests          # id -> quest JSON
        self.world_keys = world_keys  # "<kind>/<id>"
        self.prefabs = prefabs

    def objectives_of(self, quest: str) -> list[str] | None:
        q = self.quests.get(quest)
        if q is None:
            return None
        return [o.get("id", "") for o in q.get("objectives", []) if isinstance(o, dict)]

    def known_character(self, who: Any) -> bool:
        if not isinstance(who, str) or not who or who == "player" or "/" not in who:
            return True  # plain names ("A guard") are allowed
        return who in self.world_keys


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
        quest = effect.get("quest", "")
        objectives = scope.objectives_of(quest)
        if objectives is None:
            errors.append(f"{where}: no quest {quest!r} (write it with write_quest first)")
        elif op == "complete_objective" and effect.get("objective") not in objectives:
            errors.append(f"{where}: quest {quest!r} has no objective {effect.get('objective')!r}")
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
        elif scope.objectives_of(c.get("quest", "")) is None:
            errors.append(f"{where}: condition on unknown quest {c.get('quest')!r}")
        elif c.get("is", "active") not in QUEST_STATES:
            errors.append(f"{where}: quest state should be inactive, active or completed")
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
        stack += [c.get("next") for c in choices if isinstance(c, dict) and c.get("next")]

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
        if len(choices) > 4:
            errors.append(f"{at} has more than 4 choices")
        errors += _check_effects(node.get("do"), at, scope)
        for i, choice in enumerate(choices, start=1):
            c_at = f"{at} choice {i}"
            if not isinstance(choice, dict) or not choice.get("text"):
                errors.append(f"{c_at} has no text")
                continue
            if choice.get("next") and choice["next"] not in by_id:
                errors.append(f"{c_at} goes to {choice['next']!r}, which isn't a node")
            errors += _check_conditions(choice.get("if"), c_at, scope)
            errors += _check_effects(choice.get("do"), c_at, scope)
    return errors


def check_quest(quest_id: str, q: dict[str, Any], scope: StoryScope, where: str) -> list[str]:
    errors: list[str] = []
    if not SAFE_ID.match(quest_id or ""):
        errors.append(f"{where}: id must be lowercase letters, digits, - and _")
    if not q.get("name"):
        errors.append(f"{where}: needs a \"name\"")
    if not scope.known_character(q.get("giver")):
        errors.append(f"{where}: giver {q.get('giver')!r} isn't in the World Bible")
    objectives = q.get("objectives")
    if not isinstance(objectives, list) or not objectives:
        return errors + [f"{where}: needs a non-empty \"objectives\" list"]
    ids = []
    for o in objectives:
        if not isinstance(o, dict):
            errors.append(f"{where}: objectives must be objects")
            continue
        at = f"{where} objective {o.get('id')!r}"
        ids.append(o.get("id"))
        if not o.get("text"):
            errors.append(f"{at} needs \"text\" (shown on screen)")
        kind, target = o.get("type"), o.get("target", "")
        if kind not in OBJECTIVE_TYPES:
            errors.append(f"{at}: type must be defeat, talk or collect")
        elif kind == "talk" and target not in scope.world_keys:
            errors.append(f"{at}: {target!r} isn't in the World Bible")
        elif kind in ("defeat", "collect") and target not in scope.prefabs and not (kind == "defeat" and target in TEAMS):
            errors.append(f"{at}: {target!r} isn't a prefab" + (" or a team (Enemy)" if kind == "defeat" else ""))
        count = o.get("count", 1)
        if not isinstance(count, int) or isinstance(count, bool) or count < 1:
            errors.append(f"{at}: count must be a whole number ≥ 1")
    if None in ids or "" in ids or len(set(ids)) != len(ids):
        errors.append(f"{where}: objective ids must be present and unique")
    errors += _check_effects(q.get("onComplete"), f"{where} onComplete", scope)
    return errors
