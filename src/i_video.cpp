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

#include <fcntl.h>
#include <linux/input.h>

// Undef name collisions between input.h and doomdef.h, cant map these without some trickery
#undef KEY_ENTER
#undef KEY_TAB
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_BACKSPACE
#undef KEY_PAUSE
#undef KEY_EQUALS
#undef KEY_MINUS

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <errno.h> // LLatta replace errnos.h
#include <signal.h>

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

const char* g_gamepadName = "/dev/input/event2";
int g_gamepad = -1;


////
////  Translates the key currently in X_event
////
//
//int xlatekey(void)
//{
//
//    int rc;
//
//    switch(rc = XKeycodeToKeysym(X_display, X_event.xkey.keycode, 0))
//    {
//      case XK_Left:	rc = KEY_LEFTARROW;	break;
//      case XK_Right:	rc = KEY_RIGHTARROW;	break;
//      case XK_Down:	rc = KEY_DOWNARROW;	break;
//      case XK_Up:	rc = KEY_UPARROW;	break;
//      case XK_Escape:	rc = KEY_ESCAPE;	break;
//      case XK_Return:	rc = KEY_ENTER;		break;
//      case XK_Tab:	rc = KEY_TAB;		break;
//      case XK_F1:	rc = KEY_F1;		break;
//      case XK_F2:	rc = KEY_F2;		break;
//      case XK_F3:	rc = KEY_F3;		break;
//      case XK_F4:	rc = KEY_F4;		break;
//      case XK_F5:	rc = KEY_F5;		break;
//      case XK_F6:	rc = KEY_F6;		break;
//      case XK_F7:	rc = KEY_F7;		break;
//      case XK_F8:	rc = KEY_F8;		break;
//      case XK_F9:	rc = KEY_F9;		break;
//      case XK_F10:	rc = KEY_F10;		break;
//      case XK_F11:	rc = KEY_F11;		break;
//      case XK_F12:	rc = KEY_F12;		break;
//
//      case XK_BackSpace:
//      case XK_Delete:	rc = KEY_BACKSPACE;	break;
//
//      case XK_Pause:	rc = KEY_PAUSE;		break;
//
//      case XK_KP_Equal:
//      case XK_equal:	rc = KEY_EQUALS;	break;
//
//      case XK_KP_Subtract:
//      case XK_minus:	rc = KEY_MINUS;		break;
//
//      case XK_Shift_L:
//      case XK_Shift_R:
//	rc = KEY_RSHIFT;
//	break;
//
//      case XK_Control_L:
//      case XK_Control_R:
//	rc = KEY_RCTRL;
//	break;
//
//      case XK_Alt_L:
//      case XK_Meta_L:
//      case XK_Alt_R:
//      case XK_Meta_R:
//	rc = KEY_RALT;
//	break;
//
//      default:
//	if (rc >= XK_space && rc <= XK_asciitilde)
//	    rc = rc - XK_space + ' ';
//	if (rc >= 'A' && rc <= 'Z')
//	    rc = rc - 'A' + 'a';
//	break;
//    }
//
//    return rc;
//
//}

void I_ShutdownGraphics(void)
{
	free(g_framebuffer);
	g_framebuffer = 0;

	if (g_gamepad >= 0)
	{
		close(g_gamepad);
	}
}



event_t joystickEvent = { ev_joystick };

void I_GetEvent(void)
{
	while (1)
	{
		struct input_event ev[64];

		int rd = read(g_gamepad, ev, sizeof(ev));

		if (rd < (int) sizeof(struct input_event))
		{
			break;
		}

		for (int i = 0; i < rd / (int)sizeof(struct input_event); i++)
		{
			//printf("Event: time %ld.%06ld, type %d, code %d,", ev[i].time.tv_sec, ev[i].time.tv_usec, ev[i].type, ev[i].code);

			input_event const& e = ev[i];

			switch (e.type)
			{
			case EV_ABS:
				if (e.code == ABS_X)
					joystickEvent.data2 = e.value < 100 ? -1 : e.value < 150 ? 0 : 1;
				else if (e.code == ABS_Y)
					joystickEvent.data3 = e.value < 100 ? -1 : e.value < 150 ? 0 : 1;

				break;
			case EV_KEY:
				switch (e.code)
				{
				case BTN_B:
					joystickEvent.data1 = (joystickEvent.data1 & ~1) | e.value;
					break;
				case BTN_C:
					joystickEvent.data1 = (joystickEvent.data1 & ~2) | (e.value << 1);
					break;
				case BTN_A:
					joystickEvent.data1 = (joystickEvent.data1 & ~4) | (e.value << 2);
					break;
				case BTN_X:
					joystickEvent.data1 = (joystickEvent.data1 & ~8) | (e.value << 3);
					break;
				}

				break;
			}

			D_PostEvent(&joystickEvent);
		}
	}
//
//    event_t event;
//
//    // put event-grabbing stuff in here
//    XNextEvent(X_display, &X_event);
//    switch (X_event.type)
//    {
//      case KeyPress:
//	event.type = ev_keydown;
//	event.data1 = xlatekey();
//	D_PostEvent(&event);
//	// fprintf(stderr, "k");
//	break;
//      case KeyRelease:
//	event.type = ev_keyup;
//	event.data1 = xlatekey();
//	D_PostEvent(&event);
//	// fprintf(stderr, "ku");
//	break;
//      case ButtonPress:
//	event.type = ev_mouse;
//	event.data1 =
//	    (X_event.xbutton.state & Button1Mask)
//	    | (X_event.xbutton.state & Button2Mask ? 2 : 0)
//	    | (X_event.xbutton.state & Button3Mask ? 4 : 0)
//	    | (X_event.xbutton.button == Button1)
//	    | (X_event.xbutton.button == Button2 ? 2 : 0)
//	    | (X_event.xbutton.button == Button3 ? 4 : 0);
//	event.data2 = event.data3 = 0;
//	D_PostEvent(&event);
//	// fprintf(stderr, "b");
//	break;
//      case ButtonRelease:
//	event.type = ev_mouse;
//	event.data1 =
//	    (X_event.xbutton.state & Button1Mask)
//	    | (X_event.xbutton.state & Button2Mask ? 2 : 0)
//	    | (X_event.xbutton.state & Button3Mask ? 4 : 0);
//	// suggest parentheses around arithmetic in operand of |
//	event.data1 =
//	    event.data1
//	    ^ (X_event.xbutton.button == Button1 ? 1 : 0)
//	    ^ (X_event.xbutton.button == Button2 ? 2 : 0)
//	    ^ (X_event.xbutton.button == Button3 ? 4 : 0);
//	event.data2 = event.data3 = 0;
//	D_PostEvent(&event);
//	// fprintf(stderr, "bu");
//	break;
//      case MotionNotify:
//	event.type = ev_mouse;
//	event.data1 =
//	    (X_event.xmotion.state & Button1Mask)
//	    | (X_event.xmotion.state & Button2Mask ? 2 : 0)
//	    | (X_event.xmotion.state & Button3Mask ? 4 : 0);
//	event.data2 = (X_event.xmotion.x - lastmousex) << 2;
//	event.data3 = (lastmousey - X_event.xmotion.y) << 2;
//
//	if (event.data2 || event.data3)
//	{
//	    lastmousex = X_event.xmotion.x;
//	    lastmousey = X_event.xmotion.y;
//	    if (X_event.xmotion.x != X_width/2 &&
//		X_event.xmotion.y != X_height/2)
//	    {
//		D_PostEvent(&event);
//		// fprintf(stderr, "m");
//		mousemoved = false;
//	    } else
//	    {
//		mousemoved = true;
//	    }
//	}
//	break;
//
//      case Expose:
//      case ConfigureNotify:
//	break;
//
//      default:
//	if (doShm && X_event.type == X_shmeventtype) shmFinished = true;
//	break;
//    }

}

//
// I_StartFrame
//
void I_StartFrame (void)
{
	I_GetEvent();
}

//
// I_StartTic
//
void I_StartTic (void)
{
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

	tft.pushColors(g_framebuffer, numPixel, true);
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


	if ((g_gamepad = open(g_gamepadName, O_RDONLY | O_NONBLOCK)) < 0)
	{
		fprintf(stderr, "Cannot access gamepad at %s\n", g_gamepadName);
	}

}

} // extern C
