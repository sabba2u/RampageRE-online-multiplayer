# Online play (port extra, work in progress)

Two players on two PCs: the **host** (player 1) runs the game; the **guest** (player 2) plays on their own PC and sees
the host's game. A guest who drops out keeps their place a minute, a 1 player game can be joined, and the host can
re-synchronize everyone: "Player slots" and "Re-synchronizing" below. Design: [superpowers/specs/2026-10-27-online-multiplayer-design.md](superpowers/specs/2026-10-27-online-multiplayer-design.md);
plan and status: [superpowers/plans/2026-10-27-online-multiplayer-phase1.md](superpowers/plans/2026-10-27-online-multiplayer-phase1.md).

## Playing

Both players need the same build of RampageRE and their own disc image.

- Host: start a 2-player game, e.g. `RampageRE-dev.exe Chicago 1 --players 2 --monster George --monster2 Lizzie --host`
  (or press **Insert** in a game and click **Host a game**). UDP port 27960 must be reachable: the host asks its
  router to forward it by itself (UPnP, below "Hosting over the internet"); without a router that does UPnP, forward
  it by hand, or use Steam, a LAN or a VPN such as Tailscale or ZeroTier.
- Guest: `RampageRE-dev.exe --join <host's address>` (or Insert, type the address, **Join**). The guest loads the host's
  city, block, monsters and the world's seeds by itself.
- The guest plays player 2 with WASD / Space / J K L R or the first controller.
- **Insert** opens the panel: role, connection, ping, updates a second, the out-of-sync counter (host) and the log.
- **Through the menus** (`--shell`, or the player build): the host hosts from the panel (Host a game / Host on Steam;
  the main menu says so when 2 players are chosen) and plays the menus as usual; the guest joins (`--shell --join <ip>`
  or the panel). While the host is in its menus **the guest's screen is the host's front end**: its main menu (the
  play mode, the players' box), Monster Select with both cards (flipped, ready), the card sheet, City Select with the
  globe turning to the host's city, the Options; drawn by the guest's own shell, which is made the host's each frame
  (`shell_net_mirror`, src/netshell.c, from the host's `shell_net_view` in each NM_MENU state), with its own fades and
  sounds. The guest's keys and pads are the host's player 2 (pad 1): on Monster Select the guest picks their own
  monster as a couch player 2 would. Not mirrored, told in words instead: the host's profile popup, title screen and
  loading screen. The guest's own profile popup (the boot's, Create New Profile) stays the guest's: its keys answer it,
  and the host's menus show once it is closed. When the host's game starts the guest leaves its menus and loads it;
  when the host goes back to its menus (or leaves) the guest goes back to its main menu. In a game the guest's
  **Pause menu opens and closes with the host's** and moves as it does (each press of the host's list is sent; the
  host's Options page stays the host's); the guest's Start opens the host's Pause menu for player 2, which the guest
  then works. The guest's results screens are the host's: they open when the host's do, move as the host's do (every
  press the host's screen takes is sent to the guest's; one that comes before the guest's screen is up is kept and
  taken once it is) and close when the host's close; the guest's own presses there are the host's player 2's (Game
  Over's Retry / Quit, the Results' Continue work from either PC). A guest that comes in while the host's results
  screen is up asks for it (NGM_RESEND): the host sends the screen's event again and every press it took. The guest
  keeps its own progress: see "The guest's profile".

## The guest's profile

The host's game runs on the host's profile. The guest's game follows it and would change the guest's own profile in
ways that mean nothing for the guest (the host's city and checkpoint written over the guest's), so the guest's
profile is set aside as the online game starts and given back when it ends, **with what the guest earned in it**
(netrep_menu.c `ngm_keep`):

- **Monsters unlocked** in front of the guest (a cryotube's reveal): unlocked in the guest's profile too.
- **Upgrades** (Charge Punch, Multi-Spin, Roar Attack, Jump Smash) that **the guest's own monster** took; the host's
  monster's upgrades stay the host's.
- **Campaign blocks won** (the host tells each one, NGM_PROGRESS: the city, its stars, the next block, destroyed or
  not): the city is opened in the guest's profile, its stars are added, its resume point moves on to the next block if
  that is further than the guest's own; a city destroyed opens the campaign's next city, as the game's own
  `profile_update` would; Boosy's rule is checked. The guest's own campaign position (the city it plays next) moves
  only when it was on that same city.

Everything is merged, never copied: flags only grow, a resume point only moves on, and nothing of the host's profile
itself (its other cities, unlocks the guest did not see, its settings, its name) reaches the guest's. The guest's
profile is written at each block won and when it leaves the host's game, when it has one loaded or made (its
autosave, as the game's `allow_save`); a guest playing without a profile keeps the merge for the session. The guest's
own front end shows the host's unlocks while it mirrors the host's screens (it is the host's Monster Select) and its
own again once the host is gone. Not kept: King of the City / of the World and Timed Run results (the game keeps none
of those in a profile either), and the Wii's five-star unlocks.

## Player slots: dropping out, coming back, joining late

The host's game has **player slots** (net.c, NET_PROTO 2): slot 0 is the host's player 1, slot k plays the game's
monster k. The session is built for up to 4 (`NET_SLOTS`: a transport peer per slot, nettransport.c taking up to
`NT_PEERS` connections; each slot its own pad, pad acks, digest runs; the slot in NM_WELCOME, every slot's pad ack and
menu bit in NM_SNAP); a hosted game uses 2 today (`NET_GAME_SLOTS`), and only that is tested. Over Steam there is one
peer: one guest at a time.

A slot is **open** (nobody plays it), **online** (a guest does), **kept** (its guest dropped out), **local** (a player
on the host's PC took it) or the host's. The guests are told every slot's state (NM_SLOTS, on change and each second
while one is kept). The panel lists the slots ("Player 2: online, Lizzie", "disconnected, rejoin 0:47", "local
controller", "waiting for a player").

- **Rejoin token.** NM_WELCOME gives the guest a token (64 bits, made by the host for that slot). The guest keeps it in
  memory and in `netrejoin.cfg` beside the game (`rampage-rejoin 1 <hex> <unix time>`; `RAMPAGE_NET_REJOIN` names
  another file; older than an hour it is not offered), and offers it in every NM_HELLO, so a guest whose game crashed
  and was started again comes back too, whichever way it joins (code through the lobby, address, rampage:// link): the
  host matches the token, not the address.
- **Dropping out.** When a guest's connection ends (ENet's timeout: 5 to 10 s after a crash; at once on Leave), its
  slot is kept `NET_RESERVE_S` (60 s) for its token. The guest's monster stands still (a still pad), takes no damage
  (`netgame_shielded`, the NET: hook first in main.c's `monster_take`: no HP lost, no hit reaction; it can still be
  grabbed or pushed), is drawn grey (`Instance.net_grey`, render_gl.c `draw_grey`: each opaque part drawn again over
  itself, a flat grey at 66 %, cut out by its texture's alpha; the rage glow and the hit effects are effects drawn
  after it, as before) and has **DISCONNECTED** (red) with **Rejoin 0:47** over its head, big and bold for a stream, on
  every screen (netui.cpp `netui_label_big`). The chat says "Lizzie disconnected - 60 s to rejoin". Anyone else who
  joins meanwhile is told "A player who dropped out has their place kept for 0:47 more; try again then" (if no other
  slot is open). The same player coming back gets the same slot and monster as they are, mid-round ("Lizzie rejoined").
- **After the minute** the slot is open: **WAITING FOR PLAYER** (yellow), grey and untouchable still ("Slot open:
  waiting for a player"); the next guest to join (anyone) takes over that monster as it is: the same monster, score,
  health and lives ("A player joined as Lizzie"). With every slot played: "The game is full".
- **Before anyone joined**: a hosted 2 Player game's player 2 is an open slot too (WAITING FOR PLAYER, grey), unless a
  player on the host's PC takes it.
- **A local player** taking a slot: a press of player 2's keys (the numpad, DRIVE_KEYS2) or controller (pad slot 1)
  on the host's PC, a button or the stick pushed from rest, while the slot is open or kept, makes it **local**: the
  label and the grey go, every screen shows "Local controller took over Player 2" (a toast and a chat line), the lobby
  counts it as taken, and a kept slot's token is void (its player coming back is told the game is full, unless another
  slot is open). The local player keeps it when they stop pressing: nothing reverts by itself. The host's panel gives
  it back with **Open for online** (waiting for a player again; this PC's keys for it are ignored until pressed
  again). An online slot has **Kick**: its guest is told "The host removed you from the game" (its token deleted
  there), the slot is open at once with no minute kept, the token void.
- **A 1 Player game** can be hosted and joined: the host's world counts with one player, and a guest in slot 1 of a
  game with one player is **dropped in** (netgame.c `ng_dropin`): the block is set up again with two players the way a
  2 player block starts (`setup_players`, `open_city`: the scene's second monster, the game's players and lives, the
  HUD's panels, the reveal's extra monster moved on a slot), then every building is put back as it was (whole, as a
  late guest takes the host's), and player 1's place, health, energy, score and lives. Player 2's monster is the one
  the guest's hello asked for (the monster the guest played last; else Lizzie, George or Ralph, whichever is not the
  host's), standing in the street beside player 1. Then it is a 2 Player game. **Limited:** the people, the army, the
  traffic, the edibles and the challenge start the block afresh, the block's intro plays again, and the clock is the
  new block's. The guest's hello reaches the host before the drop-in, so its first NM_WELCOME names the 1 player world
  (the guest logs "cannot load the host's game (1 players)") and the 2 player one follows a frame later.

The lobby lists `players` (the host, guests and local players), `max` (the slots) and `reserved` (kept slots): "1 / 2
(1 rejoining)"; a game whose slots are all played or kept is greyed as full.

Not done: 3 and 4 player online (the game's GameNetSync has 2 players' scores and lives; the guests' menus, NM_MENU,
are player 2's; the building state a guest asks for goes to every guest), a kept slot's monster not being targeted by
the army (it only takes no damage), and the label over a kept monster that is lying defeated or turned human (the
label is shown only while its model is).

## Re-synchronizing

A safety net for a difference the digest finds and nothing else mends: **Re-synchronize players** in the host's panel
(or `/resync`) has every guest take the host's whole world again, without leaving; a guest's **Ask host to re-sync**
(or `/resync`) asks the host, at most once in 5 s. The host sends NM_STATE NST_RESYNC to every guest and starts its
out-of-sync counts again (`net_digest_reset`); each guest drops what it shows of its own (the snapshots it holds, every
puppet channel's units and samples, its own monster's prediction, its block's state, taking no building change until
the new one) and asks for the block's state as a late joiner does: every building whole, the edibles' slots, the
challenge's upgrade, the street objects' lasting effects, then NST_DONE. The monsters, the game's flow and state, the
clock, the score and lives, the HUD and the menus' state come in every snapshot anyway, and the puppets in their
channels' next ones. Nothing is loaded again. Both chats say "Re-synchronized"; both logs say what was resent.
`RAMPAGE_NET_TESTDESYNC=secs[,resync secs]` (the guest; tests only) damages a building on the guest alone `secs` after
it came in and asks for a resync at `resync secs`.

## Hosting over the internet: the port forward (UPnP)

A host on UDP (not Steam) asks its router to forward the game's port to its PC (UPnP IGD, miniupnpc 2.2.8 vendored in
`src/net/third_party/miniupnpc`, src/netupnp.c): as hosting starts, on a thread of its own so the game never waits
(the router's search takes up to 2 s); the forward is taken away again when hosting stops (Leave, Join, or the game
closing). The panel shows **Port forward (UPnP)**: "203.0.113.7:27960 forwarded to 192.168.1.20; friends join
203.0.113.7", or why not: no router answered (none, or UPnP is off on it), the router said no, it forwards that port to
another PC already, or its own address is a private one (a second router or the provider's CGNAT in front: the forward
then does not reach the internet). The log says the same, and the lobby is told the external address
(`netlobby_set_external`). Hosting goes on whatever the router said: a LAN, a VPN or a forward by hand work as before.
A copy that was killed leaves its forward on the router (it is reused by the next host on that PC, or the router drops
it); `RAMPAGE_NET_UPNP=0` leaves the router alone, `RAMPAGE_NET_UPNP_IF=<address>` searches from that network card
(127.0.0.1: the no-router case, tests\net_test.c). `tools\net_test.ps1` sets `RAMPAGE_NET_UPNP=0` unless `-Upnp`.

## What is shared (phase 1)

| | Who decides | How it gets there |
|---|---|---|
| Player 2's pad | guest | every frame, unreliable, the last 6 frames in each packet |
| Monsters: place, turn, move state and clip time, HP, energy, rage; defeated, turned human (the streaker the host's person), dropping in, hidden, driven by the game (`is_ai`: the intro's and results' moves), the dynamic scale, the head's look, the face | host | 20 snapshots a second, shown 100 ms behind, interpolated; the guest makes no streaker or respawn of its own |
| The game's flow: states (intro, play, block won, Game Over, the walk to the next block, the city's end), the block, the respawns' lives, the results screens, time up, the cameras and the HUD of the intros | host | every event the host's game gives (GE_*) with its state, reliable, at its host time; the guest's game makes no decisions (game_net_follow) and hands them out as its own. The snapshots carry the state too |
| Buildings: every hit, roof hit, cell change, collapse | host | reliable, in order; the guest's game makes the same calls (building_net_gate), so cracks, debris and falls play as usual |
| Clock, points, kills, lives | host | in each snapshot (game_net_put) |
| The world (city, block, play type, monsters, seeds) | host | reliable, on change; the guest loads it |
| People: each one's template, look, play time, place and turn, phase, window; the script objects, the challenge's counts, every open window | host | 20 snapshots a second (netrep_people.c), shown 100 ms behind; their sounds, effects and banners as events. A person in a monster's hand is posed at the guest's own hand |
| Edibles | host | every grab made again on the guest (edible_net_gate), so it goes and plays there; its stats come by the snapshot; the slots (their cells, kinds, out or not, age and place) by NGP_EDIBLES' snapshot, so a guest whose own deal of a block differed takes the host's |
| Army, traffic, bosses, street objects | host | puppets (netrep_*.c): snapshots and events; a street object the host's scripts make mid-block (a broken fountain's geyser) is made on the guest from the snapshot |
| A monster's rage | host | its rage in the snapshot; the guest starts the glow, the RAMPAGE banner and the camera when the host's starts and ends the glow when it ends (`ng_rage_look`) |
| Street objects' effects (the searchlights, ...) | host | events at their host time; each effect's random numbers its own, seeded from the host's id and the block (`fx_net_seed`), so a searchlight's sweep is the host's; the host sends its lasting ones again (with their age) to a guest that asks for the block's state, which starts each one it lacks, run on to that age |
| Camera shakes, player 2's rumble | host | events (NGP_FEEL) at their host time: each shake as it started (slot, kind, place, strength, phases), so the guest's camera turns as the host's (its own monster's clip-event shakes are its own); player 2's rumble on the guest's pad |
| The challenge's upgrade object | host | its state in a small snapshot (NGP_CHALLENGE), its taking and last banner as events; the guest's own touch and tick are off |

Every puppet event carries the host's block: at a block change the guest keeps the events of its new block already
queued (they came before its block began) and drops the old block's.

**Joining late, coming back, a new block.** Buildings and edibles go as the changes the host makes, so a guest that
was not there for them asks for the block's whole state (`NM_STATE`) when it joins, reconnects or starts a block (again
each second until it comes). The host sends every building as it is (`Building` is plain data, the same build on both,
packed by `net_pack`: about 1 KB each instead of 104 KB), the edibles' slots (none left / hidden / out, and how old) and
the challenge's upgrade, then the number of its last building change. Everything is reliable and in order, so the guest
drops building changes until then (they are in the state) and makes every later one.

**The out-of-sync check.** Every 2 s the guest sends a digest with a part for each system: game (state, block, score,
lives, clock), monsters (defeated, rage, places, HP), buildings (state, HP, storeys, roof, cells, floors), people, army,
bosses, traffic, street objects, edibles, challenge. Counts and states must match exactly, places (means) and HP within a
tolerance. The host compares each part with its own from within 0.25 s of the time the guest shows (the buildings: the
newest time it has; the edibles up to 0.5 s after it, their snapshots coming 4 a second); a destroyed building counts
only as destroyed (its storeys fall a frame apart on the two PCs), a picked edible's slot as empty; a part that differs twice in a row is a `MISMATCH`, logged with the system's name and both sides'
values, and the Insert panel lists "In sync: ..." / "Out of sync: army (3 times)".

## The lobby: finding games

`lobby/` is a small server (Node, TypeScript, Colyseus 0.16, after the Fire Fight lobby) that lists games, so the guest
doesn't need the host's address. It carries no game traffic and there is no relay: it hands out an address, and the
guest's game joins the host's over ENet exactly as if the address had been typed. The same server runs on a VPS or on
the hosting player's own PC.

```
 host's PC                                 lobby (the host's PC, or a VPS)            guest's PC
 rampage --host ── POST /games ──────────▶ lobby/  TCP 19980  ◀── GET /games.txt ─── Insert: Games online
          (every 10 s: PUT /games/<code>)  code K7PQ2 → address                       or --join K7PQ2
          ◀════════════════════ ENet, UDP 27960, straight from the guest ══════════════════
```

### Running it

- **On the host's PC**: install Node.js 20 or newer, then `lobby\start.bat` (the first start runs `npm install` and makes
  `config.json`). It prints this PC's addresses. The game's default lobby is `http://localhost:19980`, so the host's own
  game finds it with nothing set. Guests on the same network use `http://<the host's LAN address>:19980`; guests on the
  internet need TCP 19980 forwarded to the host's PC, besides UDP 27960 for the game itself.
- **On a VPS**: clone the repo there (only `lobby/` is needed), `sh lobby/start.sh`, and open TCP 19980 in its firewall
  (`ufw allow 19980/tcp`, or the provider's panel). To keep it running, either pm2 (`npm i -g pm2`, then
  `cd lobby && pm2 start npm --name rampage-lobby -- start && pm2 save && pm2 startup`) or a systemd unit:
  ```ini
  # /etc/systemd/system/rampage-lobby.service    (then: systemctl enable --now rampage-lobby)
  [Service]
  WorkingDirectory=/opt/rampage.se/lobby
  ExecStart=/usr/bin/npm start
  Restart=always
  User=rampage
  [Install]
  WantedBy=multi-user.target
  ```
  Behind nginx or Caddy (for https), set `"trustProxy": true` so the lobby takes players' addresses from
  `X-Forwarded-For`, and proxy WebSockets too (the page's live list).
- `lobby/config.json` (from `config.example.json`): `port` (19980; `PORT` overrides it), `publicAddress` (this PC's
  internet address, for a lobby on the host's PC: what guests from the internet are given for games registered from
  this PC or its network that don't know their own; leave it empty on a VPS), `trustProxy`.
- `cd lobby && npm test` runs its tests (registry, addresses, codes, rate limits, HTTP, the Colyseus listing).

### Pointing the game at it

The game's lobby: `--lobby <url>` on the command line, else the `RAMPAGE_LOBBY` environment variable, else the panel's
**Lobby** field (kept in `netlobby.cfg` beside the game, with **List my game** and **Your name**), else
`http://localhost:19980`. `--lobby` and `RAMPAGE_LOBBY` are for that run only and are not saved.

### Hosting and joining

- **Host:** when hosting over ENet (not Steam) with **List my game in the lobby** ticked (the default), the game lists
  itself: its name, monster, city and block, players, port and build id (net.c's hash of the exe: guest and host must
  match). The lobby answers with a **join code** (5 of `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`: no 0/O or 1/I) that the
  panel shows with a **Copy** button. A heartbeat every 10 s keeps it listed (and says when a guest came: 2 / 2); the
  lobby drops a game silent for 30 s. Leave, a failed connection or quitting the game takes it off at once.
- **Guest:** the panel's **Games online** lists them (every 5 s while the panel is open; **Refresh** now). **Join** on a
  row does what typing its address and Join does. Rows of another build are greyed out with the reason in a tooltip,
  as are full ones. Or type the code under **Join code**; or start the game with `--join K7PQ2`, `--join
  rampage://join/K7PQ2` or `--join rampage://join/203.0.113.5:27960`: net_start_join hands a code to the lobby
  (`GET /games/<code>.txt`) and joins the address it answers.
- **Which address:** the lobby gives each guest the one that reaches the host from where that guest is: the host's
  UPnP address when the game said one (`netlobby_set_external`), else the address its registration came from; for a
  guest behind the same router (the same public address), or with both on the lobby's network, the host's LAN address
  (the game sends it); for both on the lobby's PC, 127.0.0.1. A host on the lobby's own PC or network with no UPnP is
  given to internet guests as `publicAddress`:port, else its LAN address (fine over Tailscale / ZeroTier).
- Nothing waits on the lobby: every request runs on a thread of its own (WinHTTP; netlobby.c) and the frame only
  reads finished answers. A lobby that is down shows as a status line ("the lobby is not reachable"), and the host
  tries again every 10 s.

### The games page and rampage:// links

`http://<lobby>:19980/` lists the games live (the Colyseus `lobby` room lists each game as a room; the page reads
`/games` for the addresses), with each game's code, a **Copy** button for `rampage --join <code>`, a **Join** link
`rampage://join/<code>` and the game's address as a `rampage://join/<address>` link. For those links to start the game,
each player sets them up once, from the page:

- **Windows:** `/install.ps1` (served with the lobby's address filled in; `lobby/launcher/install.ps1` and
  `rampage-handler.ps1`) registers `rampage://` for the current user (HKCU\Software\Classes\rampage, no admin), asks
  for RampageRE.exe, and runs `RampageRE.exe --lobby <lobby> --join <code or address>` for a link. Any web page can open
  a `rampage://` link, so the handler takes only `rampage://join/<code>` and `rampage://join/<host[:port]>`, and the
  game and lobby come from its config, never from the link. `-Uninstall` removes it.
- **Linux:** `/install-linux.sh` (`lobby/launcher/install-linux.sh <path to rampage>`) writes
  `~/.local/share/applications/rampage.desktop` (`Exec=<game> --lobby <lobby> --join %u`,
  `MimeType=x-scheme-handler/rampage;`), runs `xdg-mime default rampage.desktop x-scheme-handler/rampage` and
  `update-desktop-database` when there. `--uninstall` removes it. The game checks the link itself
  (netlobby_join_kind). **But:** the Linux build (build.sh) doesn't build online play (src/net*.c) yet, so for now the
  handler is ready and the game isn't; netlobby.c itself builds there (plain sockets for http://, pthreads; https needs
  Windows' WinHTTP), and resolves codes the same way once net is in build.sh.
- **macOS:** not done: it needs a .app bundle with `CFBundleURLTypes` for `rampage` in its Info.plist.

### The lobby's HTTP (lobby/src/http.ts)

| | |
|---|---|
| `POST /games` | name, monster, city, players, max, reserved? (slots kept for a player who dropped out), port, build (8 hex), external?, lan? as JSON or a form. JSON `{code, token, heartbeat}`, or `ok <code> <token> <heartbeat>` with `Accept: text/plain`. 429: more than 6 registrations a minute or 4 listed games from one address; a host listing again from the same place and port replaces its old listing |
| `PUT /games/<code>` | the heartbeat: `token` (or `X-Token`) and any changed fields. 404: forgotten, list again; 403: not your token |
| `DELETE /games/<code>` | unlist, with the token |
| `GET /games`, `GET /games/<code>` | JSON, each game with `join`: the address for whoever asked |
| `GET /games.txt`, `GET /games/<code>.txt` | `rampage-lobby 1`, then a line per game, tab-separated: code, join (`-` none), players, max, build, name, monster, city, reserved (a ninth field; a reader of eight skips it) |
| `GET /` `/install.ps1` `/install-linux.sh` `/health` | the page, the link installers, `rampage_lobby` |

Colyseus clients join `lobby` (the built-in LobbyRoom): each listed game is a `game` room whose id is its code and whose
metadata is the listing.

## Testing on one PC

```
build.bat dev
tests\build_net_test.bat                     unit tests: wire format, snapshot buffer, lobby, UPnP without a router
tools\net_test.ps1 -TestPad -Shots 2         host + guest; the guest walks, climbs and punches by itself;
                                             build\nettest\shot_N.png shows both windows side by side
tools\net_test.ps1 -TestPad -Lag 100 -Loss 5 the internet on one PC
tools\net_test.ps1 -GuestMoves -HostMoves -GameArgs "Chicago 1 --players 2 --cheat 011235" -GuestArgs "--cheat 011235"
                                             both monsters do the special moves every 10 s, in real time: onto a
                                             front, the charge punch (Circle held), the jump smash, the roar (R2;
                                             -HostDo "after 1, rage, p2, rage" fills both energies)
tools\net_test.ps1 -Seconds 0                leave both running to play them yourself
tools\net_test.ps1 -JoinDelay 30 -HostDo "wait 3, hitcell 3 0 0 10, ..."
                                             the guest joins 30 s late; the host's monster plays --do's words first
tools\net_slots_test.ps1 [-Lag 80 -Loss 3]   the guest killed mid-round and started again with its token (the same
                                             slot and monster), then left out past the minute (WAITING FOR PLAYER),
                                             then a new guest takes the monster over; screenshots slots_*.png
tools\net_slots_test.ps1 -OnePlayer -Quick   a 1 player game hosted, the guest drops in as player 2
tools\net_local_test.ps1                     player 2 taken by the host's numpad, then a guest told "The game is full"
RAMPAGE_NET_TESTGREY=1 RampageRE-dev.exe ... --shot grey.png   player 2 drawn grey, offline (the look of a slot nobody plays)
tools\net_test.ps1 -HostDo "after 35, hitcell 4 3 0 10, bosshit 3"
                                             the words 35 s (real time) after the guest came in. --do's waits run
                                             the game's frames at once, with no network: before "after" only
```

`RAMPAGE_NET_LAG` (ms) and `RAMPAGE_NET_LOSS` (%) work on any run; `RAMPAGE_NET_TRACE=1` logs every digest's contents
(`=2`, `-Trace -TraceLevel 2`: every building's, not only the damaged ones).
Both copies log the game's flow (`net: flow: ...`: states, defeats, respawns, lives, results screens and their answers,
the host's events sent and taken). Test hooks, read by the host only: `RAMPAGE_NET_TESTDIE=period[,lives]` fells both
monsters every `period` seconds of play (lives: each player's from the start), `RAMPAGE_NET_TESTEND=win,secs` brings
every building down `secs` into each block, `fountains,secs` breaks every fountain and hydrant (their geysers), `time,secs` runs the clock out (a block with a time limit: `--play timed`).
A defeated monster lies there until its player presses a button (`Faceup_Idle_Waiting2`), as in the game.

## Hooks: where online play touches upstream's files

All of online play is in its own files (`src/net.c`, `netwire.*`, `netsnap.*`, `nettransport.*`, `netgame.c`,
`netui.*`, `netlobby.*`, `netupnp.*`, `netshell.c` (included by shell.c), `src/net/third_party/` for ENet, Dear ImGui
and miniupnpc). Upstream's files have only these, each marked `NET:`:

| File | Hook |
|---|---|
| `build.bat` | the net sources in SRC; `NETINC`, `NET`, `:net_libs` (ENet, ImGui and miniupnpc built once into `build\net`); `-lws2_32 -lwinmm -ldwmapi -liphlpapi` |
| `src/main.c` | `netgame_shell_input` in `shell_frame` (the guest's menu presses go to the host); `netgame_game_input` before a game's menus take the pad; `netgame_shell_start` before `shell_enter` from a game; `card_job` writes no profile from the guest's game (its own front end's popup writes); `#include "net.h"`; `--host` / `--join` skipped by the argument loop; `#include "netgame.c"` before `viewer_main`; `netgame_init` after `win_open`; `netgame_frame` after the clock; `netgame_pads` before `play_step`; `netgame_after_step` after it; `netgame_draw` before both `win_swap`s; `net_stop` / `netui_shutdown` at the end; `save_profile` returns on the guest |
| `build.bat`, `src/main.c` (the lobby) | `src\netlobby.c` in SRC and `-lwinhttp`; `#include "netlobby.h"` and `--lobby <url>` (netlobby_set_url) in the argument loop |
| `src/render_gl.c` | `netui_wndproc` first in `wnd_proc` (ImGui's input, Insert); `Renderer.grey` and `draw_grey` after an opaque part of an instance with `net_grey` (a player slot nobody plays) |
| `src/city.h` | `Instance.net_grey` (0 offline) |
| `src/main.c` (monster_take) | `netgame_shielded` first: a player slot nobody plays takes no hit |
| `src/building.c` / `.h` | `building_net_gate` called first by `building_hit`, `building_hit_roof`, `building_set_cell`, `building_destroy` |
| `src/shell.c` / `.h` | `shell_hold_idle` (no attract mode while someone waits online); `ShellAutoView.ms_name` (Monster Select's cards, told to the guest); `#include "netshell.c"` at the end of shell.c and `ShellNetView`, `shell_net_view`, `shell_net_mirror` in shell.h (the guest's front end made the host's) |
| `src/game.c` / `.h` | `game_net_get` / `game_net_put` at the end (clock and score); `game_net_follow`, `game_net_flow_get` / `_put`, `game_net_push`, `game_net_tap` (the host's flow); the follow gate in `game_update`, the tap in `game_events` |
| `src/hud.c` / `.h` | `hud_net_tap` in `hud_input_pad` (a results screen's presses and the Pause menu list's), `hud_results_close`, `hud_pause_close` |
| `src/main.c` (script calls, GE_REVIVE) | the guest's `Monsters::becomeHuman` / `startRespawn` do nothing; its GE_REVIVE removes no person |
| `src/people.c` / `.h` | `people_net_gate` in `emit`, `people_net_tap` in `people_events`, `people_net_eaten` in `people_eat`; the `people_net_*` section at the end (netrep_people.c) |
| `src/edible.c` / `.h` | `edible_net_gate` in `edible_grab`, once it has an edible to take; `edible_net_slot` (the guest's slots made the host's, at the host's place where the guest's floor fell first); the guest's `edible_floor_fell` does nothing |
| `src/streetobj.c` / `.h` | `streetobj_net_make` (a script-made object of the host's, made on the guest) |
| `src/main.c` (feel, challenge) | `net_rumble` first in `mon_rumble`, `net_shake` before both `shake_start`s; the guest's upgrade touch loop and `challenge_upgrade_tick` are skipped |
| `src/edible.c` / `.h` | `edible_net_gate` in `edible_grab`, once it has an edible to take; `edible_net_slot` (a late guest's slots made the host's; a falling cryotube left to fall); `spawn` makes the guest's cryotube (its break and reveal play as the host's) |
| `src/fx.c` / `.h` | `fx_net_seed`: the next effect's own random numbers and a run-on (`Fx.seed`, `Fx.skip`, `FxWorld.rs`); unused offline |
| `src/boss.c` / `.h` | `boss_net_triggers` / `boss_net_set_triggers` (New York's barrier volumes: the guest's monster is pushed out of the host's) |
| `src/main.c` (mon_step) | the guest's monsters are not defeated by their own state change at 0 HP (the host's defeat comes in the snapshot) |
| `src/main.c` (mon_step) | the guest's AI picks no transition (`!net_guest_world()`): whether the game drives a monster (`is_ai`) is the host's, in the snapshot |
| `src/city.c` / `.h` | `scene_person_forget` (a person made over online: its looks dropped) |
| `src/main.c` (play_step, edible_step) | the guest runs no `scene_people_spawn` / `people_update`; a held index not here yet is let go; the guest's edible events pay no stats or reactions |

With online play off (no `--host` / `--join`, panel unused) nothing in the hooks changes the game: a `--shot` of
`Chicago 1 --players 2` with a `--do` script is byte for byte the same picture as upstream's build.

## Taking upstream's updates

`tools\update_from_upstream.ps1` fetches jjmhalew/RampageRE, merges it into this branch (never a rebase: the branch is pushed), rebuilds and runs the tests.
A conflict can only be in a `NET:` line above. A rename upstream of something `netgame.c` reads (a Viewer or Mon field,
a building or game function) shows up as a compile error in `netgame.c`, never as a silent change; then run
`tools\net_test.ps1 -TestPad -Shots 2` and look at both screens.

## Checked (two copies on one PC)

- Los Angeles 8 (the Carbonator, the searchlights): the beams sweep the same way on both, a late guest's too.
- New York 10, the blimp beaten (`-HostDo "after 4, wait 8, bosshit, wait 20, bosshit, wait 12, bosshit"`): the victory
  scene on the Empire's roof matches, no MISMATCH. (The guest made a roof hit's cell blow twice: building_hit_roof ends
  in a building_hit the host also sent; now the guest's replay of the roof hit leaves that to the host's next change.)
- Block changes (`RAMPAGE_NET_TESTEND=win,10` and `menu accept`s), also at 80 ms / 3 %: no MISMATCH.
- The special moves of both players, at 80 ms / 3 %: charge punch, jump smash, roar, the rage's glow; cracks, people and
  cars the same.
- Las Vegas 5's pirate ship (rocking, its cannon); all 30 monsters (Ramsey, Philbert and Eyegore last).
- The game's AI on the guest: a guest's own game runs its intro (BeginLevelNI: setIsAI(1), playMove React_Intro)
  before it follows the host's, and a guest that came in after the host's BeginBlockNI (setIsAI(0)) kept that AI on.
  Its goal forced React_Intro every frame on both monsters against the snapshot's state (the guest's log: `Ground_Walk
  -> React_Intro (FiFi_Intro) by -1` some 5000 times in 30 s, `Ground_Walk -> Ground_Idle` 1800 times on its own one):
  each forced start began a new blend from the pose held, so the host's monster slid in a held pose or drew two clips
  at once ("phasing"), its face flipped between FiFi_Intro and the idle one, and the guest's own monster took no pad
  (the AI's stick) and was corrected all the time. Every monster had it (George 400 a run); the Wii's showed it most.
  Now the snapshot carries `is_ai`, the guest takes it (dropping the goals when it is off), its AI picks nothing and a
  monster the host's AI drives is not predicted. Checked in Dallas 2 at 80 ms / 3 % with each of the Wii's ten on
  either side (Boris/Ruby, Plucky/Squirmy, Flea/Sarah, Brian/Vern, Cyril/Fifi and swapped) and George/Lizzie: the
  guest's transitions as many as the host's (60-115 a run, from thousands), both screens the same.
