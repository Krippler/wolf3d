//
//	ID Engine
//	ID_SD.c - Sound Manager for Wolfenstein 3D
//	v1.2
//	By Jason Blochowiak
//

//
//	This module handles dealing with generating sound on the appropriate
//		hardware
//
//	Depends on: User Mgr (for parm checking)
//
//	Globals:
//		For User Mgr:
//			SoundSourcePresent - Sound Source thingie present?
//			SoundBlasterPresent - SoundBlaster card present?
//			AdLibPresent - AdLib card present?
//			SoundMode - What device is used for sound effects
//				(Use SM_SetSoundMode() to set)
//			MusicMode - What device is used for music
//				(Use SM_SetMusicMode() to set)
//			DigiMode - What device is used for digitized sound effects
//				(Use SM_SetDigiDevice() to set)
//
//		For Cache Mgr:
//			NeedsDigitized - load digitized sounds?
//			NeedsMusic - load music?
//

//
//	THE PORT
//
//	On DOS this drove three pieces of hardware directly. Timer 0 was
//	reprogrammed to interrupt 700 times a second while music played, and
//	its handler (ID_SD_A.ASM) stepped the music, stepped the sound effect
//	every fifth time, and counted TimeCount. The AdLib's OPL2 synthesised
//	both, from register writes. The Sound Blaster played digitized sounds by
//	DMA, interrupting at the end of each block; the PC speaker played square
//	waves, its pitch set through timer 2.
//
//	Here the three are emulated and mixed into one stream, and the timer is
//	the stream itself: SD_Mix, called by sys_audio.c for every block of
//	output, runs the 700 Hz handler as often as the samples it is making
//	cover, so the music keeps time with the sound rather than with the
//	game. The OPL is Nuked OPL3 (opl/), told every register write the game
//	made. TimeCount is counted by sys_x11.c instead, which needs it whether
//	or not there is sound.
//
//	Everything the game and the handler both touch is behind one lock,
//	which stands in for the original's pushf / cli / popf: the handler took
//	the processor, and the mixer now takes the lock, for the same reason.
//
//	The logic -- which sound wins, how a digitized sound is fed a page at a
//	time, what a sequence of register writes means -- is the original's, and
//	so are the names. Only what touched hardware is new.
//

#include "ID_HEADS.H"
#include <pthread.h>
#include "opl/opl3.h"

#define	SDL_SoundFinished()	{SoundNumber = SoundPriority = 0;}

//	Global variables
	boolean		SoundSourcePresent,
				AdLibPresent,
				SoundBlasterPresent,SBProPresent,
				NeedsDigitized,NeedsMusic,
				SoundPositioned;
	SDMode		SoundMode;
	SMMode		MusicMode;
	SDSMode		DigiMode;
volatile	longword	TimeCount;		// counted by sys_x11.c
	word		HackCount;
	byte		**SoundTable;	// into audiosegs, at the current device's sounds
	int			DigiMap[LASTSOUND];

//	Internal variables
static	boolean			SD_Started;
		boolean			nextsoundpos;
static	char			*ParmStrings[] =
						{
							"noal",
							"nosb",
							"nopro",
							"noss",
							"sst",
							"ss1",
							"ss2",
							"ss3",
							nil
						};
static	void			(*SoundUserHook)(void);
		soundnames		SoundNumber,DigiNumber;
		word			SoundPriority,DigiPriority;
		int				LeftPosition,RightPosition;

		word			NumDigi,DigiLeft,DigiPage;
		word			_seg *DigiList;
		word			DigiLastStart,DigiLastEnd;
		boolean			DigiPlaying;
static	boolean			DigiMissed,DigiLastSegment;
static	memptr			DigiNextAddr;
static	word			DigiNextLen;

//	Sound Blaster: the sample being played, and where in it
static	boolean			sbNoCheck,sbNoProCheck;
static	byte			*sbSample;
static	longword		sbLength;
static	longword		sbPosition;		// 16.16, in samples
static	int				sbLeftVol = 15,sbRightVol = 15;

//	PC Sound variables
		byte			pcLastSample,*pcSound;
		longword		pcLengthLeft;
		word			pcSoundLookup[255];
static	word			pcDivisor;		// timer 2's count, or 0 for silence
static	uint64_t		pcPhase;

//	AdLib variables
		boolean			alNoCheck;
		byte			far *alSound;
		word			alBlock;
		longword		alLengthLeft;
		longword		alTimeCount;
		Instrument		alZeroInst;

// This table maps channel numbers to carrier and modulator op cells
static	byte			carriers[9] =  { 3, 4, 5,11,12,13,19,20,21},
						modifiers[9] = { 0, 1, 2, 8, 9,10,16,17,18};

//	Sequencer variables
		boolean			sqActive;
static	word			alFXReg;
		word			far *sqHack,far *sqHackPtr,sqHackLen,sqHackSeqLen;
		longword		sqHackTime;

//	The machine. There were one OPL2 chip and the effects on its channel 0,
//	the music on the rest; here there are two, so the Settings pages can set
//	the music and the effects at different levels. alOut, for the set-up and
//	shut-down the game does through it, writes to both.
static	opl3_chip		opl;			// the effects
static	opl3_chip		oplmusic;		// the music
static	int				mixrate;
static	longword		tickphase;		// toward the next 700 Hz tick
static	int				count_fx;		// ticks toward the next effects step
static	pthread_mutex_t	sdlock;

//	Internal routines
		void			SDL_DigitizedDone(void);

//
//	The lock that stands in for cli. Recursive, because the original's
//	pushf / cli / popf nested: SD_PlaySound stops the old sound, which
//	disables interrupts inside a section that already had.
//
#define	SD_Lock()	pthread_mutex_lock(&sdlock)
#define	SD_Unlock()	pthread_mutex_unlock(&sdlock)

//
//	Gains for the three sources. The OPL's own output is quiet next to a
//	full-scale 8-bit sample, and on a Sound Blaster Pro -- which is what
//	this stands in for -- the game turned the FM up to full on the mixer to
//	match (SDL_StartSB). These put the music, the AdLib effects and the
//	digitized voices at about the levels they had relative to each other
//	there.
//
#define	FM_GAIN		2
#define	DIGI_GAIN	180			// a sample of +-128 to about +-23000
#define	PC_AMPLITUDE	5000

//	The Sound Blaster's DAC rate, as SDL_StartSB set it: a time constant of
//	256 - 1000000/7000, which comes out at 7042 Hz rather than 7000.
#define	SB_RATE		(1000000 / (1000000 / 7000))

//	The PC's timer chips count at this rate.
#define	PIT_RATE	1193181

///////////////////////////////////////////////////////////////////////////
//
//	alOut(n,b) - Puts b in AdLib card register n
//
///////////////////////////////////////////////////////////////////////////
void
alOut(byte n,byte b)
{
	SD_Lock();
	OPL3_WriteReg(&opl,n,b);
	OPL3_WriteReg(&oplmusic,n,b);
	SD_Unlock();
}

//	A register of the effects' chip alone
static void
fxOut(byte n,byte b)
{
	SD_Lock();
	OPL3_WriteReg(&opl,n,b);
	SD_Unlock();
}

//
//	SoundBlaster code
//

///////////////////////////////////////////////////////////////////////////
//
//	SDL_SBStopSample() - Stops any active sampled sound
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_SBStopSample(void)
{
	SD_Lock();
	sbSample = NULL;
	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_SBPlaySample() - Plays a sampled sound on the SoundBlaster
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_SBPlaySample(byte huge *data,longword len)
{
	SD_Lock();
	sbSample = data;
	sbLength = len;
	sbPosition = 0;
	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_PositionSBP() - Sets the attenuation levels for the left and right
//		channels by using the mixer chip on the SB Pro.
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_PositionSBP(int leftpos,int rightpos)
{
	if (!SBProPresent)
		return;

	SD_Lock();
	sbLeftVol = 15 - leftpos;
	sbRightVol = 15 - rightpos;
	SD_Unlock();
}

//
//	PC Speaker code
//

///////////////////////////////////////////////////////////////////////////
//
//	SDL_PCPlaySample() - Plays the specified sample on the PC speaker
//
//	The original pulsed the speaker 7000 times a second to play 8-bit
//	samples through it, one bit deep. The Sound Blaster's path plays them
//	properly, so that is used for both.
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_PCPlaySample(byte huge *data,longword len)
{
	SDL_SBPlaySample(data,len);
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_PCStopSample() - Stops a sample playing on the PC speaker
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_PCStopSample(void)
{
	SDL_SBStopSample();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_PCPlaySound() - Plays the specified sound on the PC speaker
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_PCPlaySound(PCSound far *sound)
{
	SD_Lock();

	pcLastSample = -1;
	pcLengthLeft = sound->common.length;
	pcSound = sound->data;

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_PCStopSound() - Stops the current sound playing on the PC Speaker
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_PCStopSound(void)
{
	SD_Lock();

	pcSound = NULL;
	pcDivisor = 0;			// Turn the speaker off

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_ShutPC() - Turns off the pc speaker
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ShutPC(void)
{
	SDL_PCStopSound();
}

//
//	Stuff for digitized sounds
//
memptr
SDL_LoadDigiSegment(word page)
{
	memptr	addr;

	addr = PM_GetSoundPage(page);
	PM_SetPageLock(PMSoundStart + page,pml_Locked);

	return(addr);
}

void
SDL_PlayDigiSegment(memptr addr,word len)
{
	switch (DigiMode)
	{
	case sds_PC:
		SDL_PCPlaySample(addr,len);
		break;
	case sds_SoundBlaster:
		SDL_SBPlaySample(addr,len);
		break;
	}
}

void
SD_StopDigitized(void)
{
	int	i;

	SD_Lock();

	DigiLeft = 0;
	DigiNextAddr = nil;
	DigiNextLen = 0;
	DigiMissed = false;
	DigiPlaying = false;
	DigiNumber = DigiPriority = 0;
	SoundPositioned = false;
	if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
		SDL_SoundFinished();

	switch (DigiMode)
	{
	case sds_PC:
		SDL_PCStopSample();
		break;
	case sds_SoundBlaster:
		SDL_SBStopSample();
		break;
	}

	SD_Unlock();

	for (i = DigiLastStart;i < DigiLastEnd;i++)
		PM_SetPageLock(i + PMSoundStart,pml_Unlocked);
	DigiLastStart = 1;
	DigiLastEnd = 0;
}

void
SD_Poll(void)
{
	SYS_Throttle();
	SD_Lock();

	if (DigiLeft && !DigiNextAddr)
	{
		DigiNextLen = (DigiLeft >= PMPageSize)? PMPageSize : (DigiLeft % PMPageSize);
		DigiLeft -= DigiNextLen;
		if (!DigiLeft)
			DigiLastSegment = true;
		DigiNextAddr = SDL_LoadDigiSegment(DigiPage++);
	}
	if (DigiMissed && DigiNextAddr)
	{
		SDL_PlayDigiSegment(DigiNextAddr,DigiNextLen);
		DigiNextAddr = nil;
		DigiMissed = false;
		if (DigiLastSegment)
		{
			DigiPlaying = false;
			DigiLastSegment = false;
		}
	}

	SD_Unlock();
}

void
SD_SetPosition(int leftpos,int rightpos)
{
	if
	(
		(leftpos < 0)
	||	(leftpos > 15)
	||	(rightpos < 0)
	||	(rightpos > 15)
	||	((leftpos == 15) && (rightpos == 15))
	)
		Quit("SD_SetPosition: Illegal position");

	switch (DigiMode)
	{
	case sds_SoundBlaster:
		SDL_PositionSBP(leftpos,rightpos);
		break;
	}
}

void
SD_PlayDigitized(word which,int leftpos,int rightpos)
{
	word	len;
	memptr	addr;

	if (!DigiMode)
		return;

	SD_StopDigitized();
	if (which >= NumDigi)
		Quit("SD_PlayDigitized: bad sound number");

	SD_SetPosition(leftpos,rightpos);

	SD_Lock();

	DigiPage = DigiList[(which * 2) + 0];
	DigiLeft = DigiList[(which * 2) + 1];

	DigiLastStart = DigiPage;
	DigiLastEnd = DigiPage + ((DigiLeft + (PMPageSize - 1)) / PMPageSize);

	len = (DigiLeft >= PMPageSize)? PMPageSize : (DigiLeft % PMPageSize);
	addr = SDL_LoadDigiSegment(DigiPage++);

	DigiPlaying = true;
	DigiLastSegment = false;

	SDL_PlayDigiSegment(addr,len);
	DigiLeft -= len;
	if (!DigiLeft)
		DigiLastSegment = true;

	SD_Unlock();

	SD_Poll();
}

//
//	Called, with the lock held, when the sample being played runs out:
//	where the Sound Blaster's end-of-block interrupt called it.
//
void
SDL_DigitizedDone(void)
{
	if (DigiNextAddr)
	{
		SDL_PlayDigiSegment(DigiNextAddr,DigiNextLen);
		DigiNextAddr = nil;
		DigiMissed = false;
	}
	else
	{
		if (DigiLastSegment)
		{
			DigiPlaying = false;
			DigiLastSegment = false;
			if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
			{
				SDL_SoundFinished();
			}
			else
				DigiNumber = DigiPriority = 0;
			SoundPositioned = false;
		}
		else
			DigiMissed = true;
	}
}

void
SD_SetDigiDevice(SDSMode mode)
{
	boolean	devicenotpresent;

	if (mode == DigiMode)
		return;

	SD_StopDigitized();

	devicenotpresent = false;
	switch (mode)
	{
	case sds_SoundBlaster:
		if (!SoundBlasterPresent)
		{
			if (SoundSourcePresent)
				mode = sds_SoundSource;
			else
				devicenotpresent = true;
		}
		break;
	case sds_SoundSource:
		if (!SoundSourcePresent)
			devicenotpresent = true;
		break;
	}

	if (!devicenotpresent)
		DigiMode = mode;
}

//
//	The list of digitized sounds is the last page of VSWAP: a start page
//	and a length in bytes for each, the start counted from PMSoundStart.
//
void
SDL_SetupDigi(void)
{
	word	far *p,
			pg;
	int		i;

	p = (word far *)PM_GetPage(ChunksInFile - 1);
	pg = PMSoundStart;
	for (i = 0;i < PMPageSize / (sizeof(word) * 2);i++,p += 2)
	{
		if (pg >= ChunksInFile - 1)
			break;
		pg += (p[1] + (PMPageSize - 1)) / PMPageSize;
	}
	MM_GetPtr((memptr *)&DigiList,i * sizeof(word) * 2);
	_fmemcpy((void far *)DigiList,PM_GetPage(ChunksInFile - 1),i * sizeof(word) * 2);
	NumDigi = i;

	for (i = 0;i < LASTSOUND;i++)
		DigiMap[i] = -1;
}

// 	AdLib Code

///////////////////////////////////////////////////////////////////////////
//
//	SDL_ALStopSound() - Turns off any sound effects playing through the
//		AdLib card
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ALStopSound(void)
{
	SD_Lock();

	alSound = 0;
	fxOut(alFreqH + 0,0);

	SD_Unlock();
}

static void
SDL_AlSetFXInst(Instrument far *inst)
{
	byte		c,m;

	m = modifiers[0];
	c = carriers[0];
	fxOut(m + alChar,inst->mChar);
	fxOut(m + alScale,inst->mScale);
	fxOut(m + alAttack,inst->mAttack);
	fxOut(m + alSus,inst->mSus);
	fxOut(m + alWave,inst->mWave);
	fxOut(c + alChar,inst->cChar);
	fxOut(c + alScale,inst->cScale);
	fxOut(c + alAttack,inst->cAttack);
	fxOut(c + alSus,inst->cSus);
	fxOut(c + alWave,inst->cWave);

	// Note: Switch commenting on these lines for old MUSE compatibility
//	alOut(alFeedCon,inst->nConn);
	fxOut(alFeedCon,0);
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_ALPlaySound() - Plays the specified sound on the AdLib card
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ALPlaySound(AdLibSound far *sound)
{
	Instrument	far *inst;

	SDL_ALStopSound();

	SD_Lock();

	alLengthLeft = sound->common.length;
	alSound = sound->data;
	alBlock = ((sound->block & 7) << 2) | 0x20;
	inst = &sound->inst;

	if (!(inst->mSus | inst->cSus))
	{
		SD_Unlock();
		Quit("SDL_ALPlaySound() - Bad instrument");
	}

	SDL_AlSetFXInst(&alZeroInst);	// DEBUG
	SDL_AlSetFXInst(inst);

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_ShutAL() - Shuts down the AdLib card for sound effects
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ShutAL(void)
{
	SD_Lock();

	alOut(alEffects,0);
	alOut(alFreqH + 0,0);
	SDL_AlSetFXInst(&alZeroInst);
	alSound = 0;

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_CleanAL() - Totally shuts down the AdLib card
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_CleanAL(void)
{
	int	i;

	SD_Lock();

	alOut(alEffects,0);
	for (i = 1;i < 0xf5;i++)
		alOut(i,0);

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_StartAL() - Starts up the AdLib card for sound effects
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_StartAL(void)
{
	alFXReg = 0;
	alOut(alEffects,alFXReg);
	SDL_AlSetFXInst(&alZeroInst);
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_DetectAdLib() - Determines if there's an AdLib (or SoundBlaster
//		emulating an AdLib) present
//
//	There always is. What is left is the initialisation the detection did
//	once it had found one.
//
///////////////////////////////////////////////////////////////////////////
static boolean
SDL_DetectAdLib(void)
{
	int		i;

	for (i = 1;i <= 0xf5;i++)	// Zero all the registers
		alOut(i,0);

	alOut(1,0x20);	// Set WSE=1
	alOut(8,0);		// Set CSM=0 & SEL=0

	return(true);
}


/*
=============================================================================

								THE TIMER

	ID_SD_A.ASM's handlers, in C, run from the mixer at 700 Hz. The fast
	handler, which ran while music was on, stepped the effects every fifth
	time -- 140 Hz -- and the music every time; the slow one, which ran
	otherwise, stepped the effects at its own 140 Hz. Either way the effects
	go at 140 Hz and the music at 700, which is what this does.

=============================================================================
*/

//
//	DOFX: the next byte of the current PC speaker or AdLib effect
//
static void
SDL_DoFX(void)
{
	byte	s;

	if (pcSound)
	{
		s = *pcSound++;
		if (s != pcLastSample)
		{
			pcLastSample = s;
			pcDivisor = s ? pcSoundLookup[s] : 0;
		}

		if (!--pcLengthLeft)
		{
			pcSound = 0;
			SDL_SoundFinished();
			pcDivisor = 0;
		}
	}

	if (alSound)
	{
		s = *alSound;
		if (s)
		{
			OPL3_WriteReg(&opl,alFreqL,s);
			OPL3_WriteReg(&opl,alFreqH,alBlock);
		}
		else
			OPL3_WriteReg(&opl,alFreqH,0);

		alSound++;
		if (!--alLengthLeft)
		{
			alSound = 0;
			SDL_SoundFinished();
			OPL3_WriteReg(&opl,alFreqH,0);	// Turn off the sound
		}
	}
}

//
//	The music: a list of register writes, each a register and value byte
//	and then the number of ticks to wait before the next.
//
static void
SDL_DoMusic(void)
{
	if (sqHackLen)
	{
		while (sqHackTime <= alTimeCount)
		{
			sqHackTime = alTimeCount + sqHackPtr[1];	// time to next event
			OPL3_WriteReg(&oplmusic,sqHackPtr[0] & 0xff,sqHackPtr[0] >> 8);
			sqHackPtr += 2;
			sqHackLen -= 4;
			if (!sqHackLen)
				break;
		}
	}

	alTimeCount++;

	if (!sqHackLen)
	{
		sqHackPtr = sqHack;					// back to the start
		sqHackLen = sqHackSeqLen;
		alTimeCount = sqHackTime = 0;
	}
}

static void
SDL_Tick(void)
{
	if (++count_fx >= 5)
	{
		count_fx = 0;
		SDL_DoFX();
	}

	if (sqActive)
		SDL_DoMusic();
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_Mix() - Makes frames of 16-bit stereo sound at the rate
//		SYS_AudioRate() gave. Called by sys_audio.c from its own thread.
//
///////////////////////////////////////////////////////////////////////////
void
SD_Mix(int16_t *out,int frames)
{
	int16_t		fm[4],mus[4];
	int32_t		l,r,v;
	int32_t		musicgain,fxgain;
	longword	sbstep;

	sbstep = (longword)(((uint64_t)SB_RATE << 16) / mixrate);

	//	The Settings pages' levels, 0-10, as gains in 256ths
	musicgain = sys_setting[set_music] * 256 / 10;
	fxgain = sys_setting[set_effects] * 256 / 10;

	SD_Lock();

	while (frames--)
	{
		tickphase += 700;
		while (tickphase >= (longword)mixrate)
		{
			tickphase -= mixrate;
			SDL_Tick();
		}

		OPL3_GenerateResampled(&opl,fm);
		OPL3_GenerateResampled(&oplmusic,mus);
		l = fm[0] * FM_GAIN * fxgain;
		r = fm[1] * FM_GAIN * fxgain;
		l += mus[0] * FM_GAIN * musicgain;
		r += mus[1] * FM_GAIN * musicgain;

		if (pcDivisor)
		{
			// A square wave at the timer's rate over twice the count
			pcPhase += (uint64_t)PIT_RATE * 2;
			v = ((pcPhase / ((uint64_t)pcDivisor * mixrate)) & 1) ? PC_AMPLITUDE : -PC_AMPLITUDE;
			l += v * fxgain;
			r += v * fxgain;
		}

		if (sbSample)
		{
			v = ((int32_t)sbSample[sbPosition >> 16] - 128) * DIGI_GAIN;
			l += v * sbLeftVol / 15 * fxgain;
			r += v * sbRightVol / 15 * fxgain;

			sbPosition += sbstep;
			if ((sbPosition >> 16) >= sbLength)
			{
				sbSample = NULL;
				SDL_DigitizedDone();
			}
		}

		l >>= 8;
		r >>= 8;
		out[0] = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
		out[1] = r > 32767 ? 32767 : r < -32768 ? -32768 : r;
		out += 2;
	}

	SD_Unlock();
}

////////////////////////////////////////////////////////////////////////////
//
//	SDL_ShutDevice() - turns off whatever device was being used for sound fx
//
////////////////////////////////////////////////////////////////////////////
static void
SDL_ShutDevice(void)
{
	switch (SoundMode)
	{
	case sdm_PC:
		SDL_ShutPC();
		break;
	case sdm_AdLib:
		SDL_ShutAL();
		break;
	}
	SoundMode = sdm_Off;
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_CleanDevice() - totally shuts down all sound devices
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_CleanDevice(void)
{
	if ((SoundMode == sdm_AdLib) || (MusicMode == smm_AdLib))
		SDL_CleanAL();
}

///////////////////////////////////////////////////////////////////////////
//
//	SDL_StartDevice() - turns on whatever device is to be used for sound fx
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_StartDevice(void)
{
	switch (SoundMode)
	{
	case sdm_AdLib:
		SDL_StartAL();
		break;
	}
	SoundNumber = SoundPriority = 0;
}

//	Public routines

///////////////////////////////////////////////////////////////////////////
//
//	SD_SetSoundMode() - Sets which sound hardware to use for sound effects
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_SetSoundMode(SDMode mode)
{
	boolean	result = false;
	word	tableoffset = 0;

	SD_StopSound();

	if ((mode == sdm_AdLib) && !AdLibPresent)
		mode = sdm_PC;

	switch (mode)
	{
	case sdm_Off:
		NeedsDigitized = false;
		result = true;
		break;
	case sdm_PC:
		tableoffset = STARTPCSOUNDS;
		NeedsDigitized = false;
		result = true;
		break;
	case sdm_AdLib:
		if (AdLibPresent)
		{
			tableoffset = STARTADLIBSOUNDS;
			NeedsDigitized = false;
			result = true;
		}
		break;
	}

	if (result && (mode != SoundMode))
	{
		SDL_ShutDevice();
		SoundMode = mode;
		SoundTable = &audiosegs[tableoffset];
		SDL_StartDevice();
	}

	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_SetMusicMode() - sets the device to use for background music
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_SetMusicMode(SMMode mode)
{
	boolean	result = false;

	SD_FadeOutMusic();
	while (SD_MusicPlaying())
		SYS_Spin();

	switch (mode)
	{
	case smm_Off:
		NeedsMusic = false;
		result = true;
		break;
	case smm_AdLib:
		if (AdLibPresent)
		{
			NeedsMusic = true;
			result = true;
		}
		break;
	}

	if (result)
		MusicMode = mode;

	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_Startup() - starts up the Sound Mgr
//		Detects all additional sound hardware and installs my ISR
//
//	-noal, -nosb and -nopro still take away the AdLib, the Sound Blaster and
//	the Pro's stereo, as they did. The Sound Source is never there.
//
///////////////////////////////////////////////////////////////////////////
void
SD_Startup(void)
{
	int	i;
	pthread_mutexattr_t	attr;

	if (SD_Started)
		return;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&sdlock,&attr);
	pthread_mutexattr_destroy(&attr);

	mixrate = SYS_AudioRate();
	OPL3_Reset(&opl,mixrate);
	OPL3_Reset(&oplmusic,mixrate);
	tickphase = 0;
	count_fx = 0;

	alNoCheck = false;
	sbNoCheck = false;
	sbNoProCheck = false;
	for (i = 1;i < _argc;i++)
	{
		switch (US_CheckParm(_argv[i],ParmStrings))
		{
		case 0:						// No AdLib detection
			alNoCheck = true;
			break;
		case 1:						// No SoundBlaster detection
			sbNoCheck = true;
			break;
		case 2:						// No SoundBlaster Pro detection
			sbNoProCheck = true;
			break;
		}
	}

	SoundUserHook = 0;

	alTimeCount = 0;

	SD_SetSoundMode(sdm_Off);
	SD_SetMusicMode(smm_Off);

	SoundSourcePresent = false;

	if (!alNoCheck)
	{
		AdLibPresent = SDL_DetectAdLib();
		if (AdLibPresent && !sbNoCheck)
		{
			SoundBlasterPresent = true;
			SBProPresent = !sbNoProCheck;
		}
	}

	for (i = 0;i < 255;i++)
		pcSoundLookup[i] = i * 60;

	SDL_SetupDigi();

	SD_Started = true;

	SYS_AudioStart();
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_Default() - Sets up the default behaviour for the Sound Mgr whether
//		the config file was present or not.
//
///////////////////////////////////////////////////////////////////////////
void
SD_Default(boolean gotit,SDMode sd,SMMode sm)
{
	boolean	gotsd,gotsm;

	gotsd = gotsm = gotit;

	if (gotsd)	// Make sure requested sound hardware is available
	{
		switch (sd)
		{
		case sdm_AdLib:
			gotsd = AdLibPresent;
			break;
		}
	}
	if (!gotsd)
	{
		if (AdLibPresent)
			sd = sdm_AdLib;
		else
			sd = sdm_PC;
	}
	if (sd != SoundMode)
		SD_SetSoundMode(sd);


	if (gotsm)	// Make sure requested music hardware is available
	{
		switch (sm)
		{
		case sdm_AdLib:
			gotsm = AdLibPresent;
			break;
		}
	}
	if (!gotsm)
	{
		if (AdLibPresent)
			sm = smm_AdLib;
	}
	if (sm != MusicMode)
		SD_SetMusicMode(sm);
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_Shutdown() - shuts down the Sound Mgr
//		Removes sound ISR and turns off whatever sound hardware was active
//
///////////////////////////////////////////////////////////////////////////
void
SD_Shutdown(void)
{
	if (!SD_Started)
		return;

	SD_MusicOff();
	SD_StopSound();
	SDL_ShutDevice();
	SDL_CleanDevice();

	SYS_AudioStop();

	SD_Started = false;
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_SetUserHook() - sets the routine that the Sound Mgr calls every 1/70th
//		of a second from its timer 0 ISR
//
//	Nothing in the game sets one, and nothing here calls it.
//
///////////////////////////////////////////////////////////////////////////
void
SD_SetUserHook(void (* hook)(void))
{
	SoundUserHook = hook;
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_PositionSound() - Sets up a stereo imaging location for the next
//		sound to be played. Each channel ranges from 0 to 15.
//
///////////////////////////////////////////////////////////////////////////
void
SD_PositionSound(int leftvol,int rightvol)
{
	LeftPosition = leftvol;
	RightPosition = rightvol;
	nextsoundpos = true;
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_PlaySound() - plays the specified sound on the appropriate hardware
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_PlaySound(soundnames sound)
{
	boolean		ispos;
	SoundCommon	far *s;
	int	lp,rp;

	lp = LeftPosition;
	rp = RightPosition;
	LeftPosition = 0;
	RightPosition = 0;

	ispos = nextsoundpos;
	nextsoundpos = false;

	if (sound == -1)
		return(false);

	s = SoundMode != sdm_Off ? (SoundCommon far *)SoundTable[sound] : NULL;
	if ((SoundMode != sdm_Off) && !s)
		Quit("SD_PlaySound() - Uncached sound");

	if ((DigiMode != sds_Off) && (DigiMap[sound] != -1))
	{
		// With the sound effects off there is no table to give a priority,
		// and the original read whatever segment 0 held. Priority 0 lets
		// the digitized sound play, which is what that usually came to.
		word	priority = s ? s->priority : 0;

		if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
		{
			if (priority < SoundPriority)
				return(false);

			SDL_PCStopSound();

			SD_PlayDigitized(DigiMap[sound],lp,rp);
			SoundPositioned = ispos;
			SoundNumber = sound;
			SoundPriority = priority;
		}
		else
		{
			SD_Lock();
			if (DigiPriority && !DigiNumber)
			{
				SD_Unlock();
				Quit("SD_PlaySound: Priority without a sound");
			}
			SD_Unlock();

			if (priority < DigiPriority)
				return(false);

			SD_PlayDigitized(DigiMap[sound],lp,rp);
			SoundPositioned = ispos;
			DigiNumber = sound;
			DigiPriority = priority;
		}

		return(true);
	}

	if (SoundMode == sdm_Off)
		return(false);
	if (!s->length)
		Quit("SD_PlaySound() - Zero length sound");
	if (s->priority < SoundPriority)
		return(false);

	switch (SoundMode)
	{
	case sdm_PC:
		SDL_PCPlaySound((void far *)s);
		break;
	case sdm_AdLib:
		SDL_ALPlaySound((void far *)s);
		break;
	}

	SoundNumber = sound;
	SoundPriority = s->priority;

	return(false);
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_SoundPlaying() - returns the sound number that's playing, or 0 if
//		no sound is playing
//
///////////////////////////////////////////////////////////////////////////
word
SD_SoundPlaying(void)
{
	boolean	result = false;

	switch (SoundMode)
	{
	case sdm_PC:
		result = pcSound? true : false;
		break;
	case sdm_AdLib:
		result = alSound? true : false;
		break;
	}

	if (result)
		return(SoundNumber);
	else
		return(false);
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_StopSound() - if a sound is playing, stops it
//
///////////////////////////////////////////////////////////////////////////
void
SD_StopSound(void)
{
	if (DigiPlaying)
		SD_StopDigitized();

	switch (SoundMode)
	{
	case sdm_PC:
		SDL_PCStopSound();
		break;
	case sdm_AdLib:
		SDL_ALStopSound();
		break;
	}

	SoundPositioned = false;

	SDL_SoundFinished();
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_WaitSoundDone() - waits until the current sound is done playing
//
///////////////////////////////////////////////////////////////////////////
void
SD_WaitSoundDone(void)
{
	while (SD_SoundPlaying())
		SYS_Spin();
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_MusicOn() - turns on the sequencer
//
///////////////////////////////////////////////////////////////////////////
void
SD_MusicOn(void)
{
	sqActive = true;
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_MusicOff() - turns off the sequencer and any playing notes
//
///////////////////////////////////////////////////////////////////////////
void
SD_MusicOff(void)
{
	word	i;

	SD_Lock();

	switch (MusicMode)
	{
	case smm_AdLib:
		alFXReg = 0;
		alOut(alEffects,0);
		for (i = 0;i < sqMaxTracks;i++)
			alOut(alFreqH + i + 1,0);
		break;
	}
	sqActive = false;

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_StartMusic() - starts playing the music pointed to
//
///////////////////////////////////////////////////////////////////////////
void
SD_StartMusic(MusicGroup far *music)
{
	SD_MusicOff();

	SD_Lock();

	if (MusicMode == smm_AdLib)
	{
		sqHackPtr = sqHack = music->values;
		sqHackSeqLen = sqHackLen = music->length;
		sqHackTime = 0;
		alTimeCount = 0;
		SD_MusicOn();
	}

	SD_Unlock();
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_FadeOutMusic() - starts fading out the music. Call SD_MusicPlaying()
//		to see if the fadeout is complete
//
///////////////////////////////////////////////////////////////////////////
void
SD_FadeOutMusic(void)
{
	switch (MusicMode)
	{
	case smm_AdLib:
		// DEBUG - quick hack to turn the music off
		SD_MusicOff();
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
//
//	SD_MusicPlaying() - returns true if music is currently playing, false if
//		not
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_MusicPlaying(void)
{
	boolean	result;

	switch (MusicMode)
	{
	case smm_AdLib:
		result = false;
		// DEBUG - not written
		break;
	default:
		result = false;
	}

	return(result);
}
