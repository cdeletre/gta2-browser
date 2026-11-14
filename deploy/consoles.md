# A game server for the consoles

`GTA 2 Online`, the PortMaster package's online launcher, plays through a GTA2 game server: the
small Node server in `server/`. It relays the matches (every player connects to it, so it works
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
the game on the console). For browsers, mount the game's folder on `/srv/game`; without it, each
browser gets the freeware release from the Internet Archive itself.

> GTA2 is freeware but not freely redistributable. A server with the game files stays private,
> behind its password, for people you know.

## Run it

```sh
mkdir -p db && chmod 777 db     # the container's user saves crews here
docker run -d --name gta2 --restart unless-stopped -p 8790:8790 \
  -e GTA2_PASSWORD='pick-one' -e PUBLIC_URL=https://gta2.example.com \
  -v "$PWD/db:/srv/db" \
  ghcr.io/cdeletre/gta2-server:relay
```

For the `web` image, use `:web`, and add `-v "$PWD/game:/srv/game:ro"` if browsers should
download the game from your server (`game/` holds the game's `data/` folder).

The same with Docker Compose:

```yaml
services:
  gta2:
    image: ghcr.io/cdeletre/gta2-server:relay
    restart: unless-stopped
    ports: ["8790:8790"]
    environment:
      GTA2_PASSWORD: pick-one
      PUBLIC_URL: https://gta2.example.com
    volumes:
      - ./db:/srv/db
```

Settings (environment):

| Variable | Default | What it does |
|---|---|---|
| `GTA2_PASSWORD` | none | The password. Without one, anyone who finds the address can play. |
| `PUBLIC_URL` | `http://0.0.0.0:8790` | The address players use; the invite link is built on it. |
| `PORT` | `8790` | The port inside the container. |

`docker logs gta2` shows the invite link. `https://<your server>/games` shows the rooms and the
matches being played.

## HTTPS

Over the Internet, put HTTPS in front of it, so the password does not travel in clear. Caddy
does it in one file, and gets the certificate itself:

```
gta2.example.com {
	reverse_proxy 127.0.0.1:8790
}
```

With nginx, pass the WebSocket upgrade on (`proxy_http_version 1.1`, `Upgrade` and `Connection`
headers) and raise `proxy_read_timeout` (a match can stay quiet for a while in a menu).

On a home network only, plain `ws://<the machine's address>:8790` works without HTTPS.

## On the consoles

In `ports/gta2/conf/online.ini`, one `[server]` section per server; the lobby lists them in this
order:

```ini
[server]
name = Friends
url = wss://gta2.example.com
password =
```

- `url`: `wss://` behind HTTPS, `ws://address:8790` on a home network.
- `password`: may stay empty, the lobby asks for it with the on-screen keyboard and keeps it.
  An invite link's code also works, as `invite = ...`.
- A certificate from a public authority (Caddy's Let's Encrypt one) works as is. For a
  certificate of your own, set `ca_file` (its authority, a PEM file in `conf/`) or
  `tls_fingerprint` (`sha256:...`, printed in `log.txt` when it fails).

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
tab). Make the package public in its settings on GitHub after the first run.
