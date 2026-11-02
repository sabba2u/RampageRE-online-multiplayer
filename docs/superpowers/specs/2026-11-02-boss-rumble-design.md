# Boss Rumble — design

Asked by the owner 2026-11-02. A port extra: a new game type, not in the original game.

## Intent

A survival mode for 1–4 players working together: no buildings to clear, just waves of boss monsters that come for
the players. How long you last is the score; beating the last wave is the win. It gives the 8-monster work and the
boss outline a home, and it is a mode people can replay for a better time.

## What the player sees

- **Menu:** the main menu's game type list (shell.c PLAY_MODE_TITLES: Campaign, King of the City, King of the World,
  Timed Run) gets a fifth, **Boss Rumble**, after Timed Run (left/right cycles through all five). The owner will make
  the button art later; until then it is the localized text "Boss Rumble" in the same place the others' titles go.
  Players: 1–4, as King of the City allows (bots allowed: they fight on the players' side).
- **Map:** a random city and block from the game's own blocks (campaign cities, day or night as the launcher's Time of
  day says, else the block's own). Buildings, people, army and traffic are there as usual and can be smashed for
  points, but clearing the block does nothing: the bosses are the objective.
- **Clock:** the HUD clock counts **up** from 0:00 (the survival time), it never runs out.
- **Waves:** 12 waves: 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4 bosses at once. A wave starts when the last one is cleared:
  a short pause (about 3 s, a "Wave N" banner), then the bosses run in from off the screen's edges.
- **Bosses:**
  - any monster of the roster, picked at random (the host's random online), even one a player is playing; within one
    wave no two bosses are the same monster when the roster allows;
  - a steady glowing red outline (render_gl.c draw_grey's back-face line pass, red, with a soft glow; the empty online
    seat's outline is grey since fd30728 so the two are not confused);
  - a health bar with the monster's name at the top of the screen, one per boss alive (up to 4), placed so it does
    not cover the players' HUD (ChallengeBoss2.ifc's boss bar is the model);
  - health: wave 1–3 a player's full health; each later group of three waves adds 25 % (waves 4–6 ×1.25, 7–9 ×1.5,
    10–12 ×1.75), and their hits do the same factor of damage;
  - they fight with the bots' AI (director.c) but their target is the nearest living player, never a building; they
    do not attack each other;
  - a boss beaten is knocked out and leaves (as a monster out of lives does); no lives for bosses.
- **Power-ups:** the game's own edibles (edible.c: food for health, rage, each monster's own item, as the city's
  kinds and frequencies give) also appear out in the world: on the street and on rooftops, a few at a time, every
  15–25 s one more, each gone after 20 s if nobody takes it. Grabbing works and does what it does from a window.
  The hazards are not placed outside.
- **Players' lives:** as in the other modes (the HUD's x3). A player out of lives is out.
- **Losing:** every player out of lives → the defeat screen (GameOver.ifc, or TimedRunResults-style): this run's
  survival time, the wave reached, and the **best survival time** (saved per profile; online each PC saves its own).
- **Winning:** wave 12 cleared → a victory screen with the time and the best time (a win counts as a time too).

## Online

The host runs the bosses like bots: it picks them, runs their AI, and their moves and health go out in the snapshot as
the players' do. NM_* messages say a wave's start and its bosses so every screen shows the same banner and bars.
Power-ups placed in the world are the host's too (where, when, who took it). Guests see the same clock.

## Constraints

- The offline regression hash (`CB88DA7AB444D79F`) stays as it is: nothing changes outside Boss Rumble.
- Monster slots: the normal build holds 4 monsters (main.c MONSTERS = RAMPAGE_MAX_PLAYERS). Boss Rumble needs up to
  4 players + 4 bosses. **Builder's call (Claude's):** separate the two: monster slots 8 in every build, players still
  4 (RAMPAGE_MAX_PLAYERS); dev8 already showed 8 monsters running offline and online. The online protocol changes with
  it (old builds can't join new hosts: the version check says so).
- Commit and push after each working step; the launcher (if touched) built with `--target-dir target-alt` and copied
  to the repo root as RampageRE-Launcher.exe.

## Non-goals

- New power-ups (double damage, shield, extra life): maybe later.
- Bosses as a block option in other modes (the earlier TODO idea): not now.
- Endless waves past 12.
- New boss animations or models.

## Left to the builder

Where the boss bars sit exactly, the banner's look, the spawn edges, how a boss's damage factor is applied, how the
world power-ups are placed (street / rooftop spots from the block's geometry), the defeat/victory screen's exact
layout from the existing .ifc screens, the launcher entry (a Boss Rumble choice where game type is chosen, if the
launcher has one).
