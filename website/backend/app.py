"""The AIGE website's server: the site's content as JSON, early-access sign-ups, and the built Vue app.

Run (development):   python app.py           -> http://127.0.0.1:5000  (with `npm run dev` in ../frontend for hot reload)
Production:          build the frontend (npm run build), then serve this app; it serves ../frontend/dist.
"""
import json
import re
import threading
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, abort, jsonify, request, send_from_directory

import docs
from content import CONTENT

HERE = Path(__file__).resolve().parent
DIST = HERE.parent / "frontend" / "dist"
SIGNUPS = HERE / "data" / "signups.jsonl"
EMAIL = re.compile(r"^[^@\s]{1,64}@[^@\s]{1,255}\.[^@\s]{2,}$")

app = Flask(__name__, static_folder=None)
_lock = threading.Lock()


@app.get("/api/content")
def content():
    return jsonify(CONTENT)


@app.get("/api/docs")
def docs_tree():
    """The documentation's areas, groups, pages and their sections (the sidebar)."""
    return jsonify(docs.tree())


@app.get("/api/docs/<area>/<page>")
def docs_page(area: str, page: str):
    """One page: its sections and subsections as HTML (from data/help)."""
    if not re.fullmatch(r"[a-z0-9-]+", area) or not re.fullmatch(r"[a-z0-9-]+", page):
        abort(404)
    found = docs.page(area, page)
    if found is None:
        abort(404)
    return jsonify(found)


@app.get("/api/docs-search")
def docs_search():
    return jsonify({"hits": docs.search(str(request.args.get("q", ""))[:200])})


@app.post("/api/signup")
def signup():
    body = request.get_json(silent=True) or {}
    email = str(body.get("email", "")).strip()
    if len(email) > 320 or not EMAIL.match(email):
        return jsonify({"ok": False, "error": "That doesn't look like an email address."}), 400
    interest = str(body.get("interest", ""))[:40]
    SIGNUPS.parent.mkdir(parents=True, exist_ok=True)
    with _lock:
        known = set()
        if SIGNUPS.exists():
            for line in SIGNUPS.read_text(encoding="utf-8").splitlines():
                try:
                    known.add(json.loads(line)["email"].lower())
                except (ValueError, KeyError):
                    continue
        if email.lower() not in known:
            with SIGNUPS.open("a", encoding="utf-8") as out:
                out.write(json.dumps({"email": email, "interest": interest,
                                      "time": datetime.now(timezone.utc).isoformat(timespec="seconds")}) + "\n")
    return jsonify({"ok": True})


@app.get("/", defaults={"path": ""})
@app.get("/<path:path>")
def frontend(path: str):
    if path.startswith("api/"):
        abort(404)
    if not DIST.is_dir():
        return ("The frontend isn't built yet: run `npm install && npm run build` in website/frontend "
                "(or `npm run dev` and open http://127.0.0.1:5173).", 503)
    file = (DIST / path).resolve()
    if path and DIST in file.parents and file.is_file():
        return send_from_directory(DIST, path)
    return send_from_directory(DIST, "index.html")


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)
