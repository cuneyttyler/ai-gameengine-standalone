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


def _parse(text: str) -> tuple[Proposal | None, list[str]]:
    try:
        data = json.loads(text)
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
) -> ProposalResult:
    result = ProposalResult(proposal=None, errors=[])
    messages = build_messages(schema, context, instruction, history)
    for attempt in range(1, MAX_ATTEMPTS + 1):
        result.attempts = attempt
        try:
            completion = provider.complete(messages)
        except ProviderError as error:
            result.errors = [str(error)]
            return result
        result.usage.add(completion.usage)
        result.model = completion.model

        proposal, errors = _parse(completion.text)
        if proposal is not None:
            errors = validate_proposal(proposal, schema, context)
        if not errors:
            result.proposal, result.errors = proposal, []
            return result

        result.proposal, result.errors = proposal, errors
        messages = messages + [{"role": "assistant", "content": completion.text}, repair_message(errors)]
    return result
