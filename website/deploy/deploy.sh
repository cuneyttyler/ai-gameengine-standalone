#!/usr/bin/env bash
# Deploys the AIGE website to a server (Ubuntu 24.04 with nginx and python3-venv: see website/README.md ▸ Deploy).
#
#   website/deploy/deploy.sh <host> [beta zip]
#
# Builds the frontend, then uploads the backend, the built frontend, data/help (the docs), the server's .env (the
# backend's own with SITE_URL set to http://<host>, and BETA_DOWNLOAD_FILE to the uploaded zip) and the beta zip.
# On the server it installs them in /opt/aige, the Python packages in /opt/aige/venv, the systemd unit and the nginx
# site, and restarts both. Registrations (backend/data) on the server are kept across deploys.
#
# SSH: root@<host> with ~/.ssh/aige_hetzner (AIGE_SSH_KEY picks another). DOMAIN=<name> (aige-dev.com) serves it over
# HTTPS for that name and www.<name>: a Let's Encrypt certificate is obtained the first time (the domain must point at
# the server) and renewed by certbot; http goes to https. SITE_URL=<url> sets the public address (default
# https://<domain>, or http://<host> without one).
set -euo pipefail

HOST="${1:?usage: deploy.sh <host> [beta zip]}"
ZIP="${2:-}"
KEY="${AIGE_SSH_KEY:-$HOME/.ssh/aige_hetzner}"
DOMAIN="${DOMAIN:-}"
if [ -n "$DOMAIN" ]; then SITE="${SITE_URL:-https://$DOMAIN}"; else SITE="${SITE_URL:-http://$HOST}"; fi
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SSH=(ssh -i "$KEY" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15 "root@$HOST")
SCP=(scp -i "$KEY" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15)
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== the frontend"
(cd "$ROOT/website/frontend" && npm run build >/dev/null)

echo "== the server's .env"
ENV_IN="$ROOT/website/backend/.env"
[ -f "$ENV_IN" ] || ENV_IN="$ROOT/website/backend/.env.example"
grep -v -E '^(SITE_URL|BETA_DOWNLOAD_FILE|BETA_DOWNLOAD_URL)=' "$ENV_IN" > "$WORK/server.env" || true
echo "SITE_URL=$SITE" >> "$WORK/server.env"
if [ -n "$ZIP" ]; then
  echo "BETA_DOWNLOAD_FILE=/opt/aige/beta/$(basename "$ZIP")" >> "$WORK/server.env"
else
  # No new zip: the one the server serves now stays.
  "${SSH[@]}" "grep -E '^BETA_DOWNLOAD_(FILE|URL)=' /opt/aige/website/backend/.env 2>/dev/null" >> "$WORK/server.env" || true
fi

echo "== the bundle"
tar -C "$ROOT" -czf "$WORK/site.tgz" \
  --exclude='website/backend/.venv' --exclude='website/backend/data' --exclude='website/backend/.env' --exclude='__pycache__' \
  website/backend website/frontend/dist website/deploy data/help

echo "== uploading to $HOST"
"${SSH[@]}" 'mkdir -p /root/aige-upload && cloud-init status --wait >/dev/null 2>&1 || true'
"${SCP[@]}" "$WORK/site.tgz" "$WORK/server.env" "root@$HOST:/root/aige-upload/"
if [ -n "$ZIP" ]; then
  "${SCP[@]}" "$ZIP" "root@$HOST:/root/aige-upload/"
fi

echo "== installing"
"${SSH[@]}" "ZIP_NAME='$(basename "${ZIP:-none}")' DOMAIN='$DOMAIN' bash -s" <<'REMOTE'
set -euo pipefail
id aige >/dev/null 2>&1 || useradd --system --create-home --home-dir /opt/aige --shell /usr/sbin/nologin aige
cd /opt/aige
rm -rf website/frontend/dist website/deploy data/help
tar -xzf /root/aige-upload/site.tgz -C /opt/aige
install -m 600 -o aige -g aige /root/aige-upload/server.env /opt/aige/website/backend/.env
mkdir -p /opt/aige/website/backend/data /opt/aige/beta
if [ "$ZIP_NAME" != "none" ] && [ -f "/root/aige-upload/$ZIP_NAME" ]; then
  mv "/root/aige-upload/$ZIP_NAME" /opt/aige/beta/
fi
chown -R aige:aige /opt/aige
# nginx (www-data) serves the built frontend straight from disk: it may pass through to it and read it, nothing else
# (.env stays aige's alone, 600; the registrations in backend/data too).
chmod o+x /opt/aige /opt/aige/website /opt/aige/website/frontend
chmod -R o+rX /opt/aige/website/frontend/dist
[ -x /opt/aige/venv/bin/python ] || python3 -m venv /opt/aige/venv
/opt/aige/venv/bin/pip install --quiet --upgrade pip
/opt/aige/venv/bin/pip install --quiet -r /opt/aige/website/backend/requirements.txt gunicorn==23.0.0
install -m 644 /opt/aige/website/deploy/aige-website.service /etc/systemd/system/aige-website.service
install -m 644 /opt/aige/website/deploy/aige-website.nginx /etc/nginx/sites-available/aige-website
ln -sf /etc/nginx/sites-available/aige-website /etc/nginx/sites-enabled/aige-website
rm -f /etc/nginx/sites-enabled/default
nginx -t -q
systemctl daemon-reload
systemctl enable --now aige-website >/dev/null 2>&1
systemctl restart aige-website
mkdir -p /var/www/certbot
systemctl reload nginx
if [ -n "$DOMAIN" ]; then
  # HTTPS: the certificate the first time (over the http site above), then the https site.
  command -v certbot >/dev/null || DEBIAN_FRONTEND=noninteractive apt-get install -y -q certbot >/dev/null
  if [ ! -f "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" ]; then
    EMAIL="$(grep -E '^(AIGE_SUPPORT_EMAIL|SMTP_USER)=' /opt/aige/website/backend/.env | head -1 | cut -d= -f2-)"
    if [ -n "$EMAIL" ]; then WHO=(-m "$EMAIL"); else WHO=(--register-unsafely-without-email); fi
    certbot certonly --webroot -w /var/www/certbot -d "$DOMAIN" -d "www.$DOMAIN" --non-interactive --agree-tos \
      "${WHO[@]}" --deploy-hook "systemctl reload nginx"
  fi
  sed "s/__DOMAIN__/$DOMAIN/g" /opt/aige/website/deploy/aige-website-tls.nginx > /etc/nginx/sites-available/aige-website
  nginx -t -q
  systemctl reload nginx
fi
rm -f /root/aige-upload/site.tgz /root/aige-upload/server.env
sleep 2
# The pages, and the frontend's script (served by nginx itself: a page that loads with its script refused is blank).
ASSET="$(grep -o '/assets/[^"]*\.js' /opt/aige/website/frontend/dist/index.html | head -1)"
for path in / /beta /api/beta /docs "$ASSET"; do
  if [ -n "$DOMAIN" ]; then
    code="$(curl -s -o /dev/null -w '%{http_code}' --resolve "$DOMAIN:443:127.0.0.1" "https://$DOMAIN$path")"
  else
    code="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1$path")"
  fi
  printf '  %-10s %s\n' "$path" "$code"
done
REMOTE
echo "== done: $SITE"
