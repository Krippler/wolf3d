// id_port.h
//
// What Borland C++ 3.1 under DOS gave this code for free, and a 64-bit
// Linux C compiler does not.
//
// Included from ID_HEADS.H in place of the DOS headers, so every file of
// the game sees it before anything else. It does three jobs:
//
//   - The 16-bit memory model's keywords (far, near, huge, _seg, interrupt)
//     mean nothing on a flat address space, so they are defined away.
//
//   - The DOS and Borland library calls the game makes are given POSIX
//     equivalents (itoa, filelength, findfirst, strlwr, random...).
//
//   - The VGA hardware the game drove directly -- map mask, write mode,
//     latches, CRTC start address, DAC palette -- is emulated in memory. See
//     the long comment at "VGA" below, which is the heart of the port.
//
// Sizes: int was 16 bits and long 32. Here int is 32 and long 64, so every
// long in the original source has been changed to int32_t, and the
// structures read straight from the data files spell out 16-bit fields as
// word or int16_t and are packed. PORTING-NOTES.md has the details.
//

#ifndef __ID_PORT__
#define __ID_PORT__

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

//
// The memory model.
//
#define far
#define near
#define huge
#define _seg
#define interrupt
#define cdecl
#define pascal

#define O_BINARY	0
#define O_TEXT		0
#define S_IREAD		S_IRUSR
#define S_IWRITE	S_IWUSR

// VALUES.H
#ifndef MAXINT
#define MAXINT		32767
#endif
#define MAXLONG		0x7fffffffL

// Borland's far memory functions are the ordinary ones on a flat model.
#define _fmemcpy	memcpy
#define _fmemset	memset
#define _fmemcmp	memcmp
#define _fmemmove	memmove
#define _fstrcpy	strcpy
#define _fstrlen	strlen
#define _fstrcat	strcat
#define _fstrcmp	strcmp
#define _fstricmp	strcasecmp
#define stricmp		strcasecmp
#define strnicmp	strncasecmp
#define _fstrnicmp	strncasecmp
#define farmalloc	malloc
#define farfree		free

//
// DOS and Borland library calls.
//
extern	int		_argc;
extern	char	**_argv;

char	*itoa (int value, char *string, int radix);
char	*ltoa (long value, char *string, int radix);
char	*ultoa (unsigned long value, char *string, int radix);
char	*strlwr (char *s);
char	*strupr (char *s);
int32_t	filelength (int handle);

// random(n) is 0..n-1 in Borland's library.
#define random(n)	(rand () % (n))
#define randomize()	srand ((unsigned) getpid ())

// What findfirst/findnext fill in: only the name is ever looked at.
struct ffblk
{
	char	ff_name[256];
	void	*ff_priv;
};
#define FA_ARCH		0x20
int		findfirst (const char *pattern, struct ffblk *ff, int attrib);
int		findnext (struct ffblk *ff);

int32_t	SYS_DiskFree (void);			// bytes free where savegames go
int		SYS_Hundredths (void);			// DOS's clock: 0-99 within the second

//
// Opening the game's data files whatever their case.
//
// The DOS file system is case-insensitive and the game asks for VSWAP.WL1;
// a copy unpacked from a zip or off a Steam install may well be vswap.wl1.
// Every open of a data file goes through this, which tries the name as
// given, then upper case, then lower case.
//
int		SYS_Open (const char *name, int flags, ...);
#define open	SYS_Open


/*
=============================================================================

								VGA

	The game drew in Mode Y: 320x200 with 256 colours, the VGA's memory
	split into four planes of 64K each, a byte address naming the same byte
	in all four, and a pixel at x in plane x&3 at address x/4. The map mask
	register chose which planes a CPU write went to; write mode 1 copied all
	four of the planes' latches at once, which is how the status bar, the
	font and the page flips were moved about. bufferofs, displayofs, ylookup,
	latchpics and the three page start addresses are all addresses in that
	space, and the game's C code does arithmetic on them freely.

	So that arithmetic is kept, and the memory under it is emulated. vgamem
	holds address a's plane p at vgamem[a*4 + p]. Put the other way round,
	address a holds the four pixels a*4 .. a*4+3 side by side -- so a row of
	the screen, which is 80 consecutive addresses, is 320 consecutive bytes,
	and showing a page is reading it out in order. Writes through the map
	mask, latch copies and the CRTC start address all keep their meaning;
	only the code that touched the hardware directly has been rewritten, and
	it has been rewritten in terms of these.

=============================================================================
*/

typedef	uint8_t		byte;

#define VGA_PLANESIZE	0x10000
extern	byte	vgamem[VGA_PLANESIZE*4];
extern	byte	vga_mapmask;			// sequencer register 2
extern	byte	vga_readmap;			// graphics register 4
extern	byte	vga_writemode;			// graphics register 5, low bits

#define VGAMAPMASK(x)	(vga_mapmask = (byte)(x))
#define VGAREADMAP(x)	(vga_readmap = (byte)(x))
#define VGAWRITEMODE(x)	(vga_writemode = (byte)(x))

// One byte written to address a, as the CPU would: to every plane the map
// mask selects in write mode 0, or the latches copied in write mode 1. The
// address wraps at 64K, as the 16-bit offset into segment A000 did.
static inline byte *VGA_Ptr (unsigned a)
{
	return &vgamem[(a & 0xffff) << 2];
}

static inline void VGA_Write (unsigned a, byte v)
{
	byte	*p = VGA_Ptr (a);
	byte	m = vga_mapmask;

	if (m & 1) p[0] = v;
	if (m & 2) p[1] = v;
	if (m & 4) p[2] = v;
	if (m & 8) p[3] = v;
}

static inline byte VGA_Read (unsigned a)
{
	return VGA_Ptr (a)[vga_readmap & 3];
}

// Write mode 1: a read of src loads all four latches and a write to dest
// stores them, whatever the data. The map mask still applies.
static inline void VGA_LatchCopy (unsigned src, unsigned dest)
{
	byte	*s = VGA_Ptr (src);
	byte	*d = VGA_Ptr (dest);
	byte	m = vga_mapmask;

	if (m == 15)
	{
		memcpy (d, s, 4);
		return;
	}
	if (m & 1) d[0] = s[0];
	if (m & 2) d[1] = s[1];
	if (m & 4) d[2] = s[2];
	if (m & 8) d[3] = s[3];
}

// The pixel at x,y of the page that starts at address page, for code that
// draws a pixel at a time and has no reason to go through the map mask.
static inline byte *VGA_Pixel (unsigned page, unsigned linewidth, int x, int y)
{
	return &vgamem[(((page + y*linewidth + (x >> 2)) & 0xffff) << 2) + (x & 3)];
}


/*
=============================================================================

						THE MACHINE UNDER THE GAME

	sys_x11.c. A thread of its own stands in for the hardware interrupts
	and the video card's scan-out, so the game's code -- which waits on a
	counter going up, or on a key arriving, in loops that do nothing else --
	keeps working as written:

	  - TimeCount goes up 70 times a second, as timer 0 made it.
	  - Keyboard[], LastScan and LastASCII change as keys go down and up,
		as the keyboard ISR made them, and the mouse's motion is counted.
	  - The page at the CRTC start address is shown through the DAC
		palette, as the monitor showed it.

	The loops that did nothing but wait now call SYS_Spin(), which sleeps a
	millisecond, rather than burning a core.

=============================================================================
*/

void	SYS_Startup (void);
void	SYS_Shutdown (void);
void	SYS_Spin (void);				// a busy-wait loop's body
void	SYS_Throttle (void);			// in what a polling loop calls
void	SYS_WaitVBL (int vbls);
void	SYS_SetDisplay (unsigned crtc, int pelpan);
void	SYS_SetPalette (const byte *palette);	// 768 bytes, 6 bits each
void	SYS_GetPalette (byte *palette);
void	SYS_SetBorder (int color);
void	SYS_ShowNow (void);				// scan out now, not at the next tick
void	SYS_MouseDelta (int *dx, int *dy);
int		SYS_MouseButtons (void);
void	SYS_SetGrab (int wanted);
void	SYS_PadRumble (float strength, int ms);	// sys_pad.c
void	SYS_TextScreen (const byte *screen, int rows, const char *text, int col, int row);

// sys_settings.c: the port's own settings, changed on the Settings pages
enum
{
	set_shape,			// 1: stretched to 4:3 as on a monitor; 0: square pixels
	set_scale,			// the window, in multiples of 320x200 (a desktop)
	set_music,			// 0-10
	set_effects,		// 0-10: the AdLib effects, the digitized sounds, the speaker
	set_mousewalks,		// the mouse's forward and back moves the player
	set_alwaysrun,		// run without holding the run button; holding it walks
	set_vibration,		// the controller shakes when hit
	set_capture,		// the pointer is held in the window while playing (a desktop)
	set_strafeleft,		// the scan codes of the keys that step sideways,
	set_straferight,	// 0 for none
	SET_COUNT
};
extern	int		sys_setting[SET_COUNT];
void	SYS_LoadSettings (void);
void	SYS_SaveSettings (void);
void	SYS_SettingRange (int which, int *min, int *max);
void	SYS_SettingChanged (int which);
int		SYS_InBrowser (void);
int		SYS_NumGames (void);
const char *SYS_GameKind (int i);
const char *SYS_GameTitle (const char *kind);
int		SYS_IsGameNow (const char *kind);
int		SYS_ChooseGame (const char *kind);
int		SYS_RequestNewGame (void);			// the next engine opens at New Game
int		SYS_TakeNewGame (void);				// ...which it asks once
void	SYS_Resize (void);				// sys_x11.c: the window, after shape or scale
void	SYS_PadSayAspect (void);		// sys_pad.c: the page, after shape

// sys_audio.c: where the sound goes, and the thread that asks ID_SD.C for it
int		SYS_AudioRate (void);
void	SYS_AudioStart (void);
void	SYS_AudioStop (void);

#endif
