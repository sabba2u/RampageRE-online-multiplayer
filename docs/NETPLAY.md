# Online play (port extra, work in progress)

Two players on two PCs: the **host** (player 1) runs the game; the **guest** (player 2) plays on their own PC and sees
the host's game. Design: [superpowers/specs/2026-10-27-online-multiplayer-design.md](superpowers/specs/2026-10-27-online-multiplayer-design.md);
plan and status: [superpowers/plans/2026-10-27-online-multiplayer-phase1.md](superpowers/plans/2026-10-27-online-multiplayer-phase1.md).

## Playing

Both players need the same build of RampageRE and their own disc image.

- Host: start a 2-player game, e.g. `rampage.exe Chicago 1 --players 2 --monster George --monster2 Lizzie --host`
  (or press **Insert** in a game and click **Host a game**). UDP port 27960 must be reachable (port forward it on
  the router, or use a LAN / VPN such as Tailscale or ZeroTier until Steam is in).
- Guest: `rampage.exe --join <host's address>` (or Insert, type the address, **Join**). The guest loads the host's
  city, block, monsters and the world's seeds by itself.
- The guest plays player 2 with WASD / Space / J K L R or the first controller.
- **Insert** opens the panel: role, connection, ping, updates a second, the out-of-sync counter (host) and the log.
- **Through the menus** (`--shell`, or the player build): the host hosts from the panel (Host a game / Host on Steam;
  the main menu says so when 2 players are chosen) and plays the menus as usual; the guest joins (`--shell --join <ip>`
  or the panel). While the host is in its menus the guest's screen shows a notice of where the host is (main menu,
  Monster Select with both cards and readies, the city being picked) and the guest's keys and pads are the host's
  player 2 (pad 1): on Monster Select the guest picks their own monster as a couch player 2 would. When the host's game
  starts the guest leaves its menus and loads it; when the host goes back to its menus (or leaves) the guest goes back
  to its main menu. In a game the guest's Start opens the host's Pause menu for player 2, which the guest then works;
  the guest's results screens are the host's: they open when the host's do, move as the host's do (every press the
  host's screen takes is sent to the guest's) and close when the host's close; the guest's own presses there are the
  host's player 2's (Game Over's Retry / Quit, the Results' Continue work from either PC). The guest writes no profile while
  online and its profile is put back as it was when it leaves the host's game.

## What is shared (phase 1)

| | Who decides | How it gets there |
|---|---|---|
| Player 2's pad | guest | every frame, unreliable, the last 6 frames in each packet |
| Monsters: place, turn, move state and clip time, HP, energy, rage; defeated, turned human (the streaker the host's person), dropping in, hidden, the dynamic scale, the head's look, the face | host | 20 snapshots a second, shown 100 ms behind, interpolated; the guest makes no streaker or respawn of its own |
| The game's flow: states (intro, play, block won, Game Over, the walk to the next block, the city's end), the block, the respawns' lives, the results screens, time up, the cameras and the HUD of the intros | host | every event the host's game gives (GE_*) with its state, reliable, at its host time; the guest's game makes no decisions (game_net_follow) and hands them out as its own. The snapshots carry the state too |
| Buildings: every hit, roof hit, cell change, collapse | host | reliable, in order; the guest's game makes the same calls (building_net_gate), so cracks, debris and falls play as usual |
| Clock, points, kills, lives | host | in each snapshot (game_net_put) |
| The world (city, block, play type, monsters, seeds) | host | reliable, on change; the guest loads it |
| People: each one's template, look, play time, place and turn, phase, window; the script objects, the challenge's counts, every open window | host | 20 snapshots a second (netrep_people.c), shown 100 ms behind; their sounds, effects and banners as events. A person in a monster's hand is posed at the guest's own hand |
| Edibles | host | every grab made again on the guest (edible_net_gate), so it goes and plays there; its stats come by the snapshot; the slots (their cells, kinds, out or not, age and place) by NGP_EDIBLES' snapshot, so a guest whose own deal of a block differed takes the host's |
| Army, traffic, bosses, street objects | host | puppets (netrep_*.c): snapshots and events; a street object the host's scripts make mid-block (a broken fountain's geyser) is made on the guest from the snapshot |
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
newest time it has); a part that differs twice in a row is a `MISMATCH`, logged with the system's name and both sides'
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
| `POST /games` | name, monster, city, players, max, port, build (8 hex), external?, lan? as JSON or a form. JSON `{code, token, heartbeat}`, or `ok <code> <token> <heartbeat>` with `Accept: text/plain`. 429: more than 6 registrations a minute or 4 listed games from one address; a host listing again from the same place and port replaces its old listing |
| `PUT /games/<code>` | the heartbeat: `token` (or `X-Token`) and any changed fields. 404: forgotten, list again; 403: not your token |
| `DELETE /games/<code>` | unlist, with the token |
| `GET /games`, `GET /games/<code>` | JSON, each game with `join`: the address for whoever asked |
| `GET /games.txt`, `GET /games/<code>.txt` | `rampage-lobby 1`, then a line per game, tab-separated: code, join (`-` none), players, max, build, name, monster, city |
| `GET /` `/install.ps1` `/install-linux.sh` `/health` | the page, the link installers, `rampage_lobby` |

Colyseus clients join `lobby` (the built-in LobbyRoom): each listed game is a `game` room whose id is its code and whose
metadata is the listing.

## Testing on one PC

```
build.bat dev
tests\build_net_test.bat                     unit tests: wire format, snapshot buffer
tools\net_test.ps1 -TestPad -Shots 2         host + guest; the guest walks, climbs and punches by itself;
                                             build\nettest\shot_N.png shows both windows side by side
tools\net_test.ps1 -TestPad -Lag 100 -Loss 5 the internet on one PC
tools\net_test.ps1 -Seconds 0                leave both running to play them yourself
tools\net_test.ps1 -JoinDelay 30 -HostDo "wait 3, hitcell 3 0 0 10, ..."
                                             the guest joins 30 s late; the host's monster plays --do's words first
tools
et_test.ps1 -HostDo "after 35, hitcell 4 3 0 10, bosshit 3"
                                             the words 35 s (real time) after the guest came in. --do's waits run
                                             the game's frames at once, with no network: before "after" only
```

`RAMPAGE_NET_LAG` (ms) and `RAMPAGE_NET_LOSS` (%) work on any run; `RAMPAGE_NET_TRACE=1` logs every digest's contents.
Both copies log the game's flow (`net: flow: ...`: states, defeats, respawns, lives, results screens and their answers,
the host's events sent and taken). Test hooks, read by the host only: `RAMPAGE_NET_TESTDIE=period[,lives]` fells both
monsters every `period` seconds of play (lives: each player's from the start), `RAMPAGE_NET_TESTEND=win,secs` brings
every building down `secs` into each block, `fountains,secs` breaks every fountain and hydrant (their geysers), `time,secs` runs the clock out (a block with a time limit: `--play timed`).
A defeated monster lies there until its player presses a button (`Faceup_Idle_Waiting2`), as in the game.

## Hooks: where online play touches upstream's files

All of online play is in its own files (`src/net.c`, `netwire.*`, `netsnap.*`, `nettransport.*`, `netgame.c`,
`netui.*`, `netlobby.*`, `src/net/third_party/` for ENet and Dear ImGui). Upstream's files have only these, each marked `NET:`:

| File | Hook |
|---|---|
| `build.bat` | the net sources in SRC; `NETINC`, `NET`, `:net_libs` (ENet and ImGui built once into `build\net`); `-lws2_32 -lwinmm -ldwmapi` |
| `src/main.c` | `netgame_shell_input` in `shell_frame` (the guest's menu presses go to the host); `netgame_game_input` before a game's menus take the pad; `netgame_shell_start` before `shell_enter` from a game; `card_job` writes no profile on the guest; `#include "net.h"`; `--host` / `--join` skipped by the argument loop; `#include "netgame.c"` before `viewer_main`; `netgame_init` after `win_open`; `netgame_frame` after the clock; `netgame_pads` before `play_step`; `netgame_after_step` after it; `netgame_draw` before both `win_swap`s; `net_stop` / `netui_shutdown` at the end; `save_profile` returns on the guest |
| `build.bat`, `src/main.c` (the lobby) | `src\netlobby.c` in SRC and `-lwinhttp`; `#include "netlobby.h"` and `--lobby <url>` (netlobby_set_url) in the argument loop |
| `src/render_gl.c` | `netui_wndproc` first in `wnd_proc` (ImGui's input, Insert) |
| `src/building.c` / `.h` | `building_net_gate` called first by `building_hit`, `building_hit_roof`, `building_set_cell`, `building_destroy` |
| `src/shell.c` / `.h` | `shell_hold_idle` (no attract mode while someone waits online); `ShellAutoView.ms_name` (Monster Select's cards, told to the guest) |
| `src/game.c` / `.h` | `game_net_get` / `game_net_put` at the end (clock and score); `game_net_follow`, `game_net_flow_get` / `_put`, `game_net_push`, `game_net_tap` (the host's flow); the follow gate in `game_update`, the tap in `game_events` |
| `src/hud.c` / `.h` | `hud_net_tap` in `hud_input_pad` (a results screen's presses), `hud_results_close` |
| `src/main.c` (script calls, GE_REVIVE) | the guest's `Monsters::becomeHuman` / `startRespawn` do nothing; its GE_REVIVE removes no person |
| `src/people.c` / `.h` | `people_net_gate` in `emit`, `people_net_tap` in `people_events`, `people_net_eaten` in `people_eat`; the `people_net_*` section at the end (netrep_people.c) |
| `src/edible.c` / `.h` | `edible_net_gate` in `edible_grab`, once it has an edible to take; `edible_net_slot` (the guest's slots made the host's, at the host's place where the guest's floor fell first); the guest's `edible_floor_fell` does nothing |
| `src/streetobj.c` / `.h` | `streetobj_net_make` (a script-made object of the host's, made on the guest) |
| `src/main.c` (feel, challenge) | `net_rumble` first in `mon_rumble`, `net_shake` before both `shake_start`s; the guest's upgrade touch loop and `challenge_upgrade_tick` are skipped |
| `src/edible.c` / `.h` | `edible_net_gate` in `edible_grab`, once it has an edible to take; `edible_net_slot` (a late guest's slots made the host's; a falling cryotube left to fall); `spawn` makes the guest's cryotube (its break and reveal play as the host's) |
| `src/boss.c` / `.h` | `boss_net_triggers` / `boss_net_set_triggers` (New York's barrier volumes: the guest's monster is pushed out of the host's) |
| `src/main.c` (mon_step) | the guest's monsters are not defeated by their own state change at 0 HP (the host's defeat comes in the snapshot) |
| `src/city.c` / `.h` | `scene_person_forget` (a person made over online: its looks dropped) |
| `src/main.c` (play_step, edible_step) | the guest runs no `scene_people_spawn` / `people_update`; a held index not here yet is let go; the guest's edible events pay no stats or reactions |

With online play off (no `--host` / `--join`, panel unused) nothing in the hooks changes the game: a `--shot` of
`Chicago 1 --players 2` with a `--do` script is byte for byte the same picture as upstream's build.

## Taking upstream's updates

`tools\update_from_upstream.ps1` fetches jjmhalew/RampageRE, rebases this branch on it, rebuilds and runs the tests.
A conflict can only be in a `NET:` line above. A rename upstream of something `netgame.c` reads (a Viewer or Mon field,
a building or game function) shows up as a compile error in `netgame.c`, never as a silent change; then run
`tools\net_test.ps1 -TestPad -Shots 2` and look at both screens.
