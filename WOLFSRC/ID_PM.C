//
//	ID_PM.C
//	Id Engine's Page Manager v1.0
//	Primary coder: Jason Blochowiak
//
//	VSWAP holds every wall, sprite and digitized sound in 4K pages. The
//	original kept as many of them in conventional memory as it could, the
//	rest in EMS and XMS, and read the remainder from disk as the view needed
//	them, juggling locks and a least-recently-used count to decide what to
//	throw out. The whole file is under a megabyte, so here it is read once
//	at startup and every page stays in memory: PM_GetPage is an array index.
//
//	What remains of the interface is kept so the game's calls to it need not
//	change. Page locks, purge levels and the frame counter do nothing.
//

#include "ID_HEADS.H"

//	EMS and XMS, which the game asks about when it shows the memory figures
	boolean			XMSPresent,EMSPresent;
	word			XMSPagesAvail,EMSPagesAvail;

//	File specific variables
	char			PageFileName[13] = {"VSWAP."};
	word			ChunksInFile;
	word			PMSpriteStart,PMSoundStart;

	PageListStruct	far *PMPages;

static	boolean		PMStarted;
static	byte		**PMData;				// every page, in memory

/*
==================
=
= PM_Startup
=
= Reads VSWAP: three words of header, then a 32-bit offset and a 16-bit
= length for every page, then the pages. An offset of zero is a page that
= is not there.
=
==================
*/

void
PM_Startup(void)
{
	int			i,handle;
	uint32_t	offset;
	word		length;
	int32_t		filesize;

	if (PMStarted)
		return;

	handle = open(PageFileName,O_RDONLY | O_BINARY);
	if (handle == -1)
		Quit("PM_Startup: Unable to open page file");
	filesize = filelength(handle);

	if (read(handle,&ChunksInFile,sizeof(word)) != sizeof(word)
	||	read(handle,&PMSpriteStart,sizeof(word)) != sizeof(word)
	||	read(handle,&PMSoundStart,sizeof(word)) != sizeof(word))
		Quit("PM_Startup: Page file header read failed");

	PMPages = calloc(ChunksInFile,sizeof(PageListStruct));
	PMData = calloc(ChunksInFile,sizeof(byte *));
	if (!PMPages || !PMData)
		Quit("PM_Startup: Out of memory");

	for (i = 0;i < ChunksInFile;i++)
	{
		if (read(handle,&offset,sizeof(offset)) != sizeof(offset))
			Quit("PM_Startup: Offset read failed");
		PMPages[i].offset = offset;
	}
	for (i = 0;i < ChunksInFile;i++)
	{
		if (read(handle,&length,sizeof(length)) != sizeof(length))
			Quit("PM_Startup: Length read failed");
		PMPages[i].length = length;
	}

	for (i = 0;i < ChunksInFile;i++)
	{
		PageListStruct	*page = &PMPages[i];

		page->xmsPage = page->emsPage = page->mainPage = -1;
		if (!page->offset)
			continue;		// sparse

		if ((int32_t)(page->offset + page->length) > filesize)
			Quit("PM_Startup: Page file is shorter than its own index says");

		// Never less than a page, so code that reads a whole page past the
		// end of a short one -- a sound's last page -- reads zeroes.
		PMData[i] = calloc(1,page->length > PMPageSize ? page->length : PMPageSize);
		if (!PMData[i])
			Quit("PM_Startup: Out of memory");

		lseek(handle,page->offset,SEEK_SET);
		if (read(handle,PMData[i],page->length) != page->length)
			Quit("PM_Startup: Page read failed");
		page->mainPage = i;
	}

	close(handle);
	PMStarted = true;
}

void
PM_Shutdown(void)
{
	int	i;

	if (!PMStarted)
		return;

	for (i = 0;i < ChunksInFile;i++)
		free(PMData[i]);
	free(PMData);
	free(PMPages);
	PMData = NULL;
	PMPages = NULL;
	PMStarted = false;
}

memptr
PM_GetPageAddress(int pagenum)
{
	if (pagenum < 0 || pagenum >= ChunksInFile)
		Quit("PM_GetPageAddress: Invalid page request");
	return PMData[pagenum];
}

memptr
PM_GetPage(int pagenum)
{
	if (pagenum < 0 || pagenum >= ChunksInFile)
		Quit("PM_GetPage: Invalid page request");
	if (!PMData[pagenum])
		Quit("PM_GetPage: Tried to load a sparse page");
	return PMData[pagenum];
}

void
PM_SetPageLock(int pagenum,PMLockType lock)
{
	if (pagenum < PMSoundStart)
		Quit("PM_SetPageLock: Locking/unlocking non-sound page");
	PMPages[pagenum].locked = lock;
}

/*
==================
=
= PM_Preload
=
= Everything is in memory already. The progress bar the game draws while
= this runs ("Get Psyched!") is still drawn, full, so the screen looks as
= it did.
=
==================
*/

void
PM_Preload(boolean (*update)(word current,word total))
{
	if (update)
		update(1,1);
}

void PM_Reset(void) {}
void PM_NextFrame(void) {}
void PM_SetMainPurge(int level) {}
void PM_SetMainMemPurge(int level) {}
void PM_CheckMainMem(void) {}
