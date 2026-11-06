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

## Editing the content

Change the text in `backend/content.py`, and the site follows (no frontend rebuild needed). Facts come from
the repo: `docs/manuals`, `docs/ai/agent.md`, `docs/ai/ai-planner.md`, `docs/places/*.md`,
`ontology/README.md`, `docs/systems/asset_management.md` and `docs/project/roadmap.md`.

Early-access sign-ups are appended to `backend/data/signups.jsonl` (ignored by git).
