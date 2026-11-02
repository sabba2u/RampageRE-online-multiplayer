# Four-player online: design

Status: ready for the owner's approval (2026-11-01). Builds on [2026-10-27-online-multiplayer-design.md](2026-10-27-online-multiplayer-design.md)
and the code as it is on branch `online-multiplayer` (docs/NETPLAY.md is the reference for what exists).

## Intent

Online play today is 2 players: the host and one guest. The owner wants the full Rampage party: **up to four humans,
each normally on their own PC**, connected over the LAN or through a room server (the launcher's built-in one or the
Node one), all seeing the same game. The owner's words: "keep what we have with 2 players but expand it to 4 players."

Why now: a test with a 4 player game online showed players 3 and 4 placed right on the guest but animated by the
guest's own guesses (wrong poses, sliding, tails and tentacles stiff, scores 0 on the guest against 1115 / 1765 on the
host; `RAMPAGE_NET_TAILS=1`, build/nettest/shot_2.png). Online starts were capped at 2 players (`--no-four-players`)
because of that.

## Success

Four people on four PCs play a whole campaign city (and the other modes) together, and on every screen:
- the same four monsters, each doing the same move at the same time, tails and tentacles swinging, no sliding;
- the same buildings, people, army, traffic and effects (as 2 player online already has);
- the same scores, lives, rage and energy for all four, the same HUD, the same results screens;
- each player drives their own monster with their own pad or keyboard, on their own PC.

## Goals

1. **Seats.** A hosted game has 4 seats (the existing player slots: `NET_SLOTS` 4, `NET_GAME_SLOTS` raised from 2).
   Seat 1 is the host's. Guests take seats 2, 3, 4 in the order they join.
2. **Everything synced for every monster**, not players 1 and 2 only: moves and clip state, the tail springs' weight,
   the look (rage, grey), holds, climbing, the score, lives, energy, rage, defeats and respawns, the HUD's panels, the
   results screens (Results, LevelResults, King of the City / World and Timed Run results), challenges and upgrades
   that name a player.
3. **Menus with every player.** Guests join while the host is in the menus. On the monster select **each human picks
   their own monster with their own controller on their own PC**, everyone seeing everyone's choice, as four players
   on one couch do in the local 4 player select. Today a guest's menus (NM_MENU) are player 2's only.
4. **The start.** When the host starts, the game has as many players as the room has people in seats (1 to 4).
   Monsters 2-4 start in the darkened "connecting" look until that guest's game is fully in (as player 2 does today),
   then play. No monster exists for a seat nobody is in.
5. **Dropping out, rejoining, joining late: as today, per seat.** A guest whose connection ends has their seat kept
   60 s (grey, DISCONNECTED, Rejoin 0:47, shielded); the same player coming back (rejoin token) gets their monster
   back as it is; after the minute the seat is open (WAITING FOR PLAYER) and the next guest takes that monster over. A
   guest arriving mid-game into a seat with no monster yet is **dropped in with no restart** (the owner's choice): their
   monster appears in the block as it is, mid-play, and nothing else changes for anyone: the people, army, traffic,
   edibles, challenge, clock and intro go on, every other player's place, health, score and lives untouched. This
   replaces today's `ng_dropin`, which sets the block up again (people, army, traffic, edibles, challenge and intro
   start over, the clock is the new block's) and goes from 1 player to 2 only; the new one adds the next player to any
   N (1-3) player game, on the host and on every guest, and with the 2 player case too.
6. **Players on the host's PC.** The host's PC may have more than one player (extra controllers or the numpad), as
   today's "local controller took over Player 2", for any seat 2-4. The usual case is four PCs.
7. **Hosting tools for all seats.** The host's Insert panel and the launcher's Players list show seats 1-4; Mute, Kick,
   Ban work on any guest; the room list shows "2 / 4", "3 / 4 (1 rejoining)", full at 4.
8. **Launcher.** Online starts no longer pass `--no-four-players` (3 and 4 Player available in the host's menus); the
   wording that says online is 2 players changes everywhere (launcher, HOSTING.md, README, NETPLAY.md).

## Non-goals

- **Host on Steam stays 2 players** (one Steam peer today). A later piece of work. (Builder's call to keep it compiling
  and capped at 2.)
- **More than one person on a guest PC.** Each guest PC is one player. (My call; the owner said the usual case is four
  PCs and the host may have extra players.)
- **Computer players online.** Online, every monster is a person (offline keeps its computer players). A seat with no
  person has no monster.
- More than 4 players; split screen; a dedicated server (the host's game stays the authority).

## Anti-goals (failure even if it "works")

- Idle monsters standing around for empty seats from the start.
- Any change to offline play: the offline regression hash (`CB88DA7AB444D79F`, see NETPLAY.md's tests) must still
  match, and a 2 player online game must behave exactly as it does now.
- A guest seeing a different score, lives or winner than the host.
- A version mismatch between players becoming silent desync: an older 2-seat build must be refused with a plain
  message (raise `NET_PROTO`).

## Constraints

- C game (`src/net*.c`, `src/netrep_*.c`, `src/netui.cpp`, main.c's NET: hooks), built with `build.bat` /
  `build.bat dev` (Zig). Rust launcher (`launcher/`), Node room server (`lobby/`). Windows first.
- The host stays authoritative; guests send pads, the host sends snapshots and events, as now.
- Bandwidth: the snapshot grows with 4 monsters; it must stay well under a home upload with 3 guests (measure it; the
  host sends to 3 peers).
- Tests: the existing suites stay green (tests\build_net_test.bat, launcher `cargo test`, `lobby` npm test, the
  offline hash). New automated tests run 4 copies of RampageRE-dev.exe on one PC (one host, three guests, pads driven
  by the test hooks) and compare, per monster, state, tail weight, score and lives across all four
  (`RAMPAGE_NET_TAILS`-style traces) plus screenshots of all four windows.

## Known places that assume 2 (a start, not the full list)

`NET_GAME_SLOTS`; netgame.c loops `k < 2` (the score / lives sync around lines 798, 1054, 1139, 2442, 2515, 2526);
GameNetSync with 2 players' scores and lives; NM_MENU being player 2's; the building state a guest asks for going to
every guest; `ng_dropin` (1 → 2 only); the hosting badge ("Name is in (P2)"); the launcher's `--no-four-players` on
online starts and its "2 players" texts; the lobby's `max`.

## Decided after review (my calls, from the owner's "keep what we have, for 4")

- **Seats and players.** Before the start, seats are kept without gaps: a guest who leaves the menus for good moves
  the later ones down (seat k is always player k). From the start on, a seat keeps its monster: a seat whose guest
  is gone past the minute is WAITING FOR PLAYER as player 2 is today, and the next guest takes that monster over.
  So seat k = monster k = player k throughout; per-player code keys on that one index.
- **The player count at the start** is the people in seats (the host, its local players and the guests), 1 to 4; a
  seat kept for a guest inside their 60 s counts, their monster starting grey with the monster they picked. The host's
  1-4 Player menu choice is not a cap online. A command-line `--players N --host` start (the test scripts) keeps
  today's behaviour: N players, seats 2..N open and grey until joined.
- **The anti-goal "2 player behaves as now"** means a 2 player online game played by two people; a hosted game no one
  has joined starts with the host alone.
- **Who drives the menus.** The host drives the shared screens (main menu, City Select, Pause, results' Continue), as
  today; the guests mirror them (`shell_net_mirror`). On Monster Select each player drives only their own pick and
  ready, with their own pad; everyone sees all picks.
- **Players on the host's PC.** A second (third) controller or the numpad on the host's PC takes the next free seat
  when pressed, as on a couch; that pad then plays that seat.
- **The room list** always offers 4 seats (`max` 4).

## Left to the builder

Message formats and how the snapshot packs four monsters; whether a guest's menu inputs travel as pads or as menu
events; how the four-copy test drives the menus; the order of work. Settle in testing: whether a late joiner's monster
appears beside player 1 or at the block's usual player start spot. How the game takes a monster mid-block (the scene's
monster, the game's player, the HUD panel, lives as a city start gives them) is the hard part of this work: the game
was built to set its players up only at a block's start, so plan it early and test it on its own.
