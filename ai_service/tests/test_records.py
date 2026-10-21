"""write_record / delete_record checks (app/records.py) and the prompt's record
types (development_plan.md 12.1)."""

from app.models import EditorContext, Proposal
from app.prompt import describe_schema
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def context_with(records) -> EditorContext:
    return EditorContext.model_validate({"records": records})


def test_the_schema_lists_record_types(schema):
    item = schema.record_type("item")
    assert item is not None
    assert item.field("name").required
    assert item.field("tags").list
    text = describe_schema(schema)
    assert "RECORD TYPES" in text
    assert "tags: list of string" in text


def test_a_valid_item_and_one_inheriting_from_it_pass(schema):
    p = proposal(
        {"op": "write_record", "type": "item", "id": "potion", "record": {"name": "Potion", "kind": "consumable"}},
        {"op": "write_record", "type": "item", "id": "big-potion", "record": {"base": "potion", "value": 20}},
    )
    assert validate_proposal(p, schema, context_with([])) == []


def test_record_problems_name_the_field(schema):
    p = proposal(
        {"op": "write_record", "type": "item", "id": "junk",
         "record": {"kind": "spoon", "stack": 0, "colour": "red", "tags": "blade"}},
        {"op": "write_record", "type": "item", "id": "Bad Id", "record": {"name": "x"}},
        {"op": "write_record", "type": "spell", "id": "fire", "record": {}},
        {"op": "write_record", "type": "item", "id": "orphan", "record": {"base": "ghost"}},
        {"op": "delete_record", "type": "item", "id": "nothing"},
    )
    errors = validate_proposal(p, schema, context_with([]))
    text = "\n".join(errors)
    assert "kind: 'spoon' isn't one of" in text
    assert "stack: 0 is below the minimum 1" in text
    assert "colour: unknown field" in text
    assert "tags: expected a list" in text
    assert "name: required" in text
    assert "'Bad Id' isn't a valid record id" in text
    assert "unknown record type 'spell'" in text
    assert "base: no item 'ghost'" in text
    assert "there is no item 'nothing' to delete" in text


def test_existing_records_count_for_bases_and_deletes(schema):
    ctx = context_with([{"type": "item", "id": "sword", "record": {"name": "Sword", "value": 10}}])
    p = proposal(
        {"op": "write_record", "type": "item", "id": "fine-sword", "record": {"base": "sword", "value": 30}},
        {"op": "delete_record", "type": "item", "id": "sword"},
    )
    assert validate_proposal(p, schema, ctx) == []
