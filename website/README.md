# AIGE website

The product site for AIGE: a Vue 3 single page (Vite) served by a small Flask app.

```
website/
  backend/    Flask: /api/content (everything the site says, in content.py), /api/signup, and the built frontend
  frontend/   Vue 3 + Vite: one component per section in src/components
```

## Develop

```sh
# 1. the API
cd website/backend
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt   # macOS/Linux: .venv/bin/python
.venv/Scripts/python app.py                               # http://127.0.0.1:5000

# 2. the frontend, with hot reload (proxies /api to Flask)
cd website/frontend
npm install
npm run dev                                               # http://127.0.0.1:5173
```

## Production

```sh
cd website/frontend && npm run build    # writes frontend/dist
cd ../backend && .venv/Scripts/python app.py
```

Flask serves `frontend/dist` and the API from one port. Behind a real server, run it with a WSGI server
(for example `waitress-serve --port 8000 app:app` or gunicorn) rather than Flask's development server.

## Documentation (/docs)

`/docs` is the editor's documentation, read from its knowledge base in `../data/help` (the same files the editor's AI
panel answers questions from): `index.json` orders the areas, groups and pages; each page's `## ` headings are its
sections and `### Example: ...` headings its worked examples. `backend/docs.py` serves them (`/api/docs`,
`/api/docs/<area>/<page>`, `/api/docs-search?q=`), rereading a file when it changes; `frontend/src/docs/DocsApp.vue`
shows them with the sidebar. Edit the Markdown and reload: no rebuild needed.

## Beta (/beta)

`/beta` describes the beta and takes registrations (the main page's "Register for Beta" form posts there too).
`backend/beta.py` keeps them in a small SQLite database, `backend/data/beta.sqlite`, and:

1. On a registration it emails the support address (`AIGE_SUPPORT_EMAIL`) with a **Send download link** button.
2. The button opens a page to confirm (a POST sends, so mail scanners opening the link can't); sending stores a fresh
   UUID that expires in 7 days and emails the person their link (`/beta/download/<uuid>`), saying when it expires.
   Sending again replaces the link.
3. The link serves `BETA_DOWNLOAD_FILE` (or redirects to `BETA_DOWNLOAD_URL`) until it expires; then a page says so.

Settings live in `backend/.env` (copy `backend/.env.example`; it stays out of git): the support address, the SMTP
server, `SITE_URL` (the links in emails start with it) and the beta file. Without `SMTP_HOST`, emails are written to
`backend/data/outbox/` as `.eml` and `.html` files instead of being sent, so the whole flow can be tried locally.

## Editing the content

Change the text in `backend/content.py`, and the site follows (no frontend rebuild needed). Facts come from
the repo: `docs/manuals`, `docs/ai/agent.md`, `docs/ai/ai-planner.md`, `docs/places/*.md`,
`ontology/README.md`, `docs/systems/asset_management.md` and `docs/project/roadmap.md`.

Early-access sign-ups are appended to `backend/data/signups.jsonl` (ignored by git).
