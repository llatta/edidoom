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
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: m_bbox.c,v 1.1 1997/02/03 22:45:10 b1 Exp $";


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>

#include <fcntl.h>
#include <linux/input.h>

// Undef name collisions between input.h and doomdef.h, can't map these without some trickery
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

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "d_main.h"
#include "g_game.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"


const char* g_gamepadName = "/dev/input/event2";
int g_gamepad = -1;


int	mb_used = 6;


void
I_Tactile
( int	on,
  int	off,
  int	total )
{
  // UNUSED.
  on = off = total = 0;
}

ticcmd_t	emptycmd;
ticcmd_t*	I_BaseTiccmd(void)
{
    return &emptycmd;
}


int  I_GetHeapSize (void)
{
    return mb_used*1024*1024;
}

byte* I_ZoneBase (int*	size)
{
    *size = mb_used*1024*1024;
    return (byte *) malloc (*size);
}



//
// I_GetTime
// returns time in 1/70th second tics
//
int  I_GetTime (void)
{
    struct timeval	tp;
    // LLatta: Removed struct timezone	tzp;
    int			newtics;
    static int		basetime=0;
  
    gettimeofday(&tp, 0);
    if (!basetime)
	basetime = tp.tv_sec;
    newtics = (tp.tv_sec-basetime)*TICRATE + tp.tv_usec*TICRATE/1000000;
    return newtics;
}



//
// I_Init
//
void I_Init (void)
{
    I_InitSound();
    //  I_InitGraphics();

	if ((g_gamepad = open(g_gamepadName, O_RDONLY | O_NONBLOCK)) < 0)
	{
		fprintf(stderr, "Cannot access gamepad at %s. Will retry.\n", g_gamepadName);
	}
}

//
// I_Quit
//
void I_Quit (void)
{
    D_QuitNetGame ();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults ();
    I_ShutdownGraphics();


	if (g_gamepad >= 0)
	{
		close(g_gamepad);
	}

    exit(0);
}

void I_WaitVBL(int count)
{
#ifdef SGI
    sginap(1);                                           
#else
#ifdef SUN
    sleep(0);
#else
    usleep (count * (1000000/70) );                                
#endif
#endif
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte*	I_AllocLow(int length)
{
    byte*	mem;
        
    mem = (byte *)malloc (length);
    memset (mem,0,length);
    return mem;
}


//
// I_Error
//
extern boolean demorecording;

void I_Error (char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start (argptr,error);
    fprintf (stderr, "Error: ");
    vfprintf (stderr,error,argptr);
    fprintf (stderr, "\n");
    va_end (argptr);

    fflush( stderr );

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame ();
    I_ShutdownGraphics();
    
    exit(-1);
}


event_t joystickEvent = { ev_joystick };

void I_GetEvent(void)
{
	if (g_gamepad < 0)
	{
		if ((g_gamepad = open(g_gamepadName, O_RDONLY | O_NONBLOCK)) < 0)
		{
			return;
		}
	}

	while (1)
	{
		struct input_event ev[64];

		int rd = read(g_gamepad, ev, sizeof(ev));

		if (rd < (int) sizeof(struct input_event))
		{
			break;
		}

		int i;
		for (i = 0; i < rd / (int)sizeof(struct input_event); i++)
		{
			//printf("Event: time %ld.%06ld, type %d, code %d,", ev[i].time.tv_sec, ev[i].time.tv_usec, ev[i].type, ev[i].code);

			struct input_event e = ev[i];

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
