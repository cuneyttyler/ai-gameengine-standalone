"""Instruction -> validated proposal, with one repair round trip when the
model's first answer doesn't validate."""

from __future__ import annotations

import json
from dataclasses import dataclass, field

from pydantic import ValidationError

from .models import EditorContext, EngineSchema, Proposal, Usage
from .prompt import build_messages, repair_message
from .providers import Provider, ProviderError
from .validation import validate_proposal

MAX_ATTEMPTS = 2  # the first answer, plus one repair


@dataclass
class ProposalResult:
    proposal: Proposal | None
    errors: list[str]
    usage: Usage = field(default_factory=Usage)
    attempts: int = 0
    model: str = ""


_OPS = {
    "set_field", "add_component", "remove_component", "set_prefab_field", "set_setting", "set_script_property",
    "command", "write_dialogue", "write_quest",
}


def _drop_stray_scalars(value: object) -> object:
    """In JSON mode the model sometimes emits a bare number as the first item of
    a list of objects ([0, {...}, {...}]). No list in the answer format mixes
    objects and scalars, so such a scalar is noise; dropping it is cheaper than
    a repair round trip, and unambiguous."""
    if isinstance(value, dict):
        return {k: _drop_stray_scalars(v) for k, v in value.items()}
    if isinstance(value, list):
        items = [_drop_stray_scalars(v) for v in value]
        objects = [v for v in items if isinstance(v, dict)]
        if objects and all(isinstance(v, (dict, int, float)) and not isinstance(v, bool) for v in items):
            return objects
        return items
    return value


def _normalize(data: object, schema: EngineSchema) -> object:
    """Accepts common near-misses that are cheaper to fix than to repair, and
    unambiguous: stray scalars in lists of objects, and a command written as its
    own op ({"op": "script.write", "path": ...}) instead of {"op": "command", ...}."""
    data = _drop_stray_scalars(data)
    if not isinstance(data, dict) or not isinstance(data.get("operations"), list):
        return data
    fixed = []
    for op in data["operations"]:
        if isinstance(op, dict) and op.get("op") not in _OPS and schema.command(str(op.get("op"))):
            args = op.get("args") if isinstance(op.get("args"), dict) else {k: v for k, v in op.items() if k != "op"}
            op = {"op": "command", "name": op["op"], "args": args}
        fixed.append(op)
    return {**data, "operations": fixed}


def _parse(text: str, schema: EngineSchema) -> tuple[Proposal | None, list[str]]:
    try:
        data = _normalize(json.loads(text), schema)
    except json.JSONDecodeError as error:
        return None, [f"the answer wasn't valid JSON ({error.msg})"]
    try:
        return Proposal.model_validate(data), []
    except ValidationError as error:
        problems = []
        for issue in error.errors()[:8]:
            location = ".".join(str(part) for part in issue["loc"])
            problems.append(f"{location}: {issue['msg']}")
        return None, problems


def propose(
    provider: Provider,
    schema: EngineSchema,
    context: EditorContext,
    instruction: str,
    history: list[dict] | None = None,
    canon: str = "",
) -> ProposalResult:
    result = ProposalResult(proposal=None, errors=[])
    messages = build_messages(schema, context, instruction, history, canon)
    for attempt in range(1, MAX_ATTEMPTS + 1):
        result.attempts = attempt
        try:
            completion = provider.complete(messages)
        except ProviderError as error:
            result.errors = [str(error)]
            return result
        result.usage.add(completion.usage)
        result.model = completion.model

        proposal, errors = _parse(completion.text, schema)
        if proposal is not None:
            errors = validate_proposal(proposal, schema, context)
        if not errors:
            result.proposal, result.errors = proposal, []
            return result

        result.proposal, result.errors = proposal, errors
        messages = messages + [{"role": "assistant", "content": completion.text}, repair_message(errors)]
    return result
