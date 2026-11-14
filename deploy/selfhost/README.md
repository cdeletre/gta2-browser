# Host your own GTA2 game server

For the PortMaster consoles (`GTA 2 Online`, the package's online launcher) and, with the `web`
image, for browsers too. The server is the small Node server in `server/`. It relays the matches (every player connects to it, so it works
on every network), keeps out strangers with a password, and shows the games on `/games`.
Anyone can run one for their group, on any always-on machine the players can reach: a VPS, a home
server or NAS, a Raspberry Pi (3 or newer, 64-bit OS).

`GTA 2 LAN` needs none of this: consoles on the same network play together directly.

GitHub Pages cannot do it: it only serves files, and the consoles need the server's `/relay`.

## Pick an image

Two images, both built from this branch for amd64 and arm64 (`docker/server.Dockerfile`,
`.github/workflows/server-images-native.yml`):

| Image | What's in it | For |
|---|---|---|
| `ghcr.io/cdeletre/gta2-server:relay` | the server only, about 50 MB of RAM | consoles only |
| `ghcr.io/cdeletre/gta2-server:web` | the server plus the browser game of the same commit | consoles and browsers together |

Add `-<commit>` (say `:web-1a2b3c4`) to stay on one version. Every player of a match needs the
same multiplayer version, so update the server when you update the consoles' package.

Browsers and consoles only play together when the browser game comes from this branch (the
`web` image). The site of [M1sterMo/gta2](https://github.com/M1sterMo/gta2) or its forks is
built from upstream `main`, which the consoles don't accept.

Neither image holds game files. Consoles never need them from the server (the package unpacks
the game on the console). Browsers get Rockstar's 2004 release from the Internet Archive themselves
(346 MB, once, kept in the browser), unless the server has the game files ("Serve the game files",
below).

> GTA2 belongs to Rockstar Games / Take-Two Interactive and may not be redistributed. A server with the game files stays private,
> behind its password, for people you know.

## Run it with HTTPS (recommended)

This folder starts the server and [Caddy](https://caddyserver.com) in front of it, for HTTPS.
You need Docker with Compose. Copy the folder to the server, then:

```sh
cp .env.example .env && chmod 600 .env   # then fill in GTA2_PASSWORD and GTA2_HOST
sudo chown 1000:1000 db                  # Linux only: the server's user writes the crews there
docker compose up -d
./ca.sh                                  # internal certificates: the authority to trust
docker compose logs relay                # the invite link
```

What `.env` sets (`.env.example` explains each one):

| Setting | Default | What it does |
|---|---|---|
| `GTA2_PASSWORD` | required | The players' password. |
| `GTA2_HOST` | required | The address players use: the machine's IP on your network, or a name. |
| `GTA2_HTTPS_PORT` | `443` | The HTTPS port (8443 if 443 is taken). |
| `GTA2_HTTP_PORT` | `80` | Plain HTTP: redirects to HTTPS, Let's Encrypt's checks, `/ca.crt`. |
| `GTA2_TLS` | `internal` | `internal`: Caddy's own authority. Your e-mail address: Let's Encrypt. |
| `GTA2_SHARE_CA` | `no` | `yes`: the authority's certificate at `http://<host>/ca.crt`. |
| `GTA2_IMAGE` | `relay` | `web` for the browser game too; `relay-<commit>` to stay on one version. |
| `GTA2_PUBLIC_URL` | `https://<host>:<port>` | The invite link's address, when behind another proxy. |

The server is reached only through Caddy: its own port (8790) is not published, so the
password never travels in clear.

What it keeps:
- `db/`: the saved crews (`crews.json`): Gang Wars results reported by browsers, a match of a
  minute or more. Consoles don't report them yet, so with consoles only it stays empty. On
  Linux, the server runs as uid 1000 and must own it (`sudo chown 1000:1000 db`); otherwise the
  log says `crews: could not save`.
- Caddy's certificates and its authority, in the Docker volume `gta2-selfhost_caddy-data`.
  `docker compose down` keeps it; `down -v` deletes it, and a new authority would have to be
  trusted again everywhere.

### Serve the game files (browsers)

With `GTA2_IMAGE=web`, browsers can get the game from your server instead of the Internet
Archive:

1. Copy the contents of the game's `data/` folder into `gamedata/` (`gamedata/Audio`,
   `gamedata/wil.gmp`...). Executables, DLLs and movies are never served.
2. Rename `compose.override.disabled.yml` to `compose.override.yml`. Docker Compose reads it on
   its own; it mounts `gamedata/` in the server, read-only.
3. `docker compose up -d`.

The page asks the server (`/config.json`, `gameFiles`) and falls back to the Internet Archive when
`gamedata/` is empty or not mounted. A browser that already has the game keeps its copy.

### Certificates

- **Internal (`GTA2_TLS=internal`, the default).** Works with an IP address and with no
  Internet access. Caddy makes its own authority (valid about 10 years) and signs the server's
  certificate with it (renewed every 12 hours, on its own). The clients must trust the
  authority once: `./ca.sh` saves it as `gta2-ca.pem` and prints its SHA-256 fingerprint.
  With `GTA2_SHARE_CA=yes`, phones and browsers can also download it from
  `http://<host>/ca.crt`; compare its fingerprint with `./ca.sh`'s before trusting it.
- **Let's Encrypt (`GTA2_TLS=you@example.com`).** `GTA2_HOST` must be a public name pointing at
  the server, with ports 80 and 443 open from the Internet. Nothing to install on the clients.

### Trust the authority (internal certificates)

- **Consoles:** copy `gta2-ca.pem` to `ports/gta2/conf/`, and add `ca_file = gta2-ca.pem` to the
  server's section of `online.ini` (below). Not `tls_fingerprint`: it pins the server's own
  certificate, which changes every 12 hours.
- **Browsers** (`GTA2_IMAGE=web`): install `gta2-ca.pem` as a trusted authority, then restart
  the browser. macOS: open it, then *Always Trust* in Keychain Access. Windows: *Trusted Root
  Certification Authorities*. Firefox keeps its own list: Settings, Certificates, Authorities,
  Import. Android and iOS: install it as a CA certificate (on iOS, also turn on full trust for it).

## Run it without HTTPS (home network only)

```sh
docker run -d --name gta2 --restart unless-stopped -p 8790:8790 \
  -e GTA2_PASSWORD='pick-one' -v gta2-db:/srv/db \
  ghcr.io/cdeletre/gta2-server:relay
```

Consoles connect to `ws://<the machine's IP>:8790`. The password crosses the network in clear,
so keep this to a network you trust. Behind a proxy of your own (nginx): pass the WebSocket
upgrade on (`proxy_http_version 1.1`, `Upgrade` and `Connection` headers) and raise
`proxy_read_timeout`.

For browsers to get the game from your server (the `web` image), add
`-v "$PWD/gamedata:/srv/game/data:ro"`, `gamedata/` holding the contents of the game's `data/`.

## On the consoles

In `ports/gta2/conf/online.ini`, one `[server]` section per server; the lobby lists them in this
order:

```ini
[server]
name = Friends
url = wss://192.168.1.20:443
password =
ca_file = gta2-ca.pem
```

- `url`: `wss://<GTA2_HOST>:<GTA2_HTTPS_PORT>` behind HTTPS, `ws://<IP>:8790` without.
- `password`: may stay empty, the lobby asks for it with the on-screen keyboard and keeps it.
  An invite link's code also works, as `invite = ...`.
- `ca_file`: only for internal certificates (above). Leave it empty with Let's Encrypt.

In the lobby: pick the server, A to connect, then host a room or join one.

## Build the images yourself

From the repository root (Docker with buildx):

```sh
docker buildx build -f docker/server.Dockerfile --target relay -t gta2-server:relay .
docker buildx build -f docker/server.Dockerfile --target web   -t gta2-server:web .
```

The `web` build fetches Emscripten 6.0.12, `gta2_re` at its pinned commit and `unshield`, and
compiles the game: allow a few GB of disk and 10 to 30 minutes. Only the server itself is built
for the target platform, so an arm64 image builds as fast as an amd64 one.

On a fork, `.github/workflows/server-images-native.yml` pushes the two images to
`ghcr.io/<you>/gta2-server` on every push to `native-multiplayer` (or by hand, from the Actions
tab). If the package comes out private, make it public in its settings on GitHub.
