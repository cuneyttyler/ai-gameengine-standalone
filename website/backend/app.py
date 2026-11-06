"""The AIGE website's server: the site's content as JSON, early-access sign-ups, and the built Vue app.

Run (development):   python app.py           -> http://127.0.0.1:5000  (with `npm run dev` in ../frontend for hot reload)
Production:          build the frontend (npm run build), then serve this app; it serves ../frontend/dist.
"""
import json
import re
import threading
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, abort, jsonify, redirect, request, send_file, send_from_directory

import beta
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


# --- the beta (beta.py) ---

@app.get("/api/beta")
def beta_info():
    return jsonify({**CONTENT["beta"], "linkDays": beta.LINK_DAYS})


@app.post("/api/beta/register")
def beta_register():
    """A registration: stored, and the support team emailed with a "Send download link" button."""
    body = request.get_json(silent=True) or {}
    if str(body.get("website", "")).strip():  # the hidden field only bots fill in
        return jsonify({"ok": True})
    email = str(body.get("email", "")).strip()
    if len(email) > 320 or not beta.EMAIL.match(email):
        return jsonify({"ok": False, "error": "That doesn't look like an email address."}), 400
    if not beta.allowed(request.headers.get("X-Forwarded-For", request.remote_addr or "").split(",")[0].strip()):
        return jsonify({"ok": False, "error": "Too many registrations from here: please try again later."}), 429
    name = " ".join(str(body.get("name", "")).split())[:80]
    note = " ".join(str(body.get("note", "")).split())[:300]
    row, new = beta.register(email, name, note)
    if new:
        def notify():
            try:
                where = beta.send(beta.setting("AIGE_SUPPORT_EMAIL"), lambda cid: beta.support_email(row, cid))
                app.logger.info("beta registration %s: support email %s", email, where)
            except Exception as error:  # noqa: BLE001 - the registration is kept; the email is logged as failed
                app.logger.error("beta registration %s: the support email failed: %s", email, error)
        threading.Thread(target=notify, daemon=True).start()
    return jsonify({"ok": True, "already": not new})


@app.route("/beta/admin/send/<key>", methods=["GET", "POST"])
def beta_send(key: str):
    """The support email's button: a page to confirm (GET), then the link sent (POST)."""
    row = beta.by_approve_key(key)
    if row is None:
        return beta.page("Unknown registration", "<p>This button doesn't match a registration. It may have been made on "
                                                 "another server.</p>", "error"), 404
    who = beta.html.escape(row["email"])
    if request.method == "GET":
        earlier = (f"<p>A link was already sent {row['sent_count']} time(s), last on <strong>{beta._when(row['token_created_at'])}</strong>. "
                   "Sending again makes a new link and the old one stops working.</p>") if row["sent_count"] else ""
        return beta.page("Send the beta download link?", f"""
<dl><dt>Email</dt><dd>{who}</dd><dt>Name</dt><dd>{beta.html.escape(row['name']) or '—'}</dd>
<dt>Making</dt><dd>{beta.html.escape(row['note']) or '—'}</dd><dt>Registered</dt><dd>{beta._when(row['created_at'])}</dd></dl>
{earlier}<p>They'll get a personal link that works for {beta.LINK_DAYS} days.</p>
<form method="post"><button type="submit">Send download link</button></form>""")
    row = beta.issue_link(key)
    try:
        where = beta.send(row["email"], lambda cid: beta.download_email(row, cid))
    except Exception as error:  # noqa: BLE001 - shown on the page: the link is made, the email isn't
        app.logger.error("beta link for %s: the email failed: %s", row["email"], error)
        return beta.page("The email couldn't be sent", f"<p>The link was made but emailing {who} failed: "
                                                        f"{beta.html.escape(str(error))}. Check the SMTP settings in .env and "
                                                        "press the button again.</p>", "error"), 502
    note = "" if where == "sent" else f"<p>(Not emailed: {beta.html.escape(where)}. The email is in data/outbox.)</p>"
    return beta.page("Download link sent", f"""<p>The beta download link went to <strong>{who}</strong>.</p>
<p>It works until <strong>{beta._when(row['token_expires_at'])}</strong>.</p>{note}""", "ok")


@app.get("/beta/download/<token>")
def beta_download(token: str):
    """A personal download link: the beta while it's valid, else a page saying why not."""
    state, row = beta.check_token(token)
    if state == "unknown":
        return beta.page("This link doesn't work", "<p>It isn't a beta download link, or a newer link replaced it. "
                                                   "Use the newest email we sent, or <a href=\"/beta\">register again</a>.</p>", "error"), 404
    if state == "expired":
        return beta.page("This link has expired", f"<p>Beta download links work for {beta.LINK_DAYS} days; this one ended on "
                                                  f"<strong>{beta._when(row['token_expires_at'])}</strong>.</p><p>Reply to the email "
                                                  "it came in for a new one.</p>", "warn"), 410
    file = beta.setting("BETA_DOWNLOAD_FILE")
    if file and Path(file).is_file():
        return send_file(Path(file).resolve(), as_attachment=True, download_name=Path(file).name)
    url = beta.setting("BETA_DOWNLOAD_URL")
    if url:
        return redirect(url, code=302)
    return beta.page("The download isn't ready yet", "<p>Your link is valid, but the beta file isn't on the server yet. "
                                                     "Please try again a little later.</p>", "warn"), 503


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
