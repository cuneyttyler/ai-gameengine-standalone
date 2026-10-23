"""Checks for the story knowledge graph (development_plan.md 21, docs/systems/story-ontology.md): the
story_graph op adds nodes and temporal facts; these checks mirror the engine's shapes
(engine/story/StoryGraph Apply and StoryRules) so a bad change goes back before it's shown.
The engine also runs the full rules and refuses a change that adds rule errors."""

from __future__ import annotations

from typing import Any


def _is_a(classes: dict[str, Any], cls: str, ancestor: str) -> bool:
    for _ in range(16):
        if cls == ancestor:
            return True
        parent = (classes.get(cls) or {}).get("parent")
        if not parent:
            return False
        cls = parent
    return False


def _field_type(classes: dict[str, Any], cls: str, field: str) -> str:
    for _ in range(16):
        spec = classes.get(cls) or {}
        for entry in spec.get("fields", []):
            name, _, kind = entry.partition(":")
            if name == field:
                return kind
        cls = spec.get("parent", "")
        if not cls:
            return ""
    return ""


def check_story_graph(proposal, context) -> list[str]:
    from .models import StoryGraphOp

    ops = [(i, op) for i, op in enumerate(proposal.operations, start=1) if isinstance(op, StoryGraphOp)]
    if not ops:
        return []
    graph = context.storyGraph or {}
    ontology = graph.get("ontology") or {}
    classes: dict[str, Any] = ontology.get("classes", {})
    properties: dict[str, Any] = ontology.get("properties", {})
    vocabularies: dict[str, list[str]] = ontology.get("vocabularies", {})
    if not classes:
        return [f"operation {ops[0][0]} (story_graph): the story ontology isn't in the context"]
    # The graph's nodes (id -> class) and scenes, then what this proposal adds.
    known: dict[str, str] = dict(graph.get("nodes", {}))
    scenes = {s.get("id") for s in graph.get("scenes", [])}
    for _, op in ops:
        for node in op.nodes:
            if node.get("id") and node.get("class"):
                known[node["id"]] = node["class"]
                if node["class"] == "StoryScene":
                    scenes.add(node["id"])
    if len(ops) > 1:
        return [f"operation {ops[1][0]} (story_graph): put every story fact in one story_graph operation"]

    errors: list[str] = []
    for index, op in ops:
        where = f"operation {index} (story_graph)"
        for node in op.nodes:
            nid, cls = node.get("id", ""), node.get("class", "")
            if not nid:
                errors.append(f"{where}: a node has no id")
                continue
            if not cls and nid not in known:
                errors.append(f"{where}: node {nid} has no class")
                continue
            cls = cls or known[nid]
            if cls not in classes:
                errors.append(f"{where}: node {nid}: no class {cls!r} ({', '.join(sorted(classes))})")
                continue
            for field, value in node.items():
                if field in ("id", "class") or not isinstance(value, str):
                    continue
                kind = _field_type(classes, cls, field)
                if kind in vocabularies and value not in vocabularies[kind]:
                    errors.append(f"{where}: node {nid}: {field} {value!r} isn't a {kind} ({', '.join(vocabularies[kind])})")
            if cls == "StoryScene" and ("order" not in node or "mode" not in node) and nid not in graph.get("nodes", {}):
                errors.append(f"{where}: scene {nid} needs an order and a mode")
        for fact in op.facts:
            p, s, o = fact.get("p", ""), fact.get("s", ""), fact.get("o", "")
            spec = properties.get(p)
            if spec is None:
                errors.append(f"{where}: no property {p!r}")
                continue
            if s not in known:
                errors.append(f"{where}: {p}: no node {s!r} (add it to nodes)")
                continue
            domain = spec.get("domain") or []
            if domain and not any(_is_a(classes, known[s], c) for c in domain):
                errors.append(f"{where}: {p}: {s} is a {known[s]}, not one of {', '.join(domain)}")
            rng = spec.get("range") or []
            if rng:
                if not o and not (p == "knows" and fact.get("about")):
                    errors.append(f"{where}: {p} from {s} needs an object 'o'")
                elif o and o not in known:
                    errors.append(f"{where}: {p}: no node {o!r} (add it to nodes)")
                elif o and not any(_is_a(classes, known[o], c) for c in rng):
                    errors.append(f"{where}: {p}: {o} is a {known[o]}, not one of {', '.join(rng)}")
            for key in ("from", "until"):
                if fact.get(key) and fact[key] not in scenes:
                    errors.append(f"{where}: {p} {key} {fact[key]!r}: no such scene")
            if p == "hasRelationship" and fact.get("type") not in vocabularies.get("RelationshipType", []):
                errors.append(f"{where}: relationship type {fact.get('type')!r} isn't one of "
                              f"{', '.join(vocabularies.get('RelationshipType', []))}")
        for end in op.end:
            if end.get("at") and end["at"] not in scenes:
                errors.append(f"{where}: end {end.get('p')} at {end['at']!r}: no such scene")
    return errors
