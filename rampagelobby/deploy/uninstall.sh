#!/bin/sh
# uninstall.sh - takes out what install.sh put in: RampageLobby's service and health check, /opt/rampagelobby (its
# config.json too), the rampagelobby user and the firewall's TCP 19980 rule (an old rampage-lobby install's as well).
# Node.js stays (other things may use it): sudo apt remove nodejs, and /etc/apt/sources.list.d/nodesource.list, if not.
# A home router's port forward or a cloud's firewall rule is taken out by hand.
# Run as root:   sudo sh deploy/uninstall.sh
NAME=rampagelobby
PORT=19980

if [ "$(id -u)" != 0 ]; then echo "Run as root: sudo sh deploy/uninstall.sh"; exit 1; fi

echo "== the service"
for n in "$NAME" rampage-lobby; do
    systemctl disable --now "$n.service" "$n-health.timer" 2>/dev/null
    rm -f "/etc/systemd/system/$n.service" "/etc/systemd/system/$n-health.service" "/etc/systemd/system/$n-health.timer"
done
systemctl daemon-reload

echo "== /opt/$NAME and its user"
rm -rf "/opt/$NAME" /opt/rampage-lobby
id "$NAME" >/dev/null 2>&1 && userdel "$NAME"
id rampage >/dev/null 2>&1 && [ "$(getent passwd rampage | cut -d: -f6)" = /opt/rampage-lobby ] && userdel rampage

echo "== TCP $PORT in this machine's firewall"
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q active; then
    ufw delete allow "$PORT/tcp" 2>/dev/null
fi
if command -v iptables >/dev/null 2>&1; then
    while iptables -D INPUT -p tcp --dport "$PORT" -m state --state NEW -j ACCEPT 2>/dev/null; do :; done
    while iptables -D INPUT -p tcp --dport "$PORT" -j ACCEPT 2>/dev/null; do :; done
    command -v netfilter-persistent >/dev/null 2>&1 && netfilter-persistent save
fi
echo "== RampageLobby is gone (Node.js left in place)"
