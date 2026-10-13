# Changelog

All notable changes to this project. The top section is what the next
release will be: a pull request that renames it from `[Unreleased]` to
`[X.Y.Z] — YYYY-MM-DD` and is merged publishes that release (see
[PUBLISHING.md](PUBLISHING.md)).

## [Unreleased]

The first version: the 1995 source release, ported to 64-bit Linux and
packaged to play in a browser.

### Added

- Both engines build with gcc on 64-bit Linux and run on an X server:
  `wolf3d` for the registered game, `wolf3d-wl1` for the shareware episode.
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
