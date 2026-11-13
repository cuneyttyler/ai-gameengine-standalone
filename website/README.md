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

## Deploy

The site runs on a Hetzner Cloud server: **cx23** (2 vCPU, 4 GB, 40 GB), Ubuntu 24.04, Falkenstein (`fsn1`),
named `aige-website`. There:
- nginx answers on port 80; ufw allows only SSH and HTTP(S);
- gunicorn runs the Flask app as the unprivileged `aige` user on 127.0.0.1:8000 (`deploy/aige-website.service`);
- everything lives in `/opt/aige`: `website/`, `data/help` (the docs), `venv/`, `beta/` (the zip the links serve).

To deploy (from Git Bash, with `~/.ssh/aige_hetzner`):

```sh
website/deploy/deploy.sh <server ip> [dist/AIGE-Beta-<date>.zip]
```

It builds the frontend, then uploads the backend, the built frontend, `data/help`, the beta zip and the server's
`.env`. That `.env` is `backend/.env` with `SITE_URL=http://<server ip>` (or `SITE_URL=...` given to the script),
and `BETA_DOWNLOAD_FILE` pointing at the uploaded zip; it is installed readable by `aige` only. The script then
installs the packages, the unit and the nginx site, restarts them, and checks `/`, `/beta`, `/api/beta` and `/docs`.
Registrations in the server's `backend/data` are kept across deploys.

The site is **https://aige-dev.com** (Namecheap DNS: A records `@` and `www` to the server). Deploy it with the
domain:

```sh
DOMAIN=aige-dev.com website/deploy/deploy.sh 2.31.139.43 [dist/AIGE-Beta-<date>.zip]
```

With `DOMAIN`:
- the first deploy gets a Let's Encrypt certificate for the domain and `www` (webroot: `/var/www/certbot`);
- `certbot.timer` renews it, and reloads nginx after;
- the site is `deploy/aige-website-tls.nginx`: http and `www` redirect to `https://<domain>`;
- `SITE_URL` (the links in emails) is `https://<domain>`.

Without `DOMAIN`, the deploy goes back to plain http on the IP.

Logs: `journalctl -u aige-website`.

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
