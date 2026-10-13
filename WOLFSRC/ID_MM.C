// ID_MM.C

/*
=============================================================================

		   ID software memory manager
		   --------------------------

	The original fitted the game into 640K of conventional memory plus
	whatever EMS and XMS there was: blocks were allocated from a heap it
	managed itself, could be marked purgable and thrown away when space ran
	short, and were moved about by MM_SortMem to close the gaps between them,
	with every owner's pointer updated as they went. That is the reason the
	game hands this module the address of its pointer rather than the
	pointer: so it can be changed behind the owner's back.

	None of that is needed with the memory of a current machine. Every
	block comes from malloc and stays where it is until it is freed, and a
	purgable block is simply never purged -- which is what happened on DOS
	too whenever there was room, so nothing that worked there can depend on
	a purge.

	The interface is unchanged, so the rest of the game is.

=============================================================================
*/

#include "ID_HEADS.H"

mminfotype	mminfo;
memptr		bufferseg;
boolean		mmerror;

void		(* beforesort) (void);
void		(* aftersort) (void);

static	boolean		bombonerror = true;

/*
===================
=
= MM_Startup
=
= Says there is as much memory as a well equipped 486 had, which is what
= WL_MAIN checks before it will start.
=
===================
*/

void MM_Startup (void)
{
	mminfo.nearheap = 0x10000;
	mminfo.farheap = 600*1024;
	mminfo.mainmem = mminfo.nearheap + mminfo.farheap;
	mminfo.EMSmem = 0;
	mminfo.XMSmem = 0;

	if (!bufferseg)
		MM_GetPtr (&bufferseg,BUFFERSIZE);
}

void MM_Shutdown (void)
{
}

void MM_MapEMS (void)
{
}

/*
====================
=
= MM_GetPtr
=
= Allocates an unlocked, unpurgable block. Zeroed, which DOS did not do,
= so that any code reading a block before writing it gets the same answer
= every time rather than whatever malloc last left there.
=
====================
*/

void MM_GetPtr (memptr *baseptr,uint32_t size)
{
	mmerror = false;

	*baseptr = calloc (1,size ? size : 1);
	if (!*baseptr)
	{
		if (bombonerror)
			Quit ("MM_GetPtr: Out of memory!");
		mmerror = true;
	}
}

void MM_FreePtr (memptr *baseptr)
{
	free (*baseptr);
	*baseptr = NULL;
}

void MM_SetPurge (memptr *baseptr, int purge)
{
}

void MM_SetLock (memptr *baseptr, boolean locked)
{
}

void MM_SortMem (void)
{
	if (beforesort)
		beforesort ();
	if (aftersort)
		aftersort ();
}

void MM_ShowMemory (void)
{
}

int32_t MM_UnusedMemory (void)
{
	return mminfo.mainmem;
}

int32_t MM_TotalFree (void)
{
	return mminfo.mainmem;
}

void MM_BombOnError (boolean bomb)
{
	bombonerror = bomb;
}

void MML_UseSpace (unsigned segstart, unsigned seglength)
{
}
