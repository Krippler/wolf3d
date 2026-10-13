// sys_audio.c
//
// Where the sound goes.
//
// ID_SD.C makes it, a block at a time, from its emulated AdLib, Sound
// Blaster and PC speaker. This file runs the thread that asks for those
// blocks, as fast as they are played and no faster -- which also makes it
// the clock the music keeps time by -- and sends them wherever WOLF_AUDIO
// says:
//
//   tcp:PORT	Serve them, raw, to whoever connects: a 16-byte header saying
//				what they are, then 16-bit little-endian stereo. This is how
//				sound reaches a browser from the container: the proxy that
//				carries the picture carries this too, and play.html plays it.
//				The format is the one the DOOM container's page reads, so
//				the page's player is the same.
//
//	pulse		PulseAudio, or PipeWire's stand-in for it, on a desktop.
//
//	file:PATH	Written to a file in real time, for the smoke test to check
//				that there is sound at all.
//
//	none		Made and thrown away. The game still needs the thread: it
//				is what finishes one sound and lets the next start.
//
// With WOLF_AUDIO unset it is pulse where that was built in, and none
// otherwise.
//

#define _GNU_SOURCE
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>

#ifdef WOLF_PULSE
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

#include "ID_HEADS.H"

#undef open

#define	DEFAULT_RATE	44100
#define	PERIOD			256			// frames a block: 5.8 ms at 44.1 kHz
#define	MAXCLIENTS		4
#define	NSEC			1000000000L

// A listener this far behind is dropped rather than buffered for: it would
// only ever hear the past.
#define	MAXBEHIND		(DEFAULT_RATE * 4)		// a second of 16-bit stereo

typedef enum {out_none, out_tcp, out_pulse, out_file} outtype_t;

static	outtype_t	outtype;
static	int			rate;
static	int			port;
static	char		filepath[1024];

static	pthread_t	thread;
static	volatile int	running;

typedef struct
{
	int		fd;
	byte	*pend;			// what the socket would not take yet
	int		pendlen;
} client_t;

static	client_t	clients[MAXCLIENTS];
static	int			nclients;
static	int			listenfd = -1;

/*
=============================================================================

							CHOOSING

=============================================================================
*/

static void Choose (void)
{
	const char	*env = getenv ("WOLF_AUDIO");
	const char	*r = getenv ("WOLF_AUDIO_RATE");

	rate = DEFAULT_RATE;
	if (r && atoi (r) >= 8000 && atoi (r) <= 96000)
		rate = atoi (r);

	if (!env || !*env)
	{
#ifdef WOLF_PULSE
		outtype = out_pulse;
#else
		outtype = out_none;
#endif
		return;
	}

	if (!strncmp (env,"tcp:",4))
	{
		outtype = out_tcp;
		port = atoi (env + 4);
		if (port <= 0 || port > 65535)
		{
			fprintf (stderr,"sound: bad port in WOLF_AUDIO=%s, no sound\n",env);
			outtype = out_none;
		}
	}
	else if (!strcmp (env,"pulse"))
	{
#ifdef WOLF_PULSE
		outtype = out_pulse;
#else
		fprintf (stderr,"sound: built without PulseAudio, no sound\n");
		outtype = out_none;
#endif
	}
	else if (!strncmp (env,"file:",5))
	{
		outtype = out_file;
		snprintf (filepath,sizeof(filepath),"%s",env + 5);
	}
	else
		outtype = out_none;
}

int SYS_AudioRate (void)
{
	if (!rate)
		Choose ();
	return rate;
}


/*
=============================================================================

							SERVING

=============================================================================
*/

static int Listen (void)
{
	struct sockaddr_in	addr;
	int					fd,one = 1;

	fd = socket (AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0);
	if (fd < 0)
		return -1;

	setsockopt (fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));

	memset (&addr,0,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons (port);
	// Only the proxy in the same container connects; nothing outside it
	// needs to, and nothing outside it can reach it unpublished anyway.
	addr.sin_addr.s_addr = htonl (getenv ("WOLF_AUDIO_ANY") ? INADDR_ANY : INADDR_LOOPBACK);

	if (bind (fd,(struct sockaddr *)&addr,sizeof(addr)) || listen (fd,4))
	{
		close (fd);
		return -1;
	}
	return fd;
}

static void Drop (int i)
{
	close (clients[i].fd);
	free (clients[i].pend);
	clients[i] = clients[--nclients];
}

// Send what can be sent now and keep the rest. A write may take any number
// of bytes, not necessarily a whole frame, so nothing is ever thrown away
// part way: the listener would be left half a sample out, which plays as
// full-scale noise.
static void Owe (int i, const byte *data, int len)
{
	client_t	*c = &clients[i];

	if (!c->pendlen)
	{
		ssize_t	w = send (c->fd,data,len,MSG_NOSIGNAL | MSG_DONTWAIT);

		if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			Drop (i);
			return;
		}
		if (w > 0)
		{
			data += w;
			len -= w;
		}
	}

	if (!len)
		return;

	if (c->pendlen + len > MAXBEHIND)
	{
		fprintf (stderr,"sound: a listener fell a second behind; dropped it\n");
		Drop (i);
		return;
	}

	c->pend = realloc (c->pend,c->pendlen + len);
	memcpy (c->pend + c->pendlen,data,len);
	c->pendlen += len;
}

static void Flush (int i)
{
	client_t	*c = &clients[i];
	ssize_t		w;

	if (!c->pendlen)
		return;

	w = send (c->fd,c->pend,c->pendlen,MSG_NOSIGNAL | MSG_DONTWAIT);
	if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
	{
		Drop (i);
		return;
	}
	if (w > 0)
	{
		memmove (c->pend,c->pend + w,c->pendlen - w);
		c->pendlen -= w;
	}
}

static void Accept (void)
{
	int		fd,one = 1;
	byte	hdr[16];

	while ((fd = accept4 (listenfd,NULL,NULL,SOCK_NONBLOCK)) >= 0)
	{
		if (nclients == MAXCLIENTS)
		{
			close (fd);
			continue;
		}

		// A steady trickle of small writes; waiting to fill a segment
		// would only add delay.
		setsockopt (fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));

		clients[nclients].fd = fd;
		clients[nclients].pend = NULL;
		clients[nclients].pendlen = 0;
		nclients++;

		// What the stream is, before any of it
		memcpy (hdr,"DOOMAUD1",8);
		hdr[8] = rate & 0xff;
		hdr[9] = (rate >> 8) & 0xff;
		hdr[10] = (rate >> 16) & 0xff;
		hdr[11] = (rate >> 24) & 0xff;
		hdr[12] = 2;		// channels
		hdr[13] = 0;
		hdr[14] = 16;		// bits
		hdr[15] = 0;
		Owe (nclients-1,hdr,sizeof(hdr));

		fprintf (stderr,"sound: a listener connected (%d now)\n",nclients);
	}
}


/*
=============================================================================

							THE THREAD

=============================================================================
*/

static void *Paced (void *arg)
{
	int16_t			buf[PERIOD*2];
	struct timespec	next;
	long			period_ns = (long)((double)PERIOD * NSEC / rate);
	FILE			*file = NULL;
	int				i;

	if (outtype == out_file)
	{
		file = fopen (filepath,"wb");
		if (!file)
			fprintf (stderr,"sound: cannot write %s\n",filepath);
	}

	clock_gettime (CLOCK_MONOTONIC,&next);

	while (running)
	{
		// Wait out the period first, to an absolute deadline, so the
		// error in one sleep never adds to the next.
		next.tv_nsec += period_ns;
		while (next.tv_nsec >= NSEC)
		{
			next.tv_nsec -= NSEC;
			next.tv_sec++;
		}
		while (clock_nanosleep (CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL) == EINTR)
			;

		SD_Mix (buf,PERIOD);

		if (outtype == out_tcp && listenfd >= 0)
		{
			Accept ();
			for (i = nclients-1; i >= 0; i--)
				Flush (i);
			for (i = nclients-1; i >= 0; i--)
				Owe (i,(byte *)buf,sizeof(buf));
		}
		else if (file)
		{
			fwrite (buf,sizeof(buf),1,file);
			fflush (file);
		}
	}

	if (file)
		fclose (file);
	return NULL;
}

#ifdef WOLF_PULSE
static void *Pulse (void *arg)
{
	int16_t			buf[PERIOD*2];
	pa_simple		*pa;
	pa_sample_spec	spec;
	pa_buffer_attr	attr;
	int				err;

	spec.format = PA_SAMPLE_S16LE;
	spec.rate = rate;
	spec.channels = 2;

	// About 50 ms in hand: enough not to break up, little enough that a
	// shot is heard with the flash.
	attr.maxlength = (uint32_t)-1;
	attr.tlength = pa_usec_to_bytes (50000,&spec);
	attr.prebuf = (uint32_t)-1;
	attr.minreq = (uint32_t)-1;
	attr.fragsize = (uint32_t)-1;

	pa = pa_simple_new (NULL,"Wolfenstein 3-D",PA_STREAM_PLAYBACK,NULL,
		"Game",&spec,NULL,&attr,&err);
	if (!pa)
	{
		fprintf (stderr,"sound: PulseAudio said %s; carrying on without sound\n",
			pa_strerror (err));
		outtype = out_none;
		return Paced (arg);
	}

	while (running)
	{
		SD_Mix (buf,PERIOD);
		// Blocks until there is room, which is what paces the game's sound
		if (pa_simple_write (pa,buf,sizeof(buf),&err) < 0)
		{
			fprintf (stderr,"sound: PulseAudio said %s; sound stopped\n",
				pa_strerror (err));
			pa_simple_free (pa);
			outtype = out_none;
			return Paced (arg);
		}
	}

	pa_simple_drain (pa,NULL);
	pa_simple_free (pa);
	return NULL;
}
#endif

void SYS_AudioStart (void)
{
	void	*(*run)(void *) = Paced;

	if (running)
		return;
	if (!rate)
		Choose ();

	switch (outtype)
	{
	case out_tcp:
		listenfd = Listen ();
		if (listenfd < 0)
			fprintf (stderr,"sound: cannot listen on port %d (%s); no sound\n",
				port,strerror (errno));
		else
			fprintf (stderr,"sound: %d Hz, 16 bit, stereo on port %d\n",rate,port);
		break;
	case out_pulse:
#ifdef WOLF_PULSE
		run = Pulse;
#endif
		break;
	case out_file:
		fprintf (stderr,"sound: %d Hz, 16 bit, stereo to %s\n",rate,filepath);
		break;
	case out_none:
		break;
	}

	running = 1;
	if (pthread_create (&thread,NULL,run,NULL))
	{
		fprintf (stderr,"sound: cannot start the sound thread\n");
		running = 0;
	}
}

void SYS_AudioStop (void)
{
	int	i;

	if (!running)
		return;

	running = 0;
	pthread_join (thread,NULL);

	for (i = nclients-1; i >= 0; i--)
		Drop (i);
	if (listenfd >= 0)
		close (listenfd);
	listenfd = -1;
}
