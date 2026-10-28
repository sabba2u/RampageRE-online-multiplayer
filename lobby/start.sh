#!/bin/sh
# start.sh - runs the RampageRE lobby (Linux, macOS, a VPS): the games list on TCP 19980. Needs Node.js 20 or newer.
# The first start installs the packages and makes config.json from config.example.json. As a service: docs/NETPLAY.md
# "The lobby" (systemd or pm2).
set -e
cd "$(dirname "$0")"
[ -d node_modules ] || npm install --omit=dev
[ -f config.json ] || cp config.example.json config.json
exec npm start
