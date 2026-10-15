"""Contradiction checking (development_plan.md 3.3): a second, cheaper model
pass that reads new writing — World Bible entries, dialogue, quests — against
the canon most related to it, and lists what contradicts it. It flags; the
developer decides."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from typing import Any

from .models import Usage
from .providers import Provider, ProviderError

SEVERITIES = ("contradiction", "possible")

SYSTEM = """\
You check new writing for a game against its established canon (the World Bible).
Find places where the NEW WRITING says something that cannot be true if the CANON is true:
a different job, age, history, relationship, allegiance, location or nature.

A contradiction needs evidence: you must copy the exact canon words it conflicts with.
These are NOT contradictions, never list them:
- details the canon doesn't mention (new people, places, objects, events, opinions);
- characters' opinions, complaints or jokes about each other;
- anything you'd describe as "the canon doesn't say/mention/describe ...".

Answer with ONE JSON object and nothing else:
{"issues": [{"op": <the [op N] number the quote comes from>,
             "quote": "exact words from the new writing",
             "conflictsWith": "<the canon key in brackets, exactly, e.g. character/villager>",
             "canonSays": "exact words copied from that canon entry, which the quote contradicts",
             "explanation": "one sentence",
             "severity": "contradiction" | "possible"}]}
Use "contradiction" when both can't be true, "possible" when it depends on reading.
Most writing has no contradictions: {"issues": []}"""


@dataclass
class Issue:
    op: int | None
    quote: str
    conflictsWith: str
    canonSays: str
    explanation: str
    severity: str


@dataclass
class ConsistencyResult:
    checked: bool
    issues: list[Issue] = field(default_factory=list)
    usage: Usage = field(default_factory=Usage)
    errors: list[str] = field(default_factory=list)
    canonKeys: list[str] = field(default_factory=list)


def writing_from_operations(operations: list[dict[str, Any]]) -> str:
    """The parts of a proposal that make claims about the world, as text the
    checker can quote, each labeled with its 1-based operation number. Empty
    when there's nothing to check."""
    parts = []
    for index, op in enumerate(operations, start=1):
        kind = op.get("op")
        if kind == "command" and op.get("name") == "world.write":
            a = op.get("args", {})
            lines = [f"[op {index}] New World Bible entry {a.get('kind')}/{a.get('id') or '(from name)'}: {a.get('name')}"]
            for key in ("summary", "tags", "relations", "body"):
                if a.get(key):
                    lines.append(f"{key}: {a[key]}")
            parts.append("\n".join(lines))
        elif kind == "write_dialogue":
            d = op.get("dialogue", {})
            default = d.get("speaker", "")
            lines = [f"[op {index}] Dialogue dialogue/{op.get('id')}.json (speaker {default})"]
            for node in d.get("nodes", []):
                if not isinstance(node, dict):
                    continue
                speaker = node.get("speaker") or default
                lines.append(f"{speaker}: {node.get('text', '')}")
                for choice in node.get("choices", []) or []:
                    if isinstance(choice, dict):
                        lines.append(f"  player: {choice.get('text', '')}")
            parts.append("\n".join(lines))
        elif kind == "write_quest":
            q = op.get("quest", {})
            lines = [f"[op {index}] Quest quests/{op.get('id')}.json: {q.get('name')} (given by {q.get('giver')})"]
            if q.get("description"):
                lines.append(q["description"])
            for o in q.get("objectives", []) or []:
                if isinstance(o, dict):
                    lines.append(f"- {o.get('text', '')}")
            parts.append("\n".join(lines))
    return "\n\n".join(parts)


def _words(text: str) -> list[str]:
    return re.findall(r"[a-z0-9']+", text.lower())


def _contiguous(needle: list[str], hay: list[str]) -> bool:
    n = len(needle)
    return any(hay[i : i + n] == needle for i in range(len(hay) - n + 1))


def _in_text(fragment: str, text: str) -> bool:
    """Whether `fragment` was really copied from `text`: each of its sentences
    appears there word for word (punctuation and case aside). Sentences may come
    from different parts of an entry (its summary and its body)."""
    hay = _words(text)
    sentences = [_words(s) for s in re.split(r"[.!?;\n]+", fragment)]
    sentences = [s for s in sentences if s]
    if sum(len(s) for s in sentences) < 3:
        return False
    return all(_contiguous(s, hay) for s in sentences)


def _resolve_key(raw: str, canon_keys: set[str]) -> str | None:
    """A canon key as the model wrote it: exact, or with extra text after it
    ("character/villager/Old Tamsin")."""
    key = raw.strip().strip("[]").strip()
    if key in canon_keys:
        return key
    matches = [k for k in canon_keys if key.startswith(k + "/") or key.startswith(k + " ")]
    return max(matches, key=len) if matches else None


def parse_issues(
    text: str, canon: dict[str, str], op_count: int, writing: str = ""
) -> tuple[list[Issue], list[str]]:
    """The checker's answer, verified: an issue must name an entry it was shown,
    quote words actually in the new writing, and copy words actually in that
    entry (canonSays). Anything it can't back up is dropped — "the canon
    doesn't mention it" isn't a contradiction."""
    try:
        data = json.loads(text)
    except json.JSONDecodeError as error:
        return [], [f"the checker's answer wasn't JSON ({error.msg})"]
    raw = data.get("issues") if isinstance(data, dict) else None
    if not isinstance(raw, list):
        return [], ['the checker\'s answer has no "issues" list']
    issues = []
    for item in raw:
        if not isinstance(item, dict):
            continue
        key = _resolve_key(str(item.get("conflictsWith", "")), set(canon))
        quote = str(item.get("quote", "")).strip()
        says = str(item.get("canonSays", "")).strip()
        if key is None or not quote or not _in_text(says, canon[key]):
            continue
        if writing and not _in_text(quote, writing):
            continue
        op = item.get("op")
        op = op if isinstance(op, int) and not isinstance(op, bool) and 1 <= op <= op_count else None
        severity = item.get("severity") if item.get("severity") in SEVERITIES else "possible"
        issues.append(Issue(op, quote, key, says, str(item.get("explanation", "")).strip(), severity))
    return issues, []


def check_consistency(checker: Provider, canon: dict[str, str], writing: str, op_count: int) -> ConsistencyResult:
    """`canon` maps each entry shown to the checker to its text (format_entry)."""
    result = ConsistencyResult(checked=True, canonKeys=sorted(canon))
    if not writing.strip() or not canon:
        return result
    canon_text = "\n\n".join(canon.values())
    messages = [
        {"role": "system", "content": SYSTEM},
        {"role": "user", "content": f"CANON\n{canon_text}\n\nNEW WRITING\n{writing}"},
    ]
    try:
        completion = checker.complete(messages)
    except ProviderError as error:
        result.checked = False
        result.errors = [str(error)]
        return result
    result.usage.add(completion.usage)
    result.issues, result.errors = parse_issues(completion.text, canon, op_count, writing)
    if result.errors:
        result.checked = False
    return result
