"""World Bible index and search (development_plan.md 3.1)."""

import hashlib
import shutil
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from app.main import create_app
from app.providers import ProviderError
from app.world import WorldIndex, load_entries, parse_entry



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
