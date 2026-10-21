"""Checks for write_ui operations (development_plan.md 17, docs/game-ui.md), mirroring
engine/ui/Documents CheckUiFile: where a UI file may go, JSON with the expected shape
(theme, screens manifest, notification rules), and a document's data models."""

from __future__ import annotations

import json
import re
from typing import Any

MODELS = {"menu", "dialogue", "hud", "notes", "ui"}
LAYERS = {"screen", "window", "hud", "popup"}
CHANNELS = {"toast", "feed", "banner", "alert"}


def check_ui(path: str, content: Any, themes: list[str], where: str) -> list[str]:
    errors: list[str] = []
    if not isinstance(path, str) or not path.startswith("ui/") or ".." in path or path.startswith("/"):
        return [f"{where}: UI files live under ui/"]
    if not isinstance(content, str):
        return [f"{where}: content must be the file's text"]
    if path.endswith(".json"):
        try:
            data = json.loads(content)
        except ValueError:
            return [f"{where}: {path} isn't valid JSON"]
        if not isinstance(data, dict):
            return [f"{where}: {path} must be a JSON object"]
        if path == "ui/theme.json":
            base = data.get("base", "stone")
            if themes and base not in themes:
                errors.append(f"{where}: no built-in theme {base!r} (themes: {', '.join(themes)})")
            if "tokens" in data and not isinstance(data["tokens"], dict):
                errors.append(f"{where}: tokens must be an object of name -> value")
        elif path == "ui/screens.json":
            screens = data.get("screens")
            if not isinstance(screens, list):
                errors.append(f"{where}: needs a \"screens\" list")
            for s in screens or []:
                if not isinstance(s, dict) or not s.get("id"):
                    errors.append(f"{where}: every screen needs an id")
                elif s.get("layer", "screen") not in LAYERS:
                    errors.append(f"{where}: screen {s['id']!r}: layer is screen, window, hud or popup")
        elif path == "ui/notifications.json":
            rules = data.get("rules")
            if not isinstance(rules, list):
                errors.append(f"{where}: needs a \"rules\" list")
            for r in rules or []:
                if not isinstance(r, dict) or not r.get("event"):
                    errors.append(f"{where}: every rule needs an event")
                elif r.get("channel", "toast") not in CHANNELS:
                    errors.append(f"{where}: rule for {r['event']!r}: channel is toast, feed, banner or alert")
    elif path.endswith(".rml"):
        if "<rml>" not in content or "</rml>" not in content:
            errors.append(f"{where}: {path} must be an <rml> document")
        for match in re.finditer(r'data-model="([^"]*)"', content):
            if match.group(1) not in MODELS:
                errors.append(f"{where}: no data model {match.group(1)!r} (menu, dialogue, hud, notes, ui)")
            tag_start = content.rfind("<", 0, match.start())
            if content.startswith("<body", tag_start):
                errors.append(f"{where}: put data-model on an element inside <body>, not on <body> itself")
    elif not path.endswith(".rcss"):
        errors.append(f"{where}: UI files are .rml, .rcss or .json")
    return errors
