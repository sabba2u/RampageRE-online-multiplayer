# Hosting: what runs where

Online play has **two different things** that both get called a "server". Knowing which is which explains every
hosting option, what each port is for, and what has to stay open while you play.

## 1. The game host: inside RampageRE.exe

The host's own **RampageRE.exe** runs the match. Friends' games connect straight to it, and it keeps everyone in
step: the monsters, buildings, score, menus and chat. It also owns the player slots, and with them kicks and bans.

- Code: `src/net.c` and the other `src/net*.c` files (the C game).
- Network: **UDP 27960**. The game asks your router to forward it (UPnP) and says in the Insert panel's log whether
  that worked. Over Steam it uses Valve's relay instead: no port at all.
- It needs nothing else to run. The game's **Insert** panel → **Host a game** does exactly this.

**Every** way of hosting uses it. All the gameplay traffic goes between the host's game and the guests' games and
nowhere else.

## 2. The room server: not in RampageRE.exe

The room server is a small web server that hands out **room codes**. A host's game lists itself there and gets a
5-letter code such as `K7PQ2`. A friend who types that code (or clicks a `rampage://join/K7PQ2@…` link) is told
where to connect, and then their game connects to the host's game directly, as above. The room server never
carries any gameplay.

It also gives the launcher things the bare game can't:
- the room code and the links to send;
- the **Players** list in the host's launcher, with **Mute**, **Kick** and **Ban**;
- the guest's launcher showing the room and who is in it;
- a version check before joining (a friend with another build gets a plain message);
- help through stricter home routers: the game announces itself to the server, which helps friends connect.

There are two copies of the same room server, which speak the same protocol (`lobby/test/conformance.test.ts`
checks both):

| Where | What | When it runs |
|---|---|---|
| **Inside the launcher** (`launcher/src/lobby/`, Rust) | **Host an internet game → Run an Internet Multiplayer Server** starts it on your PC | while the launcher is open (closing it while hosting asks first) |
| | `RampageRE-Launcher.exe --serve-lobby [port]`: the room server alone, no window | until you stop it |
| **Node.js** (`lobby/`) | `lobby\start.bat` / `lobby/start.sh`: for Linux, or an always-on VPS (the public server planned for later) | as long as Node runs it |

Network: **TCP 19980** (the launcher asks your router to forward it while you host).

The game only *talks* to a room server (`src/netlobby.c`): it lists itself, sends its player list every second,
and hears back the launcher's mutes, bans and kicks. It does not contain one.

## The launcher's buttons, in these terms

| Button | The game host (in RampageRE.exe) | A room server | Friends join with |
|---|---|---|---|
| **Host Local (LAN)** (up to 4 players) | yes, started hosting (`--host`) | no | your address in the game's Insert panel → **Join**: `127.0.0.1` on this PC, your LAN address on the same Wi-Fi, your public IP from the internet (when the router forwarded UDP 27960) |
| **Run an Internet Multiplayer Server** | yes | yes, inside the launcher | the room code or the link the launcher shows |
| **Host on Steam** | yes, over Steam | no | Steam: Shift+Tab → Invite, or right-click you → **Join Game** |
| **Play Local** / **Play Alone Locally** (1, 3 or 4) | no: a game on this PC (the game's own players screen: 1 to 4, humans or computer players). You can still host later from the game's **Insert** panel | no | |

- **Host Local (LAN)** is the game alone: the launcher is only a shortcut and can be closed.
- **Run an Internet Multiplayer Server** is the game *plus* the launcher's room server. Keep the launcher open while you host.
- **Host on Steam** is the game alone, using Steam's network instead of your router.

## Joining, in these terms

- **Join Local (LAN)**: your game joins `127.0.0.1` (a game hosted on this same PC). Something must be hosting there
  first: Host Local (LAN), or the other window's Insert → **Host a game**.
- **Join a room**: **Connect** to the host's server (their address, or paste their link), then **Join** with the
  room code. The launcher asks that room server where the host's game is, then your game joins it.
- **A link** (`rampage://join/K7PQ2@192.168.7.105`): the server and the room in one; clicking it does both.
- **Join on Steam**: Steam tells your already-running game where your friend is.

## Ports at a glance

| Port | What | Needed for |
|---|---|---|
| UDP 27960 | the game host (RampageRE.exe) | every hosted game not on Steam |
| TCP 19980 | the room server (launcher or Node) | room codes and links from outside your home |

Online play is up to 4 players: the host and three friends (Host on Steam: 2). More: [NETPLAY.md](NETPLAY.md).
