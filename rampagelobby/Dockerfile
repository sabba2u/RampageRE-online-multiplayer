# RampageLobby, the RampageRE room server, in Docker: any machine with Docker (a VPS, a Raspberry Pi 3 or newer, Fly.io).
#   docker compose up -d --build          (docker-compose.yml: port 19980, restarted after a crash or a reboot)
# or by hand:
#   docker build -t rampagelobby .
#   docker run -d --name rampagelobby --restart unless-stopped -p 19980:19980 rampagelobby
# Settings: PORT, RAMPAGE_LOBBY_PUBLIC (this machine's address on the internet), or a config.json mounted at
# /app/config.json (config.example.json's fields). The rooms are kept in memory: a restart forgets them (the hosting
# games list themselves again within seconds).
FROM node:20-slim
WORKDIR /app
COPY package.json package-lock.json tsconfig.json ./
RUN npm ci --omit=dev && npm cache clean --force
COPY src ./src
COPY public ./public
COPY launcher ./launcher
COPY config.example.json ./config.json
ENV NODE_ENV=production PORT=19980
EXPOSE 19980
USER node
HEALTHCHECK --interval=60s --timeout=5s --start-period=20s \
  CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||19980)+'/health').then(r=>r.text()).then(t=>process.exit(t.startsWith('rampage_lobby')?0:1)).catch(()=>process.exit(1))"
CMD ["npx", "tsx", "src/index.ts"]
