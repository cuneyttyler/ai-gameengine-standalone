"""The editor's knowledge base (app/help.py): its pieces, search, and questions answered from it in /propose."""
import json

from app.help import HelpIndex, looks_like_question, markdown_pieces, schema_pieces
from app.models import EditorContext
from app.providers import Completion
from app.service import propose


def write_help(folder):
    (folder / "shell").mkdir(parents=True)
    (folder / "README.md").write_text("# Not indexed\n\n## Format\nrules", encoding="utf-8")
    (folder / "shell" / "menus.md").write_text(
        "# Menus\n\nThe menu bar.\n\n## File menu\nNew Project, Open, Save (Ctrl+S) and Save As.\n\n"
        "## Edit menu\nUndo (Ctrl+Z) and redo.\n", encoding="utf-8")
    (folder / "shell" / "art.md").write_text(
        "# Art model\n\n## Changing the art model\nPreferences ▸ AI ▸ Art picks who draws the art; a project's own "
        "(Project Settings ▸ AI ▸ art) wins.\n", encoding="utf-8")


def test_each_section_is_a_piece_with_its_file_title(tmp_path):
    write_help(tmp_path)
    pieces = markdown_pieces(tmp_path)
    titles = [p.title for p in pieces]
    assert "Menus › File menu" in titles and "Menus › Edit menu" in titles and "Art model › Changing the art model" in titles
    assert not any("Not indexed" in t or "Format" in t for t in titles)  # the README is the format, not help
    file_menu = next(p for p in pieces if p.title == "Menus › File menu")
    assert file_menu.id == "shell/menus.md#file-menu" and file_menu.text.startswith("Menus › File menu\n")


def test_components_records_and_settings_come_from_the_schema(schema):
    pieces = schema_pieces(schema)
    ids = {p.id for p in pieces}
    assert any(i.startswith("component/") for i in ids)
    sprite = next((p for p in pieces if p.id == "component/Sprite"), None)
    if sprite is not None:
        assert "Fields:" in sprite.text and "texture" in sprite.text


def test_keyword_search_finds_the_section_asked_about(tmp_path):
    write_help(tmp_path)
    index = HelpIndex(None, folder=tmp_path, cache=tmp_path / "cache.sqlite")
    hits, method = index.search("how do I change the art model?", 2)
    assert method == "keywords"
    assert hits[0][0].title == "Art model › Changing the art model"
    hits, _ = index.search("undo", 1)
    assert hits[0][0].title == "Menus › Edit menu"
    text = index.help_for("save the project")
    assert "Ctrl+S" in text and "[shell/menus.md#file-menu]" in text


class Embedder:
    """Two dimensions: the art pieces one way, everything else the other."""
    name = "fake"

    def __init__(self):
        self.calls = 0

    def embed(self, texts):
        self.calls += 1
        return [[1.0, 0.0] if ("art" in t.lower() or "draw" in t.lower()) else [0.0, 1.0] for t in texts]


def test_embeddings_are_cached_and_blended(tmp_path):
    write_help(tmp_path)
    embedder = Embedder()
    index = HelpIndex(embedder, folder=tmp_path, cache=tmp_path / "cache.sqlite")
    hits, method = index.search("who draws the pictures", 1)
    assert method == "embeddings+keywords" and hits[0][0].title.startswith("Art model")
    again = HelpIndex(embedder, folder=tmp_path, cache=tmp_path / "cache.sqlite")
    before = embedder.calls
    again.search("who draws the pictures", 1)
    assert embedder.calls == before + 1  # only the query: the pieces' vectors came from the cache


def test_questions_are_told_from_changes():
    assert looks_like_question("how do I add a door?")
    assert looks_like_question("Where is the art model setting")
    assert looks_like_question("is there a way to undo an AI change?")
    assert not looks_like_question("make the smith walk to the forge")
    assert not looks_like_question("Animate all characters with walk")


class Writer:
    """Asks the knowledge base first, then answers from what came back."""

    def __init__(self):
        self.asked = []

    def complete(self, messages):
        self.asked.append(messages)
        if len(self.asked) == 1:
            return Completion(json.dumps({"scope": "tools", "summary": "look it up",
                                          "tools": [{"tool": "search_help", "args": {"query": "art model"}}]}))
        return Completion(json.dumps({"scope": "answer", "summary": "the art model",
                                      "answer": "Open Preferences ▸ AI ▸ Art."}))


def test_search_help_is_answered_in_the_service_and_the_answer_comes_back(schema):
    writer = Writer()
    looked = []
    result = propose(writer, schema, EditorContext(), "where do I change the art model?", help="",
                     search_help=lambda q: looked.append(q) or "[shell/art.md] Preferences ▸ AI ▸ Art")
    assert looked == ["art model"]
    assert result.proposal is not None and result.proposal.scope == "answer"
    assert result.proposal.answer == "Open Preferences ▸ AI ▸ Art."
    assert result.attempts == 1  # the lookup isn't counted as an attempt
    assert "KNOWLEDGE BASE RESULTS" in writer.asked[1][-1]["content"]


def test_the_help_given_with_a_question_is_in_the_request(schema):
    writer = Writer()
    writer.asked.append(None)  # straight to the answer
    propose(writer, schema, EditorContext(), "how do I save?", help="[shell/menus.md#file-menu] Save (Ctrl+S)")
    assert "EDITOR HELP" in writer.asked[1][-1]["content"] and "Ctrl+S" in writer.asked[1][-1]["content"]


def test_an_answer_needs_its_text(schema):
    class Empty:
        def complete(self, messages):
            return Completion(json.dumps({"scope": "answer", "summary": "?", "answer": ""}))

    result = propose(Empty(), schema, EditorContext(), "what is this?")
    assert result.proposal is None or result.errors or result.proposal.answer
