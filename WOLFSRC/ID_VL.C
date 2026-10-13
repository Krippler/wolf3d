// ID_VL.C
//
// The VGA layer. Everything here wrote to the card's registers and memory
// directly, much of it in assembly; it now writes to the emulated card in
// id_port.h, keeping the original's planar addressing so that its callers'
// arithmetic on bufferofs, displayofs and the latch addresses means what it
// always did. What reaches the screen is decided by sys_x11.c, which shows
// the page at the CRTC start address through the palette, as the card did.

#include "ID_HEADS.H"

//
// SC_INDEX is expected to stay at SC_MAPMASK for proper operation
//

unsigned	bufferofs;
unsigned	displayofs,pelpan;

unsigned	screenseg=SCREENSEG;		// set to 0xa000 for asm convenience

unsigned	linewidth;
unsigned	ylookup[MAXSCANLINES];

boolean		screenfaded;
unsigned	bordercolor;

boolean		fastpalette;				// if true, use outsb to set

byte		far	palette1[256][3],far palette2[256][3];

// The card itself
byte		vgamem[VGA_PLANESIZE*4];
byte		vga_mapmask = 15;
byte		vga_readmap;
byte		vga_writemode;

//===========================================================================


/*
=======================
=
= VL_Startup	// WOLFENSTEIN HACK
=
= The original checked for a VGA card and refused to run without one, unless
= told -HIDDENCARD. There is always one here.
=
=======================
*/

void	VL_Startup (void)
{
	SYS_Startup ();
}



/*
=======================
=
= VL_Shutdown
=
=======================
*/

void	VL_Shutdown (void)
{
	VL_SetTextMode ();
}


/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void	VL_SetVGAPlaneMode (void)
{
	VL_DePlaneVGA ();
	VGAMAPMASK(15);
	VGAWRITEMODE(0);
	VL_SetLineWidth (40);
}


/*
=======================
=
= VL_SetTextMode
=
= Back to text mode, on the way out. There is no text mode to go back to,
= and the window is closed by SYS_Shutdown.
=
=======================
*/

void	VL_SetTextMode (void)
{
}

//===========================================================================

/*
=================
=
= VL_ClearVideo
=
= Fill the entire video buffer with a given color
=
=================
*/

void VL_ClearVideo (byte color)
{
	memset (vgamem,color,sizeof(vgamem));
}


/*
=============================================================================

			VGA REGISTER MANAGEMENT ROUTINES

=============================================================================
*/


/*
=================
=
= VL_DePlaneVGA
=
= Took mode 13h apart into unchained planes. The emulation only has the one
= mode, so all that is left is the clear.
=
=================
*/

void VL_DePlaneVGA (void)
{
	VL_ClearVideo (0);
}

//===========================================================================

/*
====================
=
= VL_SetLineWidth
=
= Line witdh is in WORDS, 40 words is normal width for vgaplanegr
=
====================
*/

void VL_SetLineWidth (unsigned width)
{
	int i,offset;

//
// set up lookup tables
//
	linewidth = width*2;

	offset = 0;

	for (i=0;i<MAXSCANLINES;i++)
	{
		ylookup[i]=offset;
		offset += linewidth;
	}
}

/*
====================
=
= VL_SetSplitScreen
=
= Wolfenstein never splits the screen.
=
====================
*/

void VL_SetSplitScreen (int linenum)
{
}


/*
=============================================================================

						PALETTE OPS

		To avoid snow, do a WaitVBL BEFORE calling these

=============================================================================
*/


/*
=================
=
= VL_FillPalette
=
=================
*/

void VL_FillPalette (int red, int green, int blue)
{
	byte	pal[768];
	int		i;

	for (i=0;i<256;i++)
	{
		pal[i*3] = red;
		pal[i*3+1] = green;
		pal[i*3+2] = blue;
	}
	SYS_SetPalette (pal);
}

//===========================================================================

/*
=================
=
= VL_SetColor
=
=================
*/

void VL_SetColor	(int color, int red, int green, int blue)
{
	byte	pal[768];

	SYS_GetPalette (pal);
	pal[color*3] = red;
	pal[color*3+1] = green;
	pal[color*3+2] = blue;
	SYS_SetPalette (pal);
}

//===========================================================================

/*
=================
=
= VL_GetColor
=
=================
*/

void VL_GetColor	(int color, int *red, int *green, int *blue)
{
	byte	pal[768];

	SYS_GetPalette (pal);
	*red = pal[color*3];
	*green = pal[color*3+1];
	*blue = pal[color*3+2];
}

//===========================================================================

/*
=================
=
= VL_SetPalette
=
=================
*/

void VL_SetPalette (byte far *palette)
{
	SYS_SetPalette (palette);
}


//===========================================================================

/*
=================
=
= VL_GetPalette
=
=================
*/

void VL_GetPalette (byte far *palette)
{
	SYS_GetPalette (palette);
}


//===========================================================================

/*
=================
=
= VL_FadeOut
=
= Fades the current palette to the given color in the given number of steps
=
=================
*/

void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
	int		i,j,orig,delta;
	byte	far *origptr, far *newptr;

	VL_WaitVBL(1);
	VL_GetPalette (&palette1[0][0]);
	_fmemcpy (palette2,palette1,768);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		origptr = &palette1[start][0];
		newptr = &palette2[start][0];
		for (j=start;j<=end;j++)
		{
			orig = *origptr++;
			delta = red-orig;
			*newptr++ = orig + delta * i / steps;
			orig = *origptr++;
			delta = green-orig;
			*newptr++ = orig + delta * i / steps;
			orig = *origptr++;
			delta = blue-orig;
			*newptr++ = orig + delta * i / steps;
		}

		VL_WaitVBL(1);
		VL_SetPalette (&palette2[0][0]);
	}

//
// final color
//
	VL_FillPalette (red,green,blue);

	screenfaded = true;
}


/*
=================
=
= VL_FadeIn
=
=================
*/

void VL_FadeIn (int start, int end, byte far *palette, int steps)
{
	int		i,j,delta;

	VL_WaitVBL(1);
	VL_GetPalette (&palette1[0][0]);
	_fmemcpy (&palette2[0][0],&palette1[0][0],sizeof(palette1));

	start *= 3;
	end = end*3+2;

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		for (j=start;j<=end;j++)
		{
			delta = palette[j]-palette1[0][j];
			palette2[0][j] = palette1[0][j] + delta * i / steps;
		}

		VL_WaitVBL(1);
		VL_SetPalette (&palette2[0][0]);
	}

//
// final color
//
	VL_SetPalette (palette);
	screenfaded = false;
}



/*
=================
=
= VL_TestPaletteSet
=
= Some cards could not take the palette with a block output instruction.
= This one can.
=
=================
*/

void VL_TestPaletteSet (void)
{
	fastpalette = true;
}


/*
==================
=
= VL_ColorBorder
=
==================
*/

void VL_ColorBorder (int color)
{
	SYS_SetBorder (color);
	bordercolor = color;
}


/*
=============================================================================

						VERTICAL RETRACE AND THE CRTC

	The original's assembly polled the status register. Here a frame is a
	70th of a second on the system clock, and the start address is handed
	to the scan-out.

=============================================================================
*/

void VL_WaitVBL (int vbls)
{
	SYS_WaitVBL (vbls);
}

void VL_SetCRTC (int crtc)
{
	SYS_SetDisplay (crtc,pelpan);
}

void VL_SetScreen (int crtc, int pelpan)
{
	SYS_SetDisplay (crtc,pelpan);
}


/*
=============================================================================

							PIXEL OPS

=============================================================================
*/

byte	pixmasks[4] = {1,2,4,8};
byte	leftmasks[4] = {15,14,12,8};
byte	rightmasks[4] = {1,3,7,15};


/*
=================
=
= VL_Plot
=
=================
*/

void VL_Plot (int x, int y, int color)
{
	*VGA_Pixel (bufferofs,linewidth,x,y) = color;
}


/*
=================
=
= VL_Hlin
=
=================
*/

void VL_Hlin (unsigned x, unsigned y, unsigned width, unsigned color)
{
	while (width--)
		*VGA_Pixel (bufferofs,linewidth,x++,y) = color;
}


/*
=================
=
= VL_Vlin
=
=================
*/

void VL_Vlin (int x, int y, int height, int color)
{
	while (height--)
		*VGA_Pixel (bufferofs,linewidth,x,y++) = color;
}


/*
=================
=
= VL_Bar
=
=================
*/

void VL_Bar (int x, int y, int width, int height, int color)
{
	while (height--)
		VL_Hlin (x,y++,width,color);
}

/*
============================================================================

							MEMORY OPS

============================================================================
*/

/*
=================
=
= VL_MemToLatch
=
= A planar picture -- all of plane 0, then all of plane 1 and so on -- into
= video memory at dest.
=
=================
*/

void VL_MemToLatch (byte far *source, int width, int height, unsigned dest)
{
	unsigned	count,i;
	byte	plane;

	count = ((width+3)/4)*height;
	for (plane = 0; plane<4 ; plane++)
	{
		for (i=0;i<count;i++)
			VGA_Ptr (dest+i)[plane] = *source++;
	}
}


//===========================================================================


/*
=================
=
= VL_MemToScreen
=
= Draws a block of data to the screen.
=
= The source is planar, as VL_MungePic leaves it, plane 0 holding columns 0,
= 4, 8... of the picture. They go to whichever plane x&3 says, so a
= picture can start at any x.
=
=================
*/

void VL_MemToScreen (byte far *source, int width, int height, int x, int y)
{
	int		plane,px,py,pwidth;

	pwidth = width>>2;

	for (plane = 0; plane<4; plane++)
		for (py=0;py<height;py++)
			for (px=0;px<pwidth;px++)
				*VGA_Pixel (bufferofs,linewidth,x+px*4+plane,y+py) = *source++;
}

//==========================================================================


/*
=================
=
= VL_MaskedToScreen
=
= Masks a block of main memory to the screen.
=
= Never called. The original left its mask uninitialised, and drew without
= masking anything; this does the same, without the uninitialised read.
=
=================
*/

void VL_MaskedToScreen (byte far *source, int width, int height, int x, int y)
{
	VL_MemToScreen (source,width,height,x,y);
}

//==========================================================================

/*
=================
=
= VL_LatchToScreen
=
= Copies width addresses (four pixels each) by height lines from video
= memory at source to the screen, through the latches.
=
=================
*/

void VL_LatchToScreen (unsigned source, int width, int height, int x, int y)
{
	unsigned	dest;
	int			i;

	VGAWRITEMODE(1);
	VGAMAPMASK(15);

	dest = bufferofs+ylookup[y]+(x>>2);

	while (height--)
	{
		for (i=0;i<width;i++)
			VGA_LatchCopy (source++,dest+i);
		dest += linewidth;
	}

	VGAWRITEMODE(0);
}


//===========================================================================

/*
=================
=
= VL_ScreenToScreen
=
= From ID_VL_A.ASM: video memory to video memory, through the latches, a
= block width addresses wide.
=
=================
*/

void VL_ScreenToScreen (unsigned source, unsigned dest,int width, int height)
{
	int		i;

	VGAWRITEMODE(1);
	VGAMAPMASK(15);

	while (height--)
	{
		for (i=0;i<width;i++)
			VGA_LatchCopy (source+i,dest+i);
		source += linewidth;
		dest += linewidth;
	}

	VGAWRITEMODE(0);
}


/*
=============================================================================

						STRING OUTPUT ROUTINES

=============================================================================
*/




/*
===================
=
= VL_DrawTile8String
=
= Each character is 64 bytes: 16 for each plane, two bytes (eight pixels) a
= row.
=
===================
*/

void VL_DrawTile8String (char *str, char far *tile8ptr, int printx, int printy)
{
	int		i,plane;
	byte	far *src;
	unsigned	dest;

	dest = bufferofs+ylookup[printy]+(printx>>2);

	while (*str)
	{
		src = (byte far *)(tile8ptr + ((byte)*str<<6));

		for (plane=0;plane<4;plane++)
			for (i=0;i<8;i++)
			{
				VGA_Ptr (dest+i*linewidth)[plane] = *src++;
				VGA_Ptr (dest+i*linewidth+1)[plane] = *src++;
			}

		str++;
		printx += 8;
		dest+=2;
	}
}



/*
===================
=
= VL_DrawLatch8String
=
===================
*/

void VL_DrawLatch8String (char *str, unsigned tile8ptr, int printx, int printy)
{
	int		i;
	unsigned	src,dest;

	dest = bufferofs+ylookup[printy]+(printx>>2);

	VGAWRITEMODE(1);
	VGAMAPMASK(15);

	while (*str)
	{
		src = tile8ptr + ((byte)*str<<4);		// each character is 16 latch bytes

		for (i=0;i<8;i++)
		{
			VGA_LatchCopy (src++,dest+i*linewidth);
			VGA_LatchCopy (src++,dest+i*linewidth+1);
		}

		str++;
		printx += 8;
		dest+=2;
	}

	VGAWRITEMODE(0);
}


/*
===================
=
= VL_SizeTile8String
=
===================
*/

void VL_SizeTile8String (char *str, int *width, int *height)
{
	*height = 8;
	*width = 8*strlen(str);
}
