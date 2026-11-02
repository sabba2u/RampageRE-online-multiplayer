#!/bin/sh
# install.sh - the RampageRE room server as an always-on service on a Linux VPS (Ubuntu / Debian; Oracle's free VM).
# Run from the lobby folder as root:   sudo sh deploy/install.sh
# What it does: Node.js 20 if missing, a "rampage" user, the server copied to /opt/rampage-lobby, its packages, the
# systemd service (restarts on a crash, starts at boot) and a health check every minute, TCP 19980 opened in this
# machine's own firewall. The cloud's firewall (Oracle: the subnet's security list) is opened by hand: docs/HOSTING.md.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEST=/opt/rampage-lobby
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

echo "== the rampage user and $DEST"
id rampage >/dev/null 2>&1 || useradd --system --home "$DEST" --shell /usr/sbin/nologin rampage
mkdir -p "$DEST"
# the server's files, not its packages or a test config (config.json kept if there is one already)
for f in package.json package-lock.json tsconfig.json config.example.json src public; do
    cp -r "$HERE/$f" "$DEST/"
done
[ -f "$DEST/config.json" ] || cp "$DEST/config.example.json" "$DEST/config.json"
chown -R rampage:rampage "$DEST"
echo "== its packages"
su -s /bin/sh rampage -c "cd '$DEST' && npm ci --omit=dev || npm install --omit=dev"

echo "== the service"
cp "$HERE/deploy/rampage-lobby.service" "$HERE/deploy/rampage-lobby-health.service" \
   "$HERE/deploy/rampage-lobby-health.timer" /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now rampage-lobby.service
systemctl enable --now rampage-lobby-health.timer

echo "== TCP $PORT in this machine's firewall"
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q active; then
    ufw allow "$PORT/tcp"
fi
if command -v iptables >/dev/null 2>&1 && ! iptables -C INPUT -p tcp --dport "$PORT" -j ACCEPT 2>/dev/null; then
    # (Oracle's Ubuntu images reject everything but SSH in iptables: the rule goes before their REJECT)
    iptables -I INPUT 5 -p tcp --dport "$PORT" -m state --state NEW -j ACCEPT 2>/dev/null ||
        iptables -I INPUT -p tcp --dport "$PORT" -j ACCEPT
    command -v netfilter-persistent >/dev/null 2>&1 && netfilter-persistent save || true
fi

sleep 3
if curl -fsS --max-time 5 "http://127.0.0.1:$PORT/health" | grep -q rampage_lobby; then
    echo "== running: http://<this server's address>:$PORT/ lists the rooms"
else
    echo "== not answering yet: journalctl -u rampage-lobby -n 50"
fi
