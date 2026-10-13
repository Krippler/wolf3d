# What this actually is

id Software published the Wolfenstein 3-D source in 1995, and re-published
it on GitHub in 2012. [`README.rst`](README.rst) has id's notes from that
release, unchanged: Carmack's own word on it was "we don't guarantee that
anything even builds in here", and that it was "built in borland c++ 3.0".

It is 16-bit DOS code from end to end. Getting from there to a game that
plays on a current 64-bit Linux took:

- **Every piece of assembly rewritten in C.** The raycaster (`WL_DR_A.ASM`),
  the Huffman, Carmack and RLEW expanders, the fixed-point multiply, the
  timer and sound interrupt handlers, the page flips, the font and latch
  copies. Where the assembly's 16-bit arithmetic decided what the game
  does -- the tile indexes it kept in registers, the IDIV that gives a wall
  its height -- the C does the same arithmetic, so the walls are where they
  were.
- **The self-compiling scalers turned into tables.** The game wrote x86
  machine code at startup, one routine for every height a wall or a sprite
  could be drawn at, and called it. Carmack called that "now a Bad Thing" in
  1995. The same scaling, as a table walked in C.
- **A VGA card, in memory.** Mode Y's four planes, the map mask, the
  latches, the CRTC start address and the DAC are emulated, laid out so a
  row of the screen is 320 bytes in a row. Every address the game computes --
  `bufferofs`, `displayofs`, the latch pictures, the three pages -- keeps its
  meaning, so the code that does that arithmetic did not have to change.
- **A PC under it.** The game waits for timer 0 to count, for the keyboard
  interrupt to fill in which keys are down, for the monitor to show the page
  it flipped to, in loops that do nothing else. A thread does all three, at
  70 Hz: it counts, it turns X key events into the PC's scan codes and feeds
  them to the game's own keyboard handler, and it shows the page.
- **An AdLib and a Sound Blaster.** The music and sound effects were register
  writes to the AdLib's OPL2 chip, timed by a 700 Hz interrupt; they go to
  Nuked OPL3, an exact emulation of the chip, with the interrupt handler run
  from the audio stream so the music keeps time. The Sound Blaster's
  digitized voices, the guards' shouts and the guns, are mixed in beside it,
  in stereo, positioned as the SB Pro's mixer placed them.
- **Savegames that survive an upgrade.** The original wrote its structures
  to disk as they lay in memory, near pointers and all. Those pointers are
  written as what they point at now, so a savegame still loads after the
  engine is rebuilt.
- **The shareware data that is actually out there.** The graphics header
  that came with the source is for version 1.0 of the shareware, from 1992.
  Every copy in circulation is version 1.4, which numbers its pictures
  differently. The header for 1.4 was worked out from the data itself.
- **Spear of Destiny too.** The same source builds the sequel, its two
  mission packs and its demo, given the headers that came with it; each is
  an engine of its own, and the launchers find whichever games are there
  and start the right one.
- **What DOS left to the hardware, in the game's own menus.** A Settings
  page, drawn like the rest, for what the monitor and the sound card's mixer
  used to decide: the picture's shape and size, the music and effects levels
  apart, and which game to play, along with how the mouse and controller
  behave.
- **A browser to play it in.** The DOOM container's arrangement: Xvfb,
  x11vnc and noVNC for the picture, the sound streamed to the same page, a
  page that captures the mouse, a controller read by the browser and passed
  to the engine. A desktop build plays in a window of its own, with sound
  through PulseAudio.

[`PORTING-NOTES.md`](PORTING-NOTES.md) goes through each change, with the
original code and why it could not stay.

## What did not change

The game. The maps, the enemies, the menus, the help screens, the demo
recordings that play behind the title screen -- all of it runs as the DOS
code ran it, from the same data, through the same logic. The random number
table is the original's, byte for byte, which is what those recorded demos
depend on to play out as they were recorded.
