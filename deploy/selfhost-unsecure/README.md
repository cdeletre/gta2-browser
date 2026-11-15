# A GTA2 game server without HTTPS (trusted networks only)

The same server as [../selfhost](../selfhost/README.md), without Caddy: the relay image alone,
its port 8790 published. Nothing to set up for certificates, nothing for the clients to trust.

> **No encryption.** The password, the invite link and every match cross the network in clear.
> Use it on a network you trust (at home, over a VPN such as WireGuard or Tailscale), or behind
> an HTTPS proxy of your own. For a server on the Internet, use [../selfhost](../selfhost/README.md).

The images, what they hold and the game files' terms: [../selfhost](../selfhost/README.md#pick-an-image).

## Run it

You need Docker with Compose v2.

1. Copy this folder to the machine (`cp -R deploy/selfhost-unsecure ~/gta2`), and go there.
2. `cp .env.example .env && chmod 600 .env`, then fill in `GTA2_PASSWORD` and `GTA2_HOST`
   (the machine's IP on the network).
3. On Linux: `sudo chown 1000:1000 db` (the server's user saves crews there).
4. `docker compose up -d`, and `docker compose logs relay` for the invite link.

| Setting | Default | What it does |
|---|---|---|
| `GTA2_PASSWORD` | (required) | the players' password |
| `GTA2_HOST` | (required) | the address the players use, for the invite link |
| `GTA2_PORT` | `8790` | the port published on the machine |
| `GTA2_BIND` | `0.0.0.0` | the interface it listens on (one address to keep it off the others) |
| `GTA2_IMAGE` | `relay` | `relay`, `web`, or either with `-<commit>` |
| `GTA2_PUBLIC_URL` | `http://GTA2_HOST:GTA2_PORT` | the invite link's address, behind another proxy |

Browsers (`web` image): `http://<GTA2_HOST>:8790/`.

## Serve the game files

As in [../selfhost](../selfhost/README.md#serve-the-game-files): the contents of the game's
`data/` folder in `gamedata/`, rename `compose.override.disabled.yml` to `compose.override.yml`,
then `docker compose up -d`. Keep such a server for people you know.

## The consoles

In `ports/gta2/conf/online.ini`, a `[server]` section with a `ws://` address and no `ca_file`
(the rest as in [../selfhost](../selfhost/README.md#on-the-consoles)):

```ini
[server]
name = Home
url = ws://192.168.1.20:8790
password =
```

## Behind a proxy of your own

To add HTTPS with nginx or another proxy: set `GTA2_BIND=127.0.0.1` (if the proxy runs on the
same machine) and `GTA2_PUBLIC_URL=https://<its address>`, pass the WebSocket upgrade on
(`proxy_http_version 1.1`, `Upgrade` and `Connection` headers) and raise `proxy_read_timeout`.
