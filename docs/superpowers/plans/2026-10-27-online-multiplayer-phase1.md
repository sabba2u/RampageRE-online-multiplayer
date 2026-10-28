# RampageRE Online Multiplayer, Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two copies of RampageRE on one PC (or LAN) play one 2-player block together over UDP: the host runs the game, the guest's pad drives player 2, and the guest sees the host's monsters, building damage, HP, score and game flow, with a Dear ImGui net panel on both.

**Architecture:** New files `src/net*.c` + `src/netui.cpp` hold the whole stack (transport on ENet, session, snapshots, replication, ImGui panel). Existing files get small hooks marked `/* NET: ... */`. The guest still runs the game's systems locally, but every system that phase 1 replicates is gated: on the guest it makes no decisions (no damage, no HP or score changes, no game-flow changes) and takes its state from the host's snapshots. Systems not yet replicated (people, army, vehicles, effects of their own) keep running locally on the guest; phase 2 moves them over.

**Tech Stack:** C99 (zig cc 0.16.0 via `build.bat`), Dear ImGui 1.91.x (C++, `zig c++`), imgui_impl_win32 + imgui_impl_opengl3, ENet 1.3.18 (vendored C), Winsock.

**Spec:** `docs/superpowers/specs/2026-10-27-online-multiplayer-design.md`

## Global Constraints

- With online off (no `--host` / `--join`), the game must behave exactly as upstream: every hook is `if (net_role() != NET_OFF)` or equivalent.
- New code in new files; hooks in existing files small and marked `/* NET: */`.
- C99 for game code, matching the codebase's style (lower_snake names, comments in the file's voice). C++ only in `src/netui.cpp`, behind a C header.
- Windows first, built by `build.bat` / `build.bat dev`; vendored libraries live in `src/net/third_party/`.
- Nothing from the disc is ever sent over the network.
- The guest never writes the profile while online.
- UI labels in plain, simple words (user preference, from Emberville). ImGui text with `%` must be escaped as `%%`.
- Host = player 1 (slot 0), guest = player 2 (slot 1).

## Ownership table (Emberville lesson: decide this up front)

| System | Owner | Guest does | Channel |
|---|---|---|---|
| Player 2 pad | guest | sends `PadState` every frame | unreliable, 60 Hz |
| Monsters (pos, heading, vel, move state + clip time, mode, mirror, hp, energy, rage, defeated) | host | runs `mon_step` on the forwarded pads for animation/effects, then takes the snapshot's fields (interpolated, 100 ms behind) | unreliable, 20 Hz |
| Building damage (cells, state, collapse) | host | no damage of its own; applies host's changes | reliable, on change |
| Game flow (state, clock, block, results), scores, stats | host | no flow changes of its own; applies | unreliable 20 Hz (+ reliable on state change) |
| Profile, unlocks, saving | host | nothing | — |
| People, army, vehicles, bosses, edibles, street objects, effects, sound | host (phase 2) | still local in phase 1 | — |

## Review Focus

1. **Guest joins before the host has finished loading the block**: the guest must wait (show "waiting for host") and not apply snapshots for a world it hasn't loaded. Test: handshake carries city/block/mode; guest refuses to apply snapshots whose `world_id` differs from its own.
2. **Packet loss / late packets**: player 2 must keep the last pad, not drop to zero input; interpolation must hold still after 150 ms of no snapshots, not fly off. Test in `net_test`: snapshot buffer extrapolation cap and pad hold.
3. **Disconnect mid-game**: host's player 2 becomes idle (zero input) and the game carries on; guest gets a "host left" message and the panel shows disconnected. No crash, no hang.
4. **Version mismatch**: different builds must refuse to connect with a clear message. Test: handshake with wrong protocol version is rejected.
5. **Online off is untouched**: a plain `rampage.exe Chicago 1 --players 2` run behaves as before. Check: run a `--shot` before and after and compare images byte for byte.

---

### Task 1: Dear ImGui in the build, with a net panel stub

**Files:**
- Create: `src/net/third_party/imgui/` (imgui core + `backends/imgui_impl_win32.*`, `backends/imgui_impl_opengl3*`, license)
- Create: `src/netui.h`, `src/netui.cpp`
- Modify: `build.bat` (compile C++ objects with `zig c++`, link with `-lc++`), `src/render_gl.c` (forward window messages to ImGui), `src/main.c` (init, frame, draw before `win_swap`)

**Interfaces:**
- Produces (C, `netui.h`): `int netui_init(void *hwnd);` `void netui_shutdown(void);` `int netui_wndproc(void *hwnd, unsigned msg, uintptr_t wp, intptr_t lp);` (returns 1 if ImGui ate it) `void netui_frame(void);` (new frame, build windows, render) `int netui_wants_input(void);` (menu open or ImGui wants keyboard/mouse) `void netui_toggle(void);`
- Insert toggles the panel. While `netui_wants_input()`, the game sees no keys (`main.c` clears `w.keys`/`w.pressed` before using them).
- Style: `StyleColorsDark`; on/off text green (0.55, 0.95, 0.6) / grey (0.7, 0.7, 0.72); `imgui.ini` beside the exe.

- [ ] Step 1: vendor imgui and ENet sources; add C++ compile + link to `build.bat` (both player and dev builds; skipped when `--shot` needs nothing: ImGui is simply not initialised in hidden windows).
- [ ] Step 2: implement `netui.cpp` with a single "Online" window (status: Offline; buttons are added in Task 4).
- [ ] Step 3: build `build.bat dev`; run `rampage.exe Chicago 1`; press Insert; screenshot shows the panel over the city; Insert again hides it; keys don't move the monster while it's open.
- [ ] Step 4: commit.

### Task 2: Wire format and snapshot buffer (unit tested)

**Files:**
- Create: `src/netwire.h`, `src/netwire.c`, `src/netsnap.h`, `src/netsnap.c`, `tests/net_test.c`, `tests/build_net_test.bat`

**Interfaces:**
- `typedef struct { uint8_t *p; int len, cap, err; } NetBuf;` with `net_w_u8/u16/u32/f32/bytes`, `net_r_u8/u16/u32/f32/bytes` (little endian; reading past the end sets `err`, returns 0).
- Message ids (`enum NetMsg`): `NM_HELLO, NM_WELCOME, NM_REJECT, NM_PAD, NM_SNAP, NM_BUILDING, NM_FLOW, NM_PING`.
- `NetSnapBuf`: ring of 32 samples `{ double t_host; uint8_t data[]; }`; `void netsnap_push(NetSnapBuf *, double t_host, double t_local_recv, const void *data, int len);` `int netsnap_sample(NetSnapBuf *, double t_local_now, double delay, const void **a, const void **b, float *alpha);` returns 0 none, 1 hold (b only), 2 interpolate. Clock offset = min over samples of (t_local_recv − t_host). Out-of-order samples dropped. Beyond the newest sample, extrapolation is capped at 150 ms, then hold. `netsnap_clear()` for teleports / world changes.

- [ ] Step 1: tests: round trip of every writer/reader; read past end sets `err`; snapbuf interpolates halfway (alpha 0.5) between samples at t=0 and t=0.1 with delay 0.1; drops a sample older than the newest; returns hold after newest + 0.15 s; clear empties.
- [ ] Step 2: run `tests\build_net_test.bat` → fails (not implemented).
- [ ] Step 3: implement.
- [ ] Step 4: tests pass.
- [ ] Step 5: commit.

### Task 3: Transport on ENet

**Files:**
- Create: `src/nettransport.h`, `src/nettransport.c`
- Test: `tests/net_test.c` (loopback test)

**Interfaces:**
- `int nt_host(int port);` `int nt_join(const char *ip, int port);` `void nt_close(void);`
- `int nt_send(int reliable, const void *data, int len);` (channel 0 reliable, 1 unreliable sequenced)
- `int nt_poll(NtEvent *ev);` with `NtEvent { int type /* NT_CONNECT, NT_DISCONNECT, NT_DATA */; const uint8_t *data; int len; }` (data valid until next poll)
- `int nt_rtt_ms(void);` `int nt_connected(void);`
- Debug: env `RAMPAGE_NET_LAG=ms`, `RAMPAGE_NET_LOSS=percent` delay/drop on send (simple queue) for local testing.
- Default port 27960.

- [ ] Step 1: test: host on 27961, join 127.0.0.1, pump both until connected (≤2 s), send reliable "hi", receive "hi" on host.
- [ ] Step 2: fail, implement, pass. Step 3: commit.

### Task 4: Session: host / join, handshake, player 2's pad over the wire

**Files:**
- Create: `src/net.h`, `src/net.c` (the public face the game hooks call)
- Modify: `src/main.c` (args `--host [port]`, `--join ip[:port]`; `net_update()` once a frame after `pad_poll`; player 2's pad), `src/netui.cpp` (status, ping, host/join buttons, log)

**Interfaces:**
- `enum { NET_OFF, NET_HOST, NET_GUEST };` `int net_role(void);` `int net_live(void);` (connected and in the same world)
- `void net_start_host(int port);` `void net_start_join(const char *addr);` `void net_stop(void);`
- `void net_update(double now);` (poll transport, handle messages)
- `void net_set_world(uint32_t world_id);` world_id = hash(city, block, mode, players) set by main.c when a block starts; handshake `NM_HELLO { proto=1, build_id, world_id }`, `NM_WELCOME` or `NM_REJECT { reason }`.
- `void net_send_pad(const PadState *p);` (guest, every frame) and `int net_remote_pad(PadState *out);` (host: last received, held when late; returns 0 if never received / disconnected → caller uses zero pad).
- `void net_log(const char *fmt, ...);` (ring of 200 lines, shown in the panel, also to stdout).
- Hook in main.c: on the host, slot 1's `PadState` is replaced with `net_remote_pad`; on the guest, the local slot 0 pad is sent as player 2 and both local monsters' pads are taken from the host's forwarded pads (`NM_SNAP` carries both players' pads).

- [ ] Step 1: test (net_test): handshake with wrong proto is rejected with a reason; with matching proto is welcomed.
- [ ] Step 2: implement; launch two windows: `rampage.exe Chicago 1 --players 2 --host` and `rampage.exe Chicago 1 --players 2 --join 127.0.0.1`; guest keyboard moves player 2 on the host's screen.
- [ ] Step 3: commit.

### Task 5: Monster replication

**Files:**
- Create: `src/netrep.c` (+ declarations in `net.h`)
- Modify: `src/main.c` (after `play_step`'s monsters step: host collects, guest applies)

**Interfaces:**
- `NetMonState { float pos[3], heading, vel[3]; int16_t state; float time; uint8_t mode, mirror, defeated; float hp, energy, rage; }` per slot; `NM_SNAP { u32 seq; f64 t_host; u8 n; NetMonState m[n]; PadState pads[2]; flow block (Task 7) }`.
- Host: 20 Hz, unreliable. Guest: positions/heading interpolated through `NetSnapBuf` at 100 ms delay; discrete fields (state, mode, mirror, defeated, hp...) from the newer sample; if the guest's `run.state` differs, it is forced to the host's state at the host's clip time.

- [ ] Step 1: implement; two windows: both monsters move the same on both screens; jumping, climbing, punching look right on the guest; RAMPAGE_NET_LAG=80 stays smooth.
- [ ] Step 2: commit.

### Task 6: Building damage owned by the host

**Files:**
- Modify: `src/main.c` / `src/building.c` at the damage entry points (gated `net_role() == NET_GUEST` → no damage applied)
- Modify: `src/netrep.c`

**Interfaces:**
- Host compares each building's cell damage and state with what it last sent; changes go out reliable as `NM_BUILDING { u16 building; u8 state; u16 ncells; {u16 cell, u8 dmg}... }` (exact fields chosen from the building struct while implementing).
- Guest applies them through the same functions the game uses when damage happens, so cracks, collapse and rubble animate and sound as normal.

- [ ] Step 1: implement; two windows: punch a building on either side, cracks appear on both; a building falls on both.
- [ ] Step 2: commit.

### Task 7: Game flow, HP, score, HUD

**Files:**
- Modify: `src/netrep.c`, hooks in `src/game.c` / `src/main.c` where scores, clock and game state change (guest: no changes of its own).

- [ ] Step 1: implement: the snapshot's flow block carries game state, clock, per-player score and stats; guest applies; block end shows results on both.
- [ ] Step 2: commit.

### Task 8: Desync check and net panel

**Files:**
- Modify: `src/net.c`, `src/netui.cpp`

- [ ] Step 1: every 2 s both sides compute a fingerprint (monsters' rounded positions and hp, building states, scores); guest sends its own; host logs MISMATCH when two in a row differ. Panel shows role, peer, ping, snapshots/s, bytes/s, last fingerprint result, and the log.
- [ ] Step 2: run two windows with RAMPAGE_NET_LAG=100 RAMPAGE_NET_LOSS=5 for a whole block; no MISMATCH for replicated systems.
- [ ] Step 3: offline regression: `--shot` image of `Chicago 1 --players 2` matches the upstream build's byte for byte.
- [ ] Step 4: commit.

## Phase 2 (separate plan, after phase 1 works)

People, army, vehicles, bosses, edibles, street objects, rail/parade, effects and sounds owned by the host; the online flow from the 2 Player menu (host / join in the menus, monster and city select, campaign); Steam (ISteamNetworkingSockets over SDR, lobbies, invites, `+connect_lobby`, app 480 for dev); prediction for the guest's own monster if needed.
