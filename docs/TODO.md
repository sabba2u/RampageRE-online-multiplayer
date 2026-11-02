# To do

Ideas the owner wants, not started. Newest at the bottom.

## Now
- (done 2026-11-01) **Four-player online**: docs/superpowers/specs/2026-11-01-four-player-online-design.md.

## Later
- (done 2026-11-02: time_of_day, launcher Time of day) **Time of day: day / night switch.** Let the player pick day or night for any city. The data is there: every city
  has `WorldData.LightingData` and `LightingDataNight` (backdrop, sky, fog, lights; `tools/city.py --night` reads
  them), and the campaign already plays Las Vegas, London and Hong Kong at night (GAME_NOTES.md, the Night column).
  A setting (launcher and/or in-game) and, online, the host's choice sent to the guests.
- (done 2026-11-02: weather.c, launcher Weather) **Weather: rain, lightning and thunder.** Rain falling over the block, lightning flashes lighting the scene, thunder
  sounds a moment after. Look at how it was done for Emberville first:
  `I:\programming.projects\claude.code\multiplayer.litenetlib.spacewar.steamworks\spike\BetterWeather\` (`Weather.cs`,
  `Plugin.cs`). Online: the host decides when the lightning strikes so everyone sees the same flash.
- (done 2026-11-02: --random-blocks SEED / random_blocks=, launcher Shuffle the city's buildings; the blocks' own decorations stay where they were, so a banner can hang on another building) **Random city blocks** (offered, not decided): shuffle a city's real building templates into new block layouts from a
  seed (`--random-city <seed>`); online, the host sends the seed.
- (prototype 2026-11-02) **More than 4 players**: `build.bat dev8` makes RampageRE-dev8.exe with up to 8
  (RAMPAGE_MAX_PLAYERS: the monsters, the scene's, the game's players, the seats; the controllers stay 4). Offline 8
  monsters (--players 8 --monster5 .. --monster8) played three campaign blocks with no problem; online a host and 5
  guests on one PC passed every net4_test check (-Exe RampageRE-dev8.exe). The HUD (2026-11-02): with more than 4 players
  every panel is made smaller, all in one row across the top, the clock under it (hud.c hud_quads, row_place); --players 5+
  without --monsterN takes monsters not taken yet (it crashed). The results screens (2026-11-02): every player counted and listed (the King screens' lists grown to 8 rows, P5 .. P8; the
  blocks to win 2 from 4 players on: 6 - players was none with 6). Not done: the menus (Monster Select's 4 cards), the launcher; the host's
  upload with 5 guests about 880 KB/s (7 Mbit/s).
- (done 2026-11-02: docs/MODELS.md) **Model import** (asked about): glTF files in `mods/models/` replacing a model, like `mods/textures/` does for
  textures. Re-skinning a monster on its own skeleton keeps all its moves; a new skeleton needs new animations and a
  move graph. Export already works (`tools/anim.py`, `tools/city.py`, `tools/mesh.py`).
- (done 2026-11-02: launcher Smooth edges, tools/upscale_textures.py; AI needs Real-ESRGAN installed) **Graphics upgrades** (suggested): anti-aliasing (multisampling or 2x supersampling) and AI-upscaled textures as a
  texture pack. DLSS does not fit (OpenGL renderer, and the game is light enough to render at full resolution).
- **Online upload with 3 guests** (measured 2026-11-01): the host sends about 199 KB/s to each guest of a 4 player game
  (136 to a 2 player game's guest), 4.8 Mbit/s on average and 6.6 at peak with three guests. Most is the people, army
  and traffic copied to each guest at 60 updates a second. Fewer updates a second for the world's puppets, or sending
  only what changed, would cut it.
- (done 2026-11-02 for lightning: main.c lightning_zap, render_gl.c draw_xray) **X-ray zap** (asked): in a storm 2
  strikes in 5 land on a monster in play on the screen (the host picks; NM_WEATHER names it to the guests): the bolt
  onto it, the thunder at once, React_Front_Zap (React_Side_L_Zap on a wall) and 15 HP, and for 1.2 s it flickers
  between its look and an electric-blue x-ray with its skeleton's bones white. Not yet: the same on the game's own
  shocks (an electric sign, Hazard_Shock, the blimp's barriers: Instance.xray set as their React_*_Zap starts).
- (done 2026-11-02: **Boss Rumble**, the fifth game type: docs/superpowers/specs/2026-11-02-boss-rumble-design.md, main.c
  rumble_*, rumble.c) 12 waves (1,1,1 .. 4,4,4) of any monster as bosses, hunting the players, a red glowing outline, bars
  with heads and names under the clock, the clock counting up, power-ups on the streets and roofs, the end line (time,
  wave, best, rumble_best.txt), online (the host's waves in the world, its power-ups by NM_RUMBLE_PICKUP). Not done: the
  owner's button art (the menu shows the words), new power-ups (double damage, shield, extra life), joining a run under
  way, the launcher (no game type choice there). The ideas it came from:
- **Boss mode** (asked 2026-11-02): a mode where a monster (or up to 3) is spawned as a boss whose only
  goal is to attack the players, not the buildings. Each boss gets a health bar at the top of the screen with its
  name, and the block's objective is to defeat it (all of them). Decided: any monster of the roster can be a boss,
  even one a player is playing (the same model twice); a boss has a glowing red outline around it so it is told from
  the players' monsters (render_gl.c draw_grey already draws an outline, now grey, around an online seat nobody plays:
  the same back-face line pass, with a soft glow around it). Decided 2026-11-02: the boss keeps red (steady, glowing:
  danger), the empty online seat's outline is grey now (done 2026-11-02: it is darkened already, out of the game, not a threat). The owner liked all the open points below as
  suggestions. Things to settle: bigger and tougher (more HP, a scale-up), how it fights (the bots' AI with its target the
  nearest player instead of DestroyBuildings, director.c), what happens when the players lose (all defeated: Game
  Over or the block again), where its bars go with the players' HUD (the challenge's boss bar, ChallengeBoss2.ifc,
  is one already: a second for the Wii's Dallas), online (the host runs the bosses like bots, their HP in the
  snapshot), and whether it is its own mode in the menus / launcher or a block option.
- **Your own outline** (asked 2026-11-02, not started): a setting (launcher and in-game options) that draws an outline
  around your own monster, only on your screen, so you find yourself among up to 7 others. The outline in your
  player's colour (the HUD panel's), thin and steady, no glow, so it is not taken for a boss (red, glowing) or an
  empty seat (grey). Offline split play on one screen: each local player's in their own colour; online: only yours.
