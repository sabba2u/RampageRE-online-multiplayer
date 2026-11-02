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
  guests on one PC passed every net4_test check (-Exe RampageRE-dev8.exe). The HUD (2026-11-02): players 5-8 have
  Hud1.ifc's panel in a second row under the clock, centred by how many it holds (hud.c hud_set_players); --players 5+
  without --monsterN takes monsters not taken yet (it crashed). Not done: the results screens (the first 4), the menus (Monster Select's 4 cards), the launcher; the host's
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
