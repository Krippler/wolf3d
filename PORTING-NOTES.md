# Porting notes

What was changed in the 1995 source release to make it build and run on a
64-bit Linux, file by file, with the original where it helps and the reason
each change was needed. `git log -p WOLFSRC/` has every line.

The release's own build was a Borland C++ 3.1 project (`WOLF3D.PRJ`) linking
the C files, eight assembly files and two object files into a 16-bit DOS
program. Those files are all still here, untouched where they are not
compiled any more. `WOLFSRC/Makefile` builds the rest with gcc.

- [The approach](#the-approach)
- [Types: int was 16 bits](#types-int-was-16-bits)
- [The VGA card](#the-vga-card)
- [The machine under the game](#the-machine-under-the-game)
- [The renderer](#the-renderer)
- [Memory, pages and the cache](#memory-pages-and-the-cache)
- [Sound](#sound)
- [Input](#input)
- [Savegames and the configuration file](#savegames-and-the-configuration-file)
- [The shareware v1.4 data](#the-shareware-v14-data)
- [Things DOS let the code get away with](#things-dos-let-the-code-get-away-with)
- [Files and the DOS library](#files-and-the-dos-library)
- [The container](#the-container)

---

## The approach

Wolfenstein's engine is not written against an abstraction of the hardware.
It is written against the hardware: `bufferofs` is an offset into the VGA's
segment, the status bar is drawn by copying video memory to video memory
through the latches, a page flip is an `out` to the CRTC, the game waits for
the timer interrupt by watching a variable the interrupt changes. Wolf4SDL
and ECWolf, the ports everyone knows, rewrote that layer: a linear frame
buffer, SDL timers, SDL events, and every caller adjusted to match.

This port goes the other way. It keeps the game's view of the machine and
supplies a machine for it to see: video memory with the VGA's addressing,
registers with the VGA's meaning, a timer that counts, a keyboard that
interrupts. The game code above that layer is almost unchanged -- beyond
`long` becoming `int32_t`, the diff to `WL_AGENT.C`, `WL_ACT1.C`,
`WL_ACT2.C` and `WL_STATE.C` is a handful of lines each -- and what changed
is where the code touched the hardware itself, or relied on DOS doing
something Linux does not.

## Types: int was 16 bits

Under Borland's large model an `int` was 16 bits, a `long` 32 and a pointer
32 (segment and offset). Under gcc on x86-64 an `int` is 32, a `long` 64 and
a pointer 64.

- Every `long` in the source is now `int32_t` and every `unsigned long`
  `uint32_t`. That was done mechanically, outside comments and strings, so
  the diff shows exactly where. The fixed-point maths is 16.16 in a `long`
  throughout and depends on it being 32 bits.
- `word` is `uint16_t`, which it always meant.
- `int` and `unsigned` stay as the compiler's: most of the game's arithmetic
  is happier with more room, and gcc is told `-fwrapv` so that what did
  overflow wraps as the 8086 did rather than being undefined.
- What is read straight from the data files is spelled out in fixed sizes
  and packed: the map headers (`maptype`, `mapfiletype`), the picture table
  and fonts (`pictabletype`, `fontstruct`), the sprites (`t_compshape`), the
  Huffman nodes, the sound structures (`SoundCommon`, `AdLibSound` and the
  rest, whose `longword` followed by a `word` is six bytes on disk and eight
  in a padded struct). The map planes are `word`, so every pointer into them
  -- `unsigned far *map` in a dozen places -- became `word far *`.
- `actorat[][]` holds actor pointers and, in the same slots, small integers
  -- wall tile numbers, a door's number with the top bit set -- which the
  code wrote with `(unsigned)actorat[x][y] = value`. A cast is not an lvalue
  in C, and Borland let it through; those are now
  `actorat[x][y] = (objtype *)(uintptr_t)(value)`, and the reads cast through
  `uintptr_t`.

## The VGA card

`id_port.h`, `ID_VL.C`, `ID_VH.C`.

The game drew in Mode Y: 320x200 in 256 colours with the VGA's 256K split
into four planes. A byte address names the same byte in all four planes;
pixel *x* of a row is in plane *x & 3* at address *x / 4*. The sequencer's
map mask register picks which planes a CPU write goes to. In write mode 1 a
read loads all four planes' bytes into the latches and a write stores them,
which copies four pixels for the price of one byte -- that is how the status
bar, the font and the screen-to-screen copies were drawn.

`vgamem` holds address *a*'s plane *p* at `vgamem[a*4 + p]`. Seen the other
way, address *a* holds pixels *4a* to *4a+3* side by side, so the 80
addresses of a row are 320 consecutive bytes, and showing a page is reading
200 runs of 320 bytes. `VGAMAPMASK`, `VGAWRITEMODE` and `VGAREADMAP`, which
were inline `out` instructions, now set three variables, and `VGA_Write`,
`VGA_LatchCopy` and `VGA_Read` do what a CPU access to segment A000 did with
the registers as they are. Addresses wrap at 64K, as the 16-bit offset did.

What that buys is that every address the game computes keeps its meaning.
`bufferofs` is still where the page being drawn starts, `ylookup[y]` is
still *y* times 80, the three pages are still at 0, 16640 and 33280, and the
latch pictures loaded above them are still at the addresses `LoadLatchMem`
hands out. None of the code doing that arithmetic changed. What did is
every function that wrote to the card, nearly all of it assembly: the
pixel, line and bar routines; `VL_MemToScreen`, `VL_MemToLatch`,
`VL_LatchToScreen`, `VL_ScreenToScreen` (from `ID_VL_A.ASM`);
`VH_UpdateScreen`, which copied the marked 16x16 blocks to the shown page
(`ID_VH_A.ASM`); the proportional font, whose inner loop was assembly, and
the coloured one, which stepped the colour every second row by rotating
the loop counter's low bit through the carry flag; `FizzleFade`, which
walked a 17-bit LFSR held in DX:AX. Each is the same algorithm on the
emulated card.

`VL_SetScreen` and the CRTC writes in `ThreeDRefresh` hand the start
address to the scan-out; `VL_SetPalette` and the rest write a 768-byte DAC
copy. Neither touches the picture: the scan-out reads both, as the monitor
did.

The palette and the signon screen were linked in as object files
(`OBJ/GAMEPAL.OBJ`, `OBJ/SIGNON.OBJ`) whose sources are not in the release.
`tools/obj2c.c` reads their OMF records at build time and writes the bytes
out as C arrays, so the build uses them exactly as shipped.

## The machine under the game

`sys_x11.c`.

The game assumes things happen while it is busy. Timer 0 counts `TimeCount`
seventy times a second; the keyboard interrupt fills in `Keyboard[]`,
`LastScan` and `LastASCII`; the monitor shows whatever page the start
address names. And it waits for them in loops that do nothing else:

```c
while (TimeCount<frame)		// don't go too fast
;
...
	LastScan = 0;
	while (!LastScan)
	;
```

One thread does all three, on a 70 Hz clock: it counts `TimeCount`; it takes
the X server's key events and turns each into the bytes a PC keyboard sent
-- a make code going down, the same with bit 7 set coming up, `0xE0` ahead of
the grey keys -- and hands them to `INL_KeyService`, which is the original
keyboard interrupt handler with `inportb(0x60)` taken out; and it shows the
page at the start address through the DAC, scaled, in the window. A page
flip wakes it to show the new page at once rather than at the next tick.
The game's own thread never calls Xlib.

The waiting loops, two dozen of them, now call `SYS_Spin()`, which sleeps a
millisecond. The menus poll for input in loops that do call something --
`ReadAnyControl`, `IN_CheckAck` -- but never wait, so those routines call
`SYS_Throttle()`, which sleeps a millisecond when called again within one.
Once a frame, as the play loop calls them, it never sleeps. Without both, a
menu sat on a core.

The thread waits for its tick with an absolute deadline on the monotonic
clock, through a condition variable so a flip can wake it. That condition
variable has to be told to use the monotonic clock: the default is the
real-time clock, against which every monotonic deadline is decades past,
and the first version ticked as fast as it could.

## The renderer

`WL_DRAW.C`, `WL_SCALE.C`.

**The raycaster.** `AsmRefresh` (`WL_DR_A.ASM`) traced a ray for each column
of the view through the tile map until it hit something. It is now C, kept
close to the assembly: the intercepts stepped as 16.16 values, the whole-tile
halves taken as 16-bit numbers as the registers held them, the map indexed
with a 16-bit `(tile << 6) + tile`, the comparisons the assembly patched into
its own code (a `JLE` or a `JGE` written over the instruction, according to
the ray's quadrant) chosen the same way. The partial products --
`xpartialbyystep` and `ypartialbyxstep` -- multiplied the magnitude of a
32-bit step by a 16-bit fraction and kept the top 32 bits, putting the sign
back after, and so does the C. A ray that somehow left the map read past the
end of `tilemap` into whatever followed it on DOS; here it stops at a wall.

`xpartialup` is `TILEGLOBAL - xpartialdown`, which is 65536 when the viewer
stands exactly on a grid line, and was a 0 in the assembly's 16-bit
register. It is passed as a `word`, so it still is.

**Heights.** `CalcHeight`, `TransformActor` and `TransformTile` divided with
an inline `IDIV` of a 32-bit number by the 16-bit word at `nx+1` -- bytes 1
and 2 of `nx`, which is `nx >> 8` truncated to 16 bits -- keeping a 16-bit
quotient. `HeightDiv` does exactly that.

**FixedByFrac** multiplied a 16.16 number by a fraction held as sign and
magnitude: the sign in bit 31, the magnitude in the low 16 bits. The sine
and cosine tables are built in that form. It is now three lines of C.

**The scalers.** `SetupScaling` compiled a routine for every height a wall
column or sprite column could be drawn at: for each of the 64 source pixels,
`mov al,[si+src]` and then a `mov [es:di+row*80],al` for every screen row
that pixel covered. `ScalePost` called one through a table of far pointers;
`ScaleLine`, for sprites, patched a `RETF` into the routine where a segment
of the sprite ended, jumped in where it began, and unpatched it afterwards.
The scaler is now a table of the rows each source pixel covers
(`t_compscale`), and `ScaleColumn` walks it over a range of source pixels.
`ScaleLine` reads the sprite's segment list -- end pixel, the offset that
puts pixel 0 in the right place, start pixel, as words -- and walks the same
range the patched routine would have run. The heights the original built a
scaler for, and the ones above half the view where it built one in three
and shared it, are the same.

**Strips wider than eight pixels.** `ScalePost` set the map mask from three
tables indexed by the strip's starting pixel and its width, up to eight. A
wall seen from very close can repeat a texture column across more than
eight screen columns, which indexed off the end of those tables. The C draws
the whole width.

**Clearing the view.** `VGAClearScreen` filled the top half with the
ceiling colour and the bottom with `0x19`, a word at a time with all planes
enabled. It is two `memset`s a row.

## Memory, pages and the cache

`ID_MM.C`, `ID_PM.C`, `ID_CA.C`.

The memory manager fitted the game into 640K plus EMS and XMS: blocks it
could purge when short, and move to close gaps, updating each owner's
pointer -- which is why the game hands it a pointer's address rather than
the pointer. All of that is `calloc` and `free` now, and purgable blocks are
never purged. It still reports a healthy amount of memory, because
`InitGame` refuses to start below 235000 bytes.

The page manager kept VSWAP's walls, sprites and sounds in conventional
memory, EMS and XMS, swapping them in as the view needed them. VSWAP is
under a megabyte; `PM_Startup` reads all of it, and `PM_GetPage` is an index.
The "Get Psyched!" bar, drawn while pages preloaded, is still drawn, full.

The cache manager kept its logic and lost its assembly: `CAL_HuffExpand`
(with its "screenhack", which expanded a full-screen picture straight into
the four planes), `CA_RLEWexpand`, the 64K-limited `CA_FarRead` and
`CA_FarWrite`. `CAL_OptimizeNodes` used to turn the Huffman tree's node
numbers into near pointers so the assembly could follow them; a pointer no
longer fits in the 16-bit field, so the numbers are left alone and the C
does the arithmetic. `CAL_CarmackExpand` read bytes and words through
`*((unsigned char far *)inptr)++`, a cast used as an lvalue, which Borland
allowed; it reads through a byte pointer now. The graphics header is checked
against the number of chunks the engine was built for, since the chunk
numbers are compiled in.

## Sound

`ID_SD.C`, `sys_audio.c`, `opl/`.

On DOS, `ID_SD_A.ASM` reprogrammed timer 0 to 700 Hz while music played. Its
handler stepped the music -- a list of OPL register writes, each with a
delay in ticks -- every time, stepped the current sound effect every fifth
time, and counted `TimeCount`. The AdLib's OPL2 made the music and the AdLib
effects from those register writes. The Sound Blaster played digitized
sounds -- 8-bit, about 7000 Hz -- by DMA, a 4K page at a time from VSWAP,
interrupting at the end of each; the SB Pro's mixer placed them left and
right. The PC speaker made square waves, its pitch set through timer 2.

All three are emulated and mixed into one stream. `SD_Mix` makes a block of
it: it runs the 700 Hz handler, now C, as many times as the block's samples
cover; runs Nuked OPL3, an exact emulation of the Yamaha chip, for the
samples; adds the Sound Blaster's current sample, resampled from 7042 Hz
(what `256 - 1000000/7000` as a time constant really gave) and positioned;
and adds the speaker's square wave. The digitized sound is fed a page at a
time exactly as before: `SDL_DigitizedDone` is called when a page runs out,
where the DMA interrupt called it, and `SD_Poll` lines up the next.

`sys_audio.c` runs the thread that asks for those blocks, as fast as they
are played and no faster, which also makes the stream the clock the music
keeps time by. It sends them to PulseAudio on a desktop, or serves them on a
TCP port in the container, in the format the DOOM container's page reads,
or writes them to a file for the smoke test. `TimeCount` is counted by the
machine thread, since the game needs it with sound off too.

The game's `pushf; cli ... popf` sections, which kept the interrupt handler
out while it changed the handler's state, are a recursive mutex the mixer
also takes.

The one change to what was mixed: the music and the AdLib effects, which
shared one chip -- the effects on channel 0, the music on the rest -- now
have a chip each, so the Settings pages can set their levels apart. `alOut`,
which the game's set-up and shut-down write through, writes to both; the
effects' instrument and notes go to one, the music's register list to the
other.

What the game asks is the same as it was: the menu offers the AdLib for
effects and music, the Sound Blaster for digitized sound, and `-noal`,
`-nosb` and `-nopro` still take them away. The Sound Source never answers.

## Input

`ID_IN.C`, `sys_x11.c`, `sys_pad.c`.

The keyboard handler is the original, fed bytes; see above. Pause sends
`0xE1`, as the key did. Backspace is turned into Esc there, so that it opens
the menu and backs out of each screen of it wherever Esc does, except while
`US_LineInput` runs -- a savegame's name, a high score's -- where it is
Backspace and rubs out (`IN_TextEntry`).

The mouse driver, INT 33h, kept a pointer position of its own in a 640x200
space and counted the mouse's motion in mickeys. The play loop asks for the
mickeys since it last asked (function 11); the menus ask where the pointer
is (3) and put it back in the middle (4). `ID_IN.C` now has that driver,
fed the motion the machine thread sees. The game's calls to it were inline
`_AX = 11; geninterrupt(0x33); x = _CX;`, which became `IN_MouseMickeys`,
`IN_MouseGetPos` and `IN_MouseSetPos`.

On a desktop the pointer is grabbed and put back in the middle after every
movement while a game is being played with the mouse, and let go in the
menus and the demos. In the browser the page captures the mouse instead, and
sends a pointer walking about the window: a report exactly at the centre is
where the walk starts again and moves nothing.

The joystick read the game port at `0x201` by timing how long its
capacitors took to charge. There is no game port, and it reads as unplugged.
A controller in the browser is read by the page and passed to `sys_pad.c`,
which plays it as the keyboard -- the arrows, Ctrl, Space, Alt, Shift, Esc --
through the same keyboard handler. The game's joystick needed calibrating,
had two buttons, and its menus never read it; the keyboard works everywhere.

In the browser, Esc belongs to the page, which gives the mouse back on it,
so the <kbd>`</kbd> key is Esc to the game, as the DOOM container does.

## Savegames and the configuration file

`WL_MAIN.C`, `tools/mkstates.sh`.

`SaveTheGame` wrote the game's structures to the file as they lay in memory:
every actor with its state pointer and list links, `actorat[][]` full of
pointers, `laststatobj`, each static's pointer into `spotvis`. A near pointer
was an offset into the data segment, the same every time that EXE ran, so a
savegame loaded. A 64-bit pointer moves between builds of the engine, so
the same approach would load garbage after an upgrade.

So pointers are written as what they point at. An actor's state is its
index in `allstates[]`, a table of every `statetype` the build defines,
sorted by name, which `tools/mkstates.sh` generates from the preprocessed
source -- preprocessed so that the Spear of Destiny states under `#ifdef
SPEAR` are listed exactly when they are compiled. `actorat` is written as
16-bit words: the value itself below 256, an actor's index with the top bit
set. Actors and statics are written field by field in fixed sizes. A header
says what wrote the file; a DOS savegame, or one from a build with different
states, gets "This game was saved by another version" and a new game rather
than a crash.

The configuration file is still the variables as they lie in memory. A DOS
one is a different size -- 16-bit ints and enums -- so `ReadConfig` only
reads a file of exactly the size this build writes, and starts from the
defaults otherwise. A view size the menu could not have set is reset.

Savegames and the configuration are only ever looked for in the state
directory, never among the game's data, where a DOS install would have its
own.

## The shareware v1.4 data

`GFXV_WL1.H`.

The graphics header for the shareware that came with the source was
"IGRAB-ed on Sun May 03 01:19:32 1992": version 1.0. The shareware every
archive carries is version 1.4, whose `VGAGRAPH.WL1` has 156 chunks and 144
pictures to 1.0's 556 and 136. Used as it was, the header would draw the
wrong picture for nearly everything.

The new header was worked out from the 1.4 data. Its picture table gives
each picture's size; laid against the registered game's header, everything
from the Options title on lines up with the same names, eleven later. The
eleven are help-screen pictures the shareware has and the registered game
does not, plus an advertisement for Spear of Destiny -- which was obvious
once they were drawn -- and the help text's own `^G` commands, which name
pictures by number, confirm the numbering. The audio and maps of 1.4 match
the registered game's headers, which is what the 1.4 source used.

`VERSION.H` chooses: the Makefile builds once with `WOLF_WL1`, which brings
in `WOLF1VER.H`'s `#define UPLOAD` and the shareware header, and once
without, for the registered game. The registered engine, given shareware
data, says which engine plays it rather than drawing nonsense.

## Spear of Destiny

`VERSION.H`, `SODVER.H`, `SDMVER.H`, the Makefile.

The source builds Spear with `SPEAR` defined, and its demo with `SPEARDEMO`
too; each needs its own graphics, audio and map headers, already in the
release (`GFXV_SOD.H`, `GFXV_SDM.H` and so on), so each is an engine of its
own: `wolf3d-sod` and `wolf3d-sdm`. The demo's headers match the demo's
data: 133 graphics chunks, 267 sound chunks. `WL_TEXT.C`, the help and the
end-of-episode text, is Wolfenstein's alone and is left out.

- **The mission packs.** *Return to Danger* and *Ultimate Challenge* were
  shipped as `.SD2` and `.SD3` files read by the same SPEAR.EXE, chosen
  with `-mission`; here `WOLF_MISSION=2` or `3`, which the launchers set.
- **The copy protection.** Spear asked which enemy or weapon was pictured,
  from the manual, before it started. That is off unless
  `WOLF_COPYPROTECTION` is set; its failure path switched to text mode
  through the BIOS, which is gone.
- **The boss key.** F1 in Spear's play loop is a boss key, a fake DOS
  prompt; it was compiled only for GOODTIMES, and now for Spear too.
- **A 64-bit trap.** The code that wakes Spear's ghosts read `actorat`, an
  object pointer or a small tile number, into an `unsigned`: a truncated
  pointer on 64 bits. It is a `uintptr_t`.

Spear was checked against its demo. The full game and the mission packs are
the same code built against headers that came with the source, but have not
been played here, for want of the data.

## Settings

`sys_settings.c`, the end of `WL_MENU.C`'s control section.

DOS left the shape of the picture to the monitor, its size to nobody, and
the balance of music and effects to the sound card's mixer program. Pages
in the game's own style -- built from its window, cursor and checkbox
pictures, so they look like the rest -- have those, the choice of game, and
the mouse and controller behaviour the port added.

They have the game's own settings too, so that there is one place for all
of it: the main menu's Control is Settings, its Sound and Change View are
gone as the same thing twice, and F4, F5, F6 and F12 open its pages.
Back to Demo is gone too, out of a game, with Quit moved up into its place:
`HandleMenu` returns `MENU_IDLE` when the main menu has had no key, mouse or
controller for 30 seconds, and that is taken as Back to Demo was. In a game
the item is still there, as Back to Game.
The Picture page sets the view's size as Change View did, and both go one
size further than DOS let them: 20, the full screen -- 320 wide and all the
160 lines above the status bar, with no border, as DOOM's largest was. The
renderer took it as it was (`MAXVIEWWIDTH` was already 320, and
`ReadConfig` already allowed 20); only the border drawing needed telling
that there is no border to draw, rather than drawing its edge at x = -1
and 320. It is the size to begin with (DOS's was 15). The view is remade
once, on the way out of the page.

The resolution is the window's size, 320x200 times 1 to 6 and stretched to
4:3. Behind the browser it works as well: the container's Xvfb is made as
big as the largest, and the engine tells x11vnc which part to send by
setting the `X11VNC_REMOTE` property on the root window to `cmd=clip:WxH+0+0`
-- x11vnc's own remote control, which it watches -- whenever the window's
size changes. x11vnc changes the size of what it sends, noVNC follows, and
the page goes on filling its window at 4:3.

The Sound page sets the devices the Sound menu did, as it did; the Keys
page every key the Customize screen did, and the strafe keys; the Mouse
page what the Control screen and Mouse Sensitivity did, and the mouse's
buttons. The joystick settings went with them: the DOS joystick is not
read, and a controller arrives as keys. The game's screens are still in
`WL_MENU.C`, unreached. (`sc_F12` was `0x59` in `ID_IN.H`, and in the key
names' table; it is `0x58`.)

A page is a list of items: a name, the variable it sets, its range, and
what to call when it changes. The game's variables are kept in its
`CONFIG` file, written as a page is left, and the port's in `settings`.

They are kept in `settings`, a text file of names and numbers, not in
`CONFIG.WL6`, whose shape the game checks by size. A change takes effect at
once: the machine thread remakes the window and its image at the next tick
when the shape or size changes, and in the browser the page is told the new
shape over the controller connection, as it is when it connects. Choosing
another game writes it to `game` and ends with status 3, which the
launchers -- `docker/entrypoint.sh` and `desktop/wolf3d` -- take as "start
the engine for that one"; they say which games there are in `WOLF_GAMES`,
so an engine started by hand offers none.

New Game asks the same question first, when there is more than one game
(the shareware is left out beside the registered game, and Spear's demo
beside Spear). Choosing another also leaves a file, `newgame`, which the
next engine removes as it starts and takes as "skip the title, open New
Game": the player goes from the question to the next game's episodes or
difficulty as if it were one menu.

The strafe keys are a third sideways move beside the two the game had --
the arrows with the strafe button held, and the mouse or joystick with it
held -- added in `ControlMovement` from `controlstrafe`, which
`PollKeyboardMove` sets from the keys in `settings`. They are kept there,
not in `buttonscan`, since that array's size is part of `CONFIG.WL6`'s.
They are not recorded in demos, which have no room for them.

## Things DOS let the code get away with

- **Writing to string constants.** `US_Print` and `US_CPrint` cut their
  string at each newline in place. String constants were writable under
  Borland and are not here: the first menu crashed. They work on a copy.
- **Reading address 0.** `Quit (NULL)` tested `!*error`, which read DS:0 on
  DOS. It checks for `NULL` first.
- **Ctrl-Z at the end of a file.** `FOREIGN.H` and `F_SPEAR.H` ended with
  the DOS end-of-file byte, which gcc reads as a stray character.
- **Errors before there is a screen.** `Quit` showed `ERRORSCREEN` from the
  graphics file, and was called before the graphics file was open when the
  data was missing. It prints the error instead.
- **Text mode on the way out.** The order screen and the error box were
  copied into the text mode's video memory at B800. They are printed to the
  terminal, in their colours if it is one, the box-drawing characters from
  code page 437 translated.
- **Patch386.** On a 386 the game patched Borland's long division routine,
  in memory, to use 32-bit instructions (`WL_ASM.ASM`'s `jabhack2`). There
  is nothing to patch.

## Files and the DOS library

`sys_port.c`, `id_port.h`.

The game opens its data by bare name from the current directory and writes
there too. That directory is now the state directory; the data can be
elsewhere, read-only, named by `WOLF_DATA`, and reads look there after the
current directory. Names are tried as given, in upper case, in lower case,
and then by searching the directory, so a copy off a CD or out of a Steam
install is found whatever case its names are in. `findfirst` and `findnext`
search both directories with `fnmatch`. `itoa`, `ltoa`, `ultoa`, `strlwr`,
`filelength`, `random` and the free-disk check before saving have POSIX
equivalents.

## The container

The DOOM container's arrangement, carried over with its lessons: Xvfb sized
to the picture, x11vnc with `-nowireframe -noscrollcopyrect` (which
otherwise hold the picture back whenever a mouse button is down, and a
shooter holds the fire button), the picture, the sound and the controller on
one WebSocket port, a page that captures the mouse and plays the sound
through an AudioWorklet. The engine serves its own sound to the page, which
the DOOM container needed a separate mixer for. Behind the browser the
engine draws square pixels and the page stretches the picture to 4:3, which
is a sixth less to send.
