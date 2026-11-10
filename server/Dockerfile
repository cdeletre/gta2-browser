# The GTA2 game server (D21): only the Node server. The built game and the game
# files are mounted from the host (/opt/gta2/data), so the image stays tiny and
# the game files never go into an image registry.
FROM node:22-alpine
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev --no-audit --no-fund && npm cache clean --force
COPY server.mjs crews.mjs games-page.mjs ./
ENV HOST=0.0.0.0 PORT=8790 WEB_ROOT=/srv/web GAME_ROOT=/srv/game DATA_DIR=/srv/db NODE_ENV=production
USER node
EXPOSE 8790
CMD ["node", "server.mjs"]
