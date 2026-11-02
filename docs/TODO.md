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
- **More than 4 players** (offered, not decided): after four-player online; a prototype first.
- (done 2026-11-02: docs/MODELS.md) **Model import** (asked about): glTF files in `mods/models/` replacing a model, like `mods/textures/` does for
  textures. Re-skinning a monster on its own skeleton keeps all its moves; a new skeleton needs new animations and a
  move graph. Export already works (`tools/anim.py`, `tools/city.py`, `tools/mesh.py`).
- (done 2026-11-02: launcher Smooth edges, tools/upscale_textures.py; AI needs Real-ESRGAN installed) **Graphics upgrades** (suggested): anti-aliasing (multisampling or 2x supersampling) and AI-upscaled textures as a
  texture pack. DLSS does not fit (OpenGL renderer, and the game is light enough to render at full resolution).
- **Online upload with 3 guests** (measured 2026-11-01): the host sends about 199 KB/s to each guest of a 4 player game
  (136 to a 2 player game's guest), 4.8 Mbit/s on average and 6.6 at peak with three guests. Most is the people, army
  and traffic copied to each guest at 60 updates a second. Fewer updates a second for the world's puppets, or sending
  only what changed, would cut it.
- **X-ray zap** (asked): when a monster is electrocuted (an electric sign, Hazard_Shock, the blimp's barriers: the
  React_*_Zap clips), flicker it between its look and a dark silhouette with its skeleton's bones glowing white.
