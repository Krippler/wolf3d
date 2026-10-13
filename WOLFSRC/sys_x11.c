// sys_x11.c
//
// The machine under the game.
//
// Wolfenstein 3-D was written against a PC's hardware, and it leans on the
// hardware doing things while the game is busy elsewhere: timer 0 counting
// TimeCount up seventy times a second, the keyboard interrupt filling in
// Keyboard[] as keys go down, the VGA card showing whichever page the CRTC
// start address names, through whatever is in the DAC. The game waits on
// those in loops that do nothing else -- "while (TimeCount < frame);",
// "while (!LastScan);" -- because on DOS something else always made them
// change.
//
// So something else still does. One thread here plays all three parts, on
// a 70 Hz clock:
//
//   - It counts TimeCount, as timer 0 did.
//
//   - It takes the X server's key and button events and turns them into the
//     bytes the keyboard controller would have sent -- make codes, break
//     codes, the 0xe0 prefix -- and hands each to ID_IN.C's original
//     keyboard handler, so the game sees exactly what it saw on DOS. Mouse
//     motion is added up for INT 33h's "how far since last asked".
//
//   - It shows the page at the start address through the palette in the
//     window, scaled, as the monitor did -- but only when that has changed,
//     and straight away when the game flips pages rather than waiting for
//     the next tick, so a frame is on screen as soon as it is finished.
//
// The game's own thread never touches X. That keeps Xlib out of any
// question of locking, and it means a game stuck in a loop still has a
// window that answers.
//

#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>

// Not ID_HEADS.H: its names (Button0, Button1...) collide with Xlib's.
#include "id_port.h"

typedef	uint32_t	longword;
extern	volatile longword	TimeCount;
extern	unsigned	linewidth;
void	INL_KeyService (byte k);
void	Quit (char *error);

#define	TICK_NS		(1000000000L / 70)

static	Display		*dpy;
static	Window		win;
static	GC			gc;
static	XImage		*image;
static	XShmSegmentInfo	shminfo;
static	int		useshm;
static	Atom		wm_delete;
static	Cursor		blankcursor;

static	int			winwidth,winheight;		// the picture, in window pixels
static	int			xmap[1920];				// window column -> screen column
static	int			ymap[1440];				// window row -> screen row

static	pthread_t		machine;
static	pthread_mutex_t	lock = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t	ticked = PTHREAD_COND_INITIALIZER;
static	volatile int	running;
static	volatile int	shownow;			// a flip: scan out before the next tick
static	longword		vblcount;			// ticks since startup, for WaitVBL

// The video card's state, as the game last set it
static	volatile unsigned	crtcstart;
static	byte				dac[768];			// 6 bits a component
static	volatile int		dacchanged;

// What was last put in the window, to tell whether anything has changed
static	byte		shownscreen[320*200];
static	byte		shownpal[768];
static	int			shownonce;

// The mouse, as INT 33h reported it
static	int			mousedx,mousedy;
static	int			mousebuttons;
static	int			lastmx,lastmy;
static	int			grabwanted,grabbed;
static	int			focused = 1;

// Keys currently held, by the byte that pressed them, so a lost focus can
// let them all go
static	byte		held[256];
static	byte		heldext[256];

/*
=============================================================================

							THE KEYBOARD

	X key symbols to PC/AT set 1 scan codes. A key with the extended flag
	sends 0xe0 first, as the grey cursor keys, the right-hand Ctrl and Alt
	and the keypad's Enter and / do.

=============================================================================
*/

typedef struct
{
	KeySym	sym;
	byte	code;
	byte	extended;
} keymap_t;

static keymap_t keymap[] =
{
	{XK_Escape,0x01},
	{XK_1,0x02},{XK_2,0x03},{XK_3,0x04},{XK_4,0x05},{XK_5,0x06},
	{XK_6,0x07},{XK_7,0x08},{XK_8,0x09},{XK_9,0x0a},{XK_0,0x0b},
	{XK_minus,0x0c},{XK_equal,0x0d},{XK_BackSpace,0x0e},{XK_Tab,0x0f},
	{XK_q,0x10},{XK_w,0x11},{XK_e,0x12},{XK_r,0x13},{XK_t,0x14},
	{XK_y,0x15},{XK_u,0x16},{XK_i,0x17},{XK_o,0x18},{XK_p,0x19},
	{XK_bracketleft,0x1a},{XK_bracketright,0x1b},{XK_Return,0x1c},
	{XK_Control_L,0x1d},
	{XK_a,0x1e},{XK_s,0x1f},{XK_d,0x20},{XK_f,0x21},{XK_g,0x22},
	{XK_h,0x23},{XK_j,0x24},{XK_k,0x25},{XK_l,0x26},
	{XK_semicolon,0x27},{XK_apostrophe,0x28},{XK_grave,0x29},
	{XK_Shift_L,0x2a},{XK_backslash,0x2b},
	{XK_z,0x2c},{XK_x,0x2d},{XK_c,0x2e},{XK_v,0x2f},{XK_b,0x30},
	{XK_n,0x31},{XK_m,0x32},
	{XK_comma,0x33},{XK_period,0x34},{XK_slash,0x35},{XK_Shift_R,0x36},
	{XK_KP_Multiply,0x37},{XK_Alt_L,0x38},{XK_Meta_L,0x38},
	{XK_space,0x39},{XK_Caps_Lock,0x3a},
	{XK_F1,0x3b},{XK_F2,0x3c},{XK_F3,0x3d},{XK_F4,0x3e},{XK_F5,0x3f},
	{XK_F6,0x40},{XK_F7,0x41},{XK_F8,0x42},{XK_F9,0x43},{XK_F10,0x44},
	{XK_Num_Lock,0x45},{XK_Scroll_Lock,0x46},
	{XK_KP_Home,0x47},{XK_KP_7,0x47},{XK_KP_Up,0x48},{XK_KP_8,0x48},
	{XK_KP_Prior,0x49},{XK_KP_9,0x49},{XK_KP_Subtract,0x4a},
	{XK_KP_Left,0x4b},{XK_KP_4,0x4b},{XK_KP_Begin,0x4c},{XK_KP_5,0x4c},
	{XK_KP_Right,0x4d},{XK_KP_6,0x4d},{XK_KP_Add,0x4e},
	{XK_KP_End,0x4f},{XK_KP_1,0x4f},{XK_KP_Down,0x50},{XK_KP_2,0x50},
	{XK_KP_Next,0x51},{XK_KP_3,0x51},{XK_KP_Insert,0x52},{XK_KP_0,0x52},
	{XK_KP_Delete,0x53},{XK_KP_Decimal,0x53},
	{XK_F11,0x57},{XK_F12,0x58},

	{XK_KP_Enter,0x1c,1},{XK_Control_R,0x1d,1},{XK_KP_Divide,0x35,1},
	{XK_Alt_R,0x38,1},{XK_Meta_R,0x38,1},{XK_ISO_Level3_Shift,0x38,1},
	{XK_Home,0x47,1},{XK_Up,0x48,1},{XK_Prior,0x49,1},
	{XK_Left,0x4b,1},{XK_Right,0x4d,1},
	{XK_End,0x4f,1},{XK_Down,0x50,1},{XK_Next,0x51,1},
	{XK_Insert,0x52,1},{XK_Delete,0x53,1},
	{0}
};

static void SendKey (byte code, int extended, int down)
{
	if (extended)
		INL_KeyService (0xe0);
	INL_KeyService (down ? code : code | 0x80);

	if (extended)
		heldext[code] = down;
	else
		held[code] = down;
}

static void KeyEvent (XKeyEvent *ev, int down)
{
	KeySym		sym = XLookupKeysym (ev,0);
	keymap_t	*k;

	if (sym == XK_Pause || sym == XK_Break)
	{
		// The Pause key sends 0xe1 and a sequence the handler never looks
		// past, and only when pressed.
		if (down)
			INL_KeyService (0xe1);
		return;
	}

	// Shifted symbols from keyboards that report them first
	if (sym >= XK_A && sym <= XK_Z)
		sym += XK_a - XK_A;

	for (k = keymap; k->sym; k++)
		if (k->sym == sym)
		{
			SendKey (k->code,k->extended,down);
			return;
		}
}

static void ReleaseAll (void)
{
	int	i;

	for (i = 0; i < 256; i++)
	{
		if (held[i])
			SendKey (i,0,0);
		if (heldext[i])
			SendKey (i,1,0);
	}
	mousebuttons = 0;
}


/*
=============================================================================

							THE MOUSE

	INT 33h function 11 said how far the mouse had moved since it was last
	asked; the game turns by that. The X server says where the pointer is,
	so the difference between one report and the last is what is counted.

	A report exactly at the centre of the window is not counted, only taken
	as the new place to measure from. play.html, which locks the browser's
	pointer and sends where a pointer would be if it moved by what the mouse
	did, walks that position around the window and sends the centre when it
	nears an edge, to start again from the middle; it sends button changes
	at the centre too, so that clicking cannot also turn. On a desktop, with
	the pointer grabbed, the pointer is put back in the centre after every
	move, and the report that produces is the one ignored.

=============================================================================
*/

static void MouseMoved (int x, int y)
{
	int	cx = winwidth/2;
	int	cy = winheight/2;

	if (x == cx && y == cy)
	{
		lastmx = cx;
		lastmy = cy;
		return;
	}

	mousedx += x - lastmx;
	mousedy += y - lastmy;
	lastmx = x;
	lastmy = y;

	if (grabbed)
	{
		XWarpPointer (dpy,None,win,0,0,0,0,cx,cy);
		lastmx = cx;
		lastmy = cy;
	}
}

static void UpdateGrab (void)
{
	int	want = grabwanted && focused;

	if (want == grabbed)
		return;
	grabbed = want;

	if (want)
	{
		XDefineCursor (dpy,win,blankcursor);
		XGrabPointer (dpy,win,True,
			ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
			GrabModeAsync,GrabModeAsync,win,None,CurrentTime);
		XWarpPointer (dpy,None,win,0,0,0,0,winwidth/2,winheight/2);
		lastmx = winwidth/2;
		lastmy = winheight/2;
	}
	else
	{
		XUngrabPointer (dpy,CurrentTime);
		XUndefineCursor (dpy,win);
	}
}

// The ratio of the window's pixels to the screen's that a mouse movement is
// scaled by, so turning feels the same whatever size the window is.
static int MouseScale (int d, int windowsize, int screensize)
{
	return d * screensize / windowsize;
}

void SYS_MouseDelta (int *dx, int *dy)
{
	pthread_mutex_lock (&lock);
	*dx = MouseScale (mousedx,winwidth,320) * 2;
	*dy = MouseScale (mousedy,winheight,200) * 2;
	mousedx = mousedy = 0;
	pthread_mutex_unlock (&lock);
}

int SYS_MouseButtons (void)
{
	return mousebuttons;
}

//
// Capture the pointer, on a desktop, while a game is being played. The
// game asks for it from PlayLoop and lets go in the menus.
//
void SYS_SetGrab (int wanted)
{
	if (getenv ("WOLF_NOGRAB"))
		wanted = 0;
	grabwanted = wanted;
}


/*
=============================================================================

							THE MONITOR

=============================================================================
*/

static unsigned long	redmask,greenmask,bluemask;
static int				redshift,greenshift,blueshift;
static uint32_t			pal32[256];

static int MaskShift (unsigned long mask)
{
	int	s = 0;

	if (!mask)
		return 0;
	while (!(mask & 1))
	{
		mask >>= 1;
		s++;
	}
	return s;
}

static void BuildPalette (const byte *pal)
{
	int	i;

	for (i = 0; i < 256; i++)
	{
		unsigned	r = pal[i*3] & 63, g = pal[i*3+1] & 63, b = pal[i*3+2] & 63;

		// six bits to eight, so 63 is full white rather than 252
		r = (r << 2) | (r >> 4);
		g = (g << 2) | (g >> 4);
		b = (b << 2) | (b >> 4);

		pal32[i] = ((r << redshift) & redmask)
				 | ((g << greenshift) & greenmask)
				 | ((b << blueshift) & bluemask);
	}
}

//
// Show the page at the start address. Returns without drawing if neither
// it nor the palette has changed since the last time.
//
static void ScanOut (void)
{
	byte		screen[320*200];
	byte		pal[768];
	unsigned	start;
	int			x,y;

	pthread_mutex_lock (&lock);
	start = crtcstart;
	memcpy (pal,dac,768);
	pthread_mutex_unlock (&lock);

	for (y = 0; y < 200; y++)
	{
		unsigned	a = (start + y*linewidth) & 0xffff;

		// A row is contiguous unless it runs off the end of the 64K the
		// card could address, which the game never makes it do.
		if (a + 80 <= 0x10000)
			memcpy (screen + y*320,&vgamem[a*4],320);
		else
			for (x = 0; x < 320; x++)
				screen[y*320+x] = VGA_Ptr (a + (x>>2))[x&3];
	}

	if (shownonce && !memcmp (screen,shownscreen,sizeof(screen))
		&& !memcmp (pal,shownpal,768))
		return;

	memcpy (shownscreen,screen,sizeof(screen));
	memcpy (shownpal,pal,768);
	shownonce = 1;

	BuildPalette (pal);

	for (y = 0; y < winheight; y++)
	{
		byte		*src = screen + ymap[y]*320;
		char		*row = image->data + y*image->bytes_per_line;

		if (image->bits_per_pixel == 32)
		{
			uint32_t	*dst = (uint32_t *)row;

			for (x = 0; x < winwidth; x++)
				dst[x] = pal32[src[xmap[x]]];
		}
		else
			for (x = 0; x < winwidth; x++)
				XPutPixel (image,x,y,pal32[src[xmap[x]]]);
	}

	if (useshm)
		XShmPutImage (dpy,win,gc,image,0,0,0,0,winwidth,winheight,False);
	else
		XPutImage (dpy,win,gc,image,0,0,0,0,winwidth,winheight);
	XFlush (dpy);
}

static void Events (void)
{
	XEvent	ev;

	while (XPending (dpy))
	{
		XNextEvent (dpy,&ev);

		switch (ev.type)
		{
		case KeyPress:
			KeyEvent (&ev.xkey,1);
			break;
		case KeyRelease:
			KeyEvent (&ev.xkey,0);
			break;

		case ButtonPress:
		case ButtonRelease:
		{
			int	bit = 0;

			switch (ev.xbutton.button)
			{
			case Button1: bit = 1; break;
			case Button3: bit = 2; break;
			case Button2: bit = 4; break;
			}
			pthread_mutex_lock (&lock);
			if (ev.type == ButtonPress)
				mousebuttons |= bit;
			else
				mousebuttons &= ~bit;
			MouseMoved (ev.xbutton.x,ev.xbutton.y);
			pthread_mutex_unlock (&lock);
			break;
		}

		case MotionNotify:
			pthread_mutex_lock (&lock);
			MouseMoved (ev.xmotion.x,ev.xmotion.y);
			pthread_mutex_unlock (&lock);
			break;

		case FocusIn:
			focused = 1;
			break;
		case FocusOut:
			// A grab of our own reports a focus change too
			if (ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab)
				break;
			focused = 0;
			ReleaseAll ();
			break;

		case Expose:
			shownonce = 0;
			break;

		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == wm_delete)
			{
				// Closing the window ends the game, as quitting from the
				// menu would without the question.
				running = 0;
				raise (SIGTERM);
			}
			break;
		}
	}
}

static void *Machine (void *unused)
{
	struct timespec	next;

	clock_gettime (CLOCK_MONOTONIC,&next);

	while (running)
	{
		int	timedout;

		next.tv_nsec += TICK_NS;
		if (next.tv_nsec >= 1000000000L)
		{
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}

		//
		// Wait for the tick, scanning out early for a page flip. The flip's
		// signal comes on the condition variable; the tick is the timeout.
		//
		pthread_mutex_lock (&lock);
		for (;;)
		{
			int	r = 0;

			if (!shownow)
				r = pthread_cond_timedwait (&ticked,&lock,&next);
			if (shownow)
			{
				shownow = 0;
				pthread_mutex_unlock (&lock);
				ScanOut ();
				pthread_mutex_lock (&lock);
				continue;
			}
			if (r == ETIMEDOUT)
				break;
		}
		vblcount++;
		pthread_cond_broadcast (&ticked);
		pthread_mutex_unlock (&lock);

		TimeCount++;

		Events ();
		UpdateGrab ();
		ScanOut ();

		// Fell more than a few ticks behind -- the machine was suspended,
		// say -- so start counting again from now rather than racing
		{
			struct timespec	now;

			clock_gettime (CLOCK_MONOTONIC,&now);
			if (now.tv_sec > next.tv_sec + 1)
				next = now;
		}
	}

	return NULL;
}


/*
=============================================================================

						WHAT THE GAME CALLS

=============================================================================
*/

void SYS_WaitVBL (int vbls)
{
	longword	target;

	pthread_mutex_lock (&lock);
	target = vblcount + vbls;
	while (running && (int32_t)(vblcount - target) < 0)
		pthread_cond_wait (&ticked,&lock);
	pthread_mutex_unlock (&lock);
}

void SYS_SetDisplay (unsigned crtc, int pelpan)
{
	pthread_mutex_lock (&lock);
	crtcstart = crtc;
	shownow = 1;
	pthread_cond_broadcast (&ticked);
	pthread_mutex_unlock (&lock);
}

void SYS_ShowNow (void)
{
	SYS_SetDisplay (crtcstart,0);
}

void SYS_SetPalette (const byte *palette)
{
	pthread_mutex_lock (&lock);
	memcpy (dac,palette,768);
	pthread_mutex_unlock (&lock);
}

void SYS_GetPalette (byte *palette)
{
	pthread_mutex_lock (&lock);
	memcpy (palette,dac,768);
	pthread_mutex_unlock (&lock);
}

void SYS_SetBorder (int color)
{
}

//
// The body of a loop that does nothing but wait for the machine to change
// something. A millisecond is a fourteenth of a tick.
//
void SYS_Spin (void)
{
	struct timespec	ms = {0,1000000};

	nanosleep (&ms,NULL);
}

static void ParseSize (void)
{
	const char	*env;
	int			scale = 2;
	int			aspect = 1;
	int			i;

	if ((env = getenv ("WOLF_SCALE")) && atoi (env) > 0)
		scale = atoi (env);
	if (scale > 6)
		scale = 6;

	// DOS showed 320x200 on a 4:3 monitor, so every pixel was a fifth
	// taller than wide. Square pixels are there for anyone who wants them.
	if ((env = getenv ("WOLF_ASPECT")) && !strcmp (env,"square"))
		aspect = 0;

	winwidth = 320*scale;
	winheight = (aspect ? 240 : 200)*scale;

	for (i = 0; i < winwidth; i++)
		xmap[i] = i * 320 / winwidth;
	for (i = 0; i < winheight; i++)
		ymap[i] = i * 200 / winheight;
}

static int ShmError;
static int ShmErrorHandler (Display *d, XErrorEvent *e)
{
	ShmError = 1;
	return 0;
}

static void MakeImage (Visual *visual, int depth)
{
	useshm = XShmQueryExtension (dpy) && !getenv ("WOLF_NOSHM");

	if (useshm)
	{
		int	(*old)(Display *, XErrorEvent *);

		image = XShmCreateImage (dpy,visual,depth,ZPixmap,NULL,&shminfo,
			winwidth,winheight);
		if (image)
		{
			shminfo.shmid = shmget (IPC_PRIVATE,
				image->bytes_per_line * image->height,IPC_CREAT | 0600);
			shminfo.shmaddr = image->data = shmat (shminfo.shmid,0,0);
			shminfo.readOnly = False;

			// The server may be across a network, or in a container that
			// cannot see this one's shared memory; it says so with an
			// error rather than a return value.
			ShmError = 0;
			old = XSetErrorHandler (ShmErrorHandler);
			XShmAttach (dpy,&shminfo);
			XSync (dpy,False);
			XSetErrorHandler (old);

			// Gone as soon as both sides have detached
			shmctl (shminfo.shmid,IPC_RMID,0);

			if (!ShmError)
				return;

			XDestroyImage (image);
			shmdt (shminfo.shmaddr);
		}
		useshm = 0;
	}

	image = XCreateImage (dpy,visual,depth,ZPixmap,0,NULL,winwidth,winheight,32,0);
	if (!image)
		Quit ("SYS_Startup: Cannot make an image to draw into");
	image->data = calloc (1,image->bytes_per_line * winheight);
}

void SYS_Startup (void)
{
	XVisualInfo		vinfo;
	XSetWindowAttributes	attr;
	XSizeHints		*hints;
	XClassHint		*classhint;
	int				screen;
	Pixmap			blank;
	XColor			black = {0};
	static char		nodata[8];

	if (running)
		return;

	ParseSize ();

	dpy = XOpenDisplay (NULL);
	if (!dpy)
	{
		fprintf (stderr,"Cannot open the X display %s. Wolfenstein 3-D needs "
			"an X server, or XWayland, to draw into.\n",
			getenv ("DISPLAY") ? getenv ("DISPLAY") : "(DISPLAY is not set)");
		exit (1);
	}
	screen = DefaultScreen (dpy);

	if (!XMatchVisualInfo (dpy,screen,24,TrueColor,&vinfo)
	&&	!XMatchVisualInfo (dpy,screen,32,TrueColor,&vinfo)
	&&	!XMatchVisualInfo (dpy,screen,16,TrueColor,&vinfo))
	{
		fprintf (stderr,"The X server has no TrueColor visual to draw with.\n");
		exit (1);
	}

	redmask = vinfo.red_mask;
	greenmask = vinfo.green_mask;
	bluemask = vinfo.blue_mask;
	// The masks are for 8-bit components shifted into place; for a 16-bit
	// visual the low bits simply fall off the bottom of the mask.
	redshift = MaskShift (redmask) - (8 - __builtin_popcountl (redmask));
	greenshift = MaskShift (greenmask) - (8 - __builtin_popcountl (greenmask));
	blueshift = MaskShift (bluemask) - (8 - __builtin_popcountl (bluemask));

	attr.colormap = XCreateColormap (dpy,RootWindow (dpy,screen),vinfo.visual,AllocNone);
	attr.background_pixel = BlackPixel (dpy,screen);
	attr.border_pixel = 0;
	attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask
		| ButtonReleaseMask | PointerMotionMask | FocusChangeMask
		| ExposureMask | StructureNotifyMask;

	win = XCreateWindow (dpy,RootWindow (dpy,screen),0,0,winwidth,winheight,0,
		vinfo.depth,InputOutput,vinfo.visual,
		CWColormap | CWBackPixel | CWBorderPixel | CWEventMask,&attr);

	XStoreName (dpy,win,"Wolfenstein 3-D");

	classhint = XAllocClassHint ();
	classhint->res_name = "wolf3d";
	classhint->res_class = "Wolf3d";
	XSetClassHint (dpy,win,classhint);
	XFree (classhint);

	// Fixed size: the picture is scaled to the window when it is made, and
	// a window manager stretching it afterwards would only leave black bars.
	hints = XAllocSizeHints ();
	hints->flags = PMinSize | PMaxSize;
	hints->min_width = hints->max_width = winwidth;
	hints->min_height = hints->max_height = winheight;
	XSetWMNormalHints (dpy,win,hints);
	XFree (hints);

	wm_delete = XInternAtom (dpy,"WM_DELETE_WINDOW",False);
	XSetWMProtocols (dpy,win,&wm_delete,1);

	blank = XCreateBitmapFromData (dpy,win,nodata,8,8);
	blankcursor = XCreatePixmapCursor (dpy,blank,blank,&black,&black,0,0);
	XFreePixmap (dpy,blank);

	// Held keys repeat as presses alone, as a PC keyboard's typematic did,
	// rather than as a release and a press that would read as letting go.
	XkbSetDetectableAutoRepeat (dpy,True,NULL);

	gc = XCreateGC (dpy,win,0,NULL);
	MakeImage (vinfo.visual,vinfo.depth);

	XMapRaised (dpy,win);
	XSetInputFocus (dpy,win,RevertToParent,CurrentTime);
	XFlush (dpy);

	lastmx = winwidth/2;
	lastmy = winheight/2;

	running = 1;
	if (pthread_create (&machine,NULL,Machine,NULL))
	{
		fprintf (stderr,"Cannot start the timer thread.\n");
		exit (1);
	}
}

void SYS_Shutdown (void)
{
	if (!running)
		return;

	pthread_mutex_lock (&lock);
	running = 0;
	pthread_cond_broadcast (&ticked);
	pthread_mutex_unlock (&lock);

	if (!pthread_equal (pthread_self (),machine))
		pthread_join (machine,NULL);

	if (grabbed)
		XUngrabPointer (dpy,CurrentTime);
	if (useshm)
		XShmDetach (dpy,&shminfo);
	XCloseDisplay (dpy);
	dpy = NULL;
}
