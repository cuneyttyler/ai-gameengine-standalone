"""write_ui operations (development_plan.md 17): the game's UI files the AI may write."""

from app.models import Proposal
from app.validation import validate_proposal


def proposal(*operations) -> Proposal:
    return Proposal.model_validate({"summary": "test", "operations": list(operations)})


def ui_context(demo_context):
    return demo_context.model_copy(update={"ui": {"themes": ["stone", "parchment"], "theme": {}, "files": []}})


def test_a_theme_rules_and_a_screen_validate(schema, demo_context):
    context = ui_context(demo_context)
    screen = """<rml><head><link type="text/rcss" href="../styles/base.rcss"/></head>
<body><div data-model="menu"><h1>{{title}}</h1>
<div class="item" data-for="item, i : items" data-event-click="choose(i)">{{item.label}}</div></div></body></rml>"""
    p = proposal(
        {"op": "write_ui", "path": "ui/theme.json", "content": '{"base": "parchment", "tokens": {"panel": "#e8d9b5"}}'},
        {"op": "write_ui", "path": "ui/notifications.json",
         "content": '{"rules": [{"event": "level_up", "channel": "banner", "text": "Level {amount}"}]}'},
        {"op": "write_ui", "path": "ui/screens/pause.rml", "content": screen},
        {"op": "write_ui", "path": "ui/screens.json",
         "content": '{"screens": [{"id": "pause", "document": "screens/pause.rml"}]}'},
        {"op": "write_ui", "path": "ui/styles/extra.rcss", "content": ".panel { color: var(--text); }"},
    )
    assert validate_proposal(p, schema, context) == []


def test_ui_problems_are_reported(schema, demo_context):
    context = ui_context(demo_context)
    p = proposal(
        {"op": "write_ui", "path": "scripts/hack.lua", "content": "x"},
        {"op": "write_ui", "path": "ui/theme.json", "content": '{"base": "neon"}'},
        {"op": "write_ui", "path": "ui/notifications.json", "content": '{"rules": [{"event": "x", "channel": "popup"}]}'},
        {"op": "write_ui", "path": "ui/screens/a.rml", "content": '<rml><body data-model="player"></body></rml>'},
        {"op": "write_ui", "path": "ui/screens.json", "content": "{not json"},
    )
    text = "\n".join(validate_proposal(p, schema, context))
    for expected in ["UI files live under ui/", "no built-in theme 'neon'", "channel is toast, feed, banner or alert",
                     "no data model 'player'", "not on <body> itself", "isn't valid JSON"]:
        assert expected in text, (expected, text)
