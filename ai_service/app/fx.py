"""Checks for animation and effects (development_plan.md 18, docs/effects-animation.md):
write_anim sheets (mirroring engine/gameplay/Animation ValidateAnimSheet), and the
particles, effects and sheets that components name."""

from __future__ import annotations

from typing import Any

DIRECTIONS = {"s", "se", "e", "ne", "n", "nw", "w", "sw"}
LOOPS = {"loop", "once", "pingpong", "hold"}


def check_anim(path: str, sheet: Any, pictures: set[str], where: str) -> list[str]:
    errors: list[str] = []
    if not isinstance(path, str) or not path.startswith("assets/") or not path.endswith(".anim.json") or ".." in path:
        errors.append(f"{where}: sheets are assets/<name>.anim.json")
    if not isinstance(sheet, dict) or not isinstance(sheet.get("clips"), dict) or not sheet["clips"]:
        return errors + [f"{where}: needs a \"clips\" object"]
    texture = sheet.get("texture", "")
    if not texture:
        errors.append(f"{where}: needs a \"texture\" (a picture from context assets)")
    elif pictures and texture not in pictures:
        errors.append(f"{where}: no picture {texture!r} in the project")
    grid = sheet.get("grid") or {}
    size = sheet.get("size")
    uses_indices = False
    for name, clip in sheet["clips"].items():
        at = f"{where} clip {name!r}"
        if not isinstance(clip, dict):
            errors.append(f"{at} must be an object")
            continue
        if clip.get("loop", "loop") not in LOOPS:
            errors.append(f"{at}: loop is loop, once, pingpong or hold")
        lists = []
        if isinstance(clip.get("directions"), dict):
            for direction, frames in clip["directions"].items():
                if direction not in DIRECTIONS:
                    errors.append(f"{at}: no direction {direction!r} (s, se, e, ne, n, nw, w, sw)")
                if isinstance(frames, str):
                    target = frames[7:] if frames.startswith("mirror:") else None
                    if target not in clip["directions"] or isinstance(clip["directions"].get(target), str):
                        errors.append(f"{at}: {direction} mirrors {target!r}, which it hasn't")
                else:
                    lists.append(frames)
        else:
            lists.append(clip.get("frames"))
        if not any(isinstance(f, list) and f for f in lists):
            errors.append(f"{at} has no frames")
        for frames in lists:
            for f in frames or []:
                uses_indices = uses_indices or isinstance(f, int)
    if uses_indices and (not grid.get("width") or not grid.get("height") or not isinstance(size, list)):
        errors.append(f"{where}: frames given as grid indices need \"grid\" {{width, height}} and \"size\" [w, h]")
    return errors


def check_fx_refs(proposal, context) -> list[str]:
    """ParticleEmitter.particles, EffectCues and Animator.sheet must name things that exist
    (presets, the project's records and sheets, or ones written in the same proposal)."""
    from .models import AddComponent, SetField, SetPrefabField, WriteAnim, WriteRecord

    fx = context.fx or {}
    if not fx:
        return []
    particles = set(fx.get("particles", []))
    effects = set(fx.get("effects", []))
    sheets = {s.get("path") for s in fx.get("sheets", [])}
    animators = set(fx.get("animators", []))
    for op in proposal.operations:
        if isinstance(op, WriteRecord):
            {"particles": particles, "effect": effects, "animator": animators}.get(op.type, set()).add(op.id)
        elif isinstance(op, WriteAnim):
            sheets.add(op.path)
    animators |= {r.get("id") for r in context.records if r.get("type") == "animator"}
    errors: list[str] = []
    checks = {("ParticleEmitter", "particles"): (particles, "particles"),
              ("EffectCues", "onHit"): (effects, "effect"), ("EffectCues", "onDeath"): (effects, "effect"),
              ("EffectCues", "onAttack"): (effects, "effect"), ("Animator", "sheet"): (sheets, "sheet"),
              ("Animator", "animator"): (animators, "animator")}
    for index, op in enumerate(proposal.operations, start=1):
        values = []
        if isinstance(op, AddComponent):
            values = [((op.component, k), v) for k, v in op.fields.items()]
        elif isinstance(op, (SetField, SetPrefabField)):
            values = [((op.component, op.field), op.value)]
        for key, value in values:
            if key in checks and isinstance(value, str) and value:
                known, what = checks[key]
                if value not in known:
                    errors.append(f"operation {index} ({op.op} {key[0]}.{key[1]}): no {what} {value!r}"
                                  + (" (write it, or pick one from context fx)" if what != "sheet" else
                                     " (write it with write_anim)"))
    return errors
