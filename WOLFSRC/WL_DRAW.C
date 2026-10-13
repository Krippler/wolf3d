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
	return centerx + (int32_t)((uint32_t)ny * (uint32_t)scale) / nx;
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

	*dispx = ProjectX (ny,nx);

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

			visptr->viewx = obj->viewx;
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
		angle = midangle + pixelangle[pixx];
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
========================
=
= ThreeDRefresh
=
========================
*/

void	ThreeDRefresh (void)
{
	int tracedir;

//
// clear out the traced array
//
	memset (spotvis,0,sizeof(spotvis));

	bufferofs += screenofs;

//
// follow the walls from there to the right, drawwing as we go
//
	VGAClearScreen ();

	WallRefresh ();

//
// draw all the scaled images
//
	DrawScaleds();			// draw scaled stuff
	DrawPlayerWeapon ();	// draw player's hands

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

