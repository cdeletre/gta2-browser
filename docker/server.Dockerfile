# syntax=docker/dockerfile:1
#
# The game server (server/server.mjs) as an image, in two flavours, from the repository root:
#
#   relay  the Node server only: the multiplayer relay (/relay), the password and invite link,
#          saved crews, the /games page. gta2-online on the consoles needs nothing else.
#   web    the same plus the browser game, built from this same commit, so browsers and consoles
#          agree on the multiplayer version (COOP_VERSION / kCoopVersion) and can play together.
#
#   docker buildx build -f docker/server.Dockerfile --target relay -t gta2-server:relay .
#   docker buildx build -f docker/server.Dockerfile --target web   -t gta2-server:web .
#
# Neither holds any game file. The game's data/ folder can be mounted on /srv/game (read-only)
# for browsers to download it from the server; without it they get the freeware release from the
# Internet Archive themselves. Consoles never download it: the PortMaster package unpacks it.
# How to run it: deploy/consoles.md. docker/server.Dockerfile.dockerignore keeps the build
# context to what the build reads (never game/, build/ or research/).

ARG NODE_IMAGE=node:22-alpine

# ---- relay: the Node server ----
FROM ${NODE_IMAGE} AS relay
WORKDIR /app
COPY server/package.json server/package-lock.json ./
RUN npm ci --omit=dev --no-audit --no-fund && npm cache clean --force
COPY server/server.mjs server/crews.mjs server/games-page.mjs ./
# /srv/web stays empty in this flavour (the server answers 404 there; /relay and /games work)
RUN mkdir -p /srv/web /srv/game /srv/db && chown node:node /srv/db
ENV HOST=0.0.0.0 PORT=8790 WEB_ROOT=/srv/web GAME_ROOT=/srv/game DATA_DIR=/srv/db NODE_ENV=production
USER node
EXPOSE 8790
CMD ["node", "server.mjs"]

# ---- web-build: the browser game (WebAssembly), built once on the build machine ----
# Its output is the same on every platform, so it runs on $BUILDPLATFORM (no emulation for arm64).
FROM --platform=$BUILDPLATFORM debian:bookworm-slim AS web-build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git python3 xz-utils bzip2 \
    && rm -rf /var/lib/apt/lists/*
# The toolchain and sources README.md "Build it from source" names, at the same versions
ARG EMSDK_VERSION=6.0.12
ARG GTA2_RE_URL=https://github.com/CriminalRETeam/gta2_re
ARG GTA2_RE_PIN=02de8beeb766644417f195d360eba33af0ee491f
ARG UNSHIELD_URL=https://github.com/twogood/unshield
ARG UNSHIELD_PIN=0d1e7cbac7986fbd5aa050da8249a27428fee057
WORKDIR /src
RUN git clone -q --depth 1 https://github.com/emscripten-core/emsdk research/toolchains/emsdk \
    && research/toolchains/emsdk/emsdk install "${EMSDK_VERSION}" \
    && research/toolchains/emsdk/emsdk activate "${EMSDK_VERSION}" \
    && rm -rf research/toolchains/emsdk/downloads
# One commit each, fetched by its hash (build_port.py checks out the pinned gta2_re commit)
RUN fetch() { git init -q "$3" && git -C "$3" fetch -q --depth 1 "$1" "$2" && git -C "$3" checkout -q FETCH_HEAD; } \
    && fetch "${GTA2_RE_URL}" "${GTA2_RE_PIN}" research/gta2_re \
    && fetch "${UNSHIELD_URL}" "${UNSHIELD_PIN}" research/toolchains/unshield
COPY port/ port/
COPY web/ web/
COPY scripts/build_port.py scripts/build_unshield_wasm.sh scripts/
RUN git config --global user.email port@localhost && git config --global user.name "GTA2 port" \
    && python3 scripts/build_port.py --preset release --prepare \
    && mkdir /out \
    && cd build/port/release \
    && for f in *; do case "$f" in obj|objects.rsp|old|old-local|dev) ;; *) cp -a "$f" /out/ ;; esac; done

# ---- web: the relay plus the browser game ----
FROM relay AS web
COPY --from=web-build /out /srv/web
