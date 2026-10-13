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

and it remembers. The registered game (*.WL6, or *.WL3 for three episodes),
the shareware (*.WL1, version 1.4), Spear of Destiny (*.SOD), its mission
packs (*.SD2, *.SD3) and its demo (*.SDM) are all played; with several in
the one directory, Settings -> Game in the game chooses between them.

Savegames, settings and high scores are in ~/.local/share/wolf3d.

Playing
-------

Arrow keys move, Ctrl fires, Space opens doors, Alt strafes, Shift runs,
1-4 change weapon, Esc is the menu. The mouse turns and fires too; it is
captured while you play and let go in the menus.

Settings, on the main menu or F12, has everything there is to set, a page
each: which game, the window's size and shape (4:3 as DOS showed it, or
square pixels), the sound devices and the music and effects levels, every
key (strafe left and right are , and . to begin with), and the mouse --
on or off, sensitivity, whether it walks as well as turns, its buttons --
with always running, controller vibration and holding on to the pointer.
They take effect at once. With more than
one game in the data directory, New Game asks which to play first.
WOLF_SCALE, WOLF_ASPECT=square and WOLF_NOGRAB=1 set the same things from
outside until they are changed there.

What it needs
-------------

Ubuntu 22.04, Debian 12, Fedora 36 or newer, under X11 or XWayland, with
libX11, libXext and PulseAudio's client library (or PipeWire's stand-in for
it), which every desktop has.
