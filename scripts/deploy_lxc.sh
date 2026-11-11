#!/usr/bin/env bash
# deploy_lxc.sh — the game server (server/server.mjs) on a plain Linux host or container
# (an LXC on Proxmox, say), as a systemd service, behind an HTTPS reverse proxy (nginx) run
# elsewhere. No Docker. scripts/deploy_server.sh is the DigitalOcean droplet's (Docker, Caddy).
#
# On the host, everything is in /opt/gta2:
#   server/  server.mjs, crews.mjs, games-page.mjs, package.json, package-lock.json, node_modules
#   web/     the built game (build/port/release), read-only
#   game/    the game's data/ folder only (no saves, executables or movies), read-only
#   db/      saved crews, the only place the service writes (user gta2)
# The settings are in /etc/gta2.env (chmod 600), the service is gta2.service.
#
# First time (Node from an official linux-x64 .tar.xz, checked beforehand against SHASUMS256):
#   GTA2_PASSWORD=... PUBLIC_URL=https://gta2.example.com scripts/deploy_lxc.sh root@host \
#     --node node-v22.x-linux-x64.tar.xz --web build/port/release --game game
# Later: the same without --node; --web and --game only when they change; GTA2_PASSWORD and
# PUBLIC_URL only to change them (the env file is kept otherwise).
#   SSH_KEY=~/.ssh/key   the key to use (default: ssh's own choice)
set -euo pipefail

HOST="${1:?usage: deploy_lxc.sh root@host [--node tar.xz] [--web dir] [--game dir]}"
shift
NODE_TAR=""; WEB=""; GAME=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --node) NODE_TAR="$2"; shift 2 ;;
    --web)  WEB="$2"; shift 2 ;;
    --game) GAME="$2"; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SSH=(ssh -o ConnectTimeout=10)
[[ -n "${SSH_KEY:-}" ]] && SSH+=(-i "$SSH_KEY" -o IdentitiesOnly=yes)
RSYNC=(rsync -rlt --delete -e "${SSH[*]}")
log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

log "folders, user gta2"
"${SSH[@]}" "$HOST" 'set -e
  id gta2 >/dev/null 2>&1 || useradd --system --no-create-home --home-dir /opt/gta2 --shell /usr/sbin/nologin gta2
  mkdir -p /opt/gta2/server /opt/gta2/web /opt/gta2/game /opt/gta2/db
  chown root:root /opt/gta2 /opt/gta2/server /opt/gta2/web /opt/gta2/game
  chmod 755 /opt/gta2
  chown gta2:gta2 /opt/gta2/db && chmod 700 /opt/gta2/db'

if [[ -n "$NODE_TAR" ]]; then
  name="$(basename "$NODE_TAR" .tar.xz)"
  log "Node: $name"
  "${SSH[@]}" "$HOST" "command -v xz >/dev/null || (apt-get update -qq && apt-get install -y -qq xz-utils >/dev/null)"
  "${SSH[@]}" "$HOST" "cat > /tmp/$name.tar.xz" < "$NODE_TAR"
  "${SSH[@]}" "$HOST" "set -e; rm -rf /opt/$name; tar xJf /tmp/$name.tar.xz -C /opt; rm /tmp/$name.tar.xz
    ln -sfn /opt/$name /opt/node
    for b in node npm npx; do ln -sf /opt/node/bin/\$b /usr/local/bin/\$b; done
    node --version"
fi

log "server code"
"${RSYNC[@]}" --exclude node_modules \
  "$ROOT/server/server.mjs" "$ROOT/server/crews.mjs" "$ROOT/server/games-page.mjs" \
  "$ROOT/server/package.json" "$ROOT/server/package-lock.json" \
  "$HOST:/opt/gta2/server/"
"${SSH[@]}" "$HOST" 'set -e; cd /opt/gta2/server && npm ci --omit=dev --no-audit --no-fund --loglevel=error'

if [[ -n "$WEB" ]]; then
  # The build going out of service stays in old/<build>/ (the first 24 hex digits of its
  # gta2.wasm's sha256, as scripts/build_port.py names builds): the Saves screen opens a save
  # made with it there. old/ is never deleted; the build's leftovers that are not for players
  # (obj/, objects.rsp, old-local/: earlier local builds, dev/) never go out, and are removed
  # where an earlier deploy sent them.
  log "keeping the live build in old/"
  "${SSH[@]}" "$HOST" 'set -e; cd /opt/gta2/web
    if [ -f gta2.wasm ]; then
      b=$(sha256sum gta2.wasm | cut -c1-24)
      if [ ! -f old/$b/gta2.wasm ]; then
        mkdir -p old/$b
        for f in *; do case "$f" in obj|objects.rsp|old|old-local|dev) ;; *) cp -a "$f" old/$b/ ;; esac; done
        echo "   old/$b"
      fi
    fi
    rm -rf old-local objects.rsp obj dev'
  log "the built game: $WEB"
  "${RSYNC[@]}" --exclude obj/ --exclude objects.rsp --exclude old-local/ --exclude old/ --exclude dev/ \
    "${WEB%/}/" "$HOST:/opt/gta2/web/"
fi
if [[ -n "$GAME" ]]; then
  # data/ only: never the saves (player/, ...); not what the server refuses to serve anyway
  log "the game files: ${GAME%/}/data"
  "${SSH[@]}" "$HOST" 'mkdir -p /opt/gta2/game/data'
  "${RSYNC[@]}" --exclude '*.exe' --exclude '*.dll' --exclude '*.bik' --exclude '*.m3d' --exclude '*.ico' --exclude '*.isu' \
    "${GAME%/}/data/" "$HOST:/opt/gta2/game/data/"
fi
"${SSH[@]}" "$HOST" 'chmod -R a+rX,go-w /opt/gta2/server /opt/gta2/web /opt/gta2/game'

if [[ -n "${GTA2_PASSWORD:-}" || -n "${PUBLIC_URL:-}" ]]; then
  [[ -n "${GTA2_PASSWORD:-}" && -n "${PUBLIC_URL:-}" ]] || { echo "GTA2_PASSWORD and PUBLIC_URL go together" >&2; exit 2; }
  log "settings: /etc/gta2.env (PUBLIC_URL=$PUBLIC_URL)"
  # the password goes on ssh's input, never on a command line
  printf 'HOST=0.0.0.0\nPORT=8790\nGTA2_PASSWORD=%s\nPUBLIC_URL=%s\nWEB_ROOT=/opt/gta2/web\nGAME_ROOT=/opt/gta2/game\nDATA_DIR=/opt/gta2/db\n' \
    "$GTA2_PASSWORD" "$PUBLIC_URL" | "${SSH[@]}" "$HOST" 'umask 077; cat > /etc/gta2.env'
fi

log "gta2.service"
"${SSH[@]}" "$HOST" 'set -e
  [ -f /etc/gta2.env ] || { echo "no /etc/gta2.env yet: give GTA2_PASSWORD and PUBLIC_URL" >&2; exit 1; }
  cat > /etc/systemd/system/gta2.service <<EOF
[Unit]
Description=GTA2 game server (the game, its files, the co-op relay)
After=network-online.target
Wants=network-online.target

[Service]
User=gta2
Group=gta2
EnvironmentFile=/etc/gta2.env
WorkingDirectory=/opt/gta2/server
ExecStart=/opt/node/bin/node /opt/gta2/server/server.mjs
Restart=on-failure
RestartSec=3
MemoryMax=512M
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=/opt/gta2/db

[Install]
WantedBy=multi-user.target
EOF
  systemctl daemon-reload
  systemctl enable gta2.service >/dev/null 2>&1
  systemctl restart gta2.service
  sleep 2
  systemctl is-active gta2.service
  journalctl -u gta2.service -n 3 --no-pager -o cat | grep -v "invite link"'
