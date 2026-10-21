"""Instruction -> validated proposal, with one repair round trip when the
model's first answer doesn't validate."""

from __future__ import annotations

import json
import re
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
    "command", "write_dialogue", "write_quest", "write_schedule", "write_cutscene", "write_ui", "write_record", "delete_record",
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


def _with_art(proposal: Proposal, made: dict[str, str]) -> Proposal:
    """The operations with "art:<name>" replaced by the path of the art made for it."""
    def swap(value):
        if isinstance(value, str) and value.startswith("art:") and value[4:] in made:
            return made[value[4:]]
        if isinstance(value, list):
            return [swap(v) for v in value]
        if isinstance(value, dict):
            return {k: swap(v) for k, v in value.items()}
        return value
    data = proposal.model_dump()
    data["operations"] = swap(data["operations"])
    return Proposal.model_validate(data)


def propose(
    provider: Provider,
    schema: EngineSchema,
    context: EditorContext,
    instruction: str,
    history: list[dict] | None = None,
    canon: str = "",
    make_art=None,
) -> ProposalResult:
    """`make_art(requests) -> {name: path}` draws a proposal's art (the Art tool); without it, a
    proposal that needs art is told it can't have it."""
    result = ProposalResult(proposal=None, errors=[])
    made: dict[str, str] = {}
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
        if proposal is not None and proposal.art:
            # Its art first (once per name): then its operations can use it, and it's known art.
            wanted = [a for a in proposal.art if a.name not in made]
            if wanted and make_art is not None:
                made.update(make_art(wanted))
            missing = [a.name for a in proposal.art if a.name not in made]
            proposal = _with_art(proposal, made)
            if made:
                context = context.model_copy(update={"assets": list(context.assets) + [{"path": p, "type": "texture"} for p in made.values()]})
            if missing:
                errors = [f"art {', '.join(missing)} couldn't be made: use art the project has"]
        if proposal is not None and not errors:
            errors = validate_proposal(proposal, schema, context)
        if not errors:
            result.proposal, result.errors = proposal, []
            return result

        result.proposal, result.errors = proposal, errors
        messages = messages + [{"role": "assistant", "content": completion.text}, repair_message(errors)]
    salvaged = _salvage_step(result.proposal, result.errors)
    if salvaged is not None:
        result.proposal, result.errors = salvaged, []
    return result


_OPERATION_ERROR = re.compile(r"^operation (\d+) \(")


def _salvage_step(proposal: Proposal | None, errors: list[str]) -> Proposal | None:
    """A task step whose remaining problems are all in particular operations
    (typically changing something spawned in the same answer, which has no id
    yet) keeps its valid operations: they go to the working copy, and the left-
    out ones go back to the model as "skipped", to redo in the next step once
    what they refer to exists. Losing a whole step, and with it the task, over
    one bad operation helps nobody. Plain proposals still fail as a whole."""
    if proposal is None or proposal.scope != "task" or not errors:
        return None
    bad: set[int] = set()
    for error in errors:
        match = _OPERATION_ERROR.match(error)
        if match is None:
            return None  # a problem with the answer as a whole
        bad.add(int(match.group(1)))
    kept = [op for index, op in enumerate(proposal.operations, start=1) if index not in bad]
    if not kept:
        return None
    return proposal.model_copy(update={"operations": kept, "skipped": list(errors)})
