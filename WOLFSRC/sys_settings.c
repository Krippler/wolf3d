// sys_settings.c
//
// The port's own settings: what the DOS game never had to ask about, because
// the monitor, the sound card's mixer and the shape of the window were
// somebody else's business. They are changed on the Settings pages (WL_MENU.C,
// from the Control menu or F12), take effect at once, and are kept in a text
// file named "settings" in the directory the game runs in -- the state
// directory, for the container and the desktop launcher -- next to the
// game's own CONFIG and savegames, which they stay out of the way of.
//
// Until a setting has been changed in the game, it comes from the
// environment: WOLF_ASPECT, WOLF_SCALE and WOLF_NOGRAB, as before.
//

#include "id_port.h"

#define SETTINGSFILE	"settings"
#define GAMEFILE		"game"

int	sys_setting[SET_COUNT];

static const struct
{
	const char	*key;
	int			min,max;
} setinfo[SET_COUNT] =
{
	{"shape",		0,1},
	{"scale",		1,6},
	{"music",		0,10},
	{"effects",		0,10},
	{"mousewalks",	0,1},
	{"alwaysrun",	0,1},
	{"vibration",	0,1},
	{"capture",		0,1},
};

static int	loaded;

static void Defaults (void)
{
	const char	*env;

	sys_setting[set_shape] = !((env = getenv ("WOLF_ASPECT")) && !strcmp (env,"square"));
	sys_setting[set_scale] = 2;
	if ((env = getenv ("WOLF_SCALE")) && atoi (env) > 0)
		sys_setting[set_scale] = atoi (env);
	sys_setting[set_music] = 10;
	sys_setting[set_effects] = 10;
	sys_setting[set_mousewalks] = 1;
	sys_setting[set_alwaysrun] = 0;
	sys_setting[set_vibration] = 1;
	sys_setting[set_capture] = !getenv ("WOLF_NOGRAB");
}

static void Clamp (int i)
{
	if (sys_setting[i] < setinfo[i].min)
		sys_setting[i] = setinfo[i].min;
	if (sys_setting[i] > setinfo[i].max)
		sys_setting[i] = setinfo[i].max;
}

void SYS_LoadSettings (void)
{
	FILE	*f;
	char	line[128],key[64];
	int		value,i;

	if (loaded)
		return;
	loaded = 1;

	Defaults ();

	// A line it does not understand -- a setting from a later version, say --
	// is passed over rather than refused
	if ((f = fopen (SETTINGSFILE,"r")))
	{
		while (fgets (line,sizeof(line),f))
		{
			if (sscanf (line,"%63s %d",key,&value) != 2)
				continue;
			for (i = 0; i < SET_COUNT; i++)
				if (!strcmp (key,setinfo[i].key))
					sys_setting[i] = value;
		}
		fclose (f);
	}

	for (i = 0; i < SET_COUNT; i++)
		Clamp (i);
}

void SYS_SaveSettings (void)
{
	FILE	*f;
	int		i;

	if (!(f = fopen (SETTINGSFILE ".new","w")))
		return;
	fprintf (f,"# Wolfenstein 3-D's settings, as the Settings pages in the game left them\n");
	for (i = 0; i < SET_COUNT; i++)
		fprintf (f,"%s %d\n",setinfo[i].key,sys_setting[i]);
	if (fclose (f) == 0)
		rename (SETTINGSFILE ".new",SETTINGSFILE);
	else
		remove (SETTINGSFILE ".new");
}

//
// Change one setting by a step, within its range, and make it so.
// Returns whether it changed.
//
int SYS_StepSetting (int which, int step)
{
	int	old = sys_setting[which];

	sys_setting[which] += step;
	Clamp (which);
	if (sys_setting[which] == old)
		return 0;

	if (which == set_shape || which == set_scale)
	{
		SYS_Resize ();
		SYS_PadSayAspect ();
	}
	return 1;
}

//
// The browser stretches the picture; on a desktop the window is made the
// size of it, and the size there is the player's to choose.
//
int SYS_InBrowser (void)
{
	return getenv ("WOLF_PAGE_ASPECT") != NULL;
}


/*
=============================================================================

						WHICH GAME

	The launchers -- docker/entrypoint.sh and desktop/wolf3d -- find every
	game whose data is there and say so in WOLF_GAMES ("wl6 sod wl1"), and
	which one this engine is playing in WOLF_GAME_NOW. Choosing another
	writes its name to the file "game" and ends the engine with status 3;
	the launcher starts the engine for that one.

=============================================================================
*/

static char		games[16][8];
static int		numgames = -1;

static void ListGames (void)
{
	const char	*env = getenv ("WOLF_GAMES");
	char		name[8];
	int			n;

	numgames = 0;
	while (env && numgames < 16 && sscanf (env," %7s%n",name,&n) == 1)
	{
		strcpy (games[numgames++],name);
		env += n;
	}
}

int SYS_NumGames (void)
{
	if (numgames < 0)
		ListGames ();
	return numgames;
}

const char *SYS_GameKind (int i)
{
	if (numgames < 0)
		ListGames ();
	return i >= 0 && i < numgames ? games[i] : "";
}

const char *SYS_GameTitle (const char *kind)
{
	static const char	*titles[][2] =
	{
		{"wl6","Wolfenstein 3-D"},
		{"wl3","Wolfenstein 3-D"},
		{"wl1","Shareware episode"},
		{"sod","Spear of Destiny"},
		{"sd2","Return to Danger"},
		{"sd3","Ultimate Challenge"},
		{"sdm","Spear demo"},
	};
	unsigned	i;

	for (i = 0; i < sizeof(titles)/sizeof(titles[0]); i++)
		if (!strcmp (kind,titles[i][0]))
			return titles[i][1];
	return kind;
}

int SYS_IsGameNow (const char *kind)
{
	const char	*now = getenv ("WOLF_GAME_NOW");

	return now && !strcmp (now,kind);
}

//
// Remember the choice for the launcher. Returns 0 if it cannot be written.
//
int SYS_ChooseGame (const char *kind)
{
	FILE	*f = fopen (GAMEFILE,"w");

	if (!f)
		return 0;
	fprintf (f,"%s\n",kind);
	return fclose (f) == 0;
}
