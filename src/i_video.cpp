// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

extern "C"
{

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <unistd.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"

#include "Adafruit_TFTLCD/Adafruit_TFTLCD.h"

Adafruit_TFTLCD tft;

uint16_t* g_framebuffer = 0;

uint16_t g_palette[256];


void I_ShutdownGraphics(void)
{
	free(g_framebuffer);
	g_framebuffer = 0;
}




//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate (void)
{
	size_t numPixel = SCREENWIDTH * SCREENHEIGHT;

	uint16_t* p = g_framebuffer;
	uint8_t* s = screens[0];
	for (int y = 0; y < SCREENHEIGHT; ++y)
	{
		for (int x = 0; x < SCREENWIDTH; ++x)
		{
			*p++ = g_palette[*s++];
		}
	}

#ifdef PROFILE_FRAME
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	double startTime = (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;
#endif

	tft.pushColors(g_framebuffer, numPixel, true);

#ifdef PROFILE_FRAME
	clock_gettime(CLOCK_REALTIME, &t);
	double endTime = (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;

	static double lastEndTime = 0;

	printf("I_Video: Draw time %f, frame time %f\n", endTime - startTime, endTime - lastEndTime);
	lastEndTime = endTime;
#endif
}


//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}


//
// I_SetPalette
//
void I_SetPalette (byte* palette)
{
	int i;
	for (i=0 ; i<256 ; i++)
	{
		uint8_t r, g, b;
		r = gammatable[usegamma][*palette++];
		g = gammatable[usegamma][*palette++];
		b = gammatable[usegamma][*palette++];
		g_palette[i] = tft.color565(r, g, b);
	}
}

void I_InitGraphics(void)
{
	tft.reset();

	//uint16_t identifier = tft.readID();
	uint16_t identifier = 0x9341;

	if (identifier == 0x9341) {
		printf("Found ILI9341 LCD driver\n");
	} else {
		printf("Unknown LCD driver chip: 0x%x\n", identifier);
	}

	tft.begin(identifier);

	tft.setRotation(1);

	g_framebuffer = (uint16_t*) malloc (SCREENWIDTH * SCREENHEIGHT * sizeof(uint16_t));
}

} // extern C
