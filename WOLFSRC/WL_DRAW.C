// WL_DRAW.C

#include "WL_DEF.H"
#pragma hdrstop

//#define DEBUGWALLS
//#define DEBUGTICS

/*
=============================================================================

						 LOCAL CONSTANTS

=============================================================================
*/

// the door is the last picture before the sprites
#define DOORWALL	(PMSpriteStart-8)

#define ACTORSIZE	0x4000

/*
=============================================================================

						 GLOBAL VARIABLES

=============================================================================
*/


#ifdef DEBUGWALLS
unsigned screenloc[3]= {0,0,0};
#else
unsigned screenloc[3]= {PAGE1START,PAGE2START,PAGE3START};
#endif
unsigned freelatch = FREESTART;

int32_t 	lasttimecount;
int32_t 	frameon;

unsigned	wallheight[MAXVIEWWIDTH];

fixed	tileglobal	= TILEGLOBAL;
fixed	mindist		= MINDIST;

/*
=============================================================================

					THE VIEW AT A HIGHER RESOLUTION

	The DOS game drew its view into the VGA page at 320x200's own pixels,
	and so does everything else here. The view alone can be drawn sharper:
	at renderscale times as many pixels each way, into renderbuf, with the
	projection and the scalers made for that size (SetViewSize). While it
	is drawn, viewwidth and viewheight are the larger size (RenderDims), so
	the raycaster and the scalers, which go by them, need nothing else.

	The frame is then shrunk into the VGA page as it would have been drawn
	there, so everything that draws over the view, reads it or fizzles it
	in goes on as before; and sys_x11.c shows the larger frame instead of
	the page's view wherever the page still matches what was put there.

	What the game decides by stays at 320 wide: an actor's viewx -- where
	the gun aims and which way a guard is seen to face -- is worked out as
	it always was, with logicscale; drawx is where it is drawn. Which tiles
	are seen, and so which actors may be shot (FL_VISABLE), comes from the
	original 320 rays, cast first without drawing (tracing), so a demo plays
	out the same at any resolution.

=============================================================================
*/

int			renderscale = 1;
int			rviewwidth,rviewheight;
int			rcenterx;
fixed		logicscale;
boolean		rendering;
byte		*renderbuf;
static	byte	*renderlow;
int			lowpixelangle[320];	// the rays at 320 wide, which the game sees by
static	boolean	tracing;		// casting them, drawing nothing
static	int		lowviewwidth,lowviewheight;

void RenderDims (boolean drawing)
{
	if (drawing)
	{
		lowviewwidth = viewwidth;
		lowviewheight = viewheight;
		viewwidth = rviewwidth;
		viewheight = rviewheight;
	}
	else
	{
		viewwidth = lowviewwidth;
		viewheight = lowviewheight;
	}
}

static void RenderBegin (void)
{
	renderbuf = SYS_RenderBuffer (bufferofs-screenofs,renderscale,
		viewwidth,viewheight,&renderlow);
	rendering = true;
	RenderDims (true);
}

static void RenderEnd (void)
{
	int		x,y,k = renderscale;
	byte	*row,*low;

	RenderDims (false);
	rendering = false;

	// The VGA page's view, as it would have been drawn at 320x200
	for (y=0;y<viewheight;y++)
	{
		row = renderbuf + y*k*rviewwidth;
		low = renderlow + y*viewwidth;
		for (x=0;x<viewwidth;x++)
			low[x] = *VGA_Pixel (bufferofs,SCREENBWIDE,x,y) = row[x*k];
	}

	SYS_RenderDone (bufferofs-screenofs,(320-viewwidth)/2,
		(200-STATUSLINES-viewheight)/2);
}


//
// math tables
//
int			pixelangle[MAXVIEWWIDTH];
int32_t		far finetangent[FINEANGLES/4];
fixed 		far sintable[ANGLES+ANGLES/4],far *costable = sintable+(ANGLES/4);

//
// refresh variables
//
fixed	viewx,viewy;			// the focal point
int		viewangle;
fixed	viewsin,viewcos;



fixed	FixedByFrac (fixed a, fixed b);
void	TransformActor (objtype *ob);
void	BuildTables (void);
void	ClearScreen (void);
int		CalcRotate (objtype *ob);
void	DrawScaleds (void);
void	CalcTics (void);
void	FixOfs (void);
void	ThreeDRefresh (void);



//
// wall optimization variables
//
int		lastside;		// true for vertical
int32_t	lastintercept;
int		lasttilehit;


//
// ray tracing variables
//
int			focaltx,focalty,viewtx,viewty;

int			midangle,angle;
unsigned	xpartial,ypartial;
unsigned	xpartialup,xpartialdown,ypartialup,ypartialdown;
unsigned	xinttile,yinttile;

unsigned	tilehit;
unsigned	pixx;

int		xtile,ytile;
int		xtilestep,ytilestep;
int32_t	xintercept,yintercept;
int32_t	xstep,ystep;

int		horizwall[MAXWALLTILES],vertwall[MAXWALLTILES];


/*
=============================================================================

						 LOCAL VARIABLES

=============================================================================
*/


void AsmRefresh (void);			// was WL_DR_A.ASM

/*
============================================================================

			   3 - D  DEFINITIONS

============================================================================
*/


//==========================================================================


/*
========================
=
= FixedByFrac
=
= multiply a 16/16 bit, 2's complement fixed point number by a 16 bit
= fraction, passed as a signed magnitude 32 bit number
=
========================
*/

fixed FixedByFrac (fixed a, fixed b)
{
	uint32_t	ua,r;
	int			negative;

//
// sign of result = sign of fraction, which is in the top bit; the low 16
// bits are its magnitude
//
	negative = (b & 0x80000000) != 0;

	if (a < 0)
	{
		ua = -(uint32_t)a;
		negative = !negative;
	}
	else
		ua = a;

//
// The assembly multiplied the two halves of a by the fraction separately
// and kept the top 32 bits of the 48, which is this
//
	r = (uint32_t)(((uint64_t)ua * (b & 0xffff)) >> 16);

	return negative ? -(int32_t)r : (int32_t)r;
}

/*
========================
=
= HeightDiv
=
= heightnumerator/(nx>>8), as the assembly did it: a 32-bit by 16-bit IDIV
= of bytes 1 and 2 of nx, keeping the 16-bit quotient
=
========================
*/

static int HeightDiv (fixed nx)
{
	int16_t	divisor = (int16_t)(nx >> 8);

	if (!divisor)
		return 0x7fff;
	return (int16_t)(heightnumerator / divisor);
}

/*
========================
=
= ProjectX
=
= centerx + ny*scale/nx, the product kept to 32 bits as it was
=
========================
*/

static int ProjectX (fixed ny, fixed nx)
{
	return centerx + (int32_t)((uint32_t)ny * (uint32_t)logicscale) / nx;
}

//
// Where across the view it is drawn: at 320 wide the same, and above that
// worked out in 64 bits, which the larger scale needs
//
static int DrawX (fixed ny, fixed nx)
{
	if (renderscale == 1)
		return rcenterx + (int32_t)((uint32_t)ny * (uint32_t)scale) / nx;
	return rcenterx + (int)(((int64_t)ny * scale) / nx);
}

//==========================================================================

/*
========================
=
= TransformActor
=
= Takes paramaters:
=   gx,gy		: globalx/globaly of point
=
= globals:
=   viewx,viewy		: point of view
=   viewcos,viewsin	: sin/cos of viewangle
=   scale		: conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
========================
*/


//
// transform actor
//
void TransformActor (objtype *ob)
{
	int ratio;
	fixed gx,gy,gxt,gyt,nx,ny;
	int32_t	temp;

//
// translate point to view centered coordinates
//
	gx = ob->x-viewx;
	gy = ob->y-viewy;

//
// calculate newx
//
	gxt = FixedByFrac(gx,viewcos);
	gyt = FixedByFrac(gy,viewsin);
	nx = gxt-gyt-ACTORSIZE;		// fudge the shape forward a bit, because
								// the midpoint could put parts of the shape
								// into an adjacent wall

//
// calculate newy
//
	gxt = FixedByFrac(gx,viewsin);
	gyt = FixedByFrac(gy,viewcos);
	ny = gyt+gxt;

//
// calculate perspective ratio
//
	ob->transx = nx;
	ob->transy = ny;

	if (nx<mindist)			// too close, don't overflow the divide
	{
	  ob->viewheight = 0;
	  return;
	}

	ob->viewx = ProjectX (ny,nx);
	ob->drawx = DrawX (ny,nx);

//
// calculate height (heightnumerator/(nx>>8))
//
	ob->viewheight = HeightDiv (nx);
}

//==========================================================================

/*
========================
=
= TransformTile
=
= Takes paramaters:
=   tx,ty		: tile the object is centered in
=
= globals:
=   viewx,viewy		: point of view
=   viewcos,viewsin	: sin/cos of viewangle
=   scale		: conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
= Returns true if the tile is withing getting distance
=
========================
*/

boolean TransformTile (int tx, int ty, int *dispx, int *dispheight)
{
	int ratio;
	fixed gx,gy,gxt,gyt,nx,ny;
	int32_t	temp;

//
// translate point to view centered coordinates
//
	gx = ((int32_t)tx<<TILESHIFT)+0x8000-viewx;
	gy = ((int32_t)ty<<TILESHIFT)+0x8000-viewy;

//
// calculate newx
//
	gxt = FixedByFrac(gx,viewcos);
	gyt = FixedByFrac(gy,viewsin);
	nx = gxt-gyt-0x2000;		// 0x2000 is size of object

//
// calculate newy
//
	gxt = FixedByFrac(gx,viewsin);
	gyt = FixedByFrac(gy,viewcos);
	ny = gyt+gxt;


//
// calculate perspective ratio
//
	if (nx<mindist)			// too close, don't overflow the divide
	{
		*dispheight = 0;
		return false;
	}

	*dispx = DrawX (ny,nx);

//
// calculate height (heightnumerator/(nx>>8))
//
	*dispheight = HeightDiv (nx);

//
// see if it should be grabbed
//
	if (nx<TILEGLOBAL && ny>-TILEGLOBAL/2 && ny<TILEGLOBAL/2)
		return true;
	else
		return false;
}

//==========================================================================

/*
====================
=
= CalcHeight
=
= Calculates the height of xintercept,yintercept from viewx,viewy
=
====================
*/

int	CalcHeight (void)
{
	int	transheight;
	int ratio;
	fixed gxt,gyt,nx,ny;
	int32_t	gx,gy;

	gx = xintercept-viewx;
	gxt = FixedByFrac(gx,viewcos);

	gy = yintercept-viewy;
	gyt = FixedByFrac(gy,viewsin);

	nx = gxt-gyt;

  //
  // calculate perspective ratio (heightnumerator/(nx>>8))
  //
	if (nx<mindist)
		nx=mindist;			// don't let divide overflow

	return HeightDiv (nx);
}


//==========================================================================

/*
===================
=
= ScalePost
=
===================
*/

byte		*postpage;
unsigned	postsource;
unsigned	postx;
unsigned	postwidth;

void	near ScalePost (void)		// VGA version
{
	int			height;

	if (tracing)
		return;

	height = wallheight[postx] >> 3;	// low 3 bits fractional
	if (height > maxscale)
		height = maxscale;

	//
	// scale a strip of wall postwidth pixels wide from the column at
	// postsource in the wall's page
	//
	slinex = postx;
	slinewidth = postwidth;
	ScaleColumn (scaledirectory[height],postpage,postsource,0,64);
}

void  FarScalePost (void)				// just so other files can call
{
	ScalePost ();
}


/*
====================
=
= HitVertWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitVertWall (void)
{
	int			wallpic;
	unsigned	texture;

	texture = (yintercept>>4)&0xfc0;
	if (xtilestep == -1)
	{
		texture = 0xfc0-texture;
		xintercept += TILEGLOBAL;
	}
	wallheight[pixx] = CalcHeight();

	if (lastside==1 && lastintercept == xtile && lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lastside = true;
		lastintercept = xtile;

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		if (tilehit & 0x40)
		{								// check for adjacent doors
			ytile = yintercept>>TILESHIFT;
			if ( tilemap[xtile-xtilestep][ytile]&0x80 )
				wallpic = DOORWALL+3;
			else
				wallpic = vertwall[tilehit & ~0x40];
		}
		else
			wallpic = vertwall[tilehit];

		postpage = PM_GetPage(wallpic);
		postsource = texture;

	}
}


/*
====================
=
= HitHorizWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitHorizWall (void)
{
	int			wallpic;
	unsigned	texture;

	texture = (xintercept>>4)&0xfc0;
	if (ytilestep == -1)
		yintercept += TILEGLOBAL;
	else
		texture = 0xfc0-texture;
	wallheight[pixx] = CalcHeight();

	if (lastside==0 && lastintercept == ytile && lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lastside = 0;
		lastintercept = ytile;

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		if (tilehit & 0x40)
		{								// check for adjacent doors
			xtile = xintercept>>TILESHIFT;
			if ( tilemap[xtile][ytile-ytilestep]&0x80 )
				wallpic = DOORWALL+2;
			else
				wallpic = horizwall[tilehit & ~0x40];
		}
		else
			wallpic = horizwall[tilehit];

		postpage = PM_GetPage(wallpic);
		postsource = texture;
	}

}

//==========================================================================

/*
====================
=
= HitHorizDoor
=
====================
*/

void HitHorizDoor (void)
{
	unsigned	texture,doorpage,doornum;

	doornum = tilehit&0x7f;
	texture = ( (xintercept-doorposition[doornum]) >> 4) &0xfc0;

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
	// in the same door as last time, so check for optimized draw
		if (texture == postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();			// draw last post
	// first pixel in this door
		lastside = 2;
		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		switch (doorobjlist[doornum].lock)
		{
		case dr_normal:
			doorpage = DOORWALL;
			break;
		case dr_lock1:
		case dr_lock2:
		case dr_lock3:
		case dr_lock4:
			doorpage = DOORWALL+6;
			break;
		case dr_elevator:
			doorpage = DOORWALL+4;
			break;
		}

		postpage = PM_GetPage(doorpage);
		postsource = texture;
	}
}

//==========================================================================

/*
====================
=
= HitVertDoor
=
====================
*/

void HitVertDoor (void)
{
	unsigned	texture,doorpage,doornum;

	doornum = tilehit&0x7f;
	texture = ( (yintercept-doorposition[doornum]) >> 4) &0xfc0;

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
	// in the same door as last time, so check for optimized draw
		if (texture == postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();			// draw last post
	// first pixel in this door
		lastside = 2;
		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		switch (doorobjlist[doornum].lock)
		{
		case dr_normal:
			doorpage = DOORWALL;
			break;
		case dr_lock1:
		case dr_lock2:
		case dr_lock3:
		case dr_lock4:
			doorpage = DOORWALL+6;
			break;
		case dr_elevator:
			doorpage = DOORWALL+4;
			break;
		}

		postpage = PM_GetPage(doorpage+1);
		postsource = texture;
	}
}

//==========================================================================


/*
====================
=
= HitHorizPWall
=
= A pushable wall in action has been hit
=
====================
*/

void HitHorizPWall (void)
{
	int			wallpic;
	unsigned	texture,offset;

	texture = (xintercept>>4)&0xfc0;
	offset = pwallpos<<10;
	if (ytilestep == -1)
		yintercept += TILEGLOBAL-offset;
	else
	{
		texture = 0xfc0-texture;
		yintercept += offset;
	}

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		wallpic = horizwall[tilehit&63];

		postpage = PM_GetPage(wallpic);
		postsource = texture;
	}

}


/*
====================
=
= HitVertPWall
=
= A pushable wall in action has been hit
=
====================
*/

void HitVertPWall (void)
{
	int			wallpic;
	unsigned	texture,offset;

	texture = (yintercept>>4)&0xfc0;
	offset = pwallpos<<10;
	if (xtilestep == -1)
	{
		xintercept += TILEGLOBAL-offset;
		texture = 0xfc0-texture;
	}
	else
		xintercept += offset;

	wallheight[pixx] = CalcHeight();

	if (lasttilehit == tilehit)
	{
		// in the same wall type as last time, so check for optimized draw
		if (texture == postsource)
		{
		// wide scale
			postwidth++;
			wallheight[pixx] = wallheight[pixx-1];
			return;
		}
		else
		{
			ScalePost ();
			postsource = texture;
			postwidth = 1;
			postx = pixx;
		}
	}
	else
	{
	// new wall
		if (lastside != -1)				// if not the first scaled post
			ScalePost ();

		lasttilehit = tilehit;
		postx = pixx;
		postwidth = 1;

		wallpic = vertwall[tilehit&63];

		postpage = PM_GetPage(wallpic);
		postsource = texture;
	}

}

//==========================================================================

//==========================================================================

#if 0
/*
=====================
=
= ClearScreen
=
=====================
*/

void ClearScreen (void)
{
 unsigned floor=egaFloor[gamestate.episode*10+mapon],
	  ceiling=egaCeiling[gamestate.episode*10+mapon];

  //
  // clear the screen
  //
asm	mov	dx,GC_INDEX
asm	mov	ax,GC_MODE + 256*2		// read mode 0, write mode 2
asm	out	dx,ax
asm	mov	ax,GC_BITMASK + 255*256
asm	out	dx,ax

asm	mov	dx,40
asm	mov	ax,[viewwidth]
asm	shr	ax,3
asm	sub	dx,ax					// dx = 40-viewwidth/8

asm	mov	bx,[viewwidth]
asm	shr	bx,4					// bl = viewwidth/16
asm	mov	bh,BYTE PTR [viewheight]
asm	shr	bh,1					// half height

asm	mov	ax,[ceiling]
asm	mov	es,[screenseg]
asm	mov	di,[bufferofs]

toploop:
asm	mov	cl,bl
asm	rep	stosw
asm	add	di,dx
asm	dec	bh
asm	jnz	toploop

asm	mov	bh,BYTE PTR [viewheight]
asm	shr	bh,1					// half height
asm	mov	ax,[floor]

bottomloop:
asm	mov	cl,bl
asm	rep	stosw
asm	add	di,dx
asm	dec	bh
asm	jnz	bottomloop


asm	mov	dx,GC_INDEX
asm	mov	ax,GC_MODE + 256*10		// read mode 1, write mode 2
asm	out	dx,ax
asm	mov	al,GC_BITMASK
asm	out	dx,al

}
#endif
//==========================================================================

unsigned vgaCeiling[]=
{
#ifndef SPEAR
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0xbfbf,
 0x4e4e,0x4e4e,0x4e4e,0x1d1d,0x8d8d,0x4e4e,0x1d1d,0x2d2d,0x1d1d,0x8d8d,
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x2d2d,0xdddd,0x1d1d,0x1d1d,0x9898,

 0x1d1d,0x9d9d,0x2d2d,0xdddd,0xdddd,0x9d9d,0x2d2d,0x4d4d,0x1d1d,0xdddd,
 0x7d7d,0x1d1d,0x2d2d,0x2d2d,0xdddd,0xd7d7,0x1d1d,0x1d1d,0x1d1d,0x2d2d,
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0xdddd,0xdddd,0x7d7d,0xdddd,0xdddd,0xdddd
#else
 0x6f6f,0x4f4f,0x1d1d,0xdede,0xdfdf,0x2e2e,0x7f7f,0x9e9e,0xaeae,0x7f7f,
 0x1d1d,0xdede,0xdfdf,0xdede,0xdfdf,0xdede,0xe1e1,0xdcdc,0x2e2e,0x1d1d,0xdcdc
#endif
};

/*
=====================
=
= VGAClearScreen
=
=====================
*/

void VGAClearScreen (void)
{
	unsigned ceiling=vgaCeiling[gamestate.episode*10+mapon] & 0xff;
	int		y;

  //
  // clear the screen: the ceiling colour over the top half of the view and
  // the floor's, 0x19, over the bottom
  //
	if (rendering)
	{
		memset (renderbuf,ceiling,viewwidth*(viewheight/2));
		memset (renderbuf+viewwidth*(viewheight/2),0x19,viewwidth*(viewheight/2));
		return;
	}
	for (y=0;y<viewheight/2;y++)
		memset (VGA_Pixel (bufferofs,SCREENBWIDE,0,y),ceiling,viewwidth);
	for (;y<(viewheight/2)*2;y++)
		memset (VGA_Pixel (bufferofs,SCREENBWIDE,0,y),0x19,viewwidth);
}

//==========================================================================

/*
=====================
=
= CalcRotate
=
=====================
*/

int	CalcRotate (objtype *ob)
{
	int	angle,viewangle;

	// this isn't exactly correct, as it should vary by a trig value,
	// but it is close enough with only eight rotations

	viewangle = player->angle + (centerx - ob->viewx)/8;

	if (ob->obclass == rocketobj || ob->obclass == hrocketobj)
		angle =  (viewangle-180)- ob->angle;
	else
		angle =  (viewangle-180)- dirangle[ob->dir];

	angle+=ANGLES/16;
	while (angle>=ANGLES)
		angle-=ANGLES;
	while (angle<0)
		angle+=ANGLES;

	if (ob->state->rotate == 2)             // 2 rotation pain frame
		return 4*(angle/(ANGLES/2));        // seperated by 3 (art layout...)

	return angle/(ANGLES/8);
}


/*
=====================
=
= DrawScaleds
=
= Draws all objects that are visable
=
=====================
*/

#define MAXVISABLE	50

typedef struct
{
	int	viewx,
		viewheight,
		shapenum;
} visobj_t;

visobj_t	vislist[MAXVISABLE],*visptr,*visstep,*farthest;

void DrawScaleds (void)
{
	int 		i,j,least,numvisable,height;
	memptr		shape;
	byte		*tilespot,*visspot;
	int			shapenum;
	unsigned	spotloc;

	statobj_t	*statptr;
	objtype		*obj;

	visptr = &vislist[0];

//
// place static objects
//
	for (statptr = &statobjlist[0] ; statptr !=laststatobj ; statptr++)
	{
		if ((visptr->shapenum = statptr->shapenum) == -1)
			continue;						// object has been deleted

		if (!*statptr->visspot)
			continue;						// not visable

		if (TransformTile (statptr->tilex,statptr->tiley
			,&visptr->viewx,&visptr->viewheight) && statptr->flags & FL_BONUS)
		{
			GetBonus (statptr);
			continue;
		}

		if (!visptr->viewheight)
			continue;						// to close to the object

		if (visptr < &vislist[MAXVISABLE-1])	// don't let it overflow
			visptr++;
	}

//
// place active objects
//
	for (obj = player->next;obj;obj=obj->next)
	{
		if (!(visptr->shapenum = obj->state->shapenum))
			continue;						// no shape

		spotloc = (obj->tilex<<6)+obj->tiley;	// optimize: keep in struct?
		visspot = &spotvis[0][0]+spotloc;
		tilespot = &tilemap[0][0]+spotloc;

		//
		// could be in any of the nine surrounding tiles
		//
		if (*visspot
		|| ( *(visspot-1) && !*(tilespot-1) )
		|| ( *(visspot+1) && !*(tilespot+1) )
		|| ( *(visspot-65) && !*(tilespot-65) )
		|| ( *(visspot-64) && !*(tilespot-64) )
		|| ( *(visspot-63) && !*(tilespot-63) )
		|| ( *(visspot+65) && !*(tilespot+65) )
		|| ( *(visspot+64) && !*(tilespot+64) )
		|| ( *(visspot+63) && !*(tilespot+63) ) )
		{
			obj->active = true;
			TransformActor (obj);
			if (!obj->viewheight)
				continue;						// too close or far away

			visptr->viewx = obj->drawx;
			visptr->viewheight = obj->viewheight;
			if (visptr->shapenum == -1)
				visptr->shapenum = obj->temp1;	// special shape

			if (obj->state->rotate)
				visptr->shapenum += CalcRotate (obj);

			if (visptr < &vislist[MAXVISABLE-1])	// don't let it overflow
				visptr++;
			obj->flags |= FL_VISABLE;
		}
		else
			obj->flags &= ~FL_VISABLE;
	}

//
// draw from back to front
//
	numvisable = visptr-&vislist[0];

	if (!numvisable)
		return;									// no visable objects

	for (i = 0; i<numvisable; i++)
	{
		least = 32000;
		for (visstep=&vislist[0] ; visstep<visptr ; visstep++)
		{
			height = visstep->viewheight;
			if (height < least)
			{
				least = height;
				farthest = visstep;
			}
		}
		//
		// draw farthest
		//
		ScaleShape(farthest->viewx,farthest->shapenum,farthest->viewheight);

		farthest->viewheight = 32000;
	}

}

//==========================================================================

/*
==============
=
= DrawPlayerWeapon
=
= Draw the player's hands
=
==============
*/

int	weaponscale[NUMWEAPONS] = {SPR_KNIFEREADY,SPR_PISTOLREADY
	,SPR_MACHINEGUNREADY,SPR_CHAINREADY};

void DrawPlayerWeapon (void)
{
	int	shapenum;

#ifndef SPEAR
	if (gamestate.victoryflag)
	{
		if (player->state == &s_deathcam && (TimeCount&32) )
			SimpleScaleShape(viewwidth/2,SPR_DEATHCAM,viewheight+1);
		return;
	}
#endif

	if (gamestate.weapon != -1)
	{
		shapenum = weaponscale[gamestate.weapon]+gamestate.weaponframe;
		SimpleScaleShape(viewwidth/2,shapenum,viewheight+1);
	}

	if (demorecord || demoplayback)
		SimpleScaleShape(viewwidth/2,SPR_DEMO,viewheight+1);
}


//==========================================================================


/*
=====================
=
= CalcTics
=
=====================
*/

void CalcTics (void)
{
	int32_t	newtime,oldtimecount;

//
// calculate tics since last refresh for adaptive timing
//
	if (lasttimecount > TimeCount)
		TimeCount = lasttimecount;		// if the game was paused a LONG time

	for (;;)
	{
		newtime = TimeCount;
		tics = newtime-lasttimecount;
		if (tics)
			break;				// make sure at least one tic passes
		SYS_Spin ();
	}

	lasttimecount = newtime;

#ifdef FILEPROFILE
		strcpy (scratch,"\tTics:");
		itoa (tics,str,10);
		strcat (scratch,str);
		strcat (scratch,"\n");
		write (profilehandle,scratch,strlen(scratch));
#endif

	if (tics>MAXTICS)
	{
		TimeCount -= (tics-MAXTICS);
		tics = MAXTICS;
	}
}


//==========================================================================


/*
========================
=
= FixOfs
=
========================
*/

void	FixOfs (void)
{
	VW_ScreenToScreen (displayofs,bufferofs,viewwidth/8,viewheight);
}


//==========================================================================


/*
=============================================================================

								ASMREFRESH

	WL_DR_A.ASM, in C. For each column of the view, follow the ray through
	that pixel across the map until it hits a wall, a door or a pushwall,
	and hand the hit to HitVertWall and the rest to draw.

	The ray is stepped across two sets of grid lines at once: the vertical
	ones (x = whole tiles), where yintercept says how far up it has got, and
	the horizontal ones, where xintercept says how far across. Whichever is
	behind is advanced next. The assembly patched a JLE or a JGE into its
	own code to make that comparison the right way round for the direction
	of the ray; the comparisons here are chosen the same way.

	It kept the whole-tile halves of the intercepts in registers as 16-bit
	numbers, and indexed the map with (tile<<6)+tile in 16 bits too. Both
	are kept: the tiles as int16_t, the map index as a word. The partial
	products are the assembly's: the magnitude of a step times a 16-bit
	fraction, keeping the top 32 bits, the sign put back afterwards.

=============================================================================
*/

#define DEG90	900
#define DEG180	1800
#define DEG270	2700
#define DEG360	3600

static int32_t PartialByStep (int32_t step, word partial)
{
	uint32_t	u;

	if (step < 0)
	{
		u = (uint32_t)(((uint64_t)(-(uint32_t)step) * partial) >> 16);
		return -(int32_t)u;
	}
	u = (uint32_t)(((uint64_t)(uint32_t)step * partial) >> 16);
	return (int32_t)u;
}

// The map, read where a ray has got to. Every map has a border of solid
// walls, so a ray never leaves it; one that somehow did would have read
// whatever followed tilemap on DOS, and here stops at a wall instead.
static byte MapAt (word spot)
{
	if (spot >= MAPSIZE*MAPSIZE)
		return 1;
	return ((byte *)tilemap)[spot];
}

static void SeeSpot (word spot)
{
	if (renderscale > 1 && !tracing)	// seen by the 320 rays alone
		return;
	if (spot < MAPSIZE*MAPSIZE)
		((byte *)spotvis)[spot] = 1;
}

void AsmRefresh (void)
{
	int			angle;
	word		xpartial,ypartial;
	int32_t		xint,yint;		// the intercepts, as the registers had them
	int16_t		xinttile,yinttile;
	word		xspot,yspot;
	boolean		horizge,vertge;	// the patched JGEs; JLE when false
	int32_t		mid;
	word		doornum;

	for (pixx=0;pixx<viewwidth;pixx++)
	{
		angle = midangle + (tracing ? lowpixelangle : pixelangle)[pixx];
		if (angle < 0)
			angle += FINEANGLES;			// -90 is the same as 270
		else if (angle >= DEG360)
			angle -= FINEANGLES;			// 449 is the same as 89

		if (angle < DEG90)
		{
			xtilestep = 1;
			ytilestep = -1;
			horizge = true;
			vertge = false;
			xstep = finetangent[DEG90-1-angle];
			ystep = -finetangent[angle];
			xpartial = xpartialup;
			ypartial = ypartialdown;
		}
		else if (angle < DEG180)
		{
			xtilestep = -1;
			ytilestep = -1;
			horizge = false;
			vertge = false;
			xstep = -finetangent[angle-DEG90];
			ystep = -finetangent[DEG180-1-angle];
			xpartial = xpartialdown;
			ypartial = ypartialdown;
		}
		else if (angle < DEG270)
		{
			xtilestep = -1;
			ytilestep = 1;
			horizge = false;
			vertge = true;
			xstep = -finetangent[DEG270-1-angle];
			ystep = finetangent[angle-DEG180];
			xpartial = xpartialdown;
			ypartial = ypartialup;
		}
		else
		{
			xtilestep = 1;
			ytilestep = 1;
			horizge = true;
			vertge = true;
			xstep = finetangent[angle-DEG270];
			ystep = finetangent[DEG360-1-angle];
			xpartial = xpartialup;
			ypartial = ypartialup;
		}

	//
	// initialise variables for intersection testing
	//
		yint = PartialByStep (ystep,xpartial) + viewy;
		yinttile = (int16_t)(yint >> 16);
		xtile = focaltx + xtilestep;
		xspot = (word)((xtile<<6) + yinttile);

		xint = PartialByStep (xstep,ypartial) + viewx;
		xinttile = (int16_t)(xint >> 16);
		ytile = focalty + ytilestep;
		yspot = (word)((xinttile<<6) + ytile);

	//
	// trace along this angle until we hit a wall
	//
		goto vertcheck;

vertcheck:
		if (vertge ? yinttile >= ytile : yinttile <= ytile)
			goto horizentry;
vertentry:
		if (MapAt (xspot))
			goto hitvert;
passvert:
		SeeSpot (xspot);
		xtile += xtilestep;
		yint += ystep;
		yinttile = (int16_t)(yint >> 16);
		xspot = (word)((xtile<<6) + yinttile);
		goto vertcheck;

horizcheck:
		if (horizge ? xinttile >= xtile : xinttile <= xtile)
			goto vertentry;
horizentry:
		if (MapAt (yspot))
			goto hithoriz;
passhoriz:
		SeeSpot (yspot);
		ytile += ytilestep;
		xint += xstep;
		xinttile = (int16_t)(xint >> 16);
		yspot = (word)((xinttile<<6) + ytile);
		goto horizcheck;

hitvert:
		tilehit = MapAt (xspot);
		if (tilehit & 0x80)
			goto vertdoor;
		xintercept = (int32_t)xtile << 16;
		yintercept = yint;
		ytile = yinttile;
		HitVertWall ();
		continue;

hithoriz:
		tilehit = MapAt (yspot);
		if (tilehit & 0x80)
			goto horizdoor;
		xintercept = xint;
		xtile = xinttile;
		yintercept = (int32_t)ytile << 16;
		HitHorizWall ();
		continue;

	//
	// hit a special horizontal wall, so find which coordinate a door would
	// be intersected at, and check to see if the door is open past that point
	//
horizdoor:
		yintercept = yint;
		if (tilehit & 0x40)
		{
		//
		// hit a sliding horizontal wall: pwallpos (0-63) of the way across
		//
			mid = xint + ((int32_t)((uint32_t)xstep * pwallpos) >> 6);
			if ((int16_t)(mid >> 16) != xinttile)
				goto passhoriz;			// it hit the side
			xintercept = mid;
			yintercept = (int32_t)ytile << 16;
			HitHorizPWall ();
			continue;
		}

		doornum = tilehit & 0x7f;
		mid = xint + (xstep >> 1);		// half a step gets to the door
		if ((int16_t)(mid >> 16) != xinttile)
			goto passhoriz;				// it hit the side of the wall before a door
		if ((word)mid < doorposition[doornum])
			goto passhoriz;				// the door is open that much
		xintercept = mid;
		yintercept = ((int32_t)ytile << 16) | 0x8000;	// in the middle of the tile
		HitHorizDoor ();
		continue;

	//
	// hit a special vertical wall, so find which coordinate a door would be
	// intersected at, and check to see if the door is open past that point
	//
vertdoor:
		yintercept = yint;
		if (tilehit & 0x40)
		{
			mid = yint + ((int32_t)((uint32_t)ystep * pwallpos) >> 6);
			if ((int16_t)(mid >> 16) != yinttile)
				goto passvert;
			yintercept = mid;
			xintercept = (int32_t)xtile << 16;
			HitVertPWall ();
			continue;
		}

		doornum = tilehit & 0x7f;
		mid = yint + (ystep >> 1);
		if ((int16_t)(mid >> 16) != yinttile)
			goto passvert;
		if ((word)mid < doorposition[doornum])
			goto passvert;
		yintercept = mid;
		xintercept = ((int32_t)xtile << 16) | 0x8000;
		HitVertDoor ();
		continue;
	}
}

//==========================================================================

/*
====================
=
= WallRefresh
=
====================
*/

void WallRefresh (void)
{
//
// set up variables for this view
//
	viewangle = player->angle;
	midangle = viewangle*(FINEANGLES/ANGLES);
	viewsin = sintable[viewangle];
	viewcos = costable[viewangle];
	viewx = player->x - FixedByFrac(focallength,viewcos);
	viewy = player->y + FixedByFrac(focallength,viewsin);

	focaltx = viewx>>TILESHIFT;
	focalty = viewy>>TILESHIFT;

	viewtx = player->x >> TILESHIFT;
	viewty = player->y >> TILESHIFT;

	xpartialdown = viewx&(TILEGLOBAL-1);
	xpartialup = TILEGLOBAL-xpartialdown;
	ypartialdown = viewy&(TILEGLOBAL-1);
	ypartialup = TILEGLOBAL-ypartialdown;

	lastside = -1;			// the first pixel is on a new wall
	AsmRefresh ();
	ScalePost ();			// no more optimization on last post
}

//==========================================================================


/*
=============================================================================

								THE AUTOMAP

	DOOM's map, on Tab: the walls of every place seen so far, the doors,
	and the player's arrow, north up and following the player, drawn over
	the view while the game goes on. + and - zoom it.

	It is drawn last, over the view the game has just drawn -- so the view
	is worked out as ever, and what the game decides by it (FL_VISABLE)
	does not change with the map open -- and at the view's resolution.

	A place is seen once the rays have crossed it (spotvis). Which places
	have been seen is not kept in a savegame; the map of a loaded game
	starts from where the player stands.

=============================================================================
*/

boolean		automapon;
byte		mapseen[MAPSIZE][MAPSIZE];

#define AM_BACK		0		// black
#define AM_WALL		0x20	// red
#define AM_DOOR		0x47	// yellow
#define AM_GOLD		0xa4	// a door the gold key opens
#define AM_SILVER	0x89	// and the silver
#define AM_PLAYER	0x0f	// white

// Tiles' sizes on the map, in 320x200's pixels
static	int		mapzooms[] = {3,4,6,8,12,16};
#define	NUMZOOMS	(sizeof(mapzooms)/sizeof(mapzooms[0]))
static	int		mapzoom = 3;

void AutomapSetup (void)
{
	automapon = false;
	memset (mapseen,0,sizeof(mapseen));
}

void AutomapKeys (void)
{
	static boolean	tabheld,zoomheld;
	boolean			in,out;

	if (Keyboard[sc_Tab])
	{
		if (!tabheld)
			automapon ^= true;
		tabheld = true;
	}
	else
		tabheld = false;

	in = Keyboard[0x0d] || Keyboard[0x4e];		// = or keypad +
	out = Keyboard[0x0c] || Keyboard[0x4a];		// - or keypad -
	if (automapon && (in || out))
	{
		if (!zoomheld)
		{
			if (in && mapzoom < NUMZOOMS-1)
				mapzoom++;
			if (out && mapzoom > 0)
				mapzoom--;
		}
		zoomheld = true;
	}
	else
		zoomheld = false;
}

static void MapBar (int x, int y, int w, int h, byte color)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > viewwidth)
		w = viewwidth - x;
	if (y + h > viewheight)
		h = viewheight - y;
	if (w <= 0 || h <= 0)
		return;
	for (;h--;y++)
		memset (renderbuf + y*viewwidth + x,color,w);
}

static void MapLine (int x1, int y1, int x2, int y2, int thick, byte color)
{
	int	dx = abs (x2-x1), sx = x1 < x2 ? 1 : -1;
	int	dy = -abs (y2-y1), sy = y1 < y2 ? 1 : -1;
	int	err = dx+dy,e2;

	for (;;)
	{
		MapBar (x1-thick/2,y1-thick/2,thick,thick,color);
		if (x1 == x2 && y1 == y2)
			break;
		e2 = 2*err;
		if (e2 >= dy)
		{
			err += dy;
			x1 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y1 += sy;
		}
	}
}

static boolean MapSolid (int x, int y)
{
	byte	tile;

	if (x < 0 || y < 0 || x >= MAPSIZE || y >= MAPSIZE)
		return true;
	tile = tilemap[x][y];
	return tile && !(tile & 0x80);
}

static void DrawAutomap (void)
{
	int		ts = mapzooms[mapzoom]*renderscale;		// a tile, in pixels
	int		thick = (renderscale+1)/2;
	int		cx = viewwidth/2, cy = viewheight/2;
	int		x,y,sx,sy;
	int		px,py,ax,ay,lx,ly;
	doorobj_t	*door;
	byte	color;

	MapBar (0,0,viewwidth,viewheight,AM_BACK);

	// The map's (0,0), from the player's place in it
	sx = cx - (int)(((int64_t)player->x * ts) >> TILESHIFT);
	sy = cy - (int)(((int64_t)player->y * ts) >> TILESHIFT);

	//
	// a wall is drawn along each side it shows to a place that was seen
	//
	for (x=0;x<MAPSIZE;x++)
		for (y=0;y<MAPSIZE;y++)
		{
			int	l = sx + x*ts, t = sy + y*ts;

			if (!mapseen[x][y] || MapSolid (x,y))
				continue;
			if (l + ts < 0 || t + ts < 0 || l > viewwidth || t > viewheight)
				continue;
			if (MapSolid (x-1,y))
				MapBar (l,t,thick,ts+thick,AM_WALL);
			if (MapSolid (x+1,y))
				MapBar (l+ts,t,thick,ts+thick,AM_WALL);
			if (MapSolid (x,y-1))
				MapBar (l,t,ts+thick,thick,AM_WALL);
			if (MapSolid (x,y+1))
				MapBar (l,t+ts,ts+thick,thick,AM_WALL);
		}

	//
	// doors, across their places, coloured by the key they need
	//
	for (door = doorobjlist;door < lastdoorobj;door++)
	{
		int	l = sx + door->tilex*ts, t = sy + door->tiley*ts;

		if (!mapseen[door->tilex][door->tiley])
			continue;
		switch (door->lock)
		{
		case dr_lock1: color = AM_GOLD; break;
		case dr_lock2: color = AM_SILVER; break;
		default: color = AM_DOOR; break;
		}
		if (door->vertical)
			MapBar (l+ts/2,t,thick,ts+thick,color);
		else
			MapBar (l,t+ts/2,ts+thick,thick,color);
	}

	//
	// the player: an arrow the way the player faces, as DOOM drew it
	//
	ax = costable[player->angle];		// FRACUNIT long
	ay = -sintable[player->angle];		// the map's y is south
	lx = (int)(((int64_t)ax * ts) >> 17);	// half a tile
	ly = (int)(((int64_t)ay * ts) >> 17);
	MapLine (cx-lx,cy-ly,cx+lx,cy+ly,thick,AM_PLAYER);
	MapLine (cx+lx,cy+ly,cx+lx/3-ly/2,cy+ly/3+lx/2,thick,AM_PLAYER);
	MapLine (cx+lx,cy+ly,cx+lx/3+ly/2,cy+ly/3-lx/2,thick,AM_PLAYER);
}

/*
========================
=
= ThreeDRefresh
=
========================
*/

void	ThreeDRefresh (void)
{
	int tracedir;
	int	x,y;

//
// clear out the traced array
//
	memset (spotvis,0,sizeof(spotvis));

	bufferofs += screenofs;

//
// what the game sees by: the rays at 320 wide (see THE VIEW AT A HIGHER
// RESOLUTION, above)
//
	if (renderscale > 1)
	{
		tracing = true;
		WallRefresh ();
		tracing = false;
	}

//
// follow the walls from there to the right, drawwing as we go
//
	RenderBegin ();
	VGAClearScreen ();

	WallRefresh ();

//
// draw all the scaled images
//
	DrawScaleds();			// draw scaled stuff
	DrawPlayerWeapon ();	// draw player's hands
	for (x=0;x<MAPSIZE;x++)		// seen, for the automap
		for (y=0;y<MAPSIZE;y++)
			mapseen[x][y] |= spotvis[x][y];
	if (automapon)
		DrawAutomap ();
	RenderEnd ();

//
// show screen and time last cycle
//
	if (fizzlein)
	{
		FizzleFade(bufferofs,displayofs+screenofs,viewwidth,viewheight,20,false);
		fizzlein = false;

		lasttimecount = TimeCount = 0;		// don't make a big tic count

	}

	bufferofs -= screenofs;
	displayofs = bufferofs;

	VL_SetCRTC (displayofs);	// the high byte was all that changed

	bufferofs += SCREENSIZE;
	if (bufferofs > PAGE3START)
		bufferofs = PAGE1START;

	frameon++;
	PM_NextFrame();
}


//===========================================================================

