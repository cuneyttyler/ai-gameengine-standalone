"""The service's models match what the engine exports (the C++ side checks the
snapshot matches its live registry; together they pin the contract)."""

from app.models import EngineSchema, FieldSpec
from app.prompt import describe_schema


def test_snapshot_parses_into_the_service_models(schema_json):
    schema = EngineSchema.model_validate(schema_json)
    assert schema.version == 1
    # Round-trips without losing anything the engine sent.
    assert schema.model_dump(exclude_none=True) == EngineSchema.model_validate(
        schema.model_dump(exclude_none=True)
    ).model_dump(exclude_none=True)


def test_engine_exposes_the_building_blocks_the_prompt_relies_on(schema):
    for name in ["Health", "MeleeAttack", "Follow", "LuaScript", "Sprite", "Faction"]:
        assert schema.component(name) is not None, name
    assert schema.component("MeleeAttack").field("effect").options[:2] == ["None", "Burn"]
    for name in ["scene.spawn", "scene.paintRect", "entity.delete", "prefab.createFromEntity", "script.write"]:
        assert schema.command(name) is not None, name
    for name in ["Renderer", "Camera", "Combat", "Physics"]:
        assert schema.settings_section(name) is not None, name


def test_bookkeeping_components_are_not_exported(schema):
    assert schema.component("PrefabInstance") is None


def test_every_field_is_documented_and_well_formed(schema):
    def check(spec: FieldSpec, where: str):
        assert spec.description, where
        if spec.type == "enum":
            assert spec.options, where
        if spec.min is not None and spec.max is not None:
            assert spec.min <= spec.max, where

    for component in schema.components:
        assert component.description, component.name
        for field in component.fields:
            check(field, f"{component.name}.{field.name}")
    for section in schema.settings:
        for field in section.fields:
            check(field, f"{section.name}.{field.name}")
    for command in schema.commands:
        for param in command.params:
            check(param, f"{command.name}.{param.name}")


def test_compact_description_mentions_every_component_and_command(schema):
    text = describe_schema(schema)
    for component in schema.components:
        assert f"- {component.name}" in text
    for command in schema.commands:
        assert command.name in text
    assert "one of None|Burn|Poison|Slow|Regen" in text
