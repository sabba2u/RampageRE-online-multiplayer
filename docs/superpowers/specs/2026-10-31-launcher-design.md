# RampageRE Launcher: design

## Why

Online play works, but starting it takes command lines, PowerShell and Node.js. The people who will use it are the
owner's kid, the kid's friends, the owner's friends and strangers who download the release. They need a program they
double-click and click big buttons in. It must be foolproof: no command line, no PowerShell, nothing to install, no
networking knowledge.

The launcher is for **private groups**: someone hosts, and their friends join. Each group runs its own lobby, usually
the host's launcher. A public lobby on a VPS comes later and must fit in without changing how anyone plays.

## What it is

`RampageRE-Launcher.exe`, a Windows program written in Rust, in the release zip next to `RampageRE.exe`. It is one exe
with no installer, and it needs no admin rights. It starts the game with command-line flags the game already has.

### The screen

Big, plain buttons:

- **Play**: starts the game offline.
- **Host a game**, with two choices:
  - **On Steam**: see "Steam" below. Friends join through Steam's invite or "Join Game"; there are no codes or servers.
  - **With my own server**: starts the launcher's built-in lobby and then the game as host, listed in that lobby (see
    "Hosting"). The launcher then shows big:
    - the 5-letter **code** (`K7PQ2`), with a Copy button;
    - the **group link** (`rampage://join/K7PQ2@203.0.113.5`), with a Copy button.
- **Join on Steam**: see "Steam" below.
- **Join by code**: a box and a Join button. It takes a 5-letter code (looked up in the remembered group server) or a
  pasted group link (which saves that link's server first).
- **Set up join links**: registers `rampage://` for this Windows user.
  - Before it is set up, the button reads **Set up join links**.
  - Once set up, the button becomes a **big green checkmark** ("Links set up"), with a small **Remove** next to it.
- **The group's server**: shown small, with a way to change it. The launcher remembers the servers it has used (a
  short list), so someone in two groups can switch between them.

### The first run

If `rampage://` isn't registered for this user, the **Set up join links** button flashes, and a small pop-up says:
*"Set up rampage:// links so a link from a friend opens the game and joins straight away."* It has two buttons:

- **Set up**: registers the links.
- **Not now**: closes the pop-up. The button stays gently highlighted until the links are set up.

The pop-up shows only on that first run.

### Join links

- **The launcher is the handler.** Registering writes `HKCU\Software\Classes\rampage` pointing at
  `RampageRE-Launcher.exe "%1"`. No PowerShell script and no console window are involved.
- **A link opens the launcher.** The launcher checks the link, saves the server it names, and starts the game joining.
- **Link forms accepted:**
  - `rampage://join/<code>`: the remembered group server.
  - `rampage://join/<code>@<host[:port]>`: that server, which is then remembered.
  - `rampage://join/<host[:port]>`: a direct address, as today.

  Anything else is refused with a plain message.
- **The game always comes from next to the launcher, never from the link.** Any web page can open a link, so the link
  can only say which game and which server.
- **If the launcher moved,** and the registered path points somewhere else, it offers to fix the registration.
- The PowerShell installer (`lobby/launcher/install.ps1`) stays for people who don't use the launcher.

### Hosting with your own server

1. **The built-in lobby starts** on TCP 19980.
2. **The router is opened:** the launcher asks the router (UPnP) to forward TCP 19980. The game already forwards its
   own UDP port.
3. **The game starts** as `RampageRE.exe --host --lobby http://127.0.0.1:19980`. It lists itself and gets its code,
   as it does today.
4. **The launcher reads the code** from its own lobby and builds the group link from the PC's public address (from
   UPnP, else STUN, else the address the lobby sees).
5. **The launcher stays open while hosting,** because the lobby lives inside it. Closing it asks first ("Your friends
   won't be able to join").

**When the router can't be opened.** Sometimes the launcher can't forward the port: the router has UPnP off, or the
connection is behind a shared address (phone hotspots, Starlink, some fibre). The launcher finds this out within a few
seconds and shows a pop-up:

> **Friends outside your home can't connect to you on this network.**
> Friends on the same home network can still join with your code.
> **[Host on Steam instead]** **[Keep hosting for my home network]**

**The Windows Firewall prompt** (my call). The lobby's listener, and the game's socket, make Windows ask the first time.
Avoiding that would need admin rights, and the game already causes the same prompt today. So just before it appears,
the launcher says: "Windows will ask whether RampageRE may use the network: click **Allow**."

**Code vs. link** (my call, following "each group's host runs its own server"):
- The **group link** is the main thing to share.
- A bare code works while the saved server is the one hosting.
- When the saved server doesn't answer: "Can't reach your group's server: ask your host for a new link."

**Matching versions** (my call):
- The launcher works out the game's build ID itself, by hashing `RampageRE.exe` the same way `net.c` does.
- Before joining by code or link, it asks the lobby for the host's build and compares the two.
- If they differ: "Your friend has a different version of RampageRE: you both need the same release." No game is
  started.

### Steam

**Why Spacewar.** The launcher explains this in plain words on a "What's Spacewar?" card, shown the first time either
Steam button is used and from a small **?** after that:

> Steam online play only works for games that have their own Steam app. RampageRE doesn't have one (that needs a paid
> Steam listing). Valve gives every game maker a free test app called **Spacewar** (app 480). Game developers use it to
> test their multiplayer through Steam before their own game has a store page, and RampageRE uses the same technique.
> Steam treats RampageRE as Spacewar, so you and your friends each need Spacewar in your Steam library. It's free and
> tiny, and you never have to play it. Your friends list will show you as "playing Spacewar".

**Checks, on both Steam buttons, before the game starts.** Each failed check is one plain line with a button that fixes
it:
- **Steam is running** (`steam.exe` is running): "Steam isn't running." **[Open Steam]**
- **Steam's files are next to the game** (`steam_api64.dll`, `steam_appid.txt`): "This copy is missing Steam's files:
  download the release again."
- **Spacewar is installed** (Steam's own records, under `HKCU\Software\Valve\Steam\Apps\480`): "You need Spacewar
  (free) in your Steam library." **[Get Spacewar]** opens `steam://install/480`.

**Hosting on Steam:**
- The button starts the game with `--steam-host`, which makes a friends-only Steam lobby.
- The launcher then shows a card: **"Press Shift+Tab in the game to open Steam, then click Invite to choose your
  friends."** The game's panel (Insert) also has an Invite button.
- The **Shift+Tab** reminder also shows on the router pop-up's "Host on Steam instead" path.

**Joining on Steam:**
- The button starts RampageRE waiting in its menu. It must be running first: if a friend clicks Join Game while it
  isn't, Steam starts the real Spacewar instead.
- The launcher then shows an instruction card: **"Open your Steam friends list, right-click your friend, then click
  Join Game"** (or accept their invite in Steam chat). Next to it is an illustration of a friends list with a
  right-click menu and "Join Game" highlighted.
- The illustration is our own drawing, not a Steam screenshot: no Valve logo or real interface.

### The built-in lobby

The lobby is rewritten in Rust inside the launcher and answers the game exactly as the Node lobby does today. It is
plain HTTP; this release uses no Colyseus rooms. It has:

- **Routes:**
  - `POST /games`
  - `PUT /games/<code>`
  - `DELETE /games/<code>`
  - `GET /games`
  - `GET /games.txt`
  - `GET /games/<code>` and `/games/<code>.txt`
  - `POST /games/<code>/knock` and `GET /games/<code>/knocks`
  - `GET /health`
- **The same rules:**
  - codes;
  - the token;
  - the heartbeat (10 s), and expiry (30 s);
  - the rate limits;
  - the per-asker join address (LAN, localhost or public);
  - STUN and knocks.

  The reference is `lobby/src/Games.ts` and `lobby/src/http.ts`.
- **Tests** (my call): `lobby/test/games.test.ts`'s cases are rewritten as Rust unit tests, with a fake clock and fake
  source addresses. A shared HTTP check, needing no fake time, is run against both the Node and the Rust lobby so the two
  can't drift apart.

### The Node lobby

`lobby/` still ships, unchanged, for running a lobby on Linux, a VPS, Fly.io and the like. The release includes it.

## Goals

- A child can host or join without help.
- Every action is one click, plus at most a typed code.
- No admin prompts, no console windows, no PowerShell, no Node.js for players.

## Non-goals (for now)

- A public VPS lobby. Later it becomes the launcher's default server, and nothing else changes.
- A launcher for Linux or macOS.
- Accounts, friends lists or chat in the launcher.
- Changing the game's online play itself.

## Anti-goals: a failure even if it works

- Anything that asks a player to type a command, open a port by hand or install something.
- A link that can make the launcher run anything other than the game next to it.
- The launcher's lobby disagreeing with the Node lobby on anything the game sees.

## Constraints

- **Windows 10/11, and per-user only:** registry under HKCU, data in `%LOCALAPPDATA%\RampageRE`.
- **The game is only started, never changed,** except for any small flag the launcher truly needs, which must keep the
  offline regression hash `CB88DA7AB444D79F`.
- **The host and the guests must run the same build.** The launcher shows the game's version.
- **Steam currently uses the Spacewar app (480).** For Steam hosting to work from the release, the zip must also carry
  `steam_api64.dll` and `steam_appid.txt`. Players need Steam running and Spacewar owned (it is free). Our own Steam app
  ID (a $100 listing) would later make invites say RampageRE: a non-goal for now.

## Left to the builder

- The GUI library (my call: egui/eframe, a single static exe), the UPnP and registry crates, and the look within "big
  and plain".
- Where the remembered servers and settings are stored, in `%LOCALAPPDATA%\RampageRE`.
- How the launcher reads the host's code from its own lobby (it is in-process, so it can be read directly).
- Building and packaging in CI: add a cargo build to `.github/workflows/build.yml`, then add the launcher, `lobby/`
  (without `node_modules`), `steam_api64.dll` and `steam_appid.txt` to the Windows zip.
