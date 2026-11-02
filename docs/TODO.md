# To do

Ideas the owner wants, not started. Newest at the bottom.

## Now
- **Four-player online** (in progress): docs/superpowers/specs/2026-11-01-four-player-online-design.md.

## Later
- **Time of day: day / night switch.** Let the player pick day or night for any city. The data is there: every city
  has `WorldData.LightingData` and `LightingDataNight` (backdrop, sky, fog, lights; `tools/city.py --night` reads
  them), and the campaign already plays Las Vegas, London and Hong Kong at night (GAME_NOTES.md, the Night column).
  A setting (launcher and/or in-game) and, online, the host's choice sent to the guests.
- **Weather: rain, lightning and thunder.** Rain falling over the block, lightning flashes lighting the scene, thunder
  sounds a moment after. Look at how it was done for Emberville first:
  `I:\programming.projects\claude.code\multiplayer.litenetlib.spacewar.steamworks\spike\BetterWeather\` (`Weather.cs`,
  `Plugin.cs`). Online: the host decides when the lightning strikes so everyone sees the same flash.
- **Random city blocks** (offered, not decided): shuffle a city's real building templates into new block layouts from a
  seed (`--random-city <seed>`); online, the host sends the seed.
- **More than 4 players** (offered, not decided): after four-player online; a prototype first.
- **Model import** (asked about): glTF files in `mods/models/` replacing a model, like `mods/textures/` does for
  textures. Re-skinning a monster on its own skeleton keeps all its moves; a new skeleton needs new animations and a
  move graph. Export already works (`tools/anim.py`, `tools/city.py`, `tools/mesh.py`).
- **Graphics upgrades** (suggested): anti-aliasing (multisampling or 2x supersampling) and AI-upscaled textures as a
  texture pack. DLSS does not fit (OpenGL renderer, and the game is light enough to render at full resolution).
