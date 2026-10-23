"""An answer put into the shape its step asked for, before it's checked or sent to the engine.

Models (small ones especially) give a list where one text was asked for, an object where a
list was, a number as text, null for an object. Each step's *_SHAPE is an example of its answer;
`coerce` walks the answer beside it and converts what's of the wrong kind, keeping what it
doesn't know (extra keys) as it is. The engine then reads every field as the kind it expects.
"""

from __future__ import annotations

import json
from typing import Any

# Objects that may be null on purpose ("no quest stage here"): left null, not made {}.
NULLABLE = {"questStage"}
# Which key of an object a bare text most likely is ("tall and grey" for appearance).
TEXT_KEYS = ("description", "text", "name", "summary", "line", "id", "with", "template", "value")


def parse_shape(shape: str) -> Any:
    try:
        return json.loads(shape)
    except (TypeError, ValueError):
        return None


def _text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if isinstance(value, bool):
        return "yes" if value else "no"
    if isinstance(value, (int, float)):
        return str(int(value)) if float(value).is_integer() else str(value)
    if isinstance(value, list):
        return ", ".join(t for t in (_text(v) for v in value) if t)
    if isinstance(value, dict):
        for key in TEXT_KEYS:
            if isinstance(value.get(key), str) and value[key]:
                return value[key]
        return "; ".join(f"{k}: {_text(v)}" for k, v in value.items() if _text(v))
    return str(value)


def _number(value: Any, integer: bool) -> int | float:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return int(value) if integer and float(value).is_integer() else value
    if isinstance(value, str):
        try:
            n = float(value.strip().rstrip("%"))
            return int(n) if integer and n.is_integer() else n
        except ValueError:
            return 0
    if isinstance(value, list) and value:
        return _number(value[0], integer)
    return 0


def coerce(value: Any, example: Any, key: str = "") -> Any:
    if example is None:
        return value
    if isinstance(example, bool):
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, float)):
            return value != 0
        if isinstance(value, str):
            return value.strip().lower() in ("true", "yes", "1", "y")
        return False
    if isinstance(example, (int, float)):
        return _number(value, isinstance(example, int))
    if isinstance(example, str):
        return _text(value)
    if isinstance(example, list):
        if value is None:
            return []
        if not example:  # anything goes in it, but it's a list
            return value if isinstance(value, list) else [value]
        kinds = {type(x) for x in example}
        if len(example) > 1 and len(kinds) > 1:  # a tuple: ["brave", 0.6]
            items = value if isinstance(value, list) else [value]
            return [coerce(items[i] if i < len(items) else None, ex) for i, ex in enumerate(example)]
        item = example[0]
        items = value if isinstance(value, list) else [value]
        out = []
        for v in items:
            if v is None:
                continue
            if isinstance(item, dict) and not isinstance(v, dict):
                first = next((k for k in TEXT_KEYS if k in item), next(iter(item), None))
                v = {first: _text(v)} if first else {}
            out.append(coerce(v, item))
        return out
    if isinstance(example, dict):
        if value is None:
            return None if key in NULLABLE else {}
        if not isinstance(value, dict):
            if key in NULLABLE and not value:
                return None
            first = next((k for k in TEXT_KEYS if isinstance(example.get(k), str)), None) or next(
                (k for k, ex in example.items() if isinstance(ex, str)), None)
            return {first: _text(value)} if first else {}
        out = dict(value)
        for k, ex in example.items():
            if k in value:
                out[k] = coerce(value[k], ex, k)
        return out
    return value
