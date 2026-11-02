# RampageRE: Online Multiplayer

> **This fork adds online play for up to 8, a launcher, Boss Rumble and more to
> [jjmhalew/RampageRE](https://github.com/jjmhalew/RampageRE).** Everything below the line is the original project's
> README. All credit for the port itself goes to jjmhalew; this fork only adds what this first part describes (branch
> `online-multiplayer`).

## Getting started

1. Download **all.zip** from this fork's [Releases](https://github.com/sabba2u/RampageRE-online-multiplayer/releases)
   and unzip it anywhere.
2. Double-click **RampageRE-Launcher.exe**. Windows may say "Windows protected your PC" (the exes are not signed):
   *More info*, then *Run anyway*.
3. Click **Play Local**. The first time, the game asks for your own disc image of *Rampage: Total Destruction* (the
   European or USA PS2 `.iso`); it reads it where it is and remembers it. Nothing from the game is in the zip.
4. Optional: the Wii version's ten extra monsters and Dallas from your own extracted Wii disc (the game asks once;
   see "The Wii's monsters and Dallas" below).

The zip holds `RampageRE.exe` (the game), `RampageRE-dev.exe` (the same game with a console, for the command line),
`RampageRE-Launcher.exe`, Steam's `steam_api64.dll` and `steam_appid.txt` (for Host on Steam), the `lobby/` room
server, and the docs this README links to.

## Online, up to 8 players (work in progress)

Up to eight players, each on their own PC (or up to four on the host's PC with its controllers, the rest online). The
**host** is player 1 and runs the game; each **guest** is player 2 to 8 (in the order they join) and sees the host's world: the monsters, buildings, people, army, traffic, bosses, score, clock, results and Game Over all come from the
host. Each player needs their own copy of the game disc and the same build of this fork.

### The easy way: the launcher

Unzip the release and double-click **RampageRE-Launcher.exe**: big buttons, nothing to set up.

- **HOST A GAME (LAN)**: pick **Players** under the big button. **2** or **5 to 8** players: **Host Local (LAN)** starts
  the game already hosting, and up to seven friends join your address; in the game pick that many players (**5 Player** ..
  **8 Player** are in the menu when hosting). **1, 3 or 4**: the normal game on this PC (**Play Local**), one controller
  each (a PC takes four). **Join Local (LAN)** joins a game hosted on
  this PC (127.0.0.1).
- **HOST AN INTERNET GAME**: **Run an Internet Multiplayer Server** shows a 5-letter room code and links to send your friends (the
  launcher opens your router for them), and a **Players** list with Mute, Kick and Ban.
- **JOIN A ROOM**: click the link your host sent, or **Connect** to their server and **Join** with the room code.
- **HOST ON STEAM**: **Host on Steam** (then Shift+Tab in the game to invite friends), or **Join on Steam** (then
  right-click your friend in Steam's friends list and pick **Join Game**). Steam play borrows Valve's free test app
  **Spacewar** (the launcher explains it): add it to your Steam library once.
- The first time, click **REGISTER RAMPAGE:// LINKS** so a link from a friend opens the game straight away.
- **Settings**: join links, servers, banned players, and the **Game** options below.

**The launcher's Game options** (Settings; each is passed to the game every time it starts, so nothing in your profile
changes):

| Option | What it does |
|---|---|
| **Unlock all monsters** (on) | every monster (all 40 with the Wii data) and every city |
| **Unlock all moves** (on) | every monster's upgrade moves from the start; untick it and each monster has only the upgrades it earned |
| **Shuffle the city's buildings** | every game a new street: each lot gets another of the city's buildings of its size (online, the host's) |
| **Time of day** | As the game picks / Always day / Always night |
| **Weather** | Clear / Rain / Storm (rain, lightning and thunder; a bolt can zap a monster, which flashes as an x-ray) |
| **Look** | Normal / 1950s monster movie (black and white, scratches, grain) |
| **Smooth edges** | Game's setting / Medium / High (anti-aliasing) |

**Play Local**'s **Players** choice (1 to 4) sets how many the game's menus offer.

**Which "server" is which:** the game host is inside **RampageRE.exe** (every way of hosting uses it); the room server
(codes, links, the player list) is in the launcher, or the Node `lobby/` for a VPS, and is not in the game. Read
[docs/HOSTING.md](docs/HOSTING.md): what runs where, what each button starts, and the ports.

Details: [docs/NETPLAY.md "The launcher"](docs/NETPLAY.md#the-launcher-launcher-rust).

### How to play

1. Both players start RampageRE.
2. Press **Insert** at any time to open the online panel.
3. **Host:** press **Insert** and click **Host a game**, then pick **2 to 8 Player** in the main menu (from the launcher,
   or with `menu_players=8` in `rampage.cfg` / `--menu-players 8`, the menu has 5 to 8 Player too), and **0** computer
   players for a game of friends only.
   The game uses UDP port **27960**: the game asks your router to forward it by itself (UPnP); the panel's
   **Port forward** line says whether it worked and the address friends join. If your router has no UPnP (or it is
   off), forward the port by hand, or use Steam, a LAN or a VPN like Tailscale or ZeroTier.
4. **Guest:** press **Insert**, type the host's address and click **Join**. Your screen then shows the host's menus
   (main menu, Monster Select with every player's card (two rows of four with 5 to 8 players; players 5 to 8 have the
   colours of 1 to 4), City Select, and in the game its Pause menu and results); your
   controller or keyboard picks your own card on the host's Monster Select, and you all end up in the same city.
   What you earn as the guest (monsters unlocked, your monster's upgrades, campaign blocks won) is kept in your own
   profile; the host's profile never overwrites yours.
5. The panel shows who is host, the ping, the updates a second (20 / 30 / 60) and whether everything is in sync.
   The window title also says **HOST** or **GUEST**.
6. **Chat:** press **Enter** in a game to type, **Enter** sends, **Esc** cancels (**P** pauses). Lines show bottom
   left and fade out; what you say also floats over your monster's head. `/help` lists the commands.
7. While a player has a menu up (the Insert panel, the chat box or the Pause menu), **IN MENU** floats over their
   monster on the other players' screens. Any player's Start pauses the game for everyone ("Player 3 paused the game").

8. **Dropping out and coming back:** if a guest's connection drops (or their game crashes), their monster stands
   still, takes no damage, turns grey, and a big red **DISCONNECTED / Rejoin 0:47** floats over it on every screen.
   The same player joining again within the minute (by code, address or link: their game remembers a rejoin token,
   also in `netrejoin.cfg`) gets the same monster back mid-round, score and health as they were. After the minute the
   slot says **WAITING FOR PLAYER** (yellow) and anyone can join and take that monster over as it is. A hosted game
   shows WAITING FOR PLAYER over a seat nobody has joined yet, unless someone on the host's PC presses a button on
   another controller (or the numpad): it takes the next free seat (**Local controller took over Player 3**) and it is
   theirs (the panel's **Open for online** gives it back; **Kick** removes an online player).
9. **Joining a game already going:** a friend who joins mid-game walks straight in as the next player, beside player 1:
   nothing starts over (the buildings, people, army, clock and everyone's score go on), up to four players.
10. **Re-synchronize players** (host's panel, or `/resync`; a guest's panel asks the host): every guest takes the host's
   whole world again, without leaving. A safety net if something looks different on the two screens.

Command line, for testing: `RampageRE-dev.exe Chicago 1 --players 2 --host` on one PC and `RampageRE-dev.exe --join <ip>` on the
other. `tools\net_test.ps1 -Seconds 0` starts a host and a guest on the same PC.

### Finding games: the lobby

Instead of passing an IP address around, run the **lobby** (the room server: `lobby/`, a small Node server, or the one
built into the launcher) and the guest picks the game from a list. The lobby only lists games and hands out addresses;
the game itself still goes straight from guest to host ([docs/HOSTING.md](docs/HOSTING.md)).

- **On the host's PC** (no server needed): install [Node.js](https://nodejs.org) 20+, double-click `lobby\start.bat`.
  The host's game lists itself with `http://localhost:19980` by default. Guests set the panel's **Lobby** field to
  `http://<host's address>:19980` (TCP 19980 must be reachable too, next to UDP 27960).
- **On a VPS** (always on, for everyone): `lobby/start.sh` there (or as a service), open TCP 19980, and every player
  sets **Lobby** to `http://<vps>:19980` (or starts the game with `--lobby http://<vps>:19980`, or sets `RAMPAGE_LOBBY`).
- **Host:** host as usual. With **List my game in the lobby** ticked (the default) the panel shows a short **join code**
  like `K7PQ2` with a **Copy** button: say it or paste it to your friend.
- **Guest:** Insert, **Games online** lists the games (refreshing itself while open); click **Join**. Or type the code
  under **Join code**, or start with `RampageRE-dev.exe --join K7PQ2`. Games of another build are greyed out.
- **Browser:** `http://<lobby>:19980/` lists the games live, with their codes and Join links (`rampage://join/<code>`).
  The page's setup scripts teach Windows or a Linux desktop to open those links with the game, once per PC.

Details, ports and the VPS setup: [docs/NETPLAY.md "The lobby"](docs/NETPLAY.md#the-lobby-finding-games).

### Camera and look (also new in this fork)

The Insert panel has a **Camera and look** section with picture effects the original game never had. They work online
and offline, each player sets their own, and they are kept in `lookfx.cfg` for next time:

- **Background blur** (depth of field): the monsters stay sharp and the distance goes soft. It focuses on what is in the
  middle of the screen by itself. **Sharp distance** sets how much around the focus stays sharp.
- **Tilt-shift**: the top and bottom of the picture blur while a band across the street stays sharp, so the city looks
  like a toy model. **Sharp band height** and **Sharp band place** move the sharp band; **Toy colours** makes the
  colours livelier, like a miniature.
- **Zoom out**: pulls the camera back to see more of the block.
- **Tilt camera down**: looks down on the city from higher up (up to 35 degrees).
- Presets: **Miniature city** (zoomed out, tilted down, tilt-shift), **Soft background** (depth of field) and
  **The game's look** (everything off).

The score and timer always stay sharp. The blurs switch themselves off while MSAA is on (F7); SMAA is fine. The camera
change is only what you see: the game still spawns things for its own camera, so zoomed far out you may see people or
cars appear at the edges.

### Boss Rumble (new game type)

A survival mode for 1 to 4 players on the same side, offline or online, made for this fork.

- **Start it:** on the main menu, press **left or right** to the fifth icon (the ape and the lizard): **BOSS RUMBLE**.
  Pick players, bots (they fight with you) and monsters; there is no City Select, the game picks a random city and
  block. From the command line: `RampageRE-dev.exe <iso> --play rumble`.
- **The run:** 12 waves of bosses, 1, 1, 1, then 2, 2, 2, then 3, 3, 3, then 4, 4, 4 at once. Any monster can be a
  boss; bosses have a **red glowing outline** and a health bar with their head and name under the clock. Every three
  waves they get 25 % more health and hit 25 % harder. A "WAVE N" banner comes between waves.
- **The clock counts up:** how long you last is your score. The team shares 3 lives, as in the campaign.
- **Power-ups:** the game's own food and items appear on the street and on rooftops (up to 4 at a time, each gone after
  20 s); walk into one to take it.
- **The bosses' manners:** a boss catches its breath for a second or so after each attack, and a player knocked down
  or hit three times in a row is left alone for 2 seconds, so nobody gets juggled against a wall.
- **The end:** clear wave 12 and it's a **VICTORY**; lose every life and it's Game Over. Both screens show the time,
  the wave and your **best time** (kept in `rumble_best.txt` beside your profile; online, each PC keeps its own).
- **Online:** the host runs the bosses; guests see the same waves, bars and power-ups. Everyone must be in before the
  run starts: once the waves have begun, new players are told the game is full (a player who drops can rejoin).

### Other changes in this fork

- **Outlines:** a boss glows red; an online seat nobody is playing (disconnected) is drawn grey.
- **8 players online**: the host and seven guests, through the menus (5 to 8 Player, Monster Select's 8 cards), King
  of the City and the other modes; the HUD puts the eight panels in one row. Up to 8 monsters on screen in Boss Rumble
  too (players and bosses).
- **Results screens** list 6 and 8 players.
- **Fixed:** the tentacles of Cal, Eyegore, Venus, Natalie and Jill were frozen straight (the face animation
  overrode them; their tips sank into the street). They now curl, stretch and plant as on the console.
- **Texture and model mods:** PNGs in `mods/textures/` ([docs/TEXTURES.md](docs/TEXTURES.md)), glTF models in
  `mods/models/` ([docs/MODELS.md](docs/MODELS.md)).

### Please read: what has and has not been tested

This is early. Expect issues.

- **Up to 8 players** (the host and seven guests). Eight players have been through the menus into a game on one PC
  (`tools
et8_menu_test.ps1`), never yet on eight PCs. Host on Steam takes 8 as well (not yet tried with real Steam
  accounts). With three guests the host sends each about 200 KB/s (4.8 Mbit/s of upload in all on average, 6.6 at peak);
  with seven, plan on about 1.4 MB/s (11 Mbit/s) of upload: hosting 8 needs a good connection.
- **Tested only on one PC**, up to four copies talking over localhost, including with simulated lag and packet loss.
  It has **not** been played between two real PCs over the internet yet.
- **Steam has not been tested.** The code is there (Host on Steam, Invite a friend, Join Game from the friends list,
  using the Spacewar test app 480), but no one has joined a game through Steam yet.
- **Only tested with George and Lizzie.** Ralph and the other monsters, and the **Wii disc's extra monsters**, have not
  been tried online at all.
- Mostly tested in **Chicago**. Other cities, their bosses, King of the City / King of the World, the Timed Run's
  details, Change Monster, power-ups being grabbed and the end-of-city screens have not been tested online.
- The guest has a small delay on their own monster (it is not predicted on the guest's PC yet).

If something goes out of sync, the panel names the system (for example "Out of sync: army"). Bug reports with what you
were doing, the panel's log and both windows' `rampage.log` / console output are very welcome.

How it works, what is shared and where it hooks into the original code: [docs/NETPLAY.md](docs/NETPLAY.md).

---

# RampageRE

[![build](https://github.com/jjmhalew/RampageRE/actions/workflows/build.yml/badge.svg)](https://github.com/jjmhalew/RampageRE/actions/workflows/build.yml)

A native PC port of *Rampage: Total Destruction* (PS2, Pipeworks / Midway, 2006) that runs the game from
**your own disc**: no game files are included in this repository.

It has two halves, the same way as [WoodyRE](https://github.com/jjmhalew/WoodyRE):
- **Recompiled base.** The original game code, statically recompiled to C++ with
  [PS2Recomp](https://github.com/ran-j/PS2Recomp). It runs the real game logic and serves as the reference.
- **Reimplementation.** The game's formats are reverse engineered, documented in `docs/` and read by tools
  that are checked byte for byte against every file on the disc. Those parts then replace the recompiled code
  with readable native code: first the scripts and data, then the renderer, then the game systems.

> Unofficial fan project for preservation. Not affiliated with or endorsed by Pipeworks, Midway, Warner Bros.
> or Sony. Rampage and related names are trademarks of their owners. You need your own copy of the original game.

## Status

**Playable from start to end in the native port** (`RampageRE.exe`, [docs/NATIVE.md](docs/NATIVE.md)): the front end
(legal screen, intro movies, the attract mode and its demo games, menus, profiles, monster and city select), the
campaign's seven cities block by block
with their challenges, bosses, results screens and the credits, the other play modes, one or two players as on the PS2 (computer monsters
included; up to four in King of the City, King of the World and the Timed Run, as on the game's other consoles (`four_players=0`
in `rampage.cfg` keeps the PS2's menu); the command line's `--players 4` in any mode), sound and music, the Pause menu and saving the profile. It runs on Windows, Linux, the Switch and the PS Vita (homebrew)
and Android, on keyboard, controllers (rumble included) or the touch screen, with texture packs and graphics
settings (GTAO, SMAA / MSAA). It has been played on Windows; the Switch, the Vita, phones and controllers are not yet tried on
real hardware. Still missing: the smaller gaps that the notes in `docs/` list under "Not ported" (and mark [G] where
the port guesses). After v1: the two arcade games of Bonus Content (Midway Classics), separate emulator programs on
the disc rather than part of the game ([docs/ARCADE_NOTES.md](docs/ARCADE_NOTES.md)).

**Optional, the Wii version's extras** (a port extra, off unless you give it your own Wii disc's files; see "The Wii's
monsters and Dallas" below): its ten more monsters, Dallas as an eighth city (the campaign's fourth) with its two-boss block, and
the fifth upgrade, SuperJump.

The recompiled game (`build.ps1`) is the reference the port is checked against. It runs the front end and the first
campaign level, Las Vegas, with errors in the 3D picture (black ground), slowly and without sound
([docs/NOTES.md](docs/NOTES.md)).

| Step | |
|---|---|
| Recompile the whole ELF to C++ (8,818 functions, 0 errors) | ✅ |
| Boot, C++ static constructors (280) | ✅ |
| IOP modules (sio2man, libsd, MultiStream) | ✅ |
| File system | ✅ |
| zlib asset loading, data archive mounted, menu loop | ✅ |
| Graphics (GS / VU1): legal screen, GUI (autosave notice) | ✅ |
| Game scripts compile and run in the recompiled game (shell, attract mode) | ✅ |
| Movies (`.pss`) decode and play | ✅ |
| Front end: attract mode, main menu, profile entry | ✅ |
| Into a game: campaign, monster select, Las Vegas loads and runs | ✅ (black ground, slow, no sound) |
| Scripts (`.bsf`) decompiled to source, 138/138 rebuilt byte for byte | ✅ [docs/FORMAT_BSF.md](docs/FORMAT_BSF.md) |
| Bundles (`.bdp`/`.cmp`): container parsed, 1,428 textures to PNG | ✅ [docs/FORMAT_BDP.md](docs/FORMAT_BDP.md) |
| Meshes and materials to glTF (5,577 meshes), three.js viewer | ✅ |
| Property books (`.prb`: city layout, buildings, AI, challenges), 28/28 decoded | ✅ |
| All 7 cities assembled block by block in glTF (buildings with generated facades, street objects) | ✅ |
| Skeletons and animations (60 skeletons, 5,379 animations): all 30 monsters animated in glTF | ✅ |
| City models with node trees (people, vehicles, bosses) with their clips; moving props loop in the blocks | ✅ |
| People and traffic in the blocks (379 people, 375 vehicles; lanes from the game's traffic code) | ✅ |
| Lighting: the game's lights, fog, drifting clouds; every block also at night | ✅ |
| Effects (297, rebuilt byte for byte), string tables (13, 9 languages), GUI layouts (XML) | ✅ [docs/FORMAT_EDF.md](docs/FORMAT_EDF.md) |
| Native viewer in C (`RampageRE-dev.exe`): every city block from `RAMPAGE.VOL`, lit, day and night, traffic, animated people and props, any monster with its clips | ✅ [docs/NATIVE.md](docs/NATIVE.md) |
| The game's play camera (TwoTilts) transcribed; walk a monster about with it following | ✅ [docs/CAMERA.md](docs/CAMERA.md) |
| Monster move graphs (`MONSTER_DATA`): all 30 parsed, the pad variables and the game's runner decoded; clip events (script calls, movement modes), jump physics and the landing slots, the state nodes (playback rate, flags), the game's steering, air control and clip blending; the native viewer plays every monster by its own graph, jumps included, its punches damage the buildings' cells by the game's rules until floors collapse and buildings fall to rubble, and it climbs them | ✅ [docs/FORMAT_MONSTER.md](docs/FORMAT_MONSTER.md) |
| The game in the native port: front end and shell, profiles, game flow, challenges, people, army, traffic, bosses, edibles, effects, sound and music, movies, HUD and Pause | ✅ [docs/NATIVE.md](docs/NATIVE.md) |
| Linux, Switch and PS Vita (homebrew) and Android builds, controllers and rumble, touch pad, texture packs, graphics settings, CI | ✅ (not yet tried on real hardware) [docs/PLATFORM_PLAN.md](docs/PLATFORM_PLAN.md) |
| The attract mode's demo games (a random city, block and monsters, played by the computer) | ✅ [docs/SHELL_NOTES.md](docs/SHELL_NOTES.md) |
| The Wii version's extras from your own Wii disc: 40 monsters, Dallas, SuperJump and its fifth star, the Wii's unlocks | ✅ [docs/WII_PLAN.md](docs/WII_PLAN.md) |
| The arcade games (Midway Classics: Digital Eclipse's emulators on the disc) | after v1 [docs/ARCADE_NOTES.md](docs/ARCADE_NOTES.md) |
| Coverage of the game's code: every function of the ELF ported, read, replaced by the port's own, unused by the data, or open (0.8 % of the code: the monsters' clip effects and roar, the tail motion, a few smaller items) | [docs/COVERAGE.md](docs/COVERAGE.md) |

**Supported versions:** the European PS2 disc, *Rampage - Total Destruction (Europe) (En,Fr,De,Es,It)*,
SLES-53800, `SLES_538.00` version 1.00, and the USA disc, *Rampage - Total Destruction (USA)*, SLUS-21323,
`SLUS_213.23` version 1.01. Both hold the same game, byte for byte (the ELF and `RAMPAGE.VOL`); only `SYSTEM.CNF`
differs, which picks the region: the USA disc plays the NTSC movies and has the Ballers box in the Midway Classics.
Other releases may have different addresses and may not work as is.

## Building
Needs Windows, Python 3, Git and Visual Studio 2022 or later with the *Desktop development with C++* workload.

```powershell
git clone --recursive https://github.com/<you>/RampageRE.git
cd RampageRE
.\build.ps1 -Iso "D:\path\to\Rampage - Total Destruction (Europe) (En,Fr,De,Es,It).iso" -Run
```

The first build extracts the disc into `build\disc`, recompiles the game to C++ and builds it (about 30 minutes).
After that, `.\build.ps1 -Run` rebuilds and starts it. Everything generated lives in `build\` and in the
PS2Recomp submodule's `src/runner` folder, which are not committed.

### Prebuilt
The native port ([docs/NATIVE.md](docs/NATIVE.md)) is built by GitHub Actions on every push and pull request:
`RampageRE.exe` and `RampageRE-dev.exe` for Windows (`build.bat`) and `rampage` for Linux (`build.sh`, needs SDL2). Each run
of the [build workflow](https://github.com/jjmhalew/RampageRE/actions/workflows/build.yml) keeps them as artifacts, and
each version tag publishes them on the [releases](https://github.com/jjmhalew/RampageRE/releases) page. They hold
only the port's own code (the programs, README.md, LICENSE and two docs), **no game data**.

**Windows**: start `RampageRE.exe` (the exe is not signed, so SmartScreen may say "Windows protected your PC" at the
first start: *More info*, then *Run anyway*). At the first start it asks for your disc image (`.iso`) of the game, or
the `RAMPAGE.VOL` of a copy of the disc, in a file dialog; it reads it where it is (nothing is copied) and remembers it
in `%LOCALAPPDATA%\RampageRE\disc.txt`. It starts in the front end; `rampage.cfg`, the profile, `rampage.log` and
`mods\` are written beside the exe (or in `%LOCALAPPDATA%\RampageRE` when that folder is read-only or temporary).
`RampageRE-dev.exe` is the same program with a console, for the command line of [docs/NATIVE.md](docs/NATIVE.md):
`RampageRE-dev.exe "D:\Rampage - Total Destruction (Europe) (En,Fr,De,Es,It).iso" Chicago 1`, or the `RAMPAGE.VOL` from
it (`RampageRE-dev.exe D:\RAMPAGE.VOL Chicago 1`). Without a path both look for the data beside themselves, in the current
folder and in `%LOCALAPPDATA%\RampageRE` first (`~/.local/share/RampageRE` on Linux; the whole list is in
[docs/NATIVE.md](docs/NATIVE.md) "Game data"). They do not need `build.ps1`. The recompiled game (`build.ps1`) is not
built there, as it needs the disc.

**Linux / Steam Deck**: unpack `RampageRE-linux-x86_64-<tag>.tar.gz` and start `./rampage ~/Rampage.iso` (your
disc image or `RAMPAGE.VOL`; needs SDL2, which SteamOS has). It starts in the front end. On the Steam Deck add
`rampage` to Steam as a non-Steam game with the disc image's path as its launch option, so Steam hands its controls over
as a pad.

**Nintendo Switch** (homebrew, experimental, not yet tried on a Switch): unpack `RampageRE-switch-<tag>.zip` onto the
SD card (or build `rampage.nro` with `./build_switch.sh`, devkitPro or its Docker image), put your disc image (or
`RAMPAGE.VOL` with `RAMPAGE.ELF`) into `/switch/rampagere/` beside it and start it from the Homebrew Menu, preferably
by holding R while starting a game (title takeover: more memory than the Album applet). It starts in the front end;
B is Cross, the buttons go by place; `rampage.cfg`, the profile and `rampage.log` live in `/switch/rampagere/`
([docs/NATIVE.md](docs/NATIVE.md) "Nintendo Switch").

**PS Vita** (homebrew, experimental, not yet tried on a Vita): install `rampage.vpk` from `RampageRE-vita-<tag>.zip`
with VitaShell (or build it with `./build_vita.sh`, VitaSDK or its Docker image) and put your disc image (or
`RAMPAGE.VOL` with `RAMPAGE.ELF`) into `ux0:data/rampagere/`. It needs `ur0:data/libshacccg.suprx`, the system's shader
compiler: run ShaRKBR33D once to extract it. It starts in the front end on OpenGL ES 2.0 (no GTAO / SMAA); the rear
touch pad's halves are L2 / R2; `rampage.cfg`, the profile and `rampage.log` live in `ux0:data/rampagere/`
([docs/NATIVE.md](docs/NATIVE.md) "PS Vita"). Whether a Vita is fast enough, and has memory enough, is not known yet.

**Android** (7.0 and up, arm64 or x86_64; so far tried on the emulator only): install the APK from
`RampageRE-android-<tag>.zip` (or build it with `android\gradlew.bat -p android assembleDebug`). At the first start it
asks for your disc image (`.iso`) in the system's file picker and copies `RAMPAGE.VOL` and the game's ELF out of it
into its own folder once (1.35 GB); an APK with your data already inside (`make_android_bundle.bat`, below) neither
asks nor copies. It starts in the front end and plays on the on-screen pad (a stick on the left,
Cross / Square / Triangle / Circle, R2 and Start on the right) or on a controller; the back button is back or the
Pause menu ([docs/NATIVE.md](docs/NATIVE.md) "Android").

### The Wii's monsters and Dallas (optional)
The Wii version adds to the PS2 game ten monsters (Boris and Ruby from the start; Plucky, Fifi and Cyril
revealed in Dallas; Squirmy, Flea, Sarah, Brian and VERN for 1, 3, 6, 12 and 20 monsters with all five upgrades),
Dallas as the fourth of eight cities with a block of two bosses, and a fifth upgrade, SuperJump (Cross pressed again
while landing). The port plays them from **your own Wii disc**, next to the PS2 data, which it still needs:
extract the disc with Dolphin (the game's *Properties*, *Filesystem*, right-click the disc: *Extract Entire Disc*). Only its
`DATA/files/Game`, `Shell` and `Audio` folders are read (about 320 MB). Tried with the European Wii disc (RPGP5D).

| Where | How |
|---|---|
| Windows | `RampageRE.exe` asks once, at a start when nothing names it: *Yes* opens a folder dialog for the extracted disc (the folder with `DATA`, or its `DATA/files`), *No* asks no more; the answer is kept in `rampage.cfg` as `wii_data=` (delete that line to be asked again). Or `RampageRE-dev.exe ... --wii DIR`, or `RAMPAGE_WII_DATA` |
| Linux | a `wii` folder with `Game`, `Shell` and `Audio` in the folder `./rampage` is started from (beside its `rampage.cfg`), found on its own; or `wii_data=` in `rampage.cfg` set to the extracted disc, or `--wii DIR`, or `RAMPAGE_WII_DATA` |
| Switch, PS Vita | a `wii` folder with `Game`, `Shell` and `Audio` in `/switch/rampagere/` or `ux0:data/rampagere/`, found on its own |
| Android | the same `wii` folder in the app folder by USB cable, or the launcher's shortcut **Add the Wii's data** (a long press on the icon), which copies it from the folder you pick; after the first start's copy the app asks once |
| `make_standalone.bat`, `make_android_bundle.bat` | drag the extracted disc's folder onto the .bat with the rest (or leave `wii_data=` in the `rampage.cfg` beside it): it goes into the exe or APK and is used from there |

Without it the port is the PS2 game, byte for byte, and saves as it did. With it the shell, the profile and the
unlocks follow the Wii's roster of 40 monsters and its city order; the save stays the PS2's (`rampage_profile.bin`),
with what the PS2 file cannot hold in `rampage_profile.wii.bin` beside it. `wii_data=none` leaves it out (and a `wii` folder too).
It has been played on Windows (and on the Android emulator); the Linux build has drawn Dallas from it (offscreen, under WSL) but is not yet played with it, and the Switch, the Vita and phones are not yet tried with it. As with the PS2 data, the bundles that hold it
are for your own use only.

### Building the native port
Windows 10 or 11; nothing needs to be installed first (the scripts are WoodyRE's, adapted).

| Script | Builds |
|---|---|
| `build.bat` | `RampageRE.exe`, the player build: no console, the icon and version of `res/rampagere.rc`, starts in the front end, asks for the disc image at the first start, logs to `rampage.log`, says in a message box when it cannot start |
| `build.bat dev` | `RampageRE-dev.exe`, the developer build: the same program with a console that prints what each block holds (the one docs/NATIVE.md and the tests use; `build_viewer.bat`, the old name, builds it) |
| `build.sh` | `./rampage` for Linux and the Steam Deck (SDL2; `GLES=1` for OpenGL ES; `build_viewer.sh` is the old name) |
| `build_switch.sh` | `rampage.nro` for the Switch (devkitPro, or its Docker image) |
| `build_vita.sh` | `rampage.vpk` for the PS Vita (VitaSDK, or its Docker image) |
| `android\gradlew.bat -p android assembleRelease` | the Android APK (docs/NATIVE.md "Android") |
| `make_standalone.bat` | `RampageRE-standalone.exe`: `RampageRE.exe` and **your** game data (`RAMPAGE.VOL`, the ELF and `SYSTEM.CNF` from your disc, 1.35 GB) in one exe, which unpacks itself to `%LOCALAPPDATA%\RampageRE` at its first start and runs from there. Drag your disc image or `RAMPAGE.VOL` onto the .bat (else the data `RampageRE.exe` finds is taken), and your extracted Wii disc's folder for the Wii's extras (about 320 MB more) |
| `make_android_bundle.bat` | `RampageRE-bundle.apk`: the APK of a release (or of your own build) and **your** game data in one APK; the app reads the data from its own APK, so it neither asks for the disc nor copies anything. Drag the APK and your disc image (or `RAMPAGE.VOL`) onto the .bat, or put them next to it, and your extracted Wii disc's folder for the Wii's extras (about 320 MB more). It is signed with a key of your own (made once, kept in `%LOCALAPPDATA%\RampageRE\android-bundle.key`), so later bundles install as updates of it but not over a RampageRE from the Releases page: uninstall that first (its saves and copied files go with it) |

The C compiler is [Zig](https://ziglang.org): one in `tools\zig`, on `PATH` or from `pip install ziglang` is used,
otherwise `build.bat` downloads Zig 0.16.0 (about 95 MB) into `tools\zig` once and checks its SHA-256.
`build.bat standalone` and `build.bat apkbundle` are the steps the two make_ scripts run; `build.bat tools` only builds
their helpers (`build\bundle_stub.exe`, `build\pack.exe`, `build\apkbundle.exe`).

**The standalone exe and the APK bundle contain the game's data: they are for your own use on your own devices only.
Never share or upload them.**

## Repository layout
| Path | |
|---|---|
| `build.ps1` | the whole pipeline: disc → split ELF → C++ → `ps2EntryRunner.exe` |
| `build.bat`, `src/` | the native port `RampageRE.exe` / `RampageRE-dev.exe` (C99, Zig, OpenGL; see docs/NATIVE.md) |
| `build.sh`, `build_switch.sh`, `build_vita.sh` | the same port for Linux (SDL2), the Switch (homebrew `rampage.nro`) and the PS Vita (homebrew `rampage.vpk`) |
| `android/` | the same port as an Android app (Gradle, SDL2's activity, the on-screen pad of `src/touch.c`) |
| `make_standalone.bat`, `make_android_bundle.bat`, `tools/native/` | your own one-file builds with your game data inside: the standalone launcher (`bundle.c`) and its packer (`pack.c`), the APK bundler and signer (`apkbundle.c`) |
| `res/` | the port's own icon (`rampagere.ico`, made from `switch_icon.jpg` by `tools/mkicon.py`) and `rampagere.rc` (icon and version of the Windows exes); `vita/`, the Vita's LiveArea images (`tools/mkvita.py`) |
| `build_viewer.bat`, `build_viewer.sh` | the old names of `build.bat dev` and `build.sh`, kept for older instructions |
| `recomp/config.toml` | PS2Recomp config: stub bindings for the SCE libraries and newlib, plus Rampage fixes |
| `recomp/funcmap.csv` | function map (Ghidra CSV format) the current state was tested with |
| `tools/split_elf.py` | splits the retail ELF's single code+data segment into real sections |
| `tools/funcmap.py` | finds function starts without Ghidra (jal targets, vtables, constructors, ...) |
| `tools/pvol.py` | lists or extracts `RAMPAGE.VOL`, the game's data archive (352 named files) |
| `tools/bsf.py` | decompiles the 138 game scripts (`.bsf`) to source and compiles them back (`--verify`) |
| `tools/bdp.py` | lists and extracts bundle entries (`--verify`: all 117 bundles fully accounted for) |
| `tools/tex.py` | exports bundle textures to PNG |
| `tools/mesh.py` | exports bundle meshes with materials and textures as glTF (`--verify`: all 5,577 meshes) |
| `tools/city.py` | assembles a city's blocks (ground, buildings, street objects) into glTF scenes |
| `tools/edf.py` | decompiles the 297 effect files ("Text EDF file") to text and compiles them back (`--verify`) |
| `tools/monster.py` | monster move graphs (`MONSTER_DATA`): parses all 30 (`--verify`), prints states, transitions and their conditions by pad input (`Square==1(new) L.up==1`) |
| `tools/loc.py` | string tables (`.loc`): 574 keys in 9 languages to TSV (`--verify`) |
| `tools/anim.py` | skeletons and animations: exports each monster as an animated, skinned glTF, and a city's people, vehicles and bosses with their clips (`--verify`: all 615 node trees and 5,379 animations) |
| `tools/prb.py` | prints property books (design data) as readable trees |
| `viewer/index.html` | three.js viewer for the exported glTF, plays animations (serve the repository root) |
| `tools/refs.py` | finds the code that references an address (strings, tables) |
| `tools/coverage.py` | the coverage map: every function's status from the addresses `src/` and `docs/` cite and the rules in `tools/coverage_groups.txt` (`--md docs/COVERAGE.md`, `--html` for a clickable map, `--range` to sort more) |
| `tools/vudis.py` | disassembles VU micro code and decodes the runtime's VU1 traces |
| `tools/iso9660.py` | extracts a PS2 disc image |
| `patches/ps2recomp-runtime.diff` | runtime changes applied to the PS2Recomp submodule by `build.ps1` |
| `PS2Recomp/` | submodule, pinned to the tested commit |

## License
GPL-3.0, like PS2Recomp. See [LICENSE](LICENSE).

Third party: `src/stb/` holds Sean Barrett's stb_image and stb_image_write (public domain or MIT, see the end of each
file), used by the texture packs ([docs/TEXTURES.md](docs/TEXTURES.md)); `src/texpack.c`, `src/postfx.c` and
`src/gtao.c` come from [WoodyRE](https://github.com/jjmhalew/WoodyRE) (GPL-3.0-or-later). `src/smaa/smaa.h` is
[SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego
Gutierrez (shader source and lookup textures, as generated by WoodyRE), under the MIT licence printed at its top.
`android/` is WoodyRE's Android project renamed; `src/touch.c` and `src/android.c` follow WoodyRE's. `build.bat`,
`make_standalone.bat`, `make_android_bundle.bat`, `res/rampagere.rc`, `tools/mkicon.py` and `tools/native/` (`bundle.c`,
`pack.c`, `apkbundle.c`) are adapted from WoodyRE's files of the same names (GPL-3.0-or-later, the same author). The icon
(`res/switch_icon.jpg`, `res/rampagere.ico`) is the project's own art (the words RAMPAGE RE over a plain skyline), not
the game's. The Java files
in `android/app/src/main/java/org/libsdl/app/` are SDL 2.32.10's (`android-project`, unchanged), Copyright (C)
1997-2025 Sam Lantinga, under the zlib licence ([android/SDL-LICENSE.txt](android/SDL-LICENSE.txt)); the APK links
SDL 2.32.10 itself, which the build fetches, under the same licence.
