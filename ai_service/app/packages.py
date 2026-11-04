"""AI packages written by the AI panel (docs/ai/ai-packages.md): each package record and procedure tree checked before
the proposal goes to the user. This covers its template's data, its own procedure's steps, its script and the things
it names. The engine's runtime is engine/gameplay/Behaviour.cpp; the steps here match its Tick."""

from __future__ import annotations

import json
from collections.abc import Callable, Iterable

# Composite steps (children) and decorators (one child).
COMPOSITES = {"sequence", "selector", "stack", "random", "parallel", "loop"}
DECORATORS = {"invert", "condition", "cooldown"}
# Leaf steps and what each needs (one of the keys, when a list).
LEAVES: dict[str, list[str]] = {
    "move-to": ["to"], "wander": [], "wait": [], "idle": [], "face": [], "say": ["text"], "bark": ["text"],
    "use-object": [], "use-part": ["part", "action", "parts"], "act": ["action"], "play": ["clip"], "talk-to-player": [],
    "follow": [], "flee": [], "patrol": ["points"], "ambush": [], "escort": [], "set": [], "emit": ["event"],
    "script": ["script"], "follow-path": ["path"],
}
STEPS = COMPOSITES | DECORATORS | set(LEAVES)

# Templates whose data must name something.
NEEDS_DATA = {"use-part": "actions", "live": "actions", "tend": "action", "script": "script",
              "approach-player": "dialogue", "follow-path": "route"}
PATH_ENDS = {"stop", "loop", "back"}
PATH_MODES = {"once", "loop", "wander"}
NEEDS_TARGET = {"follow", "escort", "flee"}


def _tree(value) -> tuple[dict | None, str]:
    if isinstance(value, dict):
        return value, ""
    if isinstance(value, str) and value.strip():
        try:
            parsed = json.loads(value)
        except json.JSONDecodeError as error:
            return None, f"its procedure isn't JSON ({error.msg} at {error.pos})"
        return (parsed, "") if isinstance(parsed, dict) else (None, "its procedure must be one step (an object)")
    return None, ""


def check_procedure(tree: dict, where: str, path: str = "root") -> list[str]:
    """Each step known, composites with children, decorators with one, leaves with what they need."""
    errors: list[str] = []
    kind = tree.get("type")
    if not isinstance(kind, str) or kind not in STEPS:
        return [f"{where}: step {path} has type {kind!r}; steps: {', '.join(sorted(STEPS))}"]
    children = tree.get("children", [])
    if not isinstance(children, list):
        return [f"{where}: step {path} ({kind}): children must be a list"]
    if kind in COMPOSITES and not children:
        errors.append(f"{where}: step {path} ({kind}) needs children")
    if kind in DECORATORS and len(children) != 1:
        errors.append(f"{where}: step {path} ({kind}) takes exactly one child")
    if kind in LEAVES and children:
        errors.append(f"{where}: step {path} ({kind}) has no children")
    needs = LEAVES.get(kind, [])
    if needs and not any(tree.get(key) not in (None, "", []) for key in needs):
        errors.append(f"{where}: step {path} ({kind}) needs {' or '.join(repr(k) for k in needs)}")
    if kind == "random":
        weights = tree.get("weights")
        if weights is not None and (not isinstance(weights, list) or len(weights) != len(children)):
            errors.append(f"{where}: step {path} (random): weights must be a list, one per child")
    if kind == "wait":
        for key in ("seconds", "min", "max"):
            if key in tree and not isinstance(tree[key], (int, float)) and not str(tree[key]).startswith("@data:"):
                errors.append(f"{where}: step {path} (wait): {key} must be a number")
    for i, child in enumerate(children):
        if not isinstance(child, dict):
            errors.append(f"{where}: step {path}.{i} must be an object")
            continue
        errors += check_procedure(child, where, f"{path}.{i}")
    return errors


def _paths_named(tree, out: list[str]) -> None:
    """The paths a procedure names: follow-path's "path", patrol's "points" when it's a name."""
    if not isinstance(tree, dict):
        return
    if tree.get("type") == "follow-path" and isinstance(tree.get("path"), str) and not tree["path"].startswith("@data:"):
        out.append(tree["path"])
    if tree.get("type") == "patrol" and isinstance(tree.get("points"), str) and not tree["points"].startswith("@data:"):
        out.append(tree["points"])
    for child in tree.get("children", []) if isinstance(tree.get("children"), list) else []:
        _paths_named(child, out)


def check_package(package_id: str, record: dict, where: str, scripts: set[str], names: set[str],
                  paths: set[str] | None = None) -> list[str]:
    """A package record beyond its fields: what its template needs, its own procedure, its script, the thing it's at."""
    if not isinstance(record, dict):
        return []
    errors: list[str] = []
    template = record.get("template", "")
    data = record.get("data") if isinstance(record.get("data"), dict) else {}
    tree, problem = _tree(record.get("procedure"))
    if problem:
        errors.append(f"{where}: {problem}")
    if template == "custom" and tree is None and not problem:
        errors.append(f"{where}: template custom needs its \"procedure\" (a step tree as JSON)")
    if tree is not None:
        errors += check_procedure(tree, where)
    need = NEEDS_DATA.get(template)
    if need == "actions" and data.get("action"):
        need = None  # an older package's single action (still read)
    if template == "patrol" and not data.get("path") and not data.get("route"):
        errors.append(f"{where}: template patrol needs data.route (a named path) or data.path (markers)")
    if need and not data.get(need):
        errors.append(f"{where}: template {template} needs data.{need}")
    if template in NEEDS_TARGET and not record.get("target"):
        errors.append(f"{where}: template {template} needs \"target\" (player, a character's name, or a marker)")
    script = data.get("script")
    if template == "script" and isinstance(script, str) and script and script not in scripts:
        errors.append(f"{where}: script {script!r} doesn't exist and isn't written here (script.write it)")
    # The paths it walks (docs/ai/ai-paths.md): in the scene, or added in this answer (scene.addPath, path.around).
    if paths is not None:
        named: list[str] = []
        if template in ("follow-path", "patrol") and isinstance(data.get("route"), str):
            named.append(data["route"])
        if tree is not None:
            _paths_named(tree, named)
        for name in named:
            if name and name not in paths:
                errors.append(f"{where}: path {name!r} isn't in the scene and isn't added here (scene.addPath or path.around)")
        mode = data.get("mode")
        if template == "follow-path" and mode not in (None, "") and mode not in PATH_MODES:
            errors.append(f"{where}: data.mode is once, loop or wander, not {mode!r}")
    location = record.get("location") if isinstance(record.get("location"), dict) else {}
    for key in ("building", "entity"):
        named = location.get(key)
        if isinstance(named, str) and named and names and named not in names:
            errors.append(f"{where}: location.{key} {named!r} isn't the name of anything in the scene")
    return errors


def check_add_path(args: dict, where: str) -> list[str]:
    """scene.addPath's points, links and end, as the engine will read them."""
    errors: list[str] = []
    try:
        points = json.loads(args.get("points") or "[]")
    except (json.JSONDecodeError, TypeError):
        return [f"{where}: points must be a JSON list ([[x, y], ...])"]
    if not isinstance(points, list) or len(points) < 2:
        return [f"{where}: a path needs at least two points"]
    for i, p in enumerate(points):
        ok = (isinstance(p, list) and len(p) >= 2 and all(isinstance(v, (int, float)) for v in p[:2])) or \
             (isinstance(p, dict) and isinstance(p.get("x"), (int, float)) and isinstance(p.get("y"), (int, float)))
        if not ok:
            errors.append(f"{where}: point {i} is [x, y] or {{\"x\", \"y\", ...}}")
    end = args.get("end") or "stop"
    if end not in PATH_ENDS:
        errors.append(f"{where}: end is stop, loop or back, not {end!r}")
    text = args.get("links") or ""
    if text:
        try:
            links = json.loads(text)
        except json.JSONDecodeError:
            return errors + [f"{where}: links must be a JSON list"]
        if not isinstance(links, list):
            return errors + [f"{where}: links must be a JSON list"]
        n = len(points)
        for i, link in enumerate(links):
            if not isinstance(link, dict):
                errors.append(f"{where}: link {i} must be an object")
                continue
            a, b = link.get("from"), link.get("to")
            if not (isinstance(a, int) and isinstance(b, int) and 0 <= a < n and 0 <= b < n) or a == b:
                errors.append(f"{where}: link {i} joins points the path hasn't (0-{n - 1})")
            weight = link.get("weight", 1)
            if not isinstance(weight, (int, float)) or (weight <= 0 and not link.get("default")):
                errors.append(f"{where}: link {i}'s weight must be a number above zero")
            cond = link.get("if")
            if cond not in (None, ""):
                parsed = cond
                if isinstance(cond, str):
                    try:
                        parsed = json.loads(cond)
                    except json.JSONDecodeError:
                        parsed = None
                if not isinstance(parsed, list):
                    errors.append(f"{where}: link {i}'s conditions (if) must be a JSON list")
    return errors


def check_stacks(values: Iterable[tuple[str, str]], exists: Callable[[str], bool]) -> list[str]:
    """Behaviour.packages lists (where, "a, b"): each package exists or is written here."""
    errors = []
    for where, text in values:
        for name in [p.strip() for p in str(text or "").split(",") if p.strip()]:
            if not exists(name):
                errors.append(f"{where}: Behaviour.packages names package {name!r}, which doesn't exist and isn't written here")
    return errors
