#!/bin/sh
# install.sh - RampageLobby (the RampageRE room server) as an always-on service on Linux: a VPS (Ubuntu / Debian, a
# DigitalOcean droplet, Oracle's free VM) or a Raspberry Pi (Raspberry Pi OS, a Pi 3 or newer).
# Run from the rampagelobby folder as root:   sudo sh deploy/install.sh        (again to update it)
# What it does: Node.js 20 if missing, a "rampagelobby" user, the server copied to /opt/rampagelobby, its packages,
# the systemd service (restarts on a crash, starts at boot) and a health check every minute, TCP 19980 opened in this
# machine's own firewall. A cloud's firewall (DigitalOcean's Cloud Firewall, Oracle's security list) or a home
# router's port forward is set by hand: docs/HOSTING.md. Undone by deploy/uninstall.sh. In Docker instead: Dockerfile.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
NAME=rampagelobby
DEST=/opt/$NAME
PORT=19980

if [ "$(id -u)" != 0 ]; then echo "Run as root: sudo sh deploy/install.sh"; exit 1; fi

if ! command -v node >/dev/null 2>&1 || [ "$(node -p 'process.versions.node.split(".")[0]')" -lt 20 ]; then
    echo "== Node.js 20"
    apt-get update
    apt-get install -y ca-certificates curl
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
fi
command -v curl >/dev/null 2>&1 || apt-get install -y curl

# an install from before the name RampageLobby (rampage-lobby, user rampage): stopped and taken out, its config kept
if [ -f /etc/systemd/system/rampage-lobby.service ] || [ -d /opt/rampage-lobby ]; then
    echo "== the old rampage-lobby install: replaced"
    systemctl disable --now rampage-lobby.service rampage-lobby-health.timer 2>/dev/null || true
    rm -f /etc/systemd/system/rampage-lobby.service /etc/systemd/system/rampage-lobby-health.service \
          /etc/systemd/system/rampage-lobby-health.timer
    mkdir -p "$DEST"
    [ -f /opt/rampage-lobby/config.json ] && [ ! -f "$DEST/config.json" ] && cp /opt/rampage-lobby/config.json "$DEST/"
    rm -rf /opt/rampage-lobby
    id rampage >/dev/null 2>&1 && userdel rampage 2>/dev/null || true
fi

echo "== the $NAME user and $DEST"
id "$NAME" >/dev/null 2>&1 || useradd --system --home "$DEST" --shell /usr/sbin/nologin "$NAME"
mkdir -p "$DEST"
# the server's files, not its packages or a test config (config.json kept if there is one already)
for f in package.json package-lock.json tsconfig.json config.example.json src public launcher; do
    cp -r "$HERE/$f" "$DEST/"
done
[ -f "$DEST/config.json" ] || cp "$DEST/config.example.json" "$DEST/config.json"
chown -R "$NAME:$NAME" "$DEST"
echo "== its packages"
su -s /bin/sh "$NAME" -c "cd '$DEST' && npm ci --omit=dev || npm install --omit=dev"

echo "== the service"
cp "$HERE/deploy/$NAME.service" "$HERE/deploy/$NAME-health.service" "$HERE/deploy/$NAME-health.timer" \
   /etc/systemd/system/
systemctl daemon-reload
systemctl enable "$NAME.service" "$NAME-health.timer"
systemctl restart "$NAME.service"                    # (an update: the new files)
systemctl start "$NAME-health.timer"

echo "== TCP $PORT in this machine's firewall"
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q active; then
    ufw allow "$PORT/tcp"
fi
if command -v iptables >/dev/null 2>&1 && ! iptables -C INPUT -p tcp --dport "$PORT" -m state --state NEW -j ACCEPT 2>/dev/null &&
   ! iptables -C INPUT -p tcp --dport "$PORT" -j ACCEPT 2>/dev/null; then
    # (Oracle's Ubuntu images reject everything but SSH in iptables: the rule goes before their REJECT)
    iptables -I INPUT 5 -p tcp --dport "$PORT" -m state --state NEW -j ACCEPT 2>/dev/null ||
        iptables -I INPUT -p tcp --dport "$PORT" -j ACCEPT
    command -v netfilter-persistent >/dev/null 2>&1 && netfilter-persistent save || true
fi

sleep 3
if curl -fsS --max-time 5 "http://127.0.0.1:$PORT/health" | grep -q rampage_lobby; then
    echo "== RampageLobby is running: http://<this server's address>:$PORT/ lists the rooms"
else
    echo "== not answering yet: journalctl -u $NAME -n 50"
fi
