"""World Bible index and search (development_plan.md 3.1)."""

import hashlib
import shutil
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.main import create_app
from app.providers import ProviderError
from app.world import WorldIndex, load_entries, parse_entry

DEMO = Path(__file__).resolve().parents[2] / "projects" / "demo"


class FakeEmbedder:
    """Deterministic bag-of-words vectors (hashed into 512 dims), so related texts
    really are closer; counts what it embeds."""

    name = "fake-embed"

    def __init__(self):
        self.embedded: list[str] = []
        self.fail = False

    def embed(self, texts):
        if self.fail:
            raise ProviderError("offline")
        self.embedded += texts
        vectors = []
        for text in texts:
            vector = [0.0] * 512
            for word in text.lower().replace(",", " ").replace(".", " ").split():
                if len(word) < 4:  # skip filler words, as real embeddings effectively do
                    continue
                vector[int(hashlib.md5(word.encode()).hexdigest(), 16) % 512] += 1.0
            vectors.append(vector)
        return vectors


@pytest.fixture
def project(tmp_path):
    shutil.copytree(DEMO / "world", tmp_path / "world")
    return tmp_path


def write(root: Path, key: str, text: str) -> None:
    path = root / "world" / f"{key}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def test_entries_parse_like_the_engine_reads_them():
    entry = parse_entry(
        "\n---\r\nname:  Brookvale \r\ntags: village,, home\r\nrel: near location/old-keep; haunted by faction/dead\r\n"
        "---\r\n\r\nA village.\r\n",
        "location",
        "brookvale",
    )
    assert entry is not None
    assert entry.key == "location/brookvale" and entry.name == "Brookvale"
    assert entry.tags == ["village", "home"]
    assert entry.relations == [("near", "location/old-keep"), ("haunted_by", "faction/dead")]
    assert entry.body == "A village."
    assert parse_entry("no header", "lore", "x") is None
    assert parse_entry("---\nname: open\n", "lore", "x") is None


def test_the_demo_canon_loads():
    keys = {e.key for e in load_entries(DEMO)}
    assert {"character/hero", "location/old-keep", "faction/restless-dead", "style/tone"} <= keys


def test_only_new_or_changed_entries_are_embedded(project):
    embedder = FakeEmbedder()
    index = WorldIndex(project, embedder)
    index.refresh()
    total = len(index.entries)
    assert index.embedded_last_refresh == total

    index.refresh()
    assert index.embedded_last_refresh == 0  # nothing changed

    write(project, "character/villager", "---\nname: Old Tamsin\nsummary: Now also the village midwife.\n---\n")
    write(project, "item/red-draught", "---\nname: Red Draught\nsummary: Tamsin's healing potion.\n---\n")
    (project / "world" / "faction" / "wardens.md").unlink()
    index.refresh()
    assert index.embedded_last_refresh == 2
    stored = {key for (key,) in index.db.execute("SELECT key FROM entries")}
    assert "faction/wardens" not in stored and "item/red-draught" in stored

    # A new index over the same cache picks up where the last left off.
    again = WorldIndex(project, embedder)
    again.refresh()
    assert again.embedded_last_refresh == 0


def test_search_ranks_related_entries_first(project):
    index = WorldIndex(project, FakeEmbedder())
    hits, method = index.search("skeletons undead haunt the keep courtyard", k=3)
    assert method == "embeddings"
    assert hits[0].key in {"faction/restless-dead", "location/old-keep"}
    assert len(hits) == 3 and hits[0].score >= hits[1].score >= hits[2].score


def test_keyword_ranking_without_an_embedder_or_when_it_fails(project):
    hits, method = WorldIndex(project, None).search("healing herbs Tamsin", k=2)
    assert method == "keywords"
    assert hits[0].key == "character/villager"

    embedder = FakeEmbedder()
    embedder.fail = True
    hits, method = WorldIndex(project, embedder).search("who protects Brookvale", k=3)
    assert method == "keywords"
    assert "location/brookvale" in [h.key for h in hits]

    assert WorldIndex(project, None).search("zzz qqq", k=3)[0] == []


def test_the_api_indexes_the_open_project(project):
    client = TestClient(create_app(embedder=FakeEmbedder()))
    assert client.post("/world/search", json={"query": "keep"}).json()["ok"] is False
    assert client.put("/project", json={"root": str(project / "nope")}).status_code == 400

    answer = client.put("/project", json={"root": str(project)}).json()
    assert answer == {"ok": True, "worldEntries": 7}
    result = client.post("/world/search", json={"query": "the hero warden", "k": 2}).json()
    assert result["ok"] and result["method"] == "embeddings"
    assert result["hits"][0]["key"] == "character/hero"
    assert set(result["hits"][0]) == {"key", "kind", "name", "summary", "score"}
