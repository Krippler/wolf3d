Wolfenstein 3-D for Linux
=========================

id Software's 1992 game, from the source release, ported to a 64-bit
Linux desktop. https://github.com/Krippler/wolf3d

Installing
----------

    ./install.sh                    for yourself, into ~/.local
    sudo ./install.sh /usr/local    for everyone

Then start Wolfenstein 3-D from your desktop's menu, or run wolf3d.
./install.sh --uninstall takes it out again.

The game
--------

The shareware episode comes with this, and plays until the launcher finds
your own copy of the game. It looks in a Steam or GOG install by itself;
anywhere else, tell it once:

    wolf3d --data /path/to/the/directory/with/VSWAP.WL6

and it remembers. The registered game (*.WL6, or *.WL3 for three episodes)
and the shareware (*.WL1, version 1.4) are both played. Spear of Destiny is
not, yet.

Savegames, settings and high scores are in ~/.local/share/wolf3d.

Playing
-------

Arrow keys move, Ctrl fires, Space opens doors, Alt strafes, Shift runs,
1-4 change weapon, Esc is the menu. The mouse turns and fires once it is
enabled in the menu's Control screen (it is by default); it is captured
while you play and let go in the menus. WOLF_NOGRAB=1 leaves it alone.

WOLF_SCALE=3 makes the window three times the original size rather than
two; WOLF_ASPECT=square shows square pixels rather than the 4:3 picture
DOS drew.

What it needs
-------------

Ubuntu 22.04, Debian 12, Fedora 36 or newer, under X11 or XWayland, with
libX11, libXext and PulseAudio's client library (or PipeWire's stand-in for
it), which every desktop has.
