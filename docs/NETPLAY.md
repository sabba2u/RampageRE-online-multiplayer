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
| Edibles | host | every grab made again on the guest (edible_net_gate), so it goes and plays there; its stats come by the snapshot |
| Army, traffic, bosses, street objects | each side by itself | **phase 2** |

Every 2 s the guest sends a digest of the game state, block and the buildings; the host compares it with its own of the
same moment and logs `MISMATCH` (and counts it in the panel) when two in a row differ.

## Testing on one PC

```
build.bat dev
tests\build_net_test.bat                     unit tests: wire format, snapshot buffer
tools\net_test.ps1 -TestPad -Shots 2         host + guest; the guest walks, climbs and punches by itself;
                                             build\nettest\shot_N.png shows both windows side by side
tools\net_test.ps1 -TestPad -Lag 100 -Loss 5 the internet on one PC
tools\net_test.ps1 -Seconds 0                leave both running to play them yourself
```

`RAMPAGE_NET_LAG` (ms) and `RAMPAGE_NET_LOSS` (%) work on any run; `RAMPAGE_NET_TRACE=1` logs every digest's contents.
Both copies log the game's flow (`net: flow: ...`: states, defeats, respawns, lives, results screens and their answers,
the host's events sent and taken). Test hooks, read by the host only: `RAMPAGE_NET_TESTDIE=period[,lives]` fells both
monsters every `period` seconds of play (lives: each player's from the start), `RAMPAGE_NET_TESTEND=win,secs` brings
every building down `secs` into each block, `time,secs` runs the clock out (a block with a time limit: `--play timed`).
A defeated monster lies there until its player presses a button (`Faceup_Idle_Waiting2`), as in the game.

## Hooks: where online play touches upstream's files

All of online play is in its own files (`src/net.c`, `netwire.*`, `netsnap.*`, `nettransport.*`, `netgame.c`,
`netui.*`, `src/net/third_party/` for ENet and Dear ImGui). Upstream's files have only these, each marked `NET:`:

| File | Hook |
|---|---|
| `build.bat` | the net sources in SRC; `NETINC`, `NET`, `:net_libs` (ENet and ImGui built once into `build\net`); `-lws2_32 -lwinmm -ldwmapi` |
| `src/main.c` | `netgame_shell_input` in `shell_frame` (the guest's menu presses go to the host); `netgame_game_input` before a game's menus take the pad; `netgame_shell_start` before `shell_enter` from a game; `card_job` writes no profile on the guest; `#include "net.h"`; `--host` / `--join` skipped by the argument loop; `#include "netgame.c"` before `viewer_main`; `netgame_init` after `win_open`; `netgame_frame` after the clock; `netgame_pads` before `play_step`; `netgame_after_step` after it; `netgame_draw` before both `win_swap`s; `net_stop` / `netui_shutdown` at the end; `save_profile` returns on the guest |
| `src/render_gl.c` | `netui_wndproc` first in `wnd_proc` (ImGui's input, Insert) |
| `src/building.c` / `.h` | `building_net_gate` called first by `building_hit`, `building_hit_roof`, `building_set_cell`, `building_destroy` |
| `src/shell.c` / `.h` | `shell_hold_idle` (no attract mode while someone waits online); `ShellAutoView.ms_name` (Monster Select's cards, told to the guest) |
| `src/game.c` / `.h` | `game_net_get` / `game_net_put` at the end (clock and score); `game_net_follow`, `game_net_flow_get` / `_put`, `game_net_push`, `game_net_tap` (the host's flow); the follow gate in `game_update`, the tap in `game_events` |
| `src/hud.c` / `.h` | `hud_net_tap` in `hud_input_pad` (a results screen's presses), `hud_results_close` |
| `src/main.c` (script calls, GE_REVIVE) | the guest's `Monsters::becomeHuman` / `startRespawn` do nothing; its GE_REVIVE removes no person |
| `src/people.c` / `.h` | `people_net_gate` in `emit`, `people_net_tap` in `people_events`, `people_net_eaten` in `people_eat`; the `people_net_*` section at the end (netrep_people.c) |
| `src/edible.c` / `.h` | `edible_net_gate` in `edible_grab`, once it has an edible to take |
| `src/city.c` / `.h` | `scene_person_forget` (a person made over online: its looks dropped) |
| `src/main.c` (play_step, edible_step) | the guest runs no `scene_people_spawn` / `people_update`; a held index not here yet is let go; the guest's edible events pay no stats or reactions |

With online play off (no `--host` / `--join`, panel unused) nothing in the hooks changes the game: a `--shot` of
`Chicago 1 --players 2` with a `--do` script is byte for byte the same picture as upstream's build.

## Taking upstream's updates

`tools\update_from_upstream.ps1` fetches jjmhalew/RampageRE, rebases this branch on it, rebuilds and runs the tests.
A conflict can only be in a `NET:` line above. A rename upstream of something `netgame.c` reads (a Viewer or Mon field,
a building or game function) shows up as a compile error in `netgame.c`, never as a silent change; then run
`tools\net_test.ps1 -TestPad -Shots 2` and look at both screens.
