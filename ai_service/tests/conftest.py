import json
from pathlib import Path

import pytest

from app.models import EditorContext, EngineSchema

FIXTURES = Path(__file__).parent / "fixtures"


@pytest.fixture(scope="session")
def schema_json() -> dict:
    # Written by the C++ test SchemaContractTest from the live registry.
    return json.loads((FIXTURES / "engine_schema.json").read_text(encoding="utf-8"))


@pytest.fixture(scope="session")
def schema(schema_json) -> EngineSchema:
    return EngineSchema.model_validate(schema_json)


@pytest.fixture
def context() -> EditorContext:
    """A small slice of the demo project, as the editor would describe it."""
    return EditorContext.model_validate(
        {
            "entities": [
                {
                    "id": 3,
                    "name": "Hero",
                    "kind": "Player",
                    "prefab": "prefabs/hero.prefab",
                    "components": {
                        "Health": {"current": 20, "max": 20},
                        "MeleeAttack": {"damage": 3, "effect": "None", "effectDuration": 3, "effectMagnitude": 1},
                        "Faction": {"team": "Player"},
                    },
                },
                {
                    "id": 7,
                    "name": "Skeleton",
                    "kind": "Enemy",
                    "prefab": "prefabs/skeleton.prefab",
                    "components": {
                        "Health": {"current": 8, "max": 8},
                        "Follow": {"target": None, "speed": 1.6, "stopDistance": 0.8, "range": 5},
                        "LuaScript": {"path": "scripts/enemy_melee.lua"},
                    },
                    "scriptProperties": {"swing_range": 1.1},
                },
            ],
            "prefabs": [
                {"path": "prefabs/skeleton.prefab", "name": "skeleton", "components": {"Health": {"max": 8}}},
                {"path": "prefabs/grass.prefab", "name": "grass", "isTile": True, "components": {"Tile": {}}},
            ],
            "assets": [
                {"path": "assets/textures/hero.png", "type": "texture"},
                {"path": "assets/audio/hit.wav", "type": "audio"},
            ],
            "scripts": ["scripts/enemy_melee.lua"],
            "settings": {"Combat": {"knockbackScale": 1}},
            "selection": 7,
        }
    )


@pytest.fixture
def demo_context() -> EditorContext:
    """The whole demo project as the editor describes it (written by the C++
    test SchemaContractTest.DemoContextSnapshotIsCurrent)."""
    return EditorContext.model_validate_json((FIXTURES / "demo_context.json").read_text(encoding="utf-8"))
