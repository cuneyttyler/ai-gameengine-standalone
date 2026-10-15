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
import shutil
import sys
import tempfile
from datetime import date
from pathlib import Path

from app.main import default_embedder, default_provider
from app.models import EditorContext, EngineSchema
from app.providers import Completion
from app.service import propose
from app.world import WorldIndex

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
    # Request sizing (development_plan.md 2.9): feature-sized -> "task", too big -> "split".
    "necromancer": {
        "instruction": "Add a necromancer enemy: it keeps its distance from the hero, summons a skeleton next to "
        "itself every 5 seconds, and is weak in melee. Make it a prefab and put two of them in the courtyard.",
    },
    "whole_rpg": {
        "instruction": "Turn this into a complete open-world RPG with an inventory, crafting, a quest line with "
        "dialogue, a world map, day and night, and save games.",
    },
    # Grounded story generation (development_plan.md 3.2d): canon names, places and tone.
    "tamsin_draughts": {
        "instruction": "Give Old Tamsin a second quest for after the courtyard is quiet: bring her two of her red "
        "healing draughts back from the courtyard. Update her conversation so she offers it.",
    },
    "quartermaster": {
        "instruction": "Add the Wardens' quartermaster in Brookvale as a new character, and write a short "
        "conversation where she warns the hero about the old keep.",
    },
    "tamsin_wardens": {
        "instruction": "Old Tamsin should have something to say about the Wardens: add a choice to her "
        "conversation where the hero asks what she thinks of them. Keep everything else she says.",
    },
}

# The demo's World Bible, indexed like the editor's service indexes it, so the
# recorded answers saw the same retrieved canon.
DEMO = HERE.parents[2] / "projects" / "demo"


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

    scratch = Path(tempfile.mkdtemp(prefix="aige_golden_"))
    shutil.copytree(DEMO / "world", scratch / "world")
    index = WorldIndex(scratch, default_embedder())

    for name in names or list(PROMPTS):
        prompt = PROMPTS[name]
        context = EditorContext.model_validate({**context_json, "focus": prompt.get("focus")})
        recorder = Recorder(provider)
        canon = index.canon_for(" ".join(filter(None, [prompt["instruction"], prompt.get("focus")])))
        result = propose(recorder, schema, context, prompt["instruction"], canon=canon)
        record = {
            "name": name,
            "instruction": prompt["instruction"],
            "focus": prompt.get("focus"),
            "model": result.model,
            "recorded": date.today().isoformat(),
            "attempts": result.attempts,
            "valid": result.proposal is not None and not result.errors,
            "canon": canon,
            "responses": recorder.responses,
        }
        (HERE / f"{name}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        status = "ok" if record["valid"] else f"INVALID: {result.errors}"
        print(f"{name}: {status} ({result.attempts} attempt(s), {result.usage.inputTokens}+{result.usage.outputTokens} tok)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
