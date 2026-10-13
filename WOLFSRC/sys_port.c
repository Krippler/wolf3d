// sys_port.c
//
// The DOS and Borland library calls the game makes, on POSIX: number
// formatting, file lengths, wildcard searches, and an open() that finds the
// game's data whatever case its file names are in and wherever it is kept.
//

#define _GNU_SOURCE
#include <dirent.h>
#include <fnmatch.h>
#include <stdarg.h>

#include "ID_HEADS.H"

#undef open

int		_argc;
char	**_argv;

/*
=============================================================================

							NUMBERS TO STRINGS

=============================================================================
*/

char *ultoa (unsigned long value, char *string, int radix)
{
	char	buf[72];
	int		i = 0;

	if (radix < 2 || radix > 36)
		radix = 10;

	do
	{
		int	d = value % radix;

		buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
		value /= radix;
	} while (value);

	while (i)
		*string++ = buf[--i];
	*string = 0;

	return string;
}

char *ltoa (long value, char *string, int radix)
{
	char	*s = string;

	if (value < 0 && radix == 10)
	{
		*s++ = '-';
		ultoa (-(unsigned long)value,s,radix);
	}
	else
		ultoa ((unsigned long)value,s,radix);

	return string;
}

// The game only ever formats ints and longs that were 16 and 32 bits wide,
// and a negative number in another base came out as its two's complement in
// that width. Not that it ever asks for one.
char *itoa (int value, char *string, int radix)
{
	if (radix != 10 && value < 0)
		return ultoa ((unsigned long)(value & 0xffff),string,radix), string;
	return ltoa (value,string,radix);
}

char *strlwr (char *s)
{
	char	*p;

	for (p = s; *p; p++)
		*p = tolower ((unsigned char)*p);
	return s;
}

char *strupr (char *s)
{
	char	*p;

	for (p = s; *p; p++)
		*p = toupper ((unsigned char)*p);
	return s;
}


/*
=============================================================================

								FILES

	The game opens its files by bare name, from the current directory, and
	writes its savegames and configuration there too. That directory is the
	state directory here: the entrypoint and the launcher both start the
	game in it. The data can be somewhere else, read-only -- a volume in a
	container, a Steam install -- named by WOLF_DATA, and reads look there
	after the current directory. Writes only ever go to the current
	directory.

	DOS did not care about case and Linux does: the game asks for VSWAP.WL6,
	and a copy off a CD or out of a zip may be vswap.wl6. So each name is
	tried as given, in upper case and in lower case.

=============================================================================
*/

static const char *DataDir (void)
{
	const char	*d = getenv ("WOLF_DATA");

	return d && *d ? d : NULL;
}

static int TryOpen (const char *dir, const char *name, int flags, mode_t mode)
{
	DIR				*d;
	struct dirent	*e;
	char	path[4096];
	char	upper[256],lower[256];
	const char	*tries[3];
	int		i,fd;

	if (strlen (name) >= sizeof(upper))
		return -1;

	strcpy (upper,name);
	strupr (upper);
	strcpy (lower,name);
	strlwr (lower);
	tries[0] = name;
	tries[1] = upper;
	tries[2] = lower;

	for (i = 0; i < 3; i++)
	{
		if (dir)
			snprintf (path,sizeof(path),"%s/%s",dir,tries[i]);
		else
			snprintf (path,sizeof(path),"%s",tries[i]);

		fd = open (path,flags,mode);
		if (fd != -1)
			return fd;
	}

	// Vswap.Wl6, say: look through the directory for it
	d = opendir (dir ? dir : ".");
	if (!d)
		return -1;
	fd = -1;
	while (fd == -1 && (e = readdir (d)))
		if (!strcasecmp (e->d_name,name))
		{
			snprintf (path,sizeof(path),"%s/%s",dir ? dir : ".",e->d_name);
			fd = open (path,flags,mode);
		}
	closedir (d);
	return fd;
}

//
// The game's own files -- its settings and savegames -- are only ever its
// own, in the current directory: one sitting in the data directory came
// with the data, from DOS perhaps, and is no business of this one's.
//
static int IsOwnFile (const char *name)
{
	return !strncasecmp (name,"CONFIG.",7) || !strncasecmp (name,"SAVEGAM",7);
}

int SYS_Open (const char *name, int flags, ...)
{
	mode_t	mode = 0;
	int		fd;

	if (flags & O_CREAT)
	{
		va_list	ap;

		va_start (ap,flags);
		mode = va_arg (ap,int);
		va_end (ap);

		// DOS's S_IREAD|S_IWRITE meant "ordinary file". Readable by the
		// group and others too, as anything else written here is.
		mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

		// open() on DOS with O_CREAT but no O_TRUNC left a longer old file's
		// tail in place too, but the game always writes a whole file, and
		// a stale tail after a shorter savegame is no use to anyone.
		return open (name,flags | O_TRUNC,mode);
	}

	if ((flags & O_ACCMODE) != O_RDONLY)
		return open (name,flags);

	fd = TryOpen (NULL,name,flags,0);
	if (fd == -1 && DataDir () && !IsOwnFile (name))
		fd = TryOpen (DataDir (),name,flags,0);
	return fd;
}

int32_t filelength (int handle)
{
	struct stat	st;

	if (fstat (handle,&st))
		return -1;
	return st.st_size;
}


/*
=============================================================================

						WILDCARD SEARCHES

	findfirst/findnext walk the matches one at a time. Here the current
	directory and the data directory are read in one go, case-insensitively,
	and handed out in order.

=============================================================================
*/

typedef struct
{
	int		count,next;
	char	names[64][256];
} findstate_t;

static void FindIn (findstate_t *st, const char *dir, const char *pattern)
{
	DIR				*d = opendir (dir);
	struct dirent	*e;
	int				i;

	if (!d)
		return;

	while ((e = readdir (d)) && st->count < 64)
	{
		if (fnmatch (pattern,e->d_name,FNM_CASEFOLD))
			continue;
		if (strlen (e->d_name) >= 256)
			continue;

		for (i = 0; i < st->count; i++)
			if (!strcasecmp (st->names[i],e->d_name))
				break;
		if (i < st->count)
			continue;		// in both: the current directory's wins

		strcpy (st->names[st->count++],e->d_name);
	}

	closedir (d);
}

int findnext (struct ffblk *ff)
{
	findstate_t	*st = ff->ff_priv;

	if (!st || st->next >= st->count)
	{
		free (st);
		ff->ff_priv = NULL;
		return -1;
	}

	strcpy (ff->ff_name,st->names[st->next++]);
	return 0;
}

int findfirst (const char *pattern, struct ffblk *ff, int attrib)
{
	findstate_t	*st = calloc (1,sizeof(findstate_t));

	if (!st)
		return -1;

	FindIn (st,".",pattern);
	if (DataDir () && !IsOwnFile (pattern))
		FindIn (st,DataDir (),pattern);

	ff->ff_priv = st;
	return findnext (ff);
}


/*
=============================================================================

								ODDS AND ENDS

=============================================================================
*/

#include <sys/statvfs.h>
#include <time.h>

//
// What the save menu checks before it writes: the free space where the
// savegame goes, which is the current directory. Capped at 2G, which is
// all the int32_t the game keeps it in can hold.
//
int32_t SYS_DiskFree (void)
{
	struct statvfs	st;
	uint64_t		bytes;

	if (statvfs (".",&st))
		return 0x7fffffff;
	bytes = (uint64_t)st.f_bavail * st.f_frsize;
	return bytes > 0x7fffffff ? 0x7fffffff : (int32_t)bytes;
}

int SYS_Hundredths (void)
{
	struct timespec	t;

	clock_gettime (CLOCK_REALTIME,&t);
	return t.tv_nsec / 10000000;
}

//
// A text mode screen, as the game left it on the way out: 80 columns of
// character and attribute pairs, rows of them. Printed to the terminal in
// the screen's own colours if the output is one, as text if it is not.
// text, if there is any, goes over it at col,row, as the game wrote its
// error message into the box it had drawn.
//
// The characters are code page 437; the box drawing and the blocks in the
// order screen are translated, and anything else past 127 is a space.
//
static const char *CP437 (byte c)
{
	static char		ascii[2];
	static const char	*high[128] =
	{
		"Ç","ü","é","â","ä","à","å","ç","ê","ë","è","ï","î","ì","Ä","Å",
		"É","æ","Æ","ô","ö","ò","û","ù","ÿ","Ö","Ü","¢","£","¥","₧","ƒ",
		"á","í","ó","ú","ñ","Ñ","ª","º","¿","⌐","¬","½","¼","¡","«","»",
		"░","▒","▓","│","┤","╡","╢","╖","╕","╣","║","╗","╝","╜","╛","┐",
		"└","┴","┬","├","─","┼","╞","╟","╚","╔","╩","╦","╠","═","╬","╧",
		"╨","╤","╥","╙","╘","╒","╓","╫","╪","┘","┌","█","▄","▌","▐","▀",
		"α","ß","Γ","π","Σ","σ","µ","τ","Φ","Θ","Ω","δ","∞","φ","ε","∩",
		"≡","±","≥","≤","⌠","⌡","÷","≈","°","∙","·","√","ⁿ","²","■"," "
	};

	if (c >= 128)
		return high[c - 128];
	ascii[0] = (c >= 32 && c < 127) ? c : ' ';
	return ascii;
}

void SYS_TextScreen (const byte *screen, int rows, const char *text, int col, int row)
{
	static const int	ansi[8] = {0,4,2,6,1,5,3,7};	// CGA to ANSI order
	byte	page[25][80][2];
	int		color = isatty (STDOUT_FILENO);
	int		x,y,last;

	if (rows > 25)
		rows = 25;
	memcpy (page,screen,rows*160);

	for (; text && *text && row < rows; text++)
	{
		if (*text == '\n' || col >= 80)
		{
			row++;
			col = 0;
			if (*text == '\n')
				continue;
		}
		page[row][col++][0] = *text;
	}

	for (y = 0; y < rows; y++)
	{
		// Trailing blanks on the same background are not worth printing
		for (last = 79; last >= 0; last--)
			if (page[y][last][0] != ' ' && page[y][last][0] != 0)
				break;
		if (color)
			last = 79;

		for (x = 0; x <= last; x++)
		{
			byte	attr = page[y][x][1];

			if (color)
				printf ("\033[%d;%d%sm",30 + ansi[attr & 7],40 + ansi[(attr >> 4) & 7],
					attr & 8 ? ";1" : ";22");
			fputs (CP437 (page[y][x][0]),stdout);
		}
		if (color)
			printf ("\033[0m");
		putchar ('\n');
	}
	fflush (stdout);
}
