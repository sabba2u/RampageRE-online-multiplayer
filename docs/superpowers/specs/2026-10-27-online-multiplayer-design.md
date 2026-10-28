# RampageRE online multiplayer: design

Date: 2026-10-27. Status: draft, waiting for approval.

## Intent

RampageRE (this repo, a fork of https://github.com/jjmhalew/RampageRE) is a native C port of the PS2 *Rampage: Total
Destruction*. It plays one or two players on one PC (up to four in some modes). We want two people on two PCs to play
together over the internet as if they were on the same couch: same city, same buildings falling, same score race.

We have the full source, so this is a real multiplayer stack built into the game, not a hook or an input relay bolted
on. This follows the same idea as the earlier online co-op projects (Scribblenauts, Emberville's EMP in
`I:\programming.projects\claude.code\multiplayer.litenetlib.spacewar.steamworks`), but in C instead of C#/BepInEx.

## How it should feel

1. The host starts RampageRE and picks **2 Player** in the main menu as usual.
2. Instead of waiting for a second local controller, the game offers to play online: the host opens a session and
   invites a friend through Steam (and, for testing, a direct IP / localhost option).
3. The guest joins. Their controller (or keyboard) is player 2. From then on both screens show the same game: menus,
   monster and city select, the city, the HUD, results, all driven by the host.

## Goals

- Two players, two PCs, one host and one guest, through the game's existing 2-player flow.
- **Host authoritative.** Only the host runs the real game: rules, AI, army, damage, spawns, score, game flow. The guest
  sends input and shows what the host says happened.
- **The whole world in sync**: monsters, building damage and collapse, people, vehicles, soldiers and the army, bosses,
  edibles and pick-ups, effects, sound, HUD, timers, challenges, results.
- Works over the internet through Steam (invites, NAT traversal), and also over direct IP / localhost so two copies on
  one PC can be tested without Steam.
- Playable feel for the guest under normal internet latency (~30 to 120 ms).

## Non-goals (for now)

- 3 or 4 online players. The game supports up to four locally (`--players 4`, `four_players` in `rampage.cfg`); the
  design should not rule four out, but only two players are built and tested.
- Mixing online and local players on one PC (e.g. host with two local pads plus an online guest).
- Android, Switch, Vita, Linux. Windows first; keep the code portable C where it costs nothing.
- Matchmaking, public lobbies, dedicated servers. Friends only.
- Cheating protection beyond what host authority gives naturally.
- The recompiled reference build (`build.ps1`, PS2Recomp). Only the native port (`src/`, `build.bat`) is touched.

## Anti-goals (a failure even if it "works")

- Guest's world visibly drifting from the host's: a building standing on one screen and rubble on the other, a
  different score, people or soldiers in different places for more than a moment.
- The guest's monster feeling like it's wading through mud: obvious input lag on every move.
- The single-player and local 2-player game changing behaviour in any way. Online is opt-in; with it off, the game
  must play byte for byte as upstream does.
- A fork that can no longer take upstream updates. Multiplayer code lives in its own files with small, clearly marked
  hooks into existing ones.

## What the code looks like today (from recon)

- `src/` is about 70k lines of C, SDL2 elsewhere and Win32 + XInput/raw HID on Windows. Built with `build.bat`
  (VS2022). `RampageRE.exe` is the windowed build; `rampage.exe` the same with a console.
- The main loop is in `src/main.c` (`while (!w.quit)` near the end). It uses **variable real time** (`dt` from
  `win_time()`); the world itself advances in fixed `TIMESTEP` = 1/30 s pieces in places, and `--autoplay` already runs
  on simulated time at 1/60 s, so a fixed-step mode exists in some form.
- Game state hangs off one big `Viewer` struct in `main.c` plus per-system modules: `army.c`, `boss.c`, `building.c`,
  `city.c`, `people.c`, `vehicle.c`, `edible.c`, `fx.c`, `hud.c`, `game.c`, `shell.c` (the menus), `challenge.c`, etc.
- Randomness comes from the world's two generators in `wrand.c` (Park-Miller and a lagged Fibonacci), seeded from game
  status, plus the shell's demo seed.
- Input: `pad.c` / `pad_sdl.c` poll up to four pads into `PadState`, slot k is player k+1; `main.c` maps them to each
  monster's `MV_*` value array (`moves.h`: sticks, buttons, derived directions) in `mon_step`.
- The 2-player choice is in `shell.c` (the main menu's `num_players` / `num_bots`, around line 3668).
- **Simulation and presentation are interleaved.** `play_step` and the per-system step functions mix game logic
  (damage, spawning, AI) with presentation (sounds, effects, animation posing) in the same functions. There is no
  clean "simulate" vs "draw" split. This is the main technical difficulty of this project; see "Guest puppet mode".

## The stack

Bottom to top. Each layer is its own file(s) under `src/net*`.

### 1. Transport (`net_transport`)

A small interface: open as host or guest, send a message on a reliable-ordered channel or an unreliable channel,
receive, peer connected / disconnected, round-trip time.

Two back ends:
- **Steam** (Steam Networking Sockets / Steam Datagram Relay through the Steamworks flat C API, with Steam lobbies and
  friend invites for joining). Used for real play.
- **Direct UDP** (host listens on a port, guest connects by IP; reliability done by us or by a small C library such as
  ENet). Used for localhost testing and LAN, and as a fallback.

The Steam app ID question (Spacewar 480 for development as Emberville did, or something else) is left to the builder
for development; shipping is out of scope here.

### 2. Session (`net_session`)

- Handshake: protocol version and game build must match; both sides must have a valid disc (each player uses their own
  disc image, as the port already requires). Mismatch → clear message, no connection.
- Roles: host = player 1, guest = player 2.
- Lifetime: connect, in menus, in game, results, back to menus, disconnect. A guest dropping mid-game: the host's game
  carries on with player 2 handed to the computer (as a bot) or the game ends, whichever is simpler; host dropping: the
  guest goes back to the main menu with a message.
- Hook: in `shell.c`, picking 2 Player (with online enabled) leads to host / join instead of waiting for a second pad.
  Online on or off is a setting (and a command line flag for testing, e.g. `--host` / `--join <ip>`).

### 3. Host simulation

The host runs the game unchanged, except that player 2's `MV_*` input comes from the network instead of a local pad.
The guest sends its raw pad state (the `PadState` it polls) each frame with a frame number; the host applies the newest
it has to player 2 and repeats the last one if a packet is late.

The host also owns everything the guest must not do for itself: saving the profile, unlocks, progress. The guest never
writes the host's profile and does not record online games into its own (builder may decide otherwise if it's trivial).

### 4. Replication: snapshots

The host sends the guest the state of the world many times a second (target 20 to 30 Hz, builder tunes it) on the
unreliable channel, delta-compressed against the last snapshot the guest acknowledged.

What's in a snapshot, by system (builder fills in the exact fields per system while porting):
- Game flow: game state, mode, city, block, clock, challenge state, scores and stats per player, pause.
- Monsters: position, facing, move-graph state and clip time, health, rage / power-ups, held person, climbing, mounts.
- Buildings: per-building cell damage, floor collapses, falling / rubble state (likely sent as changes, since damage
  only goes one way).
- People, vehicles, army units, bosses, edibles, street objects, rail / parade objects: id, kind, position / pose,
  animation state, alive/gone.
- HUD values that aren't derived from the above.

Entities need stable ids that match on both sides. Where a system already uses array slots (people, army units), the
slot index on the host is the id.

### 5. Events: reliable one-off messages

On the reliable channel, things that must not be lost or that are not state: a building collapsing, a big effect
starting, a sound or voice line, rumble for player 2, a challenge completing, game over / level complete, menu
transitions. The guest plays them when they arrive. Effects and sounds that only follow from replicated state (e.g.
footsteps from a walking animation) may be generated on the guest instead of sent, wherever that is easier.

### 6. Guest puppet mode

The guest runs the same program with the same data loaded, but every system runs in **puppet mode**: it does not make
decisions (no AI, no damage, no spawning, no random draws, no game-flow changes) and instead takes its state from the
latest snapshots, interpolated between the last two for smooth motion (~100 ms behind the host). It still poses
animations, draws, plays effects and sounds, and runs the camera locally.

Because logic and presentation are interleaved today, each system needs its update split (or a puppet branch) so that
the logic half is skipped on the guest and the presentation half still runs from replicated state. This split is done
system by system and is the bulk of the work. The split must not change the host / offline code path's behaviour.

### 7. Menus online

The host drives the shell (main menu after joining, monster select, city select, results, pause). The guest sees the
host's screens. *Not yet decided:* whether the guest picks their own monster with their own pad on the shared monster
select (as a couch player 2 does) — this design assumes yes, with the guest's menu input sent to the host like game
input.

### 8. Prediction (later phase)

After everything above works, the guest's own monster gets client-side prediction: the guest runs player 2's monster
movement locally from its own input and reconciles against the host's snapshots. Only if the input delay without it
feels bad. Prediction only covers the guest's own monster's movement, not damage or world effects.

## Build order (milestones)

1. **Two windows, one PC.** `--host` / `--join 127.0.0.1` over direct UDP. The guest's pad drives player 2 on the
   host. The guest shows the host's monsters moving (puppet mode for monsters only) in a fixed city and block launched
   from the command line, skipping the menus.
2. **Buildings.** Damage, collapse, rubble in sync.
3. **The rest of the world.** People, vehicles, army, bosses, edibles, street objects, effects, sound, HUD, game flow
   through one whole block to its results screen.
4. **Menus online.** Through the real 2 Player menu flow, monster and city select, campaign progression, pause,
   disconnects.
5. **Steam.** Lobbies, invites, relay.
6. **Prediction**, if needed.

Each milestone ends with two copies on one PC playing it, and a short note of what was checked.

## Testing

- Two instances on one PC over localhost is the default test rig. A debug option to add artificial latency, jitter
  and loss on the direct UDP back end, so internet conditions can be tried locally.
- A debug overlay on both sides: connection, ping, snapshot rate and size, and a world fingerprint (a hash of key
  state: monster positions, building damage, score) so drift shows up as a number.
- The existing `--autoplay` / `--shot` modes may be useful to drive the host without a human.
- Offline regression: with online off, the game must behave exactly as before.

## Constraints

- C, matching the style of the surrounding code (it comments heavily with the original's addresses; new net code
  should still read like this codebase).
- Windows build with VS2022 via `build.bat`. New dependencies (Steamworks SDK, a UDP library) vendored or fetched by the
  build, and the build must still work without Steam for the direct back end.
- Every player needs their own copy of the game disc; nothing from the disc is ever sent over the network.
- Keep upstream mergeable: new code in new files, hooks in existing files small and marked.

## Left to the builder

- Snapshot format, compression, rates, and interpolation delay.
- The UDP library (ENet or own) and how Steam is linked.
- The exact per-system puppet split and which effects / sounds are sent vs regenerated.
- Entity id scheme where slots don't work.
- How the host handles a guest drop (bot vs end game).
- Whether the main loop needs a fixed step on the host for steady snapshots.
