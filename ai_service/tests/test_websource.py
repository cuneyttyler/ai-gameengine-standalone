"""The web tool, with pages served from memory (no network)."""
import io

from PIL import Image

from app.websource import normalise_licence, read_page, search_web


def png(w=32, h=32):
    out = io.BytesIO()
    Image.new("RGBA", (w, h), (200, 100, 50, 255)).save(out, format="PNG")
    return out.getvalue()


def page(licence, author="someone", files=("https://opengameart.org/sites/default/files/house.png",)):
    links = "".join(f'<a href="{f}">f</a>' for f in files)
    return (f"<title>A house | OpenGameArt.org</title><h2>User login</h2><div class='license-name'>{licence}</div>"
            f"<span class='username'><a href=\"/users/x\">{author}</a></span>"
            f'<a href="https://opengameart.org/sites/default/files/css/x.css">c</a>{links}').encode()


def test_licences_are_read_and_filtered(tmp_path):
    pages = {
        "search": b'<a href="/content/good">g</a><a href="/content/nc">n</a><a href="/content/sa">s</a><a href="/content/faq">f</a>',
        "/content/good": page("CC-BY 3.0", "alice"),
        "/content/nc": page("CC-BY-NC 3.0"),
        "/content/sa": page("CC-BY-SA 4.0"),
        "house.png": png(),
    }

    def fetch(url):
        if "art-search" in url:
            return pages["search"] if url.endswith("page=0") else b""
        for key, value in pages.items():
            if url.endswith(key):
                return value
        raise OSError("not found")

    result = search_web(tmp_path, "house", "assets/web/house", fetch=fetch, pause=0)
    assert result["ok"]
    assert [a["licence"] for a in result["assets"]] == ["CC-BY"]
    assert result["assets"][0]["author"] == "alice" and result["assets"][0]["path"].startswith("assets/web/house/")
    assert (tmp_path / result["assets"][0]["path"]).exists()
    reasons = " ".join(r["reason"] for r in result["rejected"])
    assert "CC-BY-NC" in reasons and "CC-BY-SA" in reasons  # share-alike only when allowed
    again = search_web(tmp_path, "house", "assets/web/house2", share_alike=True, fetch=fetch, pause=0)
    assert sorted(a["licence"] for a in again["assets"]) == ["CC-BY", "CC-BY-SA"]


def test_only_real_pngs_inside_the_project(tmp_path):
    assert search_web(tmp_path, "x", "../outside", fetch=lambda u: b"", pause=0)["ok"] is False

    def fetch(url):
        if "art-search" in url:
            return b'<a href="/content/a">a</a>' if url.endswith("page=0") else b""
        if url.endswith("/content/a"):
            return page("CC0")
        return b"<html>not a png</html>"

    result = search_web(tmp_path, "x", "assets/web/x", fetch=fetch, pause=0)
    assert result["assets"] == [] and "not a PNG" in result["rejected"][0]["reason"]


def test_licence_names():
    assert normalise_licence("CC0") == "CC0" and normalise_licence("OGA-BY 3.0") == "OGA-BY"
    assert normalise_licence("CC-BY 4.0") == "CC-BY" and normalise_licence("CC-BY-SA 3.0") == "CC-BY-SA"
    info = read_page(page("CC0").decode())
    assert info["files"] == ["https://opengameart.org/sites/default/files/house.png"] and info["title"] == "A house"


def test_queries_and_ranking():
    from app.websource import pick_file, queries_for, score_page
    tried, prefer = queries_for("isometric cottage house")
    assert tried[0] == "cottage house" and "house" in tried and "cottage" in tried  # each word alone too
    assert prefer == {"isometric"}  # ranks, doesn't filter
    single = {"title": "Isometric cottage", "tags": []}
    pack = {"title": "Medieval buildings pack", "tags": ["house"]}
    assert score_page(single, ["cottage", "house"], prefer) > score_page(pack, ["cottage", "house"], prefer)
    files = ["https://opengameart.org/sites/default/files/sheet.png", "https://opengameart.org/sites/default/files/cottage_1.png"]
    assert pick_file(files, ["cottage"]).endswith("cottage_1.png")
