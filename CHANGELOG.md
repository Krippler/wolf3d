# Changelog

All notable changes to this project. The top section is what the next
release will be: a pull request that renames it from `[Unreleased]` to
`[X.Y.Z] — YYYY-MM-DD` and is merged publishes that release (see
[PUBLISHING.md](PUBLISHING.md)).

## [Unreleased]

### Changed

- Settings is the one place for everything there is to set, and the main
  menu's Control is Settings. Its Sound page has the sound devices as well
  as the levels; its Keys page every key -- forward, backward, turning,
  strafing, fire, open, run and the strafe button; its Mouse and controller
  page the mouse on or off, its sensitivity and its buttons, beside always
  run, vibration and the pointer. F4 opens its Sound page, F6 and F12 the
  menu. The game's own Sound, Control, Mouse Sensitivity and Customize
  screens are folded into it, and the DOS joystick's settings, which did
  nothing here, are gone.

### Fixed

- F12 is named F12 when it is bound to something.

## [1.1.0] — 2026-10-13

### Changed

- The documentation describes 1.0.0 throughout: all four engines, Spear of
  Destiny, the Settings pages, and the published image tags.

### Added

- Keys to strafe left and right on their own, without holding Alt: `,` and
  `.` to begin with, rebound under Settings → Keys.
- New Game asks which game to play -- Wolfenstein 3-D or Spear of Destiny,
  and the mission packs -- when more than one is there, then goes on to that
  game's episodes or difficulty, restarting into its engine if need be.

## [1.0.0] — 2026-10-13

The first version: the 1995 source release, ported to 64-bit Linux and
packaged to play in a browser.

### Added

- The engines build with gcc on 64-bit Linux and run on an X server:
  `wolf3d` for the registered game, `wolf3d-wl1` for the shareware episode,
  `wolf3d-sod` and `wolf3d-sdm` for Spear of Destiny and its demo.
- Spear of Destiny: `wolf3d-sod` plays the full game and, with
  `WOLF_MISSION`, its mission packs (*Return to Danger*, *Ultimate
  Challenge*); `wolf3d-sdm` plays its demo. The copy protection quiz is off.
  The container and the launcher find every game in the data directory.
- Settings, at the foot of the Control menu or on F12: which game, the
  picture's shape and the window's size (changed at once), the music and
  effects levels separately (the two now have an OPL chip each), whether
  the mouse walks, always run, controller vibration and pointer capture.
  Kept in `settings` beside the savegames.
- The assembly is C: the raycaster, the scalers, the decompressors, the
  timer and sound interrupt handlers.
- The VGA card, the AdLib (Nuked OPL3), the Sound Blaster and the PC
  speaker are emulated. Music, AdLib effects and the digitized sounds all
  play, in stereo.
- The shareware v1.4 data plays: its graphics header was worked out from
  the data, since the one with the source is for v1.0.
- Savegames record what pointers pointed at, not where, so they load after
  the engine is rebuilt. A DOS savegame or configuration file is recognised
  and turned away rather than read as nonsense.
- A container that plays in a browser: the picture over noVNC, the sound
  and a game controller over the same port, the mouse captured, the
  shareware episode built in, your own game mounted at `/data`.
- A desktop build with a launcher that finds a Steam or GOG copy, a menu
  entry and an installer.
- A smoke test that plays the first level on every push, and the Unraid
  template.
