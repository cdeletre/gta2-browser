# GTA2 game server (D21)

A private server for playing with friends. One small Node process
(`server/server.mjs`) does everything behind a password:

- **Serves the game.**
- **Serves the GTA2 game files** (except executables, DLLs and movies). A new player's
  browser downloads them once (373 MB) with a progress bar and keeps them.
- **Relays co-op** over a WebSocket at `/relay`. Players only ever connect to the
  server, so co-op works on every network.

## What friends do

1. Open the invite link you send them. Or open the address and type the password.
2. Press **Play**, or **Play with friends**. The co-op lobby shows a link that logs a
   friend in and opens your room.

This works in desktop Chrome, Edge, Firefox or Brave on Windows, macOS, Linux and
ChromeOS, with nothing to install.

## Hosting: the shared DigitalOcean droplet

The droplet also runs another production app (SkipTheBA), so this project follows
the droplet's rules (`/opt/HOSTING.md` on the server).

**Everything lives in `/opt/gta2/`:**
- `compose.yml` (from `deploy/compose.yml`): project name `gta2`, container
  `gta2-app` on the external `edge` network, 256 MB memory limit,
  `restart: unless-stopped`, log rotation, no published ports;
- `.env`, chmod 600: `GTA2_PASSWORD`, `PUBLIC_URL`;
- `data/web`: the built game (copied from `build/port/release`);
- `data/game`: the game files (copied from `game/`). They're mounted read-only and
  are never in the image.
- `data/db`: saved crews (`crews.json`), the only writable mount. The container's user
  writes it; `/opt/gta2` itself is closed to other users on the droplet.

**The image** `ghcr.io/m1stermo/gta2-server` holds only the Node server. GitHub Actions
builds it (`.github/workflows/server-image.yml`) when `server/` changes, and the droplet
only pulls it.

**HTTPS:** the shared Caddy serves the domain through one file,
`/opt/edge/sites/gta2.caddy`:

```
gta2.example.com {
	reverse_proxy gta2-app:8790
}
```

No compression is configured on purpose. The game files are large, and compressing
them on the shared single CPU would slow the other app.

**Deploy** from this repository:

```bash
# first time (or to change the domain or password)
DOMAIN=gta2.example.com GTA2_PASSWORD='choose-one' scripts/deploy_server.sh user@droplet
# updates
scripts/deploy_server.sh user@droplet
```

The script:
1. checks it's on the right server (`/opt/HOSTING.md` and the `edge` network);
2. copies the game, the game files and `compose.yml` with rsync;
3. writes `.env` through stdin, so the password is never on a command line;
4. runs `docker compose pull && up -d` in `/opt/gta2` only;
5. writes the Caddy site file and reloads Caddy.

It never touches other projects, the firewall or SSH. The invite link is in the
container log: `docker logs gta2-app | grep invite`.

**Before the first deploy:**
- The domain's DNS A record must point at the droplet.
- The droplet must be able to pull `ghcr.io/m1stermo/gta2-server`. Either make the GHCR
  package public (it holds only our server code, no game files), or run
  `docker login ghcr.io` on the droplet with a read-only token.
- The droplet needs about 400 MB of free disk for the game files.

**Load on the shared droplet:**
- The server uses about 40–60 MB of RAM and almost no CPU. The relay passes a few bytes
  per player 30 times a second.
- The main cost is each new player's one-time 373 MB download. That's outbound traffic,
  so it's worth checking against the droplet's monthly transfer allowance.
- If gangs and reputation (D21 stage 3) need storage, they go in SQLite under
  `/opt/gta2/data`, never a database server on the droplet.

## Hosting: a container behind your own nginx

`scripts/deploy_lxc.sh` puts the server on a plain Linux host or container (an LXC on
Proxmox, say) as a systemd service, with no Docker. HTTPS is left to an nginx that runs
elsewhere.

**Everything lives in `/opt/gta2/`:** `server/` (the Node server and `ws`), `web/` (the
built game), `game/` (only the game's `data/` folder: no saves, executables or movies),
and `db/` (saved crews, the only folder the service writes, as user `gta2`). The settings
are in `/etc/gta2.env` (chmod 600). `gta2.service` runs Node from `/opt/node` with
`ProtectSystem=strict` and a 512 MB limit.

```bash
# first time: an official Node linux-x64 .tar.xz, checked against its SHASUMS256.txt
SSH_KEY=~/.ssh/key GTA2_PASSWORD='choose-one' PUBLIC_URL=https://gta2.example.com \
  scripts/deploy_lxc.sh root@host --node node-v22.x-linux-x64.tar.xz \
  --web build/port/release --game game
# updates of the server code (add --web or --game when they change)
SSH_KEY=~/.ssh/key scripts/deploy_lxc.sh root@host
```

The invite link is in `journalctl -u gta2`. The nginx in front:

```nginx
map $http_upgrade $connection_upgrade { default upgrade; '' close; }   # in http {}

server {
    listen 443 ssl;
    http2 on;
    server_name gta2.example.com;
    # ssl_certificate, ssl_certificate_key: Let's Encrypt

    location / {
        proxy_pass http://<the container>:8790;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header Upgrade $http_upgrade;          # the /relay WebSocket
        proxy_set_header Connection $connection_upgrade;
        proxy_read_timeout 1h;                           # a lobby can stay quiet a long time
        proxy_send_timeout 1h;
        proxy_buffering off;                             # inputs at once, big files unbuffered
        proxy_max_temp_file_size 0;
    }
}
```

Only nginx needs to reach port 8790: close it to everything else. Native players
(`gta2-online`) use `server = wss://gta2.example.com` in `conf/net.ini`; a Let's Encrypt
certificate needs nothing more on the consoles.

## Running it on this computer

```bash
cd server && npm ci && cd ..
GTA2_PASSWORD=local-test node server/server.mjs    # http://127.0.0.1:8790
```

Without `GTA2_PASSWORD` the server only listens on this computer.

## The games in progress (`GET /games`)

Behind the login, like everything else. In a browser, `/games` is a page (cards for the rooms,
their live score, the recent matches; refreshed every 10, 30 or 60 s, 60 by default). Scripts
get the JSON (`/games` without `Accept: text/html`, or `/games.json`):

```json
{ "rooms": [ { "room": "ROOM1", "state": "playing", "host": "Kdog",
               "players": [ { "name": "Kdog", "crew": "" }, { "name": "Nina", "crew": "" } ],
               "connected": 2,
               "settings": { "mode": "ffa", "district": "downtown", "killLimit": 10, ... },
               "since": "2026-11-10T16:33:36.393Z", "playingSeconds": 26, "tick": 720,
               "score": { "tick": 600, "confirmed": true, "reporters": 2, "disagree": null,
                          "players": [ { "name": "Kdog", "crew": "", "kills": 1, "deaths": 0, "points": 2000 }, ... ] } } ],
  "recent": [ { "room": "ROOM2", "endedAt": "...", "settings": { ... }, "playingSeconds": 540, "score": { ... } } ],
  "now": "2026-11-10T16:34:02.813Z" }
```

- The relay reads the lobby messages it passes on: the host's roster, a player's hello, start,
  go, and sync every 90 ticks.
- `state`: `lobby`, `starting` (everyone loading), `playing`, or `ended` (no sync or score for
  30 s: the players are on the results). Rooms in play come first.
- `players`: those of the host's roster still connected. A host alone in its room has sent
  nothing yet, so its name is empty until someone joins.
- **The score.** Every 150 ticks (5 s), each player of the match (`gta2-online`, or a browser on
  this server) sends the standings at that tick to `@server`, which is no one in the room, so
  the relay passes it on to no one. Lockstep makes every report of one tick the same, so they
  are compared: `score` is the version most players sent for the latest tick, `confirmed` once
  two agree. Reports that differ (a drift, or a client that lies) set `disagree`. A report
  counts only if it is from a player of the match while it plays, for a multiple of 150, not
  older than the score shown, and not ahead of the match (its sync ticks, or 30 ticks a second
  since go, plus 10 s). Older clients send no score: their rooms show none.
- `recent`: the last 10 matches with a score, for an hour, newest first (a match ends when its
  players come back to the room's lobby or the room empties).
- A room with no one left is gone.

## Settings (environment)

| Variable | Default | Meaning |
|---|---|---|
| `GTA2_PASSWORD` | none | Password. Required for any address but localhost. |
| `PUBLIC_URL` | `http://HOST:PORT` | The address friends use, for the invite link and secure cookies. |
| `HOST`, `PORT` | `127.0.0.1`, `8790` (image: `0.0.0.0`) | Where Node listens. |
| `WEB_ROOT` | `build/port/release` (image: `/srv/web`) | The built game. |
| `GAME_ROOT` | `game/` (image: `/srv/game`) | The extracted GTA2 files. |

## Notes

- **Game files.** They're hosted only behind the password, for a small group of
  friends. This is an owner decision (D21, AGENTS.md). They never go into the repo,
  an image or a public page.
- **Player limit.** Rooms hold up to 12 connections, but the game itself still allows 6
  players (D21 stage 2).
