"""Checks for shipping features (development_plan.md 24): achievement records the AI writes
(mirroring engine/gameplay/Achievements ValidateAchievement) and the shipping settings."""

from __future__ import annotations

KINDS = {"quest", "flag", "event", "script"}
QUEST_STATES = {"active", "completed", "failed"}


def check_shipping(proposal, context) -> list[str]:
    from .models import SetSetting, WriteRecord

    errors: list[str] = []
    quests = {q.get("id") for q in getattr(context, "quests", []) or [] if isinstance(q, dict)}
    for index, op in enumerate(proposal.operations, start=1):
        where = f"operation {index} ({op.op})"
        if isinstance(op, WriteRecord) and op.type == "achievement":
            r = op.record
            kind = r.get("kind", "event")
            if not str(r.get("name", "")).strip():
                errors.append(f"{where} achievement {op.id}: it needs a name")
            if kind not in KINDS:
                errors.append(f"{where} achievement {op.id}: kind is quest, flag, event or script")
            if kind == "quest" and not r.get("quest"):
                errors.append(f"{where} achievement {op.id}: a quest achievement names its quest")
            if kind == "quest" and r.get("state", "completed") not in QUEST_STATES:
                errors.append(f"{where} achievement {op.id}: state is active, completed or failed")
            if kind == "quest" and quests and r.get("quest") and r.get("quest") not in quests:
                errors.append(f"{where} achievement {op.id}: no quest {r.get('quest')!r}")
            if kind == "flag" and not r.get("flag"):
                errors.append(f"{where} achievement {op.id}: a flag achievement names its flag")
            if kind == "event" and not r.get("event"):
                errors.append(f"{where} achievement {op.id}: an event achievement names its event (death, pickup, "
                              "trigger_enter, talk, quest_completed...)")
        if isinstance(op, SetSetting) and op.section == "Streaming" and op.field == "chunk":
            if isinstance(op.value, (int, float)) and not 8 <= op.value <= 256:
                errors.append(f"{where}: Streaming chunk is 8 to 256 cells")
    return errors
