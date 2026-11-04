#!/bin/sh
# Sets up RampageRE links (rampage://) for this Linux desktop user, so the lobby's games page can start the game:
# a rampage.desktop in ~/.local/share/applications that runs the game with --join %u, made the handler of
# x-scheme-handler/rampage (xdg-mime). No root needed. The game checks the link itself (src/netlobby.c): it takes only
# rampage://join/<code> and rampage://join/<host[:port]>.
#   sh install-linux.sh [path to the rampage binary] [lobby, e.g. http://host:19980]
#   sh install-linux.sh --uninstall
set -e
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
desktop="$apps/rampage.desktop"

if [ "$1" = "--uninstall" ]; then
  rm -f "$desktop"
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" || true
  echo "RampageRE links removed."
  exit 0
fi

exe="$1"
lobby="${2:-__LOBBY__}"
[ "$lobby" = "__LOBBY__" ] && lobby=""
if [ -z "$exe" ]; then
  printf "Where is RampageRE? The full path to the rampage binary: "
  read -r exe
fi
case "$exe" in /*) ;; *) exe="$(pwd)/$exe" ;; esac
if [ ! -x "$exe" ]; then echo "Not a program: $exe" >&2; exit 1; fi
case "$lobby" in ""|http://*|https://*) ;; *) echo "Not a lobby address: $lobby" >&2; exit 1 ;; esac
case "$exe$lobby" in *[\"\'\ \\\$\`]*) echo "Spaces and quotes in the path aren't supported; move the game." >&2; exit 1 ;; esac

mkdir -p "$apps"
lobbyarg=""
[ -n "$lobby" ] && lobbyarg="--lobby $lobby "
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=RampageRE
Comment=Join a RampageRE game from a rampage:// link
Exec=$exe $lobbyarg--join %u
Path=$(dirname "$exe")
Terminal=false
NoDisplay=true
MimeType=x-scheme-handler/rampage;
EOF
xdg-mime default rampage.desktop x-scheme-handler/rampage
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" || true
echo "RampageRE links are set up: $exe${lobby:+, lobby $lobby}."
echo "Back on the games page, Join now starts the game. (--uninstall removes this.)"
