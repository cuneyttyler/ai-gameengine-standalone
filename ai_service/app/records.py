"""Checks for write_record / delete_record operations (development_plan.md 12.1),
mirroring engine/records (ValidateRecord), so the model gets the same feedback
the editor would give before anything reaches the review panel.

A record is records/<type>/<id>.json: its type's fields, plus optionally
"base" (another record of the same type it inherits from) and "provenance".
"""

from __future__ import annotations

import math
import re
from typing import Any, Callable

from .models import RecordFieldSpec, RecordTypeSpec

RECORD_ID = re.compile(r"^[a-z0-9_-]{1,96}$")
RESERVED = {"base", "provenance"}
_TEXT_TYPES = {"string", "texture", "script", "audio", "prefab"}

Exists = Callable[[str, str], bool]


def _describe(value: Any) -> str:
    text = repr(value)
    return text if len(text) <= 40 else text[:37] + "..."


def _check_single(field: RecordFieldSpec, value: Any, where: str, exists: Exists | None, out: list[str]) -> None:
    if field.type == "record":
        if not isinstance(value, str):
            out.append(f"{where}: expected the id of a {field.recordType}, got {_describe(value)}")
        elif value:
            if not RECORD_ID.match(value):
                out.append(f"{where}: {value!r} isn't a valid record id")
            elif exists is not None and not exists(field.recordType or "", value):
                out.append(f"{where}: no {field.recordType} {value!r} (write it with write_record first)")
        return
    if field.type == "object":
        if not isinstance(value, dict):
            out.append(f"{where}: expected an object, got {_describe(value)}")
            return
        _check_fields(field.fields, value, where, exists, out, allow_reserved=False)
        for sub in field.fields:
            if sub.required and sub.name not in value:
                out.append(f"{where}.{sub.name}: required")
        return
    if field.type in ("float", "int"):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            out.append(f"{where}: expected a number, got {_describe(value)}")
            return
        if field.type == "int" and not float(value).is_integer():
            out.append(f"{where}: expected a whole number, got {_describe(value)}")
            return
        if not math.isfinite(float(value)):
            out.append(f"{where}: expected a finite number")
        elif field.min is not None and value < field.min:
            out.append(f"{where}: {value:g} is below the minimum {field.min:g}")
        elif field.max is not None and value > field.max:
            out.append(f"{where}: {value:g} is above the maximum {field.max:g}")
        return
    if field.type == "bool":
        if not isinstance(value, bool):
            out.append(f"{where}: expected true or false, got {_describe(value)}")
        return
    if field.type == "enum":
        if value not in (field.options or []):
            out.append(f"{where}: {_describe(value)} isn't one of {', '.join(field.options or [])}")
        return
    if field.type in _TEXT_TYPES and not isinstance(value, str):
        out.append(f"{where}: expected text, got {_describe(value)}")


def _check_value(field: RecordFieldSpec, value: Any, where: str, exists: Exists | None, out: list[str]) -> None:
    if field.list:
        if not isinstance(value, list):
            out.append(f"{where}: expected a list, got {_describe(value)}")
            return
        for i, item in enumerate(value):
            _check_single(field, item, f"{where}[{i}]", exists, out)
        return
    _check_single(field, value, where, exists, out)


def _check_fields(fields: list[RecordFieldSpec], data: dict, where: str, exists: Exists | None, out: list[str],
                  allow_reserved: bool) -> None:
    by_name = {f.name: f for f in fields}
    for key, value in data.items():
        if allow_reserved and key in RESERVED:
            continue
        path = f"{where}.{key}" if where else key
        spec = by_name.get(key)
        if spec is None:
            out.append(f"{path}: unknown field")
            continue
        _check_value(spec, value, path, exists, out)


def check_record(record_type: RecordTypeSpec, record_id: str, data: Any, exists: Exists | None,
                 base: dict | None, where: str) -> list[str]:
    """Problems with a record about to be written. `base` is its base record's
    resolved fields (required fields may come from it)."""
    errors: list[str] = []
    if not RECORD_ID.match(record_id):
        return [f"{where}: {record_id!r} isn't a valid record id (a-z, 0-9, - and _)"]
    if not isinstance(data, dict):
        return [f"{where}: a record is a JSON object"]
    if "base" in data:
        if not isinstance(data["base"], str) or not RECORD_ID.match(data["base"]):
            errors.append(f"{where}: base: expected the id of another {record_type.name}")
        elif data["base"] == record_id:
            errors.append(f"{where}: base: a record can't be its own base")
        elif exists is not None and not exists(record_type.name, data["base"]):
            errors.append(f"{where}: base: no {record_type.name} {data['base']!r}")
    found: list[str] = []
    _check_fields(record_type.fields, data, "", exists, found, allow_reserved=True)
    complete = dict(base or {})
    complete.update({k: v for k, v in data.items() if k not in RESERVED})
    for spec in record_type.fields:
        if spec.required and (spec.name not in complete or complete[spec.name] == ""):
            found.append(f"{spec.name}: required")
    errors += [f"{where}: {message}" for message in found]
    return errors
