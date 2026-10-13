// ID_CA.H
//===========================================================================

#define NUMMAPS		60
#define MAPPLANES	2

#define UNCACHEGRCHUNK(chunk)	{MM_FreePtr(&grsegs[chunk]);grneeded[chunk]&=~ca_levelbit;}

//===========================================================================

#pragma pack(push,1)
typedef	struct
{
	int32_t		planestart[3];
	word		planelength[3];
	word		width,height;
	char		name[16];
} maptype;
#pragma pack(pop)

//===========================================================================

extern	char		audioname[13];

extern	byte 		_seg	*tinf;
extern	int			mapon;

extern	word		_seg	*mapsegs[MAPPLANES];
extern	maptype		_seg	*mapheaderseg[NUMMAPS];
extern	byte		_seg	*audiosegs[NUMSNDCHUNKS];
extern	void		_seg	*grsegs[NUMCHUNKS];

extern	byte		far	grneeded[NUMCHUNKS];
extern	byte		ca_levelbit,ca_levelnum;

extern	char		*titleptr[8];

extern	int			profilehandle,debughandle;

extern	char		extension[5],
			gheadname[10],
			gfilename[10],
			gdictname[10],
			mheadname[10],
			mfilename[10],
			aheadname[10],
			afilename[10];

extern int32_t		_seg *grstarts;	// array of offsets in egagraph, -1 for sparse
extern int32_t		_seg *audiostarts;	// array of offsets in audio / audiot
//
// hooks for custom cache dialogs
//
extern	void	(*drawcachebox)		(char *title, unsigned numcache);
extern	void	(*updatecachebox)	(void);
extern	void	(*finishcachebox)	(void);

//===========================================================================

// just for the score box reshifting

void CAL_ShiftSprite (unsigned segment,unsigned source,unsigned dest,
	unsigned width, unsigned height, unsigned pixshift);

//===========================================================================

void CA_OpenDebug (void);
void CA_CloseDebug (void);
boolean CA_FarRead (int handle, byte far *dest, int32_t length);
boolean CA_FarWrite (int handle, byte far *source, int32_t length);
boolean CA_ReadFile (char *filename, memptr *ptr);
boolean CA_LoadFile (char *filename, memptr *ptr);
boolean CA_WriteFile (char *filename, void far *ptr, int32_t length);

int32_t CA_RLEWCompress (word huge *source, int32_t length, word huge *dest,
  word rlewtag);

void CA_RLEWexpand (word huge *source, word huge *dest,int32_t length,
  word rlewtag);

void CA_Startup (void);
void CA_Shutdown (void);

void CA_SetGrPurge (void);
void CA_CacheAudioChunk (int chunk);
void CA_LoadAllSounds (void);

void CA_UpLevel (void);
void CA_DownLevel (void);

void CA_SetAllPurge (void);

void CA_ClearMarks (void);
void CA_ClearAllMarks (void);

#define CA_MarkGrChunk(chunk)	grneeded[chunk]|=ca_levelbit

void CA_CacheGrChunk (int chunk);
void CA_CacheMap (int mapnum);

void CA_CacheMarks (void);

void CA_CacheScreen (int chunk);
