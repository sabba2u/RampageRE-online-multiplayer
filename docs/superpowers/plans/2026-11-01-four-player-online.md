# Four-Player Online Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a hosted game takes up to 4 people (host + 3 guests, or local players on the host's PC), every monster,
score, life, menu and results screen the same on every screen, late joiners walking in with no restart.

**Architecture:** the host stays authoritative. The seat count goes from 2 to 4 (`NET_GAME_SLOTS`), every per-player
wire field and loop goes from `k < 2` to `k < nmon`, a forced state on a guest enters the state as the game does
(tail springs, blend), menu input carries its sender's seat, one-guest replies go to that guest only, and a new
mid-block player add replaces the restarting drop-in.

**Tech Stack:** C (Zig `build.bat`), Dear ImGui (netui.cpp), Rust launcher (`launcher/`), Node lobby (`lobby/`),
PowerShell test tools.

**Spec:** docs/superpowers/specs/2026-11-01-four-player-online-design.md. Code map: the research notes in this
session (netgame.c snapshot 1004-1160, apply 1478-1556, dropin 2262-2327; net.c seats 29/225/258; netrep_menu.c).

## Global Constraints

- Offline unchanged: `RampageRE-dev.exe "<iso>" Chicago 1 --players 2 --monster George --monster2 Lizzie --do "right 1, cross, wait 0.8, p2, left 1, square, wait 1" --shot out.png` → SHA256 starts `CB88DA7AB444D79F`.
- A 2-person online game behaves as now (tools\net_test.ps1, net_slots_test.ps1, net_local_test.ps1 still pass).
- `NET_PROTO` 2 → 3 (an older build is refused with the existing version message).
- Host on Steam stays at 2 seats.
- Every suite green at each commit: `tests\build_net_test.bat` (N checks, 0 failed), `cargo test` in launcher,
  `npm test` in lobby, the offline hash.
- Commit and push after each task (`git push fork online-multiplayer`); every launcher build copied to the repo root.

## Review Focus

1. A guest dropping out and rejoining in seat 3 while seats 2 and 4 play: same monster, score, lives back (Task 2's
   4-copy test, `-Rejoin 3`).
2. A local controller on the host's PC pressed while seat 2 is online: takes seat 3, not seat 2 (Task 4 test).
3. A late guest's request for the block state must not reset the other guests' buildings (Task 5 test: the other
   guests' `net: state` log shows no re-put).
4. A drop-in while another player is climbing / holding a car: their state untouched (Task 6 test words).
5. 3 guests at 60 snapshots a second: the host's upload stays under 1.5 Mbit/s (Task 3: the panel's `snap_bytes`
   logged, asserted by the test).

---

### Task 1: Four-copy test harness

**Files:**
- Create: `tools/net4_test.ps1`
- Modify: `tools/net_shot.ps1` (2×2 grid when 4 windows), `src/netgame.c` (`RAMPAGE_NET_TAILS` line adds points and
  lives; `RAMPAGE_NET_TESTPAD=<n>` pattern seed so guests move differently), `docs/NETPLAY.md` (tests section)

**Interfaces:**
- Produces: `tools\net4_test.ps1 [-Seconds 40] [-Guests 3] [-Shots 2] [-GameArgs ...] [-Rejoin <seat>] [-Lag ms]`.
  Starts the host (`--host 27960`), then guests 1 s apart, each with its own `RAMPAGE_NET_REJOIN`
  (build\nettest\rejoin<k>.cfg), `RAMPAGE_STUN=0`, `RAMPAGE_NET_TAILS=1`, `RAMPAGE_NET_TESTPAD=<k>`; output
  `build\nettest\host.out`, `guest<k>.out`; prints one `PASS`/`FAIL <reason>` line per check and exits 1 on any FAIL.
- Checks (`Test-Net4`): every copy logged `net: tail` for the same monster count; no `digest differs` line on any
  guest; on every guest, a monster in `Ground_Walk` has `weight > 0` whenever its `links > 0`; the last trace's
  points and lives per monster equal the host's (±0 after the host's final `RAMPAGE_NET_TESTEND`).

- [ ] Step 1: write net4_test.ps1 and run it on today's code with `-GameArgs "Chicago 1 --players 4 --four-players --monster Lizzie --monster2 Ralph --monster3 Icky --monster4 Kingston" -Guests 1`. Expected: FAIL lines for the tail weight and points (the bug reproduced; the guest-count check passes).
- [ ] Step 2: commit (`tools: four-copy online test`).

### Task 2: Four seats

**Files:** `src/net.c:29,225,258`, `src/netwire.h:37` (NET_PROTO 3), `src/nettransport.h:18` (NT_PEERS 4 → 6: 3 guests, plus room for a
"game is full" refusal and a rejoiner whose old peer has not timed out yet), `src/netgame.c:98,185` (world checks: at least 1 player),
`src/netgame.c:2609-2610` (test grey/join for any k ≥ 1), `tests/net_test.c`

**Interfaces:**
- Produces: `int net_slots(void)` = 4 when hosting over ENet, 2 over Steam, the told n on a guest.

- [ ] Step 1: tests in net_test.c: `test_four_seats` — `net_slot_pick` over a 4-seat table fills 1, 2, 3 in join
  order, a kept seat 2 is skipped for a newcomer and taken by its token; NM_SLOTS round trip with n=4; lobby parse
  with `max==4`. Run `tests\build_net_test.bat`; expected FAIL on the new asserts that need n=4 written by the host.
- [ ] Step 2: implement; run the suite (PASS), net_test.ps1 and net_slots_test.ps1 (2-person, as before).
- [ ] Step 3: net4_test.ps1 `-Guests 3 -GameArgs "Chicago 1 --players 4 --four-players"`: expected the seat check
  PASS (every guest welcomed in seats 2, 3, 4), the sync checks still FAIL (Task 3). `-Rejoin 3`: the guest in seat 3
  killed and restarted with its token gets seat 3 back (PASS).
- [ ] Step 4: commit (`net: four seats (NET_PROTO 3); Steam stays 2`).

### Task 3: Every monster and player synced

**Files:** `src/netgame.c` (snapshot scores/lives write 1054-1060 and read 1139-1145 for `n = nmon` players with an
n byte; apply `game_net_put(..., nmon)` 1556; digests 798-814; test hooks 2415-2526; dropin 2314), `src/netwire.h`
(`NET_DVALS` 8 → 16), `src/main.c` (export `void mon_enter_state(Mon *mo, int cut)` = today's `play_state` plus the
state's blend), `src/netgame.c:1507-1520` (a forced state calls `mon_enter_state`)

**Interfaces:**
- Produces: `void mon_enter_state(Mon *mo, int cut)` in main.c (declared in netgame's extern block): the clip, the
  tail blend (`tail_blend(&mo->tail, st->tail, st->blend)`), the scene blend (`cut ? 0 : st->blend`), mirror and
  entry root, as `play_state` and its caller do.
- Digest NDS_MONSTERS: 4 × (x, y, z, hp) in `v[0..15]`; NDS_GAME hashes lives and points for every player.

- [ ] Step 1: net_test.c `test_digest_four`: a digest with 4 monsters' values round-trips (16 values), and two differing
  only in monster 4's hp compare unequal. FAIL first.
- [ ] Step 2: implement; suite PASS.
- [ ] Step 3: net4_test.ps1 -Guests 3 (players 4) and -Guests 1 with players 4 (two bots on the host): all checks PASS
  (tails, points, lives, no digest differs); log the host's `snap_bytes` and assert ≤ 3,200 bytes per snapshot
  (1.5 Mbit/s at 60/s to 3 guests).
- [ ] Step 4: net_test.ps1 2-person and the offline hash unchanged; commit (`net: every monster and player synced`).

### Task 4: Seats on the host's PC and each guest's own pad

**Files:** `src/main.c:7897-7898` (setup_players: a seat played online or local is never `DRIVE_COMPUTER`; seats
taken later by a pad switch drive), `src/netgame.c:2215-2260` (`ng_slots_pads`: a host-PC pad or the numpad
pressed while open takes the next free seat ≥ 1; a pad→seat map kept per pad), `src/main.c:9275-9284` (the in-game
pads read through that map), `tools/net_local_test.ps1` (`-Seat2Online`: a guest in seat 2, then the numpad takes
seat 3)

**Interfaces:**
- Produces: `int ng_pad_seat(int pad)` (−1 none) and `int ng_seat_pad(int seat)` in netgame.c.

- [ ] Step 1: net_local_test.ps1 -Seat2Online: expected FAIL today (the numpad takes seat 2's monster).
- [ ] Step 2: implement; the test PASSES (toast "Local controller took over Player 3"); the plain run still passes.
- [ ] Step 3: commit (`net: local players take the next free seat`).

### Task 5: Menus for every seat; one-guest replies

**Files:** `src/netrep_menu.c` (NGM_INPUT's sender seat from `net_msg_slot()` → `shell_input_pad(..., seat)`,
`hud_input_pad(..., seat)`, `pause_open(v, seat)`; `paused` = the seat that paused; `NgMenuState.mon[4]`; `ngm_keep`
→ `v->mon[ng_me()]`; NGM_RESEND answered to the asker; texts "player 2" → the seat's), `src/net.c`/`net.h`
(`int net_send_msg_to(int seat, int reliable, const void *d, int n)`), `src/netgame.c:348,428-495,583` (block state
to the asker), `src/netui.cpp:360,539` (badge and panel texts per seat), `src/shell.c:3638-3640` (campaign 3/4 —
per the owner's answer)

**Interfaces:**
- Produces: `net_send_msg_to(seat, ...)` (host only; 0 sent, −1 no such guest).

- [ ] Step 1: net_test.c `test_menu_input_seat`: an NGM_INPUT from seat 3 decodes to pad 3. FAIL first.
- [ ] Step 2: implement; suite PASS.
- [ ] Step 3: net4_test.ps1 `-Menus -Guests 3`: each guest's test pad picks a different monster on Monster Select
  (`RAMPAGE_NET_TESTPAD=<k>` presses right k times then cross); the host's log names 4 different monsters at the
  start; the late guest's NGM_RESEND / block-state ask logs no re-put on the other guests.
- [ ] Step 4: commit (`net: menus for every seat; replies to the asker only`).

### Task 6: Joining mid-block with no restart

**Files:** `src/main.c` (new `int mon_add_player(Viewer *v, int monster)`: scene monster nmon (the reveal's extra
moved up one slot first, `extra_add` / `reveal_extra_slot`), `start_moves`, held/shadow/streaker init, its HUD,
place beside player 1; nmon++), `src/game.c`/`game.h` (new `void game_add_player(Game *g)`: num_players+1, lives
as the mode gives a new player, King rules recomputed), `src/netgame.c` (`ng_world_id` no longer hashes nmon or the
monsters; the host calls `mon_add_player` for a seat k == nmon; a guest whose snapshot names more monsters than it
has calls `mon_add_player` with the snapshot's monster; old `ng_dropin` removed)

**Interfaces:**
- Produces: `int mon_add_player(Viewer *v, int monster)` → the new monster's index, −1 if 4 already;
  `void game_add_player(Game *g)`.

- [ ] Step 1: net4_test.ps1 `-JoinLate`: host plays 1 player, guests join 10 s apart; checks: the host's people /
  army / traffic counts and the clock do not reset at a join (`net: flow` shows no block start after the first), no
  guest logs a world reload after its first load, final sync checks PASS. FAIL first (today: restart).
- [ ] Step 2: implement; PASS; `-HostDo` with the host climbing and holding a car during a join: state kept.
- [ ] Step 3: net_slots_test.ps1 -OnePlayer -Quick (2-person drop-in) still PASS; commit (`net: join mid-block with no restart`).

### Task 7: Launcher, lobby, texts, docs

**Files:** `launcher/src/game.rs:51-59` (online starts drop `--no-four-players`; Host LAN offered for 2-4 players),
`launcher/src/ui/mod.rs:82-94,174-177,201-205`, `launcher/src/game_tests.rs`, `lobby/src/Games.ts:75` and
`launcher/src/lobby/games.rs:177` (default max 4), lobby tests, `docs/HOSTING.md`, `README.md`, `docs/NETPLAY.md`
(seats, 4 players, not-done list), `docs/TODO.md` (Now → done)

- [ ] Step 1: game_tests `args_per_start` expects no `--no-four-players` on Host/HostLan/Join, `--no-four-players`
  still on SteamHost/SteamWait (Steam 2). FAIL first; implement; `cargo test` PASS; lobby test for default max 4.
- [ ] Step 2: build the launcher, copy to the repo root; npm test; final net4_test.ps1 full run + offline hash.
- [ ] Step 3: commit (`launcher, lobby, docs: four-player online`).
