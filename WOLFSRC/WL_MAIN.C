// WL_MAIN.C

#include "WL_DEF.H"
#pragma hdrstop


/*
=============================================================================

						   WOLFENSTEIN 3-D

					  An Id Software production

						   by John Carmack

=============================================================================
*/

/*
=============================================================================

						 LOCAL CONSTANTS

=============================================================================
*/


#define FOCALLENGTH     (0x5700l)               // in global coordinates
#define VIEWGLOBAL      0x10000                 // globals visable flush to wall

#define VIEWWIDTH       256                     // size of view window
#define VIEWHEIGHT      144

/*
=============================================================================

						 GLOBAL VARIABLES

=============================================================================
*/

char            str[80],str2[20];
int				tedlevelnum;
boolean         tedlevel;
boolean         nospr;
boolean         IsA386;
int                     dirangle[9] = {0,ANGLES/8,2*ANGLES/8,3*ANGLES/8,4*ANGLES/8,
	5*ANGLES/8,6*ANGLES/8,7*ANGLES/8,ANGLES};

//
// proejection variables
//
fixed           focallength;
unsigned        screenofs;
int             viewwidth;
int             viewheight;
int             centerx;
int             shootdelta;                     // pixels away from centerx a target can be
fixed           scale,maxslope;
int32_t            heightnumerator;
int                     minheightdiv;


void            Quit (char *error);

boolean         startgame,loadedgame,virtualreality;
int             mouseadjustment;

char	configname[13]="CONFIG.";


/*
=============================================================================

						 LOCAL VARIABLES

=============================================================================
*/


/*
====================
=
= ReadConfig
=
====================
*/

void ReadConfig(void)
{
	int                     file;
	SDMode          sd;
	SMMode          sm;
	SDSMode         sds;


	if ( (file = open(configname,O_BINARY | O_RDONLY)) != -1)
	{
	//
	// valid config file
	//
		read(file,Scores,sizeof(HighScore) * MaxScores);

		read(file,&sd,sizeof(sd));
		read(file,&sm,sizeof(sm));
		read(file,&sds,sizeof(sds));

		read(file,&mouseenabled,sizeof(mouseenabled));
		read(file,&joystickenabled,sizeof(joystickenabled));
		read(file,&joypadenabled,sizeof(joypadenabled));
		read(file,&joystickprogressive,sizeof(joystickprogressive));
		read(file,&joystickport,sizeof(joystickport));

		read(file,&dirscan,sizeof(dirscan));
		read(file,&buttonscan,sizeof(buttonscan));
		read(file,&buttonmouse,sizeof(buttonmouse));
		read(file,&buttonjoy,sizeof(buttonjoy));

		read(file,&viewsize,sizeof(viewsize));
		read(file,&mouseadjustment,sizeof(mouseadjustment));

		close(file);

		if (sd == sdm_AdLib && !AdLibPresent && !SoundBlasterPresent)
		{
			sd = sdm_PC;
			sd = smm_Off;
		}

		if ((sds == sds_SoundBlaster && !SoundBlasterPresent) ||
			(sds == sds_SoundSource && !SoundSourcePresent))
			sds = sds_Off;

		if (!MousePresent)
			mouseenabled = false;
		if (!JoysPresent[joystickport])
			joystickenabled = false;

		MainMenu[6].active=1;
		MainItems.curpos=0;
	}
	else
	{
	//
	// no config file, so select by hardware
	//
		if (SoundBlasterPresent || AdLibPresent)
		{
			sd = sdm_AdLib;
			sm = smm_AdLib;
		}
		else
		{
			sd = sdm_PC;
			sm = smm_Off;
		}

		if (SoundBlasterPresent)
			sds = sds_SoundBlaster;
		else if (SoundSourcePresent)
			sds = sds_SoundSource;
		else
			sds = sds_Off;

		if (MousePresent)
			mouseenabled = true;

		joystickenabled = false;
		joypadenabled = false;
		joystickport = 0;
		joystickprogressive = false;

		viewsize = 15;
		mouseadjustment=5;
	}

	SD_SetMusicMode (sm);
	SD_SetSoundMode (sd);
	SD_SetDigiDevice (sds);

}


/*
====================
=
= WriteConfig
=
====================
*/

void WriteConfig(void)
{
	int                     file;

	file = open(configname,O_CREAT | O_BINARY | O_WRONLY,
				S_IREAD | S_IWRITE | S_IFREG);

	if (file != -1)
	{
		write(file,Scores,sizeof(HighScore) * MaxScores);

		write(file,&SoundMode,sizeof(SoundMode));
		write(file,&MusicMode,sizeof(MusicMode));
		write(file,&DigiMode,sizeof(DigiMode));

		write(file,&mouseenabled,sizeof(mouseenabled));
		write(file,&joystickenabled,sizeof(joystickenabled));
		write(file,&joypadenabled,sizeof(joypadenabled));
		write(file,&joystickprogressive,sizeof(joystickprogressive));
		write(file,&joystickport,sizeof(joystickport));

		write(file,&dirscan,sizeof(dirscan));
		write(file,&buttonscan,sizeof(buttonscan));
		write(file,&buttonmouse,sizeof(buttonmouse));
		write(file,&buttonjoy,sizeof(buttonjoy));

		write(file,&viewsize,sizeof(viewsize));
		write(file,&mouseadjustment,sizeof(mouseadjustment));

		close(file);
	}
}


//===========================================================================


/*
========================
=
= Patch386
=
= Patch ldiv to use 32 bit instructions
=
========================
*/

void Patch386 (void)
{
	//
	// On a 386 this patched Borland's 32-bit division routine, in place,
	// to use the 386's own 32-bit instructions. The division is the
	// compiler's own here, and the processor is never a 286.
	//
	IsA386 = true;
}

//===========================================================================

/*
=====================
=
= NewGame
=
= Set up new game to start from the beginning
=
=====================
*/

void NewGame (int difficulty,int episode)
{
	memset (&gamestate,0,sizeof(gamestate));
	gamestate.difficulty = difficulty;
	gamestate.weapon = gamestate.bestweapon
		= gamestate.chosenweapon = wp_pistol;
	gamestate.health = 100;
	gamestate.ammo = STARTAMMO;
	gamestate.lives = 3;
	gamestate.nextextra = EXTRAPOINTS;
	gamestate.episode=episode;

	startgame = true;
}

//===========================================================================

void DiskFlopAnim(int x,int y)
{
 static char which=0;
 if (!x && !y)
   return;
 VWB_DrawPic(x,y,C_DISKLOADING1PIC+which);
 VW_UpdateScreen();
 which^=1;
}


int32_t DoChecksum(byte far *source,unsigned size,int32_t checksum)
{
 unsigned i;

 for (i=0;i<size-1;i++)
   checksum += source[i]^source[i+1];

 return checksum;
}


/*
=============================================================================

							SAVEGAMES

	The original wrote the game's structures to the file as they lay in
	memory, pointers and all. A near pointer was an offset into the data
	segment, the same every time that EXE ran, so that worked. A pointer
	here is 64 bits and moves from one build of the engine to the next, so
	every pointer is written as what it points at instead:

	  - an actor's state as its place in allstates[], the table of every
		state the build has, sorted by name (tools/mkstates.sh);
	  - actorat[][], which holds actors, door numbers and plain wall
		values, as a 16-bit word: the value itself below 256, an actor's
		index in objlist with the top bit set;
	  - laststatobj as an index, and each static's visspot as an offset
		into spotvis.

	Actors and statics are written field by field in fixed sizes. The rest
	-- gamestate, the level ratios, the tile map, doors and pushwall -- has
	no pointers in it and goes as it is, as it did.

	A header ahead of it all says what wrote it, so a savegame from DOS or
	from a build whose states differ is turned away rather than read as
	nonsense.

=============================================================================
*/

extern	statetype	*allstates[];
extern	int			numstates;

#define SAVEMAGIC	"WOLFLNX1"

typedef struct
{
	char		magic[8];
	int32_t		numstates;
	int32_t		objsize;			// sizeof(savedobj_t), in case it changes
} saveheader_t;

typedef struct
{
	int32_t		active,ticcount,obclass,state;
	int32_t		flags,distance,dir;
	int32_t		x,y,tilex,tiley,areanumber;
	int32_t		viewx,viewheight,transx,transy;
	int32_t		angle,hitpoints,speed;
	int32_t		temp1,temp2,temp3;
} savedobj_t;

typedef struct
{
	int16_t		tilex,tiley;
	int16_t		visspot;			// offset into spotvis
	int16_t		shapenum;
	int16_t		flags,itemnumber;
} savedstat_t;

static int StateIndex (statetype *state)
{
	int	i;

	for (i=0;i<numstates;i++)
		if (allstates[i] == state)
			return i;
	return -1;
}

static void SaveObj (objtype *ob, savedobj_t *s)
{
	memset (s,0,sizeof(*s));
	s->active = ob->active;
	s->ticcount = ob->ticcount;
	s->obclass = ob->obclass;
	s->state = ob->state ? StateIndex (ob->state) : -1;
	s->flags = ob->flags;
	s->distance = ob->distance;
	s->dir = ob->dir;
	s->x = ob->x;
	s->y = ob->y;
	s->tilex = ob->tilex;
	s->tiley = ob->tiley;
	s->areanumber = ob->areanumber;
	s->viewx = ob->viewx;
	s->viewheight = ob->viewheight;
	s->transx = ob->transx;
	s->transy = ob->transy;
	s->angle = ob->angle;
	s->hitpoints = ob->hitpoints;
	s->speed = ob->speed;
	s->temp1 = ob->temp1;
	s->temp2 = ob->temp2;
	s->temp3 = ob->temp3;
}

// Into an actor already linked into the list: the links are left alone
static void LoadObj (savedobj_t *s, objtype *ob)
{
	ob->active = s->active;
	ob->ticcount = s->ticcount;
	ob->obclass = s->obclass;
	ob->state = (s->state >= 0 && s->state < numstates) ? allstates[s->state] : NULL;
	ob->flags = s->flags;
	ob->distance = s->distance;
	ob->dir = s->dir;
	ob->x = s->x;
	ob->y = s->y;
	ob->tilex = s->tilex;
	ob->tiley = s->tiley;
	ob->areanumber = s->areanumber;
	ob->viewx = s->viewx;
	ob->viewheight = s->viewheight;
	ob->transx = s->transx;
	ob->transy = s->transy;
	ob->angle = s->angle;
	ob->hitpoints = s->hitpoints;
	ob->speed = s->speed;
	ob->temp1 = s->temp1;
	ob->temp2 = s->temp2;
	ob->temp3 = s->temp3;
}

static void SaveActorAt (word *out)
{
	int			i;
	uintptr_t	v;

	for (i=0;i<MAPSIZE*MAPSIZE;i++)
	{
		v = (uintptr_t)(&actorat[0][0])[i];
		if (v < 256)
			out[i] = v;
		else
			out[i] = 0x8000 | ((objtype *)v - objlist);
	}
}

static void LoadActorAt (word *in)
{
	int		i;

	for (i=0;i<MAPSIZE*MAPSIZE;i++)
	{
		if (in[i] & 0x8000)
			(&actorat[0][0])[i] = (in[i] & 0x7fff) < MAXACTORS ? &objlist[in[i] & 0x7fff] : NULL;
		else
			(&actorat[0][0])[i] = (objtype *)(uintptr_t)in[i];
	}
}

static void SaveStats (savedstat_t *out)
{
	int		i;

	for (i=0;i<MAXSTATS;i++)
	{
		statobj_t	*st = &statobjlist[i];

		out[i].tilex = st->tilex;
		out[i].tiley = st->tiley;
		out[i].visspot = st->visspot ? st->visspot - &spotvis[0][0] : -1;
		out[i].shapenum = st->shapenum;
		out[i].flags = st->flags;
		out[i].itemnumber = st->itemnumber;
	}
}

static void LoadStats (savedstat_t *in)
{
	int		i;

	for (i=0;i<MAXSTATS;i++)
	{
		statobj_t	*st = &statobjlist[i];

		st->tilex = in[i].tilex;
		st->tiley = in[i].tiley;
		st->visspot = (in[i].visspot >= 0 && in[i].visspot < MAPSIZE*MAPSIZE)
			? &spotvis[0][0] + in[i].visspot : &spotvis[0][0];
		st->shapenum = in[i].shapenum;
		st->flags = in[i].flags;
		st->itemnumber = in[i].itemnumber;
	}
}

//
// Both the writing and the checksum, which the original did in pairs for
// every block
//
static int32_t SaveBlock (int file, void *data, unsigned size, int32_t checksum)
{
	CA_FarWrite (file,(byte far *)data,size);
	return DoChecksum ((byte far *)data,size,checksum);
}

static int32_t LoadBlock (int file, void *data, unsigned size, int32_t checksum)
{
	CA_FarRead (file,(byte far *)data,size);
	return DoChecksum ((byte far *)data,size,checksum);
}

/*
==================
=
= SaveTheGame
=
==================
*/

boolean SaveTheGame(int file,int x,int y)
{
	int32_t			avail,size,checksum;
	objtype			*ob;
	saveheader_t	header;
	savedobj_t		sob;
	static word		savedactorat[MAPSIZE*MAPSIZE];
	static savedstat_t	savedstats[MAXSTATS];
	int32_t			laststat;
	int				count;

	avail = SYS_DiskFree ();

	count = 0;
	for (ob = player; ob ; ob=ob->next)
	  count++;

	size = sizeof(header) + sizeof(gamestate) +
			sizeof(LRstruct)*8 +
			sizeof(tilemap) +
			sizeof(savedactorat) +
			sizeof(areaconnect) +
			sizeof(areabyplayer) +
			(count+1) * sizeof(sob) +
			sizeof(laststat) +
			sizeof(savedstats) +
			sizeof(doorposition) +
			sizeof(doorobjlist) +
			sizeof(pwallstate) +
			sizeof(pwallx) +
			sizeof(pwally) +
			sizeof(pwalldir) +
			sizeof(pwallpos) +
			sizeof(checksum);

	if (avail < size)
	{
	 Message(STR_NOSPACE1"\n"
			 STR_NOSPACE2);
	 return false;
	}

	checksum = 0;

	memcpy (header.magic,SAVEMAGIC,8);
	header.numstates = numstates;
	header.objsize = sizeof(savedobj_t);
	CA_FarWrite (file,(void far *)&header,sizeof(header));

	DiskFlopAnim(x,y);
	checksum = SaveBlock (file,&gamestate,sizeof(gamestate),checksum);

	DiskFlopAnim(x,y);
#ifdef SPEAR
	checksum = SaveBlock (file,&LevelRatios[0],sizeof(LRstruct)*20,checksum);
#else
	checksum = SaveBlock (file,&LevelRatios[0],sizeof(LRstruct)*8,checksum);
#endif

	DiskFlopAnim(x,y);
	checksum = SaveBlock (file,tilemap,sizeof(tilemap),checksum);
	DiskFlopAnim(x,y);
	SaveActorAt (savedactorat);
	checksum = SaveBlock (file,savedactorat,sizeof(savedactorat),checksum);

	CA_FarWrite (file,(void far *)areaconnect,sizeof(areaconnect));
	CA_FarWrite (file,(void far *)areabyplayer,sizeof(areabyplayer));

	for (ob = player ; ob ; ob=ob->next)
	{
	 DiskFlopAnim(x,y);
	 SaveObj (ob,&sob);
	 CA_FarWrite (file,(void far *)&sob,sizeof(sob));
	}
	memset (&sob,0,sizeof(sob));
	sob.active = ac_badobject;          // end of file marker
	DiskFlopAnim(x,y);
	CA_FarWrite (file,(void far *)&sob,sizeof(sob));

	DiskFlopAnim(x,y);
	laststat = laststatobj - statobjlist;
	checksum = SaveBlock (file,&laststat,sizeof(laststat),checksum);
	DiskFlopAnim(x,y);
	SaveStats (savedstats);
	checksum = SaveBlock (file,savedstats,sizeof(savedstats),checksum);

	DiskFlopAnim(x,y);
	checksum = SaveBlock (file,doorposition,sizeof(doorposition),checksum);
	DiskFlopAnim(x,y);
	checksum = SaveBlock (file,doorobjlist,sizeof(doorobjlist),checksum);

	DiskFlopAnim(x,y);
	checksum = SaveBlock (file,&pwallstate,sizeof(pwallstate),checksum);
	checksum = SaveBlock (file,&pwallx,sizeof(pwallx),checksum);
	checksum = SaveBlock (file,&pwally,sizeof(pwally),checksum);
	checksum = SaveBlock (file,&pwalldir,sizeof(pwalldir),checksum);
	checksum = SaveBlock (file,&pwallpos,sizeof(pwallpos),checksum);

	//
	// WRITE OUT CHECKSUM
	//
	CA_FarWrite (file,(void far *)&checksum,sizeof(checksum));

	return(true);
}

//===========================================================================

/*
==================
=
= LoadTheGame
=
==================
*/

boolean LoadTheGame(int file,int x,int y)
{
	int32_t			checksum,oldchecksum;
	objtype			*ob;
	saveheader_t	header;
	savedobj_t		sob;
	static word		savedactorat[MAPSIZE*MAPSIZE];
	static savedstat_t	savedstats[MAXSTATS];
	int32_t			laststat;

	//
	// A savegame this engine did not write: from DOS, whose layout was
	// nothing like this one, or from a build with a different set of
	// states. Say so, and start a new game rather than read it.
	//
	if (!CA_FarRead (file,(void far *)&header,sizeof(header))
	|| memcmp (header.magic,SAVEMAGIC,8)
	|| header.numstates != numstates
	|| header.objsize != sizeof(savedobj_t))
	{
	 Message("This game was saved by\n"
			 "another version, and\n"
			 "cannot be loaded here.");
	 IN_ClearKeysDown();
	 IN_Ack();

	 NewGame (gamestate.difficulty,0);
	 SetupGameLevel ();
	 return false;
	}

	checksum = 0;

	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,&gamestate,sizeof(gamestate),checksum);

	DiskFlopAnim(x,y);
#ifdef SPEAR
	checksum = LoadBlock (file,&LevelRatios[0],sizeof(LRstruct)*20,checksum);
#else
	checksum = LoadBlock (file,&LevelRatios[0],sizeof(LRstruct)*8,checksum);
#endif

	DiskFlopAnim(x,y);
	SetupGameLevel ();

	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,tilemap,sizeof(tilemap),checksum);
	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,savedactorat,sizeof(savedactorat),checksum);

	CA_FarRead (file,(void far *)areaconnect,sizeof(areaconnect));
	CA_FarRead (file,(void far *)areabyplayer,sizeof(areabyplayer));

	InitActorList ();
	DiskFlopAnim(x,y);
	CA_FarRead (file,(void far *)&sob,sizeof(sob));
	LoadObj (&sob,player);

	while (1)
	{
	 DiskFlopAnim(x,y);
		if (!CA_FarRead (file,(void far *)&sob,sizeof(sob)))
			break;
		if (sob.active == ac_badobject)
			break;
		GetNewActor ();
	 // don't copy over the links
		LoadObj (&sob,new);
	}

	// Only now, with the actors where the indexes say
	LoadActorAt (savedactorat);

	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,&laststat,sizeof(laststat),checksum);
	if (laststat < 0 || laststat > MAXSTATS)
		laststat = 0;
	laststatobj = &statobjlist[laststat];
	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,savedstats,sizeof(savedstats),checksum);
	LoadStats (savedstats);

	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,doorposition,sizeof(doorposition),checksum);
	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,doorobjlist,sizeof(doorobjlist),checksum);

	DiskFlopAnim(x,y);
	checksum = LoadBlock (file,&pwallstate,sizeof(pwallstate),checksum);
	checksum = LoadBlock (file,&pwallx,sizeof(pwallx),checksum);
	checksum = LoadBlock (file,&pwally,sizeof(pwally),checksum);
	checksum = LoadBlock (file,&pwalldir,sizeof(pwalldir),checksum);
	checksum = LoadBlock (file,&pwallpos,sizeof(pwallpos),checksum);

	CA_FarRead (file,(void far *)&oldchecksum,sizeof(oldchecksum));

	if (oldchecksum != checksum)
	{
	 Message(STR_SAVECHT1"\n"
			 STR_SAVECHT2"\n"
			 STR_SAVECHT3"\n"
			 STR_SAVECHT4);

	 IN_ClearKeysDown();
	 IN_Ack();

	 gamestate.score = 0;
	 gamestate.lives = 1;
	 gamestate.weapon =
	   gamestate.chosenweapon =
	   gamestate.bestweapon = wp_pistol;
	 gamestate.ammo = 8;
	}

	return true;
}

//===========================================================================

/*
==========================
=
= ShutdownId
=
= Shuts down all ID_?? managers
=
==========================
*/

void ShutdownId (void)
{
	US_Shutdown ();
	SD_Shutdown ();
	PM_Shutdown ();
	IN_Shutdown ();
	VW_Shutdown ();
	CA_Shutdown ();
	MM_Shutdown ();
}


//===========================================================================

/*
==================
=
= BuildTables
=
= Calculates:
=
= scale                 projection constant
= sintable/costable     overlapping fractional tables
=
==================
*/

const   float   radtoint = (float)FINEANGLES/2/PI;

void BuildTables (void)
{
  int           i;
  float         angle,anglestep;
  double        tang;
  fixed         value;


//
// calculate fine tangents
//

	for (i=0;i<FINEANGLES/8;i++)
	{
		tang = tan( (i+0.5)/radtoint);
		finetangent[i] = tang*TILEGLOBAL;
		finetangent[FINEANGLES/4-1-i] = 1/tang*TILEGLOBAL;
	}

//
// costable overlays sintable with a quarter phase shift
// ANGLES is assumed to be divisable by four
//
// The low word of the value is the fraction, the high bit is the sign bit,
// bits 16-30 should be 0
//

  angle = 0;
  anglestep = PI/2/ANGLEQUAD;
  for (i=0;i<=ANGLEQUAD;i++)
  {
	value=GLOBAL1*sin(angle);
	sintable[i]=
	  sintable[i+ANGLES]=
	  sintable[ANGLES/2-i] = value;
	sintable[ANGLES-i]=
	  sintable[ANGLES/2+i] = value | 0x80000000l;
	angle += anglestep;
  }

}

//===========================================================================


/*
====================
=
= CalcProjection
=
= Uses focallength
=
====================
*/

void CalcProjection (int32_t focal)
{
	int             i;
	int32_t            intang;
	float   angle;
	double  tang;
	double  planedist;
	double  globinhalf;
	int             halfview;
	double  halfangle,facedist;


	focallength = focal;
	facedist = focal+MINDIST;
	halfview = viewwidth/2;                                 // half view in pixels

//
// calculate scale value for vertical height calculations
// and sprite x calculations
//
	scale = halfview*facedist/(VIEWGLOBAL/2);

//
// divide heightnumerator by a posts distance to get the posts height for
// the heightbuffer.  The pixel height is height>>2
//
	heightnumerator = (TILEGLOBAL*scale)>>6;
	minheightdiv = heightnumerator/0x7fff +1;

//
// calculate the angle offset from view angle of each pixel's ray
//

	for (i=0;i<halfview;i++)
	{
	// start 1/2 pixel over, so viewangle bisects two middle pixels
		tang = (int32_t)i*VIEWGLOBAL/viewwidth/facedist;
		angle = atan(tang);
		intang = angle*radtoint;
		pixelangle[halfview-1-i] = intang;
		pixelangle[halfview+i] = -intang;
	}

//
// if a point's abs(y/x) is greater than maxslope, the point is outside
// the view area
//
	maxslope = finetangent[pixelangle[0]];
	maxslope >>= 8;
}



//===========================================================================

/*
===================
=
= SetupWalls
=
= Map tile values to scaled pics
=
===================
*/

void SetupWalls (void)
{
	int     i;

	for (i=1;i<MAXWALLTILES;i++)
	{
		horizwall[i]=(i-1)*2;
		vertwall[i]=(i-1)*2+1;
	}
}

//===========================================================================

/*
==========================
=
= SignonScreen
=
==========================
*/

void SignonScreen (void)                        // VGA version
{
	VL_SetVGAPlaneMode ();
	VL_TestPaletteSet ();
	VL_SetPalette (gamepal);

	if (!virtualreality)
	{
		VW_SetScreen(0x8000,0);
		VL_MungePic (introscn,320,200);
		VL_MemToScreen (introscn,320,200,0,0);
		VW_SetScreen(0,0);
	}

//
// The original gave the memory the signon screen was linked into back to
// the memory manager here. There is no shortage of memory to reclaim.
//
}


/*
==========================
=
= FinishSignon
=
==========================
*/

void FinishSignon (void)
{

#ifndef SPEAR
	VW_Bar (0,189,300,11,VGA_Read(0));
	WindowX = 0;
	WindowW = 320;
	PrintY = 190;

	#ifndef JAPAN
	SETFONTCOLOR(14,4);

	#ifdef SPANISH
	US_CPrint ("Oprima una tecla");
	#else
	US_CPrint ("Press a key");
	#endif

	#endif

	if (!NoWait)
		IN_Ack ();

	#ifndef JAPAN
	VW_Bar (0,189,300,11,VGA_Read(0));

	PrintY = 190;
	SETFONTCOLOR(10,4);

	#ifdef SPANISH
	US_CPrint ("pensando...");
	#else
	US_CPrint ("Working...");
	#endif

	#endif

	SETFONTCOLOR(0,15);
#else
	if (!NoWait)
		VW_WaitVBL(3*70);
#endif
}

//===========================================================================

/*
=================
=
= MS_CheckParm
=
=================
*/

boolean MS_CheckParm (char far *check)
{
	int             i;
	char    *parm;

	for (i = 1;i<_argc;i++)
	{
		parm = _argv[i];

		while ( !isalpha(*parm) )       // skip - / \ etc.. in front of parm
			if (!*parm++)
				break;                          // hit end of string without an alphanum

		if ( !_fstricmp(check,parm) )
			return true;
	}

	return false;
}

//===========================================================================

/*
=====================
=
= InitDigiMap
=
=====================
*/

static  int     wolfdigimap[] =
		{
			// These first sounds are in the upload version
#ifndef SPEAR
			HALTSND,                0,
			DOGBARKSND,             1,
			CLOSEDOORSND,           2,
			OPENDOORSND,            3,
			ATKMACHINEGUNSND,       4,
			ATKPISTOLSND,           5,
			ATKGATLINGSND,          6,
			SCHUTZADSND,            7,
			GUTENTAGSND,            8,
			MUTTISND,               9,
			BOSSFIRESND,            10,
			SSFIRESND,              11,
			DEATHSCREAM1SND,        12,
			DEATHSCREAM2SND,        13,
			DEATHSCREAM3SND,        13,
			TAKEDAMAGESND,          14,
			PUSHWALLSND,            15,

			LEBENSND,               20,
			NAZIFIRESND,            21,
			SLURPIESND,             22,

			YEAHSND,				32,

#ifndef UPLOAD
			// These are in all other episodes
			DOGDEATHSND,            16,
			AHHHGSND,               17,
			DIESND,                 18,
			EVASND,                 19,

			TOT_HUNDSND,            23,
			MEINGOTTSND,            24,
			SCHABBSHASND,           25,
			HITLERHASND,            26,
			SPIONSND,               27,
			NEINSOVASSND,           28,
			DOGATTACKSND,           29,
			LEVELDONESND,           30,
			MECHSTEPSND,			31,

			SCHEISTSND,				33,
			DEATHSCREAM4SND,		34,		// AIIEEE
			DEATHSCREAM5SND,		35,		// DEE-DEE
			DONNERSND,				36,		// EPISODE 4 BOSS DIE
			EINESND,				37,		// EPISODE 4 BOSS SIGHTING
			ERLAUBENSND,			38,		// EPISODE 6 BOSS SIGHTING
			DEATHSCREAM6SND,		39,		// FART
			DEATHSCREAM7SND,		40,		// GASP
			DEATHSCREAM8SND,		41,		// GUH-BOY!
			DEATHSCREAM9SND,		42,		// AH GEEZ!
			KEINSND,				43,		// EPISODE 5 BOSS SIGHTING
			MEINSND,				44,		// EPISODE 6 BOSS DIE
			ROSESND,				45,		// EPISODE 5 BOSS DIE

#endif
#else
//
// SPEAR OF DESTINY DIGISOUNDS
//
			HALTSND,                0,
			CLOSEDOORSND,           2,
			OPENDOORSND,            3,
			ATKMACHINEGUNSND,       4,
			ATKPISTOLSND,           5,
			ATKGATLINGSND,          6,
			SCHUTZADSND,            7,
			BOSSFIRESND,            8,
			SSFIRESND,              9,
			DEATHSCREAM1SND,        10,
			DEATHSCREAM2SND,        11,
			TAKEDAMAGESND,          12,
			PUSHWALLSND,            13,
			AHHHGSND,               15,
			LEBENSND,               16,
			NAZIFIRESND,            17,
			SLURPIESND,             18,
			LEVELDONESND,           22,
			DEATHSCREAM4SND,		23,		// AIIEEE
			DEATHSCREAM3SND,        23,		// DOUBLY-MAPPED!!!
			DEATHSCREAM5SND,		24,		// DEE-DEE
			DEATHSCREAM6SND,		25,		// FART
			DEATHSCREAM7SND,		26,		// GASP
			DEATHSCREAM8SND,		27,		// GUH-BOY!
			DEATHSCREAM9SND,		28,		// AH GEEZ!
			GETGATLINGSND,			38,		// Got Gat replacement

#ifndef SPEARDEMO
			DOGBARKSND,             1,
			DOGDEATHSND,            14,
			SPIONSND,               19,
			NEINSOVASSND,           20,
			DOGATTACKSND,           21,
			TRANSSIGHTSND,			29,		// Trans Sight
			TRANSDEATHSND,			30,		// Trans Death
			WILHELMSIGHTSND,		31,		// Wilhelm Sight
			WILHELMDEATHSND,		32,		// Wilhelm Death
			UBERDEATHSND,			33,		// Uber Death
			KNIGHTSIGHTSND,			34,		// Death Knight Sight
			KNIGHTDEATHSND,			35,		// Death Knight Death
			ANGELSIGHTSND,			36,		// Angel Sight
			ANGELDEATHSND,			37,		// Angel Death
			GETSPEARSND,			39,		// Got Spear replacement
#endif
#endif
			LASTSOUND
		};


void InitDigiMap (void)
{
	int                     *map;

	for (map = wolfdigimap;*map != LASTSOUND;map += 2)
		DigiMap[map[0]] = map[1];


}


#ifndef SPEAR
CP_iteminfo	MusicItems={CTL_X,CTL_Y,6,0,32};
CP_itemtype far MusicMenu[]=
	{
		{1,"Get Them!",0},
		{1,"Searching",0},
		{1,"P.O.W.",0},
		{1,"Suspense",0},
		{1,"War March",0},
		{1,"Around The Corner!",0},

		{1,"Nazi Anthem",0},
		{1,"Lurking...",0},
		{1,"Going After Hitler",0},
		{1,"Pounding Headache",0},
		{1,"Into the Dungeons",0},
		{1,"Ultimate Conquest",0},

		{1,"Kill the S.O.B.",0},
		{1,"The Nazi Rap",0},
		{1,"Twelfth Hour",0},
		{1,"Zero Hour",0},
		{1,"Ultimate Conquest",0},
		{1,"Wolfpack",0}
	};
#else
CP_iteminfo MusicItems={CTL_X,CTL_Y-20,9,0,32};
CP_itemtype far MusicMenu[]=
   {
		{1,"Funky Colonel Bill",0},
		{1,"Death To The Nazis",0},
		{1,"Tiptoeing Around",0},
		{1,"Is This THE END?",0},
		{1,"Evil Incarnate",0},
		{1,"Jazzin' Them Nazis",0},
		{1,"Puttin' It To The Enemy",0},
		{1,"The SS Gonna Get You",0},
		{1,"Towering Above",0}
	};
#endif

#ifndef SPEARDEMO
void DoJukebox(void)
{
	int which,lastsong=-1;
	unsigned start,songs[]=
		{
#ifndef SPEAR
			GETTHEM_MUS,
			SEARCHN_MUS,
			POW_MUS,
			SUSPENSE_MUS,
			WARMARCH_MUS,
			CORNER_MUS,

			NAZI_OMI_MUS,
			PREGNANT_MUS,
			GOINGAFT_MUS,
			HEADACHE_MUS,
			DUNGEON_MUS,
			ULTIMATE_MUS,

			INTROCW3_MUS,
			NAZI_RAP_MUS,
			TWELFTH_MUS,
			ZEROHOUR_MUS,
			ULTIMATE_MUS,
			PACMAN_MUS
#else
			XFUNKIE_MUS,             // 0
			XDEATH_MUS,              // 2
			XTIPTOE_MUS,             // 4
			XTHEEND_MUS,             // 7
			XEVIL_MUS,               // 17
			XJAZNAZI_MUS,            // 18
			XPUTIT_MUS,              // 21
			XGETYOU_MUS,             // 22
			XTOWER2_MUS              // 23
#endif
		};



	IN_ClearKeysDown();
	if (!AdLibPresent && !SoundBlasterPresent)
		return;


	MenuFadeOut();

#ifndef SPEAR
#ifndef UPLOAD
	start = (SYS_Hundredths()%3)*6;
#else
	start = 0;
#endif
#else
	start = 0;
#endif


	CA_CacheGrChunk (STARTFONT+1);
#ifdef SPEAR
	CacheLump (BACKDROP_LUMP_START,BACKDROP_LUMP_END);
#else
	CacheLump (CONTROLS_LUMP_START,CONTROLS_LUMP_END);
#endif
	CA_LoadAllSounds ();

	fontnumber=1;
	ClearMScreen ();
	VWB_DrawPic(112,184,C_MOUSELBACKPIC);
	DrawStripes (10);
	SETFONTCOLOR (TEXTCOLOR,BKGDCOLOR);

#ifndef SPEAR
	DrawWindow (CTL_X-2,CTL_Y-6,280,13*7,BKGDCOLOR);
#else
	DrawWindow (CTL_X-2,CTL_Y-26,280,13*10,BKGDCOLOR);
#endif

	DrawMenu (&MusicItems,&MusicMenu[start]);

	SETFONTCOLOR (READHCOLOR,BKGDCOLOR);
	PrintY=15;
	WindowX = 0;
	WindowY = 320;
	US_CPrint ("Robert's Jukebox");

	SETFONTCOLOR (TEXTCOLOR,BKGDCOLOR);
	VW_UpdateScreen();
	MenuFadeIn();

	do
	{
		which = HandleMenu(&MusicItems,&MusicMenu[start],NULL);
		if (which>=0)
		{
			if (lastsong >= 0)
				MusicMenu[start+lastsong].active = 1;

			StartCPMusic(songs[start + which]);
			MusicMenu[start+which].active = 2;
			DrawMenu (&MusicItems,&MusicMenu[start]);
			VW_UpdateScreen();
			lastsong = which;
		}
	} while(which>=0);

	MenuFadeOut();
	IN_ClearKeysDown();
#ifdef SPEAR
	UnCacheLump (BACKDROP_LUMP_START,BACKDROP_LUMP_END);
#else
	UnCacheLump (CONTROLS_LUMP_START,CONTROLS_LUMP_END);
#endif
}
#endif


/*
==========================
=
= InitGame
=
= Load a few things right away
=
==========================
*/

void InitGame (void)
{
	int                     i,x,y;
	unsigned        *blockstart;

	if (MS_CheckParm ("virtual"))
		virtualreality = true;
	else
		virtualreality = false;

	MM_Startup ();                  // so the signon screen can be freed

	SignonScreen ();

	VW_Startup ();
	IN_Startup ();
	PM_Startup ();
	PM_UnlockMainMem ();
	SD_Startup ();
	CA_Startup ();
	US_Startup ();


#ifndef SPEAR
	if (mminfo.mainmem < 235000L)
#else
	if (mminfo.mainmem < 257000L && !MS_CheckParm("debugmode"))
#endif
	{
		memptr screen;

		CA_CacheGrChunk (ERRORSCREEN);
		screen = grsegs[ERRORSCREEN];
		ShutdownId();
		SYS_TextScreen ((byte *)screen+7+7*160,17,NULL,0,0);
		exit(1);
	}


//
// build some tables
//
	InitDigiMap ();

	for (i=0;i<MAPSIZE;i++)
	{
		nearmapylookup[i] = &tilemap[0][0]+MAPSIZE*i;
		farmapylookup[i] = i*64;
	}

	for (i=0;i<PORTTILESHIGH;i++)
		uwidthtable[i] = UPDATEWIDE*i;

	blockstart = &blockstarts[0];
	for (y=0;y<UPDATEHIGH;y++)
		for (x=0;x<UPDATEWIDE;x++)
			*blockstart++ = SCREENWIDTH*16*y+x*TILEWIDTH;

	updateptr = &update[0];

	bufferofs = 0;
	displayofs = 0;
	ReadConfig ();


//
// HOLDING DOWN 'M' KEY?
//
#ifndef SPEARDEMO
	if (Keyboard[sc_M])
	  DoJukebox();
	else
#endif
//
// draw intro screen stuff
//
	if (!virtualreality)
		IntroScreen ();

//
// load in and lock down some basic chunks
//

	CA_CacheGrChunk(STARTFONT);
	MM_SetLock (&grsegs[STARTFONT],true);

	LoadLatchMem ();
	BuildTables ();          // trig tables
	SetupWalls ();

#if 0
{
int temp,i;
temp = viewsize;
	profilehandle = open("SCALERS.TXT", O_CREAT | O_WRONLY | O_TEXT);
for (i=1;i<20;i++)
	NewViewSize(i);
viewsize = temp;
close(profilehandle);
}
#endif

	NewViewSize (viewsize);


//
// initialize variables
//
	InitRedShifts ();
	if (!virtualreality)
		FinishSignon();

	displayofs = PAGE1START;
	bufferofs = PAGE2START;

	if (virtualreality)
	{
		// INT 60h started the head tracker's driver. There is none.
		NoWait = true;
	}
}

//===========================================================================

/*
==========================
=
= SetViewSize
=
==========================
*/

boolean SetViewSize (unsigned width, unsigned height)
{
	viewwidth = width&~15;                  // must be divisable by 16
	viewheight = height&~1;                 // must be even
	centerx = viewwidth/2-1;
	shootdelta = viewwidth/10;
	screenofs = ((200-STATUSLINES-viewheight)/2*SCREENWIDTH+(320-viewwidth)/8);

//
// calculate trace angles and projection constants
//
	CalcProjection (FOCALLENGTH);

//
// build all needed compiled scalers
//
//	MM_BombOnError (false);
	SetupScaling (viewwidth*1.5);
#if 0
	MM_BombOnError (true);
	if (mmerror)
	{
		Quit ("Can't build scalers!");
		mmerror = false;
		return false;
	}
#endif
	return true;
}


void ShowViewSize (int width)
{
	int     oldwidth,oldheight;

	oldwidth = viewwidth;
	oldheight = viewheight;

	viewwidth = width*16;
	viewheight = width*16*HEIGHTRATIO;
	DrawPlayBorder ();

	viewheight = oldheight;
	viewwidth = oldwidth;
}


void NewViewSize (int width)
{
	CA_UpLevel ();
	MM_SortMem ();
	viewsize = width;
	SetViewSize (width*16,width*16*HEIGHTRATIO);
	CA_DownLevel ();
}



//===========================================================================

/*
==========================
=
= Quit
=
==========================
*/

void Quit (char *error)
{
	unsigned        finscreen;
	memptr	screen;


	ClearMemory ();
	if (!*error)
	{
	 #ifndef JAPAN
	 CA_CacheGrChunk (ORDERSCREEN);
	 screen = grsegs[ORDERSCREEN];
	 #endif
	 WriteConfig ();
	}
	else
	{
	 CA_CacheGrChunk (ERRORSCREEN);
	 screen = grsegs[ERRORSCREEN];
	}

	ShutdownId ();

	//
	// The text screens shown on the way out -- the order form, or the
	// error box with the message written into it at row 4, column 10 --
	// went straight into the text mode's video memory. They go to the
	// terminal instead, if there is one, and the error to the log.
	//
	if (error && *error)
	{
	  SYS_TextScreen ((byte *)screen+7,7,error,10,4);
	  fprintf (stderr,"Wolfenstein 3-D: %s\n",error);
	  exit(1);
	}
	else
	if (!error || !(*error))
	{
		#ifndef JAPAN
		SYS_TextScreen ((byte *)screen+7,25,NULL,0,0);
		#endif
	}

	exit(0);
}

//===========================================================================



/*
=====================
=
= DemoLoop
=
=====================
*/

static  char *ParmStrings[] = {"baby","easy","normal","hard",""};

void    DemoLoop (void)
{
	static int LastDemo;
	int     i,level;
	int32_t nsize;
	memptr	nullblock;

//
// check for launch from ted
//
	if (tedlevel)
	{
		NoWait = true;
		NewGame(1,0);

		for (i = 1;i < _argc;i++)
		{
			if ( (level = US_CheckParm(_argv[i],ParmStrings)) != -1)
			{
			 gamestate.difficulty=level;
			 break;
			}
		}

#ifndef SPEAR
		gamestate.episode = tedlevelnum/10;
		gamestate.mapon = tedlevelnum%10;
#else
		gamestate.episode = 0;
		gamestate.mapon = tedlevelnum;
#endif
		GameLoop();
		Quit (NULL);
	}


//
// main game cycle
//


//	nsize = (long)40*1024;
//	MM_GetPtr(&nullblock,nsize);

#ifndef DEMOTEST

	#ifndef UPLOAD

		#ifndef GOODTIMES
		#ifndef SPEAR
		#ifndef JAPAN
		if (!NoWait)
			NonShareware();
		#endif
		#else

			#ifndef GOODTIMES
			#ifndef SPEARDEMO
			CopyProtection();
			#endif
			#endif

		#endif
		#endif
	#endif

	StartCPMusic(INTROSONG);

#ifndef JAPAN
	if (!NoWait)
		PG13 ();
#endif

#endif

	while (1)
	{
		while (!NoWait)
		{
//
// title page
//
			MM_SortMem ();
#ifndef DEMOTEST

#ifdef SPEAR
			CA_CacheGrChunk (TITLEPALETTE);

			CA_CacheGrChunk (TITLE1PIC);
			VWB_DrawPic (0,0,TITLE1PIC);
			UNCACHEGRCHUNK (TITLE1PIC);

			CA_CacheGrChunk (TITLE2PIC);
			VWB_DrawPic (0,80,TITLE2PIC);
			UNCACHEGRCHUNK (TITLE2PIC);
			VW_UpdateScreen ();
			VL_FadeIn(0,255,grsegs[TITLEPALETTE],30);

			UNCACHEGRCHUNK (TITLEPALETTE);
#else
			CA_CacheScreen (TITLEPIC);
			VW_UpdateScreen ();
			VW_FadeIn();
#endif
			if (IN_UserInput(TickBase*15))
				break;
			VW_FadeOut();
//
// credits page
//
			CA_CacheScreen (CREDITSPIC);
			VW_UpdateScreen();
			VW_FadeIn ();
			if (IN_UserInput(TickBase*10))
				break;
			VW_FadeOut ();
//
// high scores
//
			DrawHighScores ();
			VW_UpdateScreen ();
			VW_FadeIn ();

			if (IN_UserInput(TickBase*10))
				break;
#endif
//
// demo
//

			#ifndef SPEARDEMO
			PlayDemo (LastDemo++%4);
			#else
			PlayDemo (0);
			#endif

			if (playstate == ex_abort)
				break;
			StartCPMusic(INTROSONG);
		}

		VW_FadeOut ();

#ifndef SPEAR
		if (Keyboard[sc_Tab] && MS_CheckParm("goobers"))
#else
		if (Keyboard[sc_Tab] && MS_CheckParm("debugmode"))
#endif
			RecordDemo ();
		else
			US_ControlPanel (0);

		if (startgame || loadedgame)
		{
			GameLoop ();
			VW_FadeOut();
			StartCPMusic(INTROSONG);
		}
	}
}


//===========================================================================


/*
==========================
=
= main
=
==========================
*/

char    *nosprtxt[] = {"nospr",nil};

int main (int argc, char **argv)
{
	int     i;

	_argc = argc;
	_argv = argv;


#ifdef BETA
	//
	// THIS IS FOR BETA ONLY!
	//
	struct dosdate_t d;

	_dos_getdate(&d);
	if (d.year > YEAR ||
		(d.month >= MONTH && d.day >= DAY))
	{
	 printf("Sorry, BETA-TESTING is over. Thanks for you help.\n");
	 exit(1);
	}
#endif

	CheckForEpisodes();

	Patch386 ();

	InitGame ();

	DemoLoop();

	Quit("Demo loop exited???");
}

