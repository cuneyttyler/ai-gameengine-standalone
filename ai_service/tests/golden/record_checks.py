"""Records the contradiction checker's answers (development_plan.md 3.3) on fixed
pieces of writing against the demo World Bible, so tests can replay them:

    cd ai_service
    uv run python -m tests.golden.record_checks          # all
    uv run python -m tests.golden.record_checks aren_veteran

Some pieces contradict the canon on purpose and must be flagged against the
right entry; others are consistent and must not be (false positives cost the
developer's trust as much as misses do).
"""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
from datetime import date
from pathlib import Path

from app.consistency import SYSTEM, writing_from_operations
from app.main import default_checker, default_embedder
from app.world import WorldIndex, format_entry

HERE = Path(__file__).parent
OUT = HERE / "checks"
DEMO = HERE.parents[2] / "projects" / "demo"


def dialogue(id_: str, speaker: str, *lines: str) -> dict:
    nodes = [{"id": f"n{i}", "text": t, "next": f"n{i + 1}" if i + 1 < len(lines) else ""} for i, t in enumerate(lines)]
    return {"op": "write_dialogue", "id": id_, "dialogue": {"speaker": speaker, "start": "n0", "nodes": nodes}}


def recorded_operations(name: str) -> list[dict]:
    record = json.loads((HERE / f"{name}.json").read_text(encoding="utf-8"))
    return json.loads(record["responses"][-1])["operations"]


CHECKS: dict[str, dict] = {
    # Contradictions: must be flagged against `expect`.
    "tamsin_blacksmith": {
        "operations": [dialogue("tamsin-smith", "character/villager",
                                "Forty years I've been the village blacksmith. Iron's all I know, not herbs.")],
        "expect": "character/villager",
    },
    "aren_veteran": {
        "operations": [dialogue("aren-boast", "character/hero",
                                "Thirty years I've served the Wardens. I've cleared a hundred keeps older than this one.")],
        "expect": "character/hero",
    },
    "wardens_rich": {
        "operations": [{"op": "write_quest", "id": "gold", "quest": {
            "name": "The Wardens' gold",
            "description": "The Wardens, the richest and most numerous order in the realm, reward every Warden "
                           "with a chest of gold for each keep cleared.",
            "giver": "character/villager",
            "objectives": [{"id": "a", "text": "Collect your gold", "type": "talk", "target": "character/villager"}]}}],
        "expect": "faction/wardens",
    },
    # Consistent: must not be flagged as contradictions.
    "consistent_wardens": {"operations": "tamsin_wardens", "expect": None},
    "consistent_new_character": {"operations": "quartermaster", "expect": None},
}


def main(names: list[str]) -> int:
    checker, embedder = default_checker(), default_embedder()
    if checker is None:
        print("No provider configured (MISTRAL_API_KEY in ai_service/.env)")
        return 1
    scratch = Path(tempfile.mkdtemp(prefix="aige_checks_"))
    shutil.copytree(DEMO / "world", scratch / "world")
    index = WorldIndex(scratch, embedder)
    OUT.mkdir(exist_ok=True)

    for name in names or list(CHECKS):
        spec = CHECKS[name]
        operations = spec["operations"]
        if isinstance(operations, str):
            operations = recorded_operations(operations)
        writing = writing_from_operations(operations)
        entries = index.canon_entries(writing, k=8, max_chars=10000)
        canon = "\n\n".join(format_entry(e) for e in entries)
        messages = [{"role": "system", "content": SYSTEM},
                    {"role": "user", "content": f"CANON\n{canon}\n\nNEW WRITING\n{writing}"}]
        canon_by_key = {e.key: format_entry(e) for e in entries}
        completion = checker.complete(messages)
        record = {
            "name": name,
            "expect": spec["expect"],
            "model": completion.model,
            "recorded": date.today().isoformat(),
            "operations": operations,
            "canon": canon_by_key,
            "writing": writing,
            "response": completion.text,
        }
        (OUT / f"{name}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {completion.text[:200]!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
