# Wolfenstein 3-D

id Software's 1992 source, as released in 1995, repaired until it builds and
runs on a current 64-bit Linux -- the assembly rewritten in C, the VGA card,
the AdLib and the Sound Blaster emulated -- and packaged as a container you
play in a browser.

```
docker run --rm -p 6080:6080 ghcr.io/krippler/wolf3d
```

Then open **<http://localhost:6080/>** and click to play.

Nothing is installed on the host -- no X server, no display, no audio setup,
and no game data to find: the shareware episode is in the image, so that
command is the whole of it, and picture and sound both arrive in the browser.

## Your own game data

The shareware plays episode 1. To play all six, mount a directory holding
the files from your own copy of the game -- `VSWAP.WL6`, `VGAGRAPH.WL6` and
the rest, from a CD, a DOS install, Steam or GOG -- at `/data`; mounted data
always takes precedence over the bundled episode.

```
docker run --rm -p 6080:6080 -v "$PWD/data:/data:ro" ghcr.io/krippler/wolf3d
```

Recognised, in any case:

| Files | Game |
|---|---|
| `*.WL6`, `*.WL3` | Wolfenstein 3-D, six episodes or three |
| `*.WL1` | the shareware episode, version 1.4 |
| `*.SOD` | Spear of Destiny |
| `*.SD2`, `*.SD3` | its mission packs, *Return to Danger* and *Ultimate Challenge* |
| `*.SDM` | the Spear of Destiny demo |

Put as many as you have in the one directory. The first in the table is
played, and **Settings → Game** in the game switches between them (the
choice is remembered); `WOLF_GAME=sod`, say, picks one from outside. The
mission packs' files keep their own extension, as they came, beside the
Spear of Destiny ones -- or alone, with the `*.SOD` files they need.

Spear of Destiny was checked against its demo; the full game and the mission
packs are built from the same code, but have not been played to the end
here for want of the data. The demo's files are not bundled -- unlike the
Wolfenstein shareware, they carry no terms allowing it.

## Keeping savegames and settings

Savegames, settings and the high scores live in `/wolf/state`. Without a
volume there they go when the container does:

```
docker run --rm -p 6080:6080 \
  -v "$PWD/data:/data:ro" -v wolf3d-state:/wolf/state \
  ghcr.io/krippler/wolf3d
```

With Compose: `docker compose up --build`, with your game's files in `./data`.

Running as root is not required. Started as root the container takes
ownership of the state directory as `PUID:PGID` (1001 by default, 99:100 on
Unraid) and drops to that user; started with `--user` it stays as whoever
you gave it.

## Playing

Arrow keys move, Ctrl fires, Space opens doors, Alt strafes, Shift runs,
1 to 4 change weapon. In the browser, <kbd>Esc</kbd> gives the mouse back
and <kbd>`</kbd> opens the game's menu, where the keys and the mouse are set
under Control. A game controller works too -- A fires, B opens, Y changes
weapon, the sticks walk and turn -- and vibrates when you are hit.

**Settings**, at the bottom of the Control menu or on <kbd>F12</kbd> while
playing, holds what DOS left to the monitor and the sound card: which game
to play, the picture's shape (4:3 as DOS showed it, or square pixels) and on
a desktop the window's size, the music and effects levels separately, and
whether the mouse walks as well as turns, running all the time, controller
vibration and holding on to the pointer. Changes are made at once and kept
in `settings` beside the savegames.

## On a Linux desktop, without Docker

Each [release](https://github.com/Krippler/wolf3d/releases) carries
`wolf3d-linux-x86_64-VERSION.tar.gz`: the engines, the shareware episode
and a launcher.

```
tar xzf wolf3d-linux-x86_64-*.tar.gz
cd wolf3d-linux-x86_64-*/
./install.sh                  # into ~/.local; sudo ./install.sh /usr/local for everyone
```

Then start **Wolfenstein 3-D** from your desktop's menu, or run `wolf3d`. It
finds a Steam or GOG copy by itself where it can; otherwise
`wolf3d --data /path/to/the/game`, once.

To build it yourself: `make -C WOLFSRC`, with gcc and the X11, Xext and
PulseAudio development packages. `WOLFSRC/linux/wolf3d-wl1` plays the
shareware, `wolf3d` the registered game, `wolf3d-sod` Spear of Destiny (and
with `WOLF_MISSION=2` or `3` its mission packs) and `wolf3d-sdm` its demo;
run one from the directory you want savegames in, with `WOLF_DATA` naming
the one with the game's files.

## Images

Published to `ghcr.io/krippler/wolf3d`, `linux/amd64`. `latest` is the
newest release, `edge` tracks `master`. Signed with cosign on every push.
Unraid users: the Community Applications template is
[`templates/unraid.xml`](templates/unraid.xml).

## What is here

| | |
|---|---|
| [`ABOUT.md`](ABOUT.md) | what it took to get from the 1995 release to this |
| [`PORTING-NOTES.md`](PORTING-NOTES.md) | every change to the source, and why |
| [`DOCKER.md`](DOCKER.md) | the container's options |
| [`CHANGELOG.md`](CHANGELOG.md) | what changed, release by release |
| [`README.rst`](README.rst) | id's notes from the source release, unchanged |
| `WOLFSRC/` | the source: id's files, ported, and the new `sys_*.c` |
| `shareware/` | the shareware episode, every file of it |

## Licence

The source is id Software's, released under the Limited Use Software License
Agreement reproduced in [`README.rst`](README.rst), which this repository
inherits from the release it forks; it is not GPL. The AdLib emulator in
`WOLFSRC/opl/` is Nuked OPL3, under the LGPL 2.1. The shareware episode is
Apogee's, distributed under the terms in
[`shareware/VENDOR.DOC`](shareware/VENDOR.DOC). The registered game is not
included and comes from your own copy.
