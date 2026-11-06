"""The beta: registrations, the support team's "Send download link" approval, and personal download links that expire.

Flow: someone registers on /beta (or the main page) -> a row in the database and an email to the support address with
a "Send download link" button -> the button opens a confirmation page (a POST sends, so mail scanners that open links
can't) -> a fresh UUID valid for a week is stored and emailed to the person -> /beta/download/<uuid> gives the beta
until it expires. Sending again replaces the link.

Settings come from website/backend/.env (see .env.example): the support address, the SMTP server, the site's public
address and where the beta file is. Without SMTP settings, emails are written to data/outbox/ instead of sent.
"""
from __future__ import annotations

import html
import os
import re
import secrets
import smtplib
import sqlite3
import ssl
import threading
import time
import uuid
from datetime import datetime, timedelta, timezone
from email.message import EmailMessage
from email.utils import make_msgid
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
DB_PATH = DATA / "beta.sqlite"
OUTBOX = DATA / "outbox"
LOGO = HERE / "email_assets" / "aige-logo.png"
LINK_DAYS = 7
EMAIL = re.compile(r"^[^@\s]{1,64}@[^@\s]{1,255}\.[^@\s]{2,}$")


# --- settings -------------------------------------------------------------------------------------------------------

def _load_env() -> None:
    """website/backend/.env: KEY=value lines (no extra dependency); the process's own environment wins."""
    file = HERE / ".env"
    if not file.is_file():
        return
    for line in file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


_load_env()


def setting(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def site_url() -> str:
    return setting("SITE_URL", "http://127.0.0.1:5000").rstrip("/")


# --- database -------------------------------------------------------------------------------------------------------

_lock = threading.Lock()


def _db() -> sqlite3.Connection:
    DATA.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(DB_PATH)
    db.row_factory = sqlite3.Row
    db.execute("""CREATE TABLE IF NOT EXISTS registrations (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        email TEXT NOT NULL UNIQUE COLLATE NOCASE,
        name TEXT NOT NULL DEFAULT '',
        note TEXT NOT NULL DEFAULT '',
        created_at TEXT NOT NULL,
        approve_key TEXT NOT NULL UNIQUE,
        token TEXT UNIQUE,
        token_created_at TEXT,
        token_expires_at TEXT,
        sent_count INTEGER NOT NULL DEFAULT 0,
        downloads INTEGER NOT NULL DEFAULT 0,
        last_download_at TEXT
    )""")
    return db


def _now() -> datetime:
    return datetime.now(timezone.utc)


def _iso(moment: datetime) -> str:
    return moment.isoformat(timespec="seconds")


def _when(text: str | None) -> str:
    """A stored time for people: "13 November 2026, 14:05 UTC"."""
    if not text:
        return ""
    moment = datetime.fromisoformat(text)
    return f"{moment.day} {moment:%B %Y, %H:%M} UTC"


def register(email: str, name: str = "", note: str = "") -> tuple[dict, bool]:
    """Stores a registration; returns it and whether it's new (an address already registered isn't stored twice)."""
    with _lock, _db() as db:
        row = db.execute("SELECT * FROM registrations WHERE email = ?", (email,)).fetchone()
        if row is not None:
            return dict(row), False
        db.execute("INSERT INTO registrations (email, name, note, created_at, approve_key) VALUES (?, ?, ?, ?, ?)",
                   (email, name, note, _iso(_now()), secrets.token_urlsafe(24)))
        return dict(db.execute("SELECT * FROM registrations WHERE email = ?", (email,)).fetchone()), True


def by_approve_key(key: str) -> dict | None:
    with _db() as db:
        row = db.execute("SELECT * FROM registrations WHERE approve_key = ?", (key,)).fetchone()
        return dict(row) if row else None


def issue_link(key: str) -> dict | None:
    """A fresh download UUID for the registration, valid for LINK_DAYS (replacing any earlier one)."""
    with _lock, _db() as db:
        row = db.execute("SELECT * FROM registrations WHERE approve_key = ?", (key,)).fetchone()
        if row is None:
            return None
        now = _now()
        db.execute("UPDATE registrations SET token = ?, token_created_at = ?, token_expires_at = ?, sent_count = sent_count + 1 "
                   "WHERE id = ?", (str(uuid.uuid4()), _iso(now), _iso(now + timedelta(days=LINK_DAYS)), row["id"]))
        return dict(db.execute("SELECT * FROM registrations WHERE id = ?", (row["id"],)).fetchone())


def check_token(token: str) -> tuple[str, dict | None]:
    """("ok" | "expired" | "unknown", the registration); an ok one counts a download."""
    try:
        token = str(uuid.UUID(token))
    except ValueError:
        return "unknown", None
    with _lock, _db() as db:
        row = db.execute("SELECT * FROM registrations WHERE token = ?", (token,)).fetchone()
        if row is None:
            return "unknown", None
        if datetime.fromisoformat(row["token_expires_at"]) < _now():
            return "expired", dict(row)
        db.execute("UPDATE registrations SET downloads = downloads + 1, last_download_at = ? WHERE id = ?", (_iso(_now()), row["id"]))
        return "ok", dict(row)


def count() -> int:
    with _db() as db:
        return db.execute("SELECT COUNT(*) FROM registrations").fetchone()[0]


# --- a little abuse protection ---------------------------------------------------------------------------------------

_recent: dict[str, list[float]] = {}


def allowed(ip: str, per_hour: int = 6) -> bool:
    """At most `per_hour` registrations an hour from one address."""
    now = time.time()
    with _lock:
        stamps = [t for t in _recent.get(ip, []) if now - t < 3600]
        if len(stamps) >= per_hour:
            _recent[ip] = stamps
            return False
        stamps.append(now)
        _recent[ip] = stamps
        return True


# --- email -----------------------------------------------------------------------------------------------------------

def _shell(logo_cid: str, preheader: str, body: str) -> str:
    """The emails' frame: a table layout with inline styles (what email clients render), the logo in a navy header."""
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark"><title>AIGE</title></head>
<body style="margin:0;padding:0;background:#eef2f9;">
<div style="display:none;max-height:0;overflow:hidden;opacity:0;">{html.escape(preheader)}</div>
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:#eef2f9;">
<tr><td align="center" style="padding:32px 12px;">
  <table role="presentation" width="600" cellpadding="0" cellspacing="0" style="width:100%;max-width:600px;background:#ffffff;border:1px solid #d6deee;">
    <tr><td style="background:#0b1020;padding:26px 32px;border-bottom:3px solid #22d3ee;">
      <img src="cid:{logo_cid}" width="211" height="64" alt="AIGE · AI Game Engine" style="display:block;border:0;width:211px;height:64px;">
    </td></tr>
    <tr><td style="padding:36px 32px 30px;font-family:Segoe UI,Helvetica,Arial,sans-serif;color:#1b2440;font-size:16px;line-height:1.6;">
      {body}
    </td></tr>
    <tr><td style="background:#f5f7fc;border-top:1px solid #e1e7f3;padding:18px 32px;font-family:Segoe UI,Helvetica,Arial,sans-serif;font-size:12px;line-height:1.6;color:#6c7aa3;">
      AIGE · AI Game Engine. A game engine and editor for 2D games, with AI built in.<br>
      <a href="{site_url()}" style="color:#2f7cf6;text-decoration:none;">{html.escape(site_url().split('://')[-1])}</a>
    </td></tr>
  </table>
</td></tr></table>
</body></html>"""


def _button(href: str, label: str) -> str:
    """A button that works in Outlook too (a bordered table cell, not a styled link alone)."""
    return f"""<table role="presentation" cellpadding="0" cellspacing="0" style="margin:26px 0;"><tr>
<td style="background:#2f7cf6;background-image:linear-gradient(135deg,#2f7cf6,#22d3ee);">
<a href="{html.escape(href)}" style="display:inline-block;padding:15px 30px;font-family:Segoe UI,Helvetica,Arial,sans-serif;font-size:16px;font-weight:700;color:#04101f;text-decoration:none;">{html.escape(label)}</a>
</td></tr></table>"""


def support_email(row: dict, logo_cid: str) -> tuple[str, str, str]:
    """(subject, html, text): a new registration, with the button that sends its download link."""
    send = f"{site_url()}/beta/admin/send/{row['approve_key']}"
    total = count()
    registered = "They're the first to register" if total == 1 else f"{total} people have registered so far"
    empty = '<span style="color:#9aa8cc">—</span>'
    details = "".join(
        f'<tr><td style="padding:8px 14px;background:#f5f7fc;border:1px solid #e1e7f3;color:#6c7aa3;font-size:13px;width:120px;">{label}</td>'
        f'<td style="padding:8px 14px;border:1px solid #e1e7f3;font-size:15px;">{html.escape(value) if value else empty}</td></tr>'
        for label, value in (("Email", row["email"]), ("Name", row["name"]), ("Making", row["note"]),
                             ("Registered", _when(row["created_at"])))
    )
    body = f"""
<p style="margin:0 0 6px;font-size:12px;letter-spacing:.14em;text-transform:uppercase;color:#0e9fb8;font-weight:700;">Beta registration</p>
<h1 style="margin:0 0 16px;font-size:24px;line-height:1.25;color:#0b1020;">{html.escape(row['name'] or row['email'])} wants to try the AIGE beta</h1>
<table role="presentation" cellpadding="0" cellspacing="0" style="border-collapse:collapse;width:100%;margin:0 0 4px;">{details}</table>
{_button(send, "Send download link")}
<p style="margin:0;font-size:14px;color:#55617f;">The button opens a page to confirm. Sending emails {html.escape(row['email'])} a personal
download link that works for {LINK_DAYS} days. {registered}.</p>"""
    text = (f"Beta registration\n\nEmail: {row['email']}\nName: {row['name']}\nMaking: {row['note']}\n"
            f"Registered: {_when(row['created_at'])}\n\nSend download link: {send}\n")
    return f"AIGE beta registration: {row['email']}", _shell(logo_cid, f"{row['email']} registered for the beta", body), text


def download_email(row: dict, logo_cid: str) -> tuple[str, str, str]:
    """(subject, html, text): the person's download link and when it expires."""
    link = f"{site_url()}/beta/download/{row['token']}"
    expires = _when(row["token_expires_at"])
    hello = f"Hi {html.escape(row['name'])}," if row["name"] else "Hi,"
    version = setting("BETA_VERSION")
    body = f"""
<p style="margin:0 0 6px;font-size:12px;letter-spacing:.14em;text-transform:uppercase;color:#0e9fb8;font-weight:700;">Your beta is ready</p>
<h1 style="margin:0 0 18px;font-size:26px;line-height:1.25;color:#0b1020;">Welcome to the AIGE beta{f' ({html.escape(version)})' if version else ''}</h1>
<p style="margin:0 0 14px;">{hello}</p>
<p style="margin:0 0 14px;">Thanks for registering. Your copy of AIGE is ready: download it with the button below, install it, and
make your first place: by hand, or by asking the AI.</p>
{_button(link, "Download the AIGE beta")}
<table role="presentation" cellpadding="0" cellspacing="0" style="width:100%;margin:0 0 20px;"><tr>
<td style="background:#fff8e6;border-left:4px solid #f5c76b;padding:12px 16px;font-size:14px;line-height:1.55;color:#5a4a1e;">
<strong>This link is yours and works for {LINK_DAYS} days</strong>, until <strong>{expires}</strong>. After that it expires:
reply to this email for a new one. Please don't share it.
</td></tr></table>
<p style="margin:0 0 6px;font-size:14px;color:#55617f;">If the button doesn't work, copy this address into your browser:</p>
<p style="margin:0 0 20px;font-size:13px;word-break:break-all;"><a href="{html.escape(link)}" style="color:#2f7cf6;">{html.escape(link)}</a></p>
<p style="margin:0 0 4px;font-size:15px;"><strong>Getting started</strong></p>
<p style="margin:0 0 14px;font-size:14px;color:#55617f;">The documentation walks through every panel with worked examples:
<a href="{site_url()}/docs" style="color:#2f7cf6;">{html.escape(site_url().split('://')[-1])}/docs</a>. We'd love to hear what you make, and what gets in your way: just reply.</p>
<p style="margin:0;">The AIGE team</p>"""
    text = (f"{'Hi ' + row['name'] if row['name'] else 'Hi'},\n\nThanks for registering for the AIGE beta. Download it here:\n{link}\n\n"
            f"This link is yours and works for {LINK_DAYS} days, until {expires}. After that it expires: reply for a new one.\n\n"
            f"Docs: {site_url()}/docs\n\nThe AIGE team\n")
    return "Your AIGE beta download link", _shell(logo_cid, f"Your personal download link, valid until {expires}", body), text


def send(to: str, compose) -> str:
    """Sends an email made by `compose(logo_cid)`; without SMTP settings writes it to data/outbox/. Returns where it went."""
    cid = make_msgid(domain="aige")[1:-1]
    subject, html_body, text = compose(cid)
    message = EmailMessage()
    message["Subject"] = subject
    message["From"] = setting("SMTP_FROM") or setting("AIGE_SUPPORT_EMAIL") or "AIGE <no-reply@localhost>"
    message["To"] = to
    if setting("AIGE_SUPPORT_EMAIL"):
        message["Reply-To"] = setting("AIGE_SUPPORT_EMAIL")
    message.set_content(text)
    message.add_alternative(html_body, subtype="html")
    if LOGO.is_file():
        message.get_payload()[1].add_related(LOGO.read_bytes(), maintype="image", subtype="png", cid=f"<{cid}>",
                                             filename="aige-logo.png")
    host = setting("SMTP_HOST")
    if not host or not to:
        OUTBOX.mkdir(parents=True, exist_ok=True)
        stamp = _now().strftime("%Y%m%d-%H%M%S")
        safe = re.sub(r"[^a-z0-9]+", "-", (to or "no-recipient").lower())
        (OUTBOX / f"{stamp}-{safe}.eml").write_bytes(bytes(message))
        # The HTML too, with the logo as a file next to it: open it in a browser to see the email.
        (OUTBOX / f"{stamp}-{safe}.html").write_text(html_body.replace(f"cid:{cid}", LOGO.as_uri()), encoding="utf-8")
        return f"outbox ({'no SMTP_HOST' if not host else 'no recipient'})"
    port = int(setting("SMTP_PORT", "587"))
    security = setting("SMTP_SECURITY", "starttls").lower()
    context = ssl.create_default_context()
    if security == "ssl":
        server = smtplib.SMTP_SSL(host, port, context=context, timeout=20)
    else:
        server = smtplib.SMTP(host, port, timeout=20)
    with server:
        if security == "starttls":
            server.starttls(context=context)
        if setting("SMTP_USER"):
            server.login(setting("SMTP_USER"), setting("SMTP_PASSWORD"))
        server.send_message(message)
    return "sent"


# --- pages served by Flask (outside the Vue app: the approval and the download) ------------------------------------

def page(title: str, body: str, tone: str = "info") -> str:
    """A small branded page: the confirmation, sent, expired and unknown-link pages."""
    accent = {"info": "#22d3ee", "ok": "#22d3ee", "warn": "#f5c76b", "error": "#ff8a8a"}[tone]
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)} · AIGE</title><link rel="icon" href="/img/favicon.png">
<style>
body{{margin:0;background:#0b1020;color:#e6eeff;font:16px/1.6 Inter,system-ui,'Segoe UI',sans-serif;}}
.box{{max-width:560px;margin:10vh auto;padding:0 16px;}}
.card{{background:#121a36;border:1px solid #24305a;border-top:3px solid {accent};padding:32px 30px;}}
img{{height:44px;margin-bottom:22px;display:block;}}
h1{{font-size:24px;margin:0 0 12px;line-height:1.25;}} p{{color:#9aa8cc;margin:0 0 14px;}} strong{{color:#fff;}}
a{{color:#a5dcff;}} button,.btn{{display:inline-block;background:linear-gradient(135deg,#2f7cf6,#22d3ee);color:#04101f;border:0;
padding:13px 22px;font:700 15px Inter,sans-serif;cursor:pointer;text-decoration:none;margin-top:6px;}}
dl{{display:grid;grid-template-columns:110px 1fr;gap:6px 12px;margin:0 0 18px;font-size:15px;}} dt{{color:#6c7aa3;}} dd{{margin:0;}}
</style></head><body><div class="box"><div class="card"><img src="/img/aige-logo-dark.svg" alt="AIGE">
<h1>{html.escape(title)}</h1>{body}</div></div></body></html>"""
