# Boss Rumble Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fifth game type, Boss Rumble: 1–4 players survive 12 waves of boss monsters (1,1,1,2,2,2,3,3,3,4,4,4) on a random block, with a count-up clock, boss health bars, world power-ups and a best survival time; offline and online.

**Architecture:** The rules that need no world (the wave table, health factors, boss picks, power-up timing, the best-time file) live in a new pure module `src/rumble.c` with a unit test like `tests/net_test.c`. main.c drives it: bosses are monsters in the slots after the players (`Mon.boss`), driven by the bots' AI with a new "hunter" mode that only chases players; game.c treats `GP_BOSS_RUMBLE` like a block whose finish is the last wave cleared and whose clock counts up. Online the host owns the waves and sends them (NM_RUMBLE); the monsters' snapshots already carry their moves and HP.

**Tech Stack:** C99 compiled with zig cc (build.bat), OpenGL 1.x renderer (render_gl.c), ENet netcode (netgame.c), PowerShell test scripts (tools/net4_test.ps1).

**Spec:** docs/superpowers/specs/2026-11-02-boss-rumble-design.md

## Global Constraints

- Offline regression: `RampageRE-dev.exe "<repo>\Rampage - Total Destruction (Europe) (En,Fr,De,Es,It).iso" Chicago 1 --players 2 --monster George --monster2 Lizzie --do "right 1, cross, wait 0.8, p2, left 1, square, wait 1" --shot out.png` → SHA256 starts `CB88DA7AB444D79F`, after every task.
- Nothing changes outside Boss Rumble (other modes play exactly as before).
- Players stay at most 4 (`RAMPAGE_MAX_PLAYERS`); monster slots become 8 in every build.
- Waves: 1,1,1,2,2,2,3,3,3,4,4,4. Health and damage factor: waves 1–3 ×1.0, 4–6 ×1.25, 7–9 ×1.5, 10–12 ×1.75.
- Pause between waves about 3 s with a "Wave N" banner.
- Power-ups: one more every 15–25 s, each gone after 20 s untaken; the game's own non-hazard edible kinds.
- Boss outline: red, steady, glowing (the empty online seat's is grey).
- Build from the repo root: `cmd /c .\build.bat dev` (and `dev8` still builds). Commit and push to `fork online-multiplayer` after each task.
- Don't sign in to Steam in tests; don't touch WSL, launcher.json, mods/.

## Review Focus

1. A boss slot reused for the next wave (a different monster in the same slot): no stale pointers (move graph, HUD, held objects), no crash after many waves.
2. A player who loses all lives while others live: they stay out, the bosses ignore them, the game goes on; the last one out → defeat.
3. Bosses never counted as players: lives, score, results, "game over" count, the army's targets, online seats.
4. Online guest joining/leaving mid-rumble: the guest sees the current wave's bosses (NM_RUMBLE re-sent on join), and an empty seat is not a boss target.
5. Other modes untouched: the offline hash, and a King of the City block still ends on its buildings.

Each line has its check in the owning task (Task 4 step 5, Task 3 step 5, Task 4 step 6, Task 7 step 4, every task's hash step).

---

### Task 1: The rules, a pure module with tests

**Files:**
- Create: `src/rumble.h`, `src/rumble.c`, `tests/rumble_test.c`, `tests/build_rumble_test.bat`
- Modify: `build.bat` (add `src\rumble.c` to the game's sources, the same list the other src files are in)

**Interfaces:**
- Produces:
  - `#define RUMBLE_WAVES 12`
  - `int rumble_wave_size(int wave)` — wave 1..12 → 1,1,1,2,2,2,3,3,3,4,4,4; out of range → 0.
  - `float rumble_factor(int wave)` — 1.0 / 1.25 / 1.5 / 1.75 per group of three; out of range → 1.
  - `int rumble_pick(unsigned *seed, int nroster, int count, int out[4])` — `count` distinct roster indices when `nroster >= count`, else repeats allowed; returns count; deterministic from `*seed` (advances it, LCG `s = s*69069+1`).
  - `float rumble_next_pickup(unsigned *seed)` — seconds 15..25.
  - `#define RUMBLE_PICKUP_LIFE 20.0f`, `#define RUMBLE_WAVE_PAUSE 3.0f`
  - `int rumble_best_read(const char *path, const char *profile, float *best)` / `int rumble_best_write(const char *path, const char *profile, float t)` — a text file of lines `<seconds> <profile name>`; write keeps the larger time, other profiles' lines kept; returns 1 ok / 0 none or error.
  - `void rumble_time_text(float t, char out[16])` — `M:SS` (e.g. 75.4 → "1:15"), hours past 59 minutes as `H:MM:SS`.

- [ ] **Step 1: Write the failing tests** in `tests/rumble_test.c` (the `CHECK` macro of net_test.c):

```c
CHECK(rumble_wave_size(1)==1 && rumble_wave_size(3)==1 && rumble_wave_size(4)==2 && rumble_wave_size(9)==3 && rumble_wave_size(12)==4);
CHECK(rumble_wave_size(0)==0 && rumble_wave_size(13)==0);
CHECK(rumble_factor(1)==1.0f && rumble_factor(4)==1.25f && rumble_factor(7)==1.5f && rumble_factor(12)==1.75f);
unsigned s=7; int o[4]; CHECK(rumble_pick(&s,10,4,o)==4); /* distinct */ for(i<4)for(j<i) CHECK(o[i]!=o[j]); /* in range */
unsigned a=5,b=5; int p[4],q[4]; rumble_pick(&a,10,3,p); rumble_pick(&b,10,3,q); CHECK(!memcmp(p,q,3*sizeof(int)));
unsigned t=1; int r[4]; CHECK(rumble_pick(&t,2,4,r)==4); /* small roster: repeats, no hang */
for 100 draws: float f=rumble_next_pickup(&s); CHECK(f>=15 && f<=25);
/* best file: write 90 for "AA", 60 for "AA" (kept 90), 30 for "BB"; read AA → 90, BB → 30, CC → 0 */
char tx[16]; rumble_time_text(75.4f,tx); CHECK(!strcmp(tx,"1:15")); rumble_time_text(3725,tx); CHECK(!strcmp(tx,"1:02:05"));
```

- [ ] **Step 2: Run** `cmd /c tests\build_rumble_test.bat` (zig cc like build_net_test.bat, sources `tests\rumble_test.c src\rumble.c`). Expected: compile error (no rumble.h).
- [ ] **Step 3: Implement** `src/rumble.h` / `src/rumble.c` per the Interfaces block (no dependencies besides libc).
- [ ] **Step 4: Run** the test bat. Expected: `rumble_test: N checks, 0 failed`.
- [ ] **Step 5:** `cmd /c .\build.bat dev` builds; hash check → `CB88DA7AB444D79F`.
- [ ] **Step 6: Commit** `rumble.c: Boss Rumble's rules (waves, factors, picks, pickups, best time) with tests` and push.

### Task 2: Monster slots 8, players still 4

**Files:**
- Modify: `src/main.c:103-105` (`MONSTERS` becomes `RAMPAGE_MAX_MONSTERS`, default 8), `src/city.h:177-179` (`SCENE_MONSTERS` → the monster count), `src/army.h`, `src/netwire.h`, `src/nettransport.h`, `src/game.h` — each `RAMPAGE_MAX_PLAYERS` use classified as *players* (seats, game players, HUD panels, pads) or *monsters* (scene instances, Mon array, snapshots' monster lists, army targets); the monster ones switch to `RAMPAGE_MAX_MONSTERS`.
- Modify: the online protocol version constant (netwire.h) +1.

**Interfaces:**
- Produces: `RAMPAGE_MAX_MONSTERS` (8; dev8 keeps 8 too), `MONSTERS == RAMPAGE_MAX_MONSTERS` in main.c; the game's players stay `GAME_PLAYERS` = `RAMPAGE_MAX_PLAYERS`.

- [ ] **Step 1:** `grep -n RAMPAGE_MAX_PLAYERS src/*.c src/*.h` and classify each (ledger the list).
- [ ] **Step 2:** Switch the monster ones; `build.bat dev` and `build.bat dev8` both build with no new warnings.
- [ ] **Step 3:** Hash check → `CB88DA7AB444D79F`.
- [ ] **Step 4:** `tools\net4_test.ps1` with 3 guests → all PASS; with `-Exe RampageRE-dev8.exe` and 5 guests → all PASS.
- [ ] **Step 5:** `cmd /c tests\build_net_test.bat` → 0 failed.
- [ ] **Step 6: Commit** `monster slots 8 in every build (players still 4): room for Boss Rumble's bosses` and push.

### Task 3: The game type: menu, --play rumble, random block, count-up clock, defeat on lives

**Files:**
- Modify: `src/game.h:62` (`GP_BOSS_RUMBLE = 6`), `src/game.c` (clock and finish), `src/main.c:8814-8817` (`--play rumble`), `src/shell.c` (PLAY_MODE_TITLES gets a fifth, play_mode cycles 2..6, the start skips City Select), `src/hud.c` (HUD_PLAY_* agrees with GP_*: add `HUD_PLAY_RUMBLE = 6` where HUD_PLAY_* is defined).

**Interfaces:**
- Consumes: Task 1 (`rumble_pick` for the city/block seed).
- Produces: `GP_BOSS_RUMBLE`; `void game_set_won(Game *g)` (main.c calls it when the last wave is cleared: IsBlockFinished true); `float game_clock(...)` returns the **elapsed** seconds for GP_BOSS_RUMBLE (the HUD draws it as it draws the countdown).

- [ ] **Step 1: Shell.** PLAY_MODE_TITLES[4] = `"Boss Rumble"` shown as plain text: node_localize prints a missing key as `0x key x0`, so set the title with `node_text` when the mode is 6 (a localized key `PlayMode_BossRumble` used first if the strings have it, for the owner's art later). Players 1–4 as King of the City. qicons has 4: mode 6 has no icon to glow (none highlighted). On Monster Select's end, mode 6 goes as mode 4 does (no City Select): `city_index` and `start_block` random from the profile's seed through `rumble_pick` over the campaign cities and that city's blocks.
- [ ] **Step 2: game.c.** For GP_BOSS_RUMBLE: the clock enabled, counting up from 0 with no time-up (GE_TIME_UP never); `IsBlockFinished` true only after `game_set_won`; the campaign's "fewer lives than monsters down → game over" rule applies too (the condition at game.c:1069 includes GP_BOSS_RUMBLE). The block's buildings all destroyed does not finish it. A rumble block won → GS_GAME_OVER's sibling path to the end screen of Task 6 (no next block).
- [ ] **Step 3: Run** `RampageRE-dev.exe "<iso>" --play rumble --players 1 --monster George --autoplay --ap-minutes 1 --ap-shots <dir>`. Expected: log line `rumble: city <name> block <n>`; a shot at ~30 s whose clock reads `0:2x`/`0:3x` (counting up).
- [ ] **Step 4: Run** the same with `RAMPAGE_HP=1` (each hit kills) for 2 minutes. Expected: lives go 3→0 and the game reaches GS_GAME_OVER (log `game over`), not a block won.
- [ ] **Step 5 (Review Focus 2):** `--players 2 --bots 1` with `RAMPAGE_HP=1`: the player out of lives first stays out (log), the game goes on until both are out.
- [ ] **Step 6:** Hash check. In the menu (`RampageRE-dev.exe "<iso>"`, `--do` to press right four times from the game type): shot shows "Boss Rumble".
- [ ] **Step 7: Commit** `Boss Rumble: the fifth game type (menu, --play rumble), a random block, the clock counting up, defeat when every life is gone` and push.

### Task 4: The bosses: waves, hunter AI, health and damage factor, red outline

**Files:**
- Modify: `src/main.c` (Mon gets `int boss; float boss_factor;`; `mon_set_boss`; the wave driver; the damage factor in `monster_take`), `src/director.h` / `src/director.c` (DirBot `int hunter`; DirMonster `int team, out`), `src/render_gl.c` (outline pass for `Instance.boss`), `src/city.h` (`Instance.boss`).

**Interfaces:**
- Consumes: Task 1 (`rumble_wave_size`, `rumble_factor`, `rumble_pick`, `RUMBLE_WAVE_PAUSE`), Task 2 (slots), Task 3 (`GP_BOSS_RUMBLE`, `game_set_won`).
- Produces:
  - `static int mon_set_boss(Viewer *v, int k, int monster, float factor)` — slot k (≥ players) gets roster monster `monster` as a boss: the first time through `mon_add_player`'s path without `game_add_player` / HUD panel; a slot already used by a beaten boss is rebuilt in place (its scene instance replaced through `city_monster` at that slot, its move runner restarted) — returns k or -1.
  - `typedef struct { int wave, alive, nboss, slot[4]; float pause; unsigned seed; } Rumble;` in Viewer as `v->rumble`.
  - `void rumble_driver_step(Viewer *v, float dt)` in main.c: when no boss alive → pause `RUMBLE_WAVE_PAUSE` with the banner → next wave's bosses (picked from the whole roster, a player's monster allowed) placed off the screen's left/right edges in the street and walking in; after wave 12's last boss → `game_set_won`.
  - Trace (stdout): `rumble: wave N: <name> <name>...` and `rumble: <name> beaten (wave N)`.
- Rules: a boss has a player's full health × factor, its hits × factor (in `monster_take` when the attacker is a boss); it is the computer's (`DRIVE_COMPUTER`, `bot=1`, `is_ai=1`), its goal `dir_ai_destroy` with `bot.hunter = 1`; a hunter never picks a building: `destroy_update` goes straight to `bot_hunt`, which skips monsters of its own team (`team`: 1 boss, 0 player), `out` ones (dead, out of lives, empty online seat) and human ones. In Boss Rumble the players' bots are hunters too (team 0: they go for the bosses, not the buildings). Beaten (HP 0): no respawn, its reaction plays then the instance hides. The army does not target bosses (`m[k].ignore` true for a boss); bosses don't score; lightning may strike them.
- Outline: `draw_grey`'s back-face line pass without the dark overlay, red `(1, 0.12, 0.08)` steady, 4 px line plus a second 9 px pass at alpha 0.3 for the glow.

- [ ] **Step 1:** Director hunter: add the fields; `destroy_update` / `bot_hunt` changes; the bot view fills `team`/`out`. Build; hash check (no hunter offline → unchanged).
- [ ] **Step 2:** `mon_set_boss`, the driver, the factor. Run `--play rumble --players 1 --monster George --autoplay --ap-minutes 2` with `RAMPAGE_RUMBLE_BOSS_HP=0.05` (debug: the boss factor × that, for a fast test). Expected trace: `rumble: wave 1: X`, `X beaten`, `wave 2`, … in order 1,1,1,2,2,2… and no crash.
- [ ] **Step 3:** With `RAMPAGE_RUMBLE_BOSS_HP=0.01` and `--ap-minutes 6`: all 12 waves, then `rumble: won` and the end screen state. (Review Focus 1: 30 bosses through 4 slots.)
- [ ] **Step 4:** Shot during wave 4–6 (2 bosses): both outlined red and glowing, walking at the player, not climbing buildings.
- [ ] **Step 5 (Review Focus 2):** `--players 2 --bots 1`, player 2 out of lives (`RAMPAGE_HP` on that one only is not available: use the log): bosses' targets (trace `hunt -> slot k`) never name an out player.
- [ ] **Step 6 (Review Focus 3):** the results' and HUD's player counts unchanged with bosses in (2 panels for 2 players); `game_lives` never asked for a boss slot (an assert-style log line if it is).
- [ ] **Step 7:** Hash check; commit `Boss Rumble: waves of bosses (hunters of the players, health and hits by the wave, a red glowing outline)` and push.

### Task 5: The HUD: boss bars with names, the wave banner

**Files:**
- Modify: `src/hud.c`, `src/hud.h`, `src/main.c` (feeding it).

**Interfaces:**
- Consumes: Task 4 (`v->rumble`, `Mon.boss`).
- Produces: `void hud_set_bosses(Hud *h, int n, const char *const name[], const float frac[])` (n 0..4; frac 0..1), `void hud_wave_banner(Hud *h, int wave, float seconds)` ("Wave N", or "Final wave" for 12).
- Layout: the bars under the players' row and the clock: centred, one per boss, side by side (up to 4), each the ChallengeBoss bar (ChallengeBoss.ifc's `boss_group`/`bossHealth`) with the monster's display name above it; in the 6/8-player row layout they go under the clock the same way. The banner: centre screen, the HUD font, fades in 0.3 s, holds, fades out 0.3 s.

- [ ] **Step 1:** Implement; shot with 1 boss and with 4 bosses (`RAMPAGE_RUMBLE_START_WAVE=10` debug env to begin at wave 10). Expected: 4 bars with names, none over a player panel or the clock, at 16:9 and 4:3 (`--size 1024x768`).
- [ ] **Step 2:** Shot at a wave start shows "Wave 2".
- [ ] **Step 3:** Hash check; commit `Boss Rumble HUD: a health bar with its name for each boss, the wave banner` and push.

### Task 6: World power-ups, the end screens, the best time

**Files:**
- Modify: `src/edible.c` / `src/edible.h` (an outdoor edible: kind, place, life; grab by touch), `src/main.c` (placing, drawing through the same model the window shows, grabbing), `src/hud.c` (the end screen).

**Interfaces:**
- Consumes: Task 1 (`rumble_next_pickup`, `RUMBLE_PICKUP_LIFE`, `rumble_best_*`, `rumble_time_text`), Task 3 (game over / won).
- Produces:
  - `int edible_outdoor_add(Edibles *e, int kind, const float pos[3])` → id; `int edible_outdoor_take(Edibles *e, int id, EdibleGot *got)` (the same effect a window's eaten one gives); `void edible_outdoor_step(Edibles *e, float dt)` (expiry).
  - Placement (main.c): every `rumble_next_pickup` s, a random non-hazard kind (by the city's frequencies) at a random street point on screen (y between the curb and the buildings' fronts) or on a standing building's roof; at most 4 out at once. A monster (not a boss) whose body sphere touches it takes it: the pickup sound and `Powerup_Upgrade` effect.
  - End screen: `void hud_rumble_end(Hud *h, int won, float t, int wave, float best)` — "VICTORY"/"DEFEATED", `Time 4:12`, `Wave 7`, `Best 5:03`, then the game's exit to the menu as Game Over does. Best time file: `rumble_best.txt` beside the profile file (`v->prof_path`'s folder), keyed by the profile's name.
- [ ] **Step 1:** Outdoor edibles: an autoplay run of 2 minutes logs `rumble: pickup <kind> at x y` every 15–25 s and `rumble: <slot> took <kind>` when walked over (put one under the player with `RAMPAGE_RUMBLE_PICKUP_HERE=1`); shot shows one on the street and one on a roof.
- [ ] **Step 2:** Defeat run (`RAMPAGE_HP=1`): end screen shot shows DEFEATED, the time, the wave, the best; `rumble_best.txt` has the line. A second shorter run keeps the best.
- [ ] **Step 3:** Victory run (`RAMPAGE_RUMBLE_BOSS_HP=0.01`): VICTORY screen.
- [ ] **Step 4:** Hash check; commit `Boss Rumble: power-ups out in the city, the end screen with the best survival time` and push.

### Task 7: Online

**Files:**
- Modify: `src/netwire.h` (NM_RUMBLE, NM_RUMBLE_PICKUP), `src/netgame.c`, `src/main.c`, `tools/net4_test.ps1` (a `-Play rumble` switch).

**Interfaces:**
- Consumes: Tasks 3–6.
- Produces: NM_RUMBLE `{u8 wave, u8 n, n × {u8 slot, u8 roster monster}}` sent by the host at each wave start and to a guest when it goes live; NM_RUMBLE_PICKUP `{u8 op (0 add, 1 taken, 2 gone), u16 id, u8 kind, f32 x y z, u8 taker}`. The guest calls `mon_set_boss` with the same slots/monsters (factor from the wave), runs no rumble driver, places/removes pickups as told; bosses' moves and HP come in the monsters' snapshots as they do for players (snapshot loops over all monster slots, not players). The end screen on the guest from the host's game state; each PC writes its own best.
- [ ] **Step 1:** Implement; `tests\build_net_test.bat` gets round-trip tests for both messages → 0 failed.
- [ ] **Step 2:** `tools\net4_test.ps1 -Play rumble` with 1 then 3 guests (`RAMPAGE_RUMBLE_BOSS_HP=0.05` on the host): every guest logs the same `rumble: wave N: …` lines as the host; all PASS; a guest's shot shows the bosses and bars.
- [ ] **Step 3:** Pickups: a guest's log shows the same `pickup` lines; one taken on the guest is gone on the host.
- [ ] **Step 4 (Review Focus 4):** a guest joining at wave 3+ (the test's late-join option) sees the current bosses; a guest leaving makes its seat grey, and the bosses' targets skip it.
- [ ] **Step 5:** Hash check; commit `Boss Rumble online: the host's waves and power-ups sent to the guests` and push.

### Task 8: Docs

**Files:** `docs/TODO.md` (Boss mode entry → done, with what's left: the owner's button art, new power-ups), `README.md` if it lists the game types / `--play` values.
- [ ] **Step 1:** Update; commit `docs: Boss Rumble` and push.
