# Changelog

All notable changes to this project. The top section is what the next
release will be: a pull request that renames it from `[Unreleased]` to
`[X.Y.Z] — YYYY-MM-DD` and is merged publishes that release (see
[PUBLISHING.md](PUBLISHING.md)).

## [1.6.0] — 2026-10-14

### Changed

- The main menu, left alone for 30 seconds outside a game, goes back to the
  title screens and the demos by itself, and has no Back to Demo any more;
  in a game, Back to Game is still there.

## [1.5.0] — 2026-10-14

### Added

- A resolution on Settings → Picture, from 320x240 as DOS showed it to
  1920x1440, always 4:3 and always the whole window; in the browser too,
  where it was fixed when the container started.

### Changed

- No setting is in two places: the main menu's Sound and Change View are
  gone, being Settings' Sound and Picture pages, which F4 and F5 open.

## [1.4.0] — 2026-10-14

### Added

- A full-screen view, as DOOM's: one size past the largest DOS offered, the
  picture fills the whole width above the status bar with no border. Change
  View and Settings → Picture both reach it, and it is the size a new
  player starts with.

## [1.3.0] — 2026-10-13

### Changed

- The view is the largest the game offers to begin with (19; DOS's default
  was 15), and its size is on Settings → Picture as well as Change View.
- Backspace does what Esc does: it opens the menu, and backs out of each
  screen of it. Typing a savegame's name or a high score, it still rubs out.

## [1.2.0] — 2026-10-13

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
