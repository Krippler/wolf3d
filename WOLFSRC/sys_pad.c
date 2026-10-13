// sys_pad.c
//
// A game controller, from the browser.
//
// The pad is plugged into the machine running the browser, so play.html
// reads it through the Gamepad API and sends its state across the same
// port as the picture (docker/wolf-gamepad.js), and websockify hands that
// to a TCP port here, named by WOLF_PAD_PORT. The messages are the DOOM
// container's, little-endian:
//
//	'P' connected buttons:u32 lx ly rx ry:i16 lt rt:u8		16 bytes
//	'N' length name											the pad's name
//
// and back to the page:
//
//	'R' low high:u16 milliseconds:u16						vibrate
//	'A' on:u8												4:3 or square pixels
//
// Wolfenstein 3-D had a joystick, but a two-button one that had to be
// calibrated, and its menus never read it. So the pad is played as the
// keyboard instead: each tick the machine thread asks what keys the pad is
// holding and presses and lets go of them through the same keyboard handler
// the X server's keys go through. The game cannot tell the difference, the
// menus work with it, and rebinding the controls in the game's own Control
// menu rebinds the pad too, as far as the keys allow.
//
//	Left stick, d-pad		walk and turn		(the arrow keys)
//	Right stick, across		turn
//	A, right trigger		fire, or choose		(Ctrl)
//	B, X					open, or choose		(Space)
//	Y						next weapon			(1 to 4)
//	Left bumper				strafe				(Alt)
//	Right bumper, left
//	  stick click			run					(Shift)
//	Menu, View				the menu, back		(Esc)
//

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>

#include "id_port.h"

void	SYS_PadKey (byte code, int extended, int down);		// sys_x11.c
int		SYS_PadNextWeapon (void);							// WL_PLAY.C

enum
{
	B_A, B_B, B_X, B_Y, B_LB, B_RB, B_LT, B_RT, B_VIEW, B_MENU,
	B_LS, B_RS, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_GUIDE
};

#define	STICK		(32767*35/100)	// how far a stick goes before it is a key
#define	TRIGGER		128

static	pthread_t		thread;
static	pthread_mutex_t	lock = PTHREAD_MUTEX_INITIALIZER;
static	int				listenfd = -1;
static	volatile int	conn = -1;

// The pad's state, as last sent
static	int				connected;
static	uint32_t		buttons;
static	int16_t			lx,ly,rx,ry;
static	byte			lt,rt;

// The keys the pad is holding down now
typedef struct
{
	byte	code,extended;
	int		held;
} padkey_t;

enum {K_UP, K_DOWN, K_LEFT, K_RIGHT, K_FIRE, K_USE, K_STRAFE, K_RUN, K_ESC, NUMPADKEYS};

static	padkey_t	keys[NUMPADKEYS] =
{
	{0x48,1}, {0x50,1}, {0x4b,1}, {0x4d,1},	// arrows
	{0x1d,0},		// Ctrl
	{0x39,0},		// Space
	{0x38,0},		// Alt
	{0x36,0},		// right Shift
	{0x01,0},		// Esc
};

static	int		weaponheld;		// the scan code Y is holding, or 0

static void Send (const byte *m, int len)
{
	if (conn >= 0)
		send (conn,m,len,MSG_DONTWAIT | MSG_NOSIGNAL);
}

static void SayAspect (void)
{
	byte		m[2];

	m[0] = 'A';
	m[1] = sys_setting[set_shape];
	Send (m,2);
}

// The shape was changed on the Settings pages
void SYS_PadSayAspect (void)
{
	SayAspect ();
}

//
// Shake the pad: strength 0 to 1, for so many milliseconds
//
void SYS_PadRumble (float strength, int ms)
{
	byte	m[7];
	int		lo,hi;

	if (conn < 0 || strength <= 0 || !sys_setting[set_vibration])
		return;
	if (strength > 1)
		strength = 1;

	lo = (int)(strength * 65535);
	hi = (int)(strength * 0.6f * 65535);
	m[0] = 'R';
	m[1] = lo & 255;	m[2] = lo >> 8;
	m[3] = hi & 255;	m[4] = hi >> 8;
	m[5] = ms & 255;	m[6] = ms >> 8;
	Send (m,7);
}

static void *Serve (void *unused)
{
	byte	buf[512];
	int		len,n,at;

	for (;;)
	{
		int	fd = accept (listenfd,NULL,NULL);
		int	one = 1;

		if (fd < 0)
			continue;
		setsockopt (fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));

		// One controller at a time: a new connection is a reloaded page
		if (conn >= 0)
			close (conn);
		conn = fd;
		fprintf (stderr,"controller: the page connected\n");
		SayAspect ();

		len = 0;
		while ((n = recv (fd,buf + len,sizeof(buf) - len,0)) > 0)
		{
			len += n;
			at = 0;
			while (at < len)
			{
				if (buf[at] == 'P')
				{
					byte	*m = buf + at;

					if (len - at < 16)
						break;
					pthread_mutex_lock (&lock);
					connected = m[1];
					buttons = m[2] | m[3] << 8 | m[4] << 16 | (uint32_t)m[5] << 24;
					lx = m[6] | m[7] << 8;
					ly = m[8] | m[9] << 8;
					rx = m[10] | m[11] << 8;
					ry = m[12] | m[13] << 8;
					lt = m[14];
					rt = m[15];
					pthread_mutex_unlock (&lock);
					at += 16;
				}
				else if (buf[at] == 'N')
				{
					if (len - at < 2 || len - at < 2 + buf[at+1])
						break;
					fprintf (stderr,"controller: %.*s\n",buf[at+1],(char *)buf + at + 2);
					at += 2 + buf[at+1];
				}
				else
					at = len;		// lost the thread: drop what there is
			}
			memmove (buf,buf + at,len - at);
			len -= at;
		}

		close (fd);
		if (conn == fd)
			conn = -1;
		pthread_mutex_lock (&lock);
		connected = 0;
		pthread_mutex_unlock (&lock);
		fprintf (stderr,"controller: the page went away\n");
	}

	return NULL;
}

void SYS_PadStart (void)
{
	const char			*env = getenv ("WOLF_PAD_PORT");
	struct sockaddr_in	addr;
	int					one = 1;

	if (!env || atoi (env) <= 0)
		return;

	listenfd = socket (AF_INET,SOCK_STREAM,0);
	setsockopt (listenfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
	memset (&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons (atoi (env));
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (bind (listenfd,(struct sockaddr *)&addr,sizeof(addr)) || listen (listenfd,2)
	||	pthread_create (&thread,NULL,Serve,NULL))
	{
		fprintf (stderr,"controller: cannot listen on port %s\n",env);
		close (listenfd);
		listenfd = -1;
		return;
	}
	pthread_detach (thread);
}

//
// Called by the machine thread every tick: press and release whatever the
// pad says should be held now
//
void SYS_PadTick (void)
{
	int			want[NUMPADKEYS];
	uint32_t	b;
	int			i,on,x,y,turn;

	if (listenfd < 0)
		return;

	pthread_mutex_lock (&lock);
	on = connected;
	b = buttons;
	x = lx;
	y = ly;
	turn = rx;
	if (rt >= TRIGGER)
		b |= 1 << B_RT;
	if (lt >= TRIGGER)
		b |= 1 << B_LT;
	pthread_mutex_unlock (&lock);

	if (!on)
		b = x = y = turn = 0;

	want[K_UP] = (b & 1 << B_UP) || y < -STICK;
	want[K_DOWN] = (b & 1 << B_DOWN) || y > STICK;
	want[K_LEFT] = (b & 1 << B_LEFT) || x < -STICK || turn < -STICK;
	want[K_RIGHT] = (b & 1 << B_RIGHT) || x > STICK || turn > STICK;
	want[K_FIRE] = (b & (1 << B_A | 1 << B_RT)) != 0;
	want[K_USE] = (b & (1 << B_B | 1 << B_X)) != 0;
	want[K_STRAFE] = (b & 1 << B_LB) != 0;
	want[K_RUN] = (b & (1 << B_RB | 1 << B_LS | 1 << B_LT)) != 0;
	want[K_ESC] = (b & (1 << B_MENU | 1 << B_VIEW)) != 0;

	for (i = 0; i < NUMPADKEYS; i++)
		if (want[i] != keys[i].held)
		{
			keys[i].held = want[i];
			SYS_PadKey (keys[i].code,keys[i].extended,want[i]);
		}

	// Y: the next weapon, as its number key, held while Y is
	if ((b & 1 << B_Y) && !weaponheld)
	{
		weaponheld = SYS_PadNextWeapon ();
		if (weaponheld)
			SYS_PadKey (weaponheld,0,1);
	}
	else if (!(b & 1 << B_Y) && weaponheld)
	{
		SYS_PadKey (weaponheld,0,0);
		weaponheld = 0;
	}
}
