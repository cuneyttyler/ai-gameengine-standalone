"""Records the golden answers: runs each fixed prompt against the real model
and saves exactly what it answered, so the test suite can replay it offline.

    cd ai_service
    uv run python -m tests.golden.record            # all prompts
    uv run python -m tests.golden.record burning    # just one

Re-record after changing the prompt, the schema, or the demo project, then
check the diffs of tests/golden/*.json before committing: those files are the
evidence of how the model actually behaves.
"""

from __future__ import annotations

import json
import sys
from datetime import date
from pathlib import Path

from app.main import default_provider
from app.models import EditorContext, EngineSchema
from app.providers import Completion
from app.service import propose

HERE = Path(__file__).parent
FIXTURES = HERE.parent / "fixtures"

# The fixed prompts. `focus` mimics asking from an Inspector field.
PROMPTS: dict[str, dict] = {
    "burning": {"instruction": "Add a burning status effect that deals damage over time when the hero hits enemies."},
    "tougher_skeletons": {"instruction": "Make all the skeletons tougher."},
    "spawn_skeletons": {"instruction": "Add two more skeletons near the hero."},
    "night_sky": {"instruction": "Make the background a dark blue night sky."},
    "villager_follows": {"instruction": "The villager should follow the hero around."},
    "flee_script": {
        "instruction": "Skeletons should run away from the hero when their health drops below 3.",
    },
}


class Recorder:
    """Wraps the real provider and keeps every raw answer."""

    def __init__(self, inner):
        self.inner = inner
        self.name, self.model = inner.name, inner.model
        self.responses: list[str] = []

    def complete(self, messages: list[dict]) -> Completion:
        completion = self.inner.complete(messages)
        self.responses.append(completion.text)
        return completion


def main(names: list[str]) -> int:
    provider = default_provider()
    if provider is None:
        print("No provider configured (MISTRAL_API_KEY in ai_service/.env)")
        return 1
    schema = EngineSchema.model_validate_json((FIXTURES / "engine_schema.json").read_text(encoding="utf-8"))
    context_json = json.loads((FIXTURES / "demo_context.json").read_text(encoding="utf-8"))

    for name in names or list(PROMPTS):
        prompt = PROMPTS[name]
        context = EditorContext.model_validate({**context_json, "focus": prompt.get("focus")})
        recorder = Recorder(provider)
        result = propose(recorder, schema, context, prompt["instruction"])
        record = {
            "name": name,
            "instruction": prompt["instruction"],
            "focus": prompt.get("focus"),
            "model": result.model,
            "recorded": date.today().isoformat(),
            "attempts": result.attempts,
            "valid": result.proposal is not None and not result.errors,
            "responses": recorder.responses,
        }
        (HERE / f"{name}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        status = "ok" if record["valid"] else f"INVALID: {result.errors}"
        print(f"{name}: {status} ({result.attempts} attempt(s), {result.usage.inputTokens}+{result.usage.outputTokens} tok)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
