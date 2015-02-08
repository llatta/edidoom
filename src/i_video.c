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

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <mraa/gpio.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"

// Edison Arduino pin assignments for TFT controller
static const int TFT_CS = 9;
static const int TFT_CD = 8;
static const int TFT_WR = 7;
static const int TFT_RD = 6;
static const int TFT_DATA[8] = { 13, 10, 12, 11, 14, 15, 16, 17 };

// First hardware pin number of the data bits, ie 40-47, and WR flag assumed in the same % 32 range
static const int TFT_EDISON_DATA0 = 40;
static const int TFT_EDISON_WR = 48;

// Size of the TFT display, note it's different from SCREENWIDTH/SCREENHEIGHT
#define TFTWIDTH   240
#define TFTHEIGHT  320

// Command codes for TFT display controller
#define ILI9341_SOFTRESET          0x01
#define ILI9341_SLEEPIN            0x10
#define ILI9341_SLEEPOUT           0x11
#define ILI9341_NORMALDISP         0x13
#define ILI9341_INVERTOFF          0x20
#define ILI9341_INVERTON           0x21
#define ILI9341_GAMMASET           0x26
#define ILI9341_DISPLAYOFF         0x28
#define ILI9341_DISPLAYON          0x29
#define ILI9341_COLADDRSET         0x2A
#define ILI9341_PAGEADDRSET        0x2B
#define ILI9341_MEMORYWRITE        0x2C
#define ILI9341_PIXELFORMAT        0x3A
#define ILI9341_FRAMECONTROL       0xB1
#define ILI9341_DISPLAYFUNC        0xB6
#define ILI9341_ENTRYMODE          0xB7
#define ILI9341_POWERCONTROL1      0xC0
#define ILI9341_POWERCONTROL2      0xC1
#define ILI9341_VCOMCONTROL1       0xC5
#define ILI9341_VCOMCONTROL2       0xC7
#define ILI9341_GMCTRP1            0xE0
#define ILI9341_GMCTRN1            0xE1
#define ILI9341_MEMCONTROL         0x36
#define ILI9341_MADCTL             0x36

#define ILI9341_MADCTL_MY  0x80
#define ILI9341_MADCTL_MX  0x40
#define ILI9341_MADCTL_MV  0x20
#define ILI9341_MADCTL_ML  0x10
#define ILI9341_MADCTL_RGB 0x00
#define ILI9341_MADCTL_BGR 0x08
#define ILI9341_MADCTL_MH  0x04


// Palette to convert from current 8-bit color index to display 16-bit color
static uint16_t g_palette[256];

// GPIO pins to drive display
static mraa_gpio_context g_csPinCtx = 0;
static mraa_gpio_context g_cdPinCtx = 0;
static mraa_gpio_context g_wrPinCtx = 0;
static mraa_gpio_context g_rdPinCtx = 0;
static mraa_gpio_context g_dataPinCtx[8] = { 0 };

// This is an absolute path to a resource file found within sysfs.
// Might not always be correct. First thing to check if mmap stops
// working. Check the device for 0x1199 and Intel Vendor (0x8086)
#define MMAP_PATH "/sys/devices/pci0000:00/0000:00:0c.0/resource0"

// Memory mapped registers to set the data pins faster than through MRAA interface
static uint8_t *g_MmapRegisterSet = NULL;
static uint8_t *g_MmapRegisterClear = NULL;


// Helper macros to switch GPIO pins
#define RD_ACTIVE  mraa_gpio_write(g_rdPinCtx, 0)
#define RD_IDLE    mraa_gpio_write(g_rdPinCtx, 1)
#define WR_ACTIVE  mraa_gpio_write(g_wrPinCtx, 0)
#define WR_IDLE    mraa_gpio_write(g_wrPinCtx, 1)
#define CD_COMMAND mraa_gpio_write(g_cdPinCtx, 0)
#define CD_DATA    mraa_gpio_write(g_cdPinCtx, 1)
#define CS_ACTIVE  mraa_gpio_write(g_csPinCtx, 0)
#define CS_IDLE    mraa_gpio_write(g_csPinCtx, 1)

// Data write strobe, ~2 instructions and always inline
#define WR_STROBE { uint32_t bit = (uint32_t)((uint32_t)1 << (TFT_EDISON_WR % 32)); \
	*(volatile uint32_t*)g_MmapRegisterClear = bit; \
	*(volatile uint32_t*)g_MmapRegisterSet = bit; }

void TFT_MilliSleep(int milliseconds)
{
	usleep(milliseconds * 1000);
}

// Initialize a GPIO pin for writing
mraa_gpio_context TFT_InitPin(int pin)
{
	mraa_gpio_context context = mraa_gpio_init(pin);
	if (context == NULL)
	{
		I_Error("Failed to open video display pin %d", pin);
	}

	mraa_gpio_dir(context, MRAA_GPIO_OUT);
	mraa_gpio_use_mmaped(context, 1);

	return context;
}

// Initialize fast access to GPIO via memory mapping the PCI device
void TFT_InitMmap()
{
	int mmap_fd;
	if ((mmap_fd = open(MMAP_PATH, O_RDWR)) < 0)
	{
		I_Error("Cannot open GPIO device: %s\n", MMAP_PATH);
	}

	struct stat fd_stat;
	fstat(mmap_fd, &fd_stat);

	uint8_t* mmap_reg = (uint8_t*) mmap(NULL, fd_stat.st_size,
			PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, mmap_fd, 0);
	if (mmap_reg == MAP_FAILED)
	{
		I_Error("Failed to mmap GPIO device\n");
	}

	uint8_t offset = ((TFT_EDISON_DATA0 / 32) * sizeof(uint32_t));
	g_MmapRegisterSet = mmap_reg + offset + 0x34;
	g_MmapRegisterClear = mmap_reg + offset + 0x4c;
}


void TFT_Write8(uint8_t value)
{
	uint32_t set = (uint32_t) value << (TFT_EDISON_DATA0 % 32);
	uint32_t clear = ((uint32_t) ((uint8_t) ~value) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t) 1u << (TFT_EDISON_WR % 32);
	uint32_t setWr = (uint32_t) 1 << (TFT_EDISON_WR % 32);
	*(volatile uint32_t*) g_MmapRegisterSet = set;
	*(volatile uint32_t*) g_MmapRegisterClear = clear;
	*(volatile uint32_t*) g_MmapRegisterSet = setWr;
}

void TFT_Write16(uint16_t value)
{
	uint8_t hi = value >> 8;
	uint8_t lo = value;

	uint32_t setHi = (uint32_t) hi << (TFT_EDISON_DATA0 % 32);
	uint32_t clearHi = ((uint32_t) ((uint8_t) ~hi) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t) 1 << (TFT_EDISON_WR % 32);
	uint32_t setLo = (uint32_t) lo << (TFT_EDISON_DATA0 % 32);
	uint32_t clearLo = ((uint32_t) ((uint8_t) ~lo) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t) 1 << (TFT_EDISON_WR % 32);
	uint32_t setWr = (uint32_t) 1 << (TFT_EDISON_WR % 32);

#if RUN_WITHIN_SPEC
	// The display controller spec wants us to hold the data lines at their steady value
	// on the transition of WR from low to high for at least 10 ns.
	// This version will do that:
	*(volatile uint32_t*)g_MmapRegisterSet = setHi;
	*(volatile uint32_t*)g_MmapRegisterClear = clearHi;
	*(volatile uint32_t*)g_MmapRegisterSet = setWr;
	*(volatile uint32_t*)g_MmapRegisterSet = setLo;
	*(volatile uint32_t*)g_MmapRegisterClear = clearLo;
	*(volatile uint32_t*)g_MmapRegisterSet = setWr;
#else
	// Disregarding the spec, it seems to "currently" work to merge the WR switch with
	// the subsequent data write, shaving off two of six write cycles.
	// If this stops working in the future, it might be fixable by delaying the WR signal
	// a little, maybe with a capacitor/low pass filterish circuit.
	*(volatile uint32_t*) g_MmapRegisterSet = setWr | setHi;
	*(volatile uint32_t*) g_MmapRegisterClear = clearHi;
	*(volatile uint32_t*) g_MmapRegisterSet = setWr | setLo;
	*(volatile uint32_t*) g_MmapRegisterClear = clearLo;
#endif
}

void TFT_WriteRegister8(uint8_t a, uint8_t d)
{
	CD_COMMAND;
	TFT_Write8(a);
	CD_DATA;
	TFT_Write8(d);
}

void TFT_WriteRegister16(uint8_t a, uint16_t d)
{
	CD_COMMAND;
	TFT_Write8(a);
	CD_DATA;
	TFT_Write16(d);
}

void TFT_WriteRegister32(uint8_t r, uint32_t d)
{
	CD_COMMAND;
	TFT_Write8(r);
	CD_DATA;
	TFT_Write8(d >> 24);
	TFT_Write8(d >> 16);
	TFT_Write8(d >> 8);
	TFT_Write8(d);
}

void TFT_InitDisplay()
{
	int i;

	// Initialize display
	WR_IDLE;
	RD_IDLE;

	// Data transfer sync
	CS_ACTIVE;
	CD_COMMAND;
	TFT_Write8(0x00);
	for (i = 0; i < 3; i++)
	{
		WR_STROBE // Three extra 0x00s
	}
	CS_IDLE;

	TFT_MilliSleep(200);

	CS_ACTIVE;
	TFT_WriteRegister8(ILI9341_SOFTRESET, 0);
	TFT_MilliSleep(50);
	TFT_WriteRegister8(ILI9341_DISPLAYOFF, 0);

	TFT_WriteRegister8(ILI9341_POWERCONTROL1, 0x23);
	TFT_WriteRegister8(ILI9341_POWERCONTROL2, 0x10);
	TFT_WriteRegister16(ILI9341_VCOMCONTROL1, 0x2B2B);
	TFT_WriteRegister8(ILI9341_VCOMCONTROL2, 0xC0);
	TFT_WriteRegister8(ILI9341_MEMCONTROL, ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR);
	TFT_WriteRegister8(ILI9341_PIXELFORMAT, 0x55);
	TFT_WriteRegister16(ILI9341_FRAMECONTROL, 0x001B);

	TFT_WriteRegister8(ILI9341_ENTRYMODE, 0x07);
	/* TFT_WriteRegister32(ILI9341_DISPLAYFUNC, 0x0A822700);*/

//	TFT_WriteRegister16(0xF2, 0x00);    // 3Gamma Function Disable
//	TFT_WriteRegister16(ILI9341_GAMMASET, 0x01);    //Gamma curve selected
	/*
	 CD_COMMAND;
	 TFT_Write8(ILI9341_GMCTRP1);    //Set Gamma
	 CD_DATA;
	 #ifdef ADAFRUIT_GAMMA
	 TFT_Write16(0x0F);
	 TFT_Write16(0x31);
	 TFT_Write16(0x2B);
	 TFT_Write16(0x0C);
	 TFT_Write16(0x0E);
	 TFT_Write16(0x08);
	 TFT_Write16(0x4E);
	 TFT_Write16(0xF1);
	 TFT_Write16(0x37);
	 TFT_Write16(0x07);
	 TFT_Write16(0x10);
	 TFT_Write16(0x03);
	 TFT_Write16(0x0E);
	 TFT_Write16(0x09);
	 TFT_Write16(0x00);
	 #else
	 // From MI0283QT-11 spec
	 TFT_Write16(0x001f);
	 TFT_Write16(0x001a);
	 TFT_Write16(0x0018);
	 TFT_Write16(0x000a);
	 TFT_Write16(0x000f);
	 TFT_Write16(0x0006);
	 TFT_Write16(0x0045);
	 TFT_Write16(0x0087);
	 TFT_Write16(0x0032);
	 TFT_Write16(0x000a);
	 TFT_Write16(0x0007);
	 TFT_Write16(0x0002);
	 TFT_Write16(0x0007);
	 TFT_Write16(0x0005);
	 TFT_Write16(0x0000);
	 #endif

	 CD_COMMAND;
	 TFT_Write8(ILI9341_GMCTRN1);    //Set Gamma
	 CD_DATA;
	 #ifdef ADAFRUIT_GAMMA
	 TFT_Write16(0x00);
	 TFT_Write16(0x0E);
	 TFT_Write16(0x14);
	 TFT_Write16(0x03);
	 TFT_Write16(0x11);
	 TFT_Write16(0x07);
	 TFT_Write16(0x31);
	 TFT_Write16(0xC1);
	 TFT_Write16(0x48);
	 TFT_Write16(0x08);
	 TFT_Write16(0x0F);
	 TFT_Write16(0x0C);
	 TFT_Write16(0x31);
	 TFT_Write16(0x36);
	 TFT_Write16(0x0F);
	 #else
	 // From MI0283QT-11 spec
	 TFT_Write16(0x0000);
	 TFT_Write16(0x0025);
	 TFT_Write16(0x0027);
	 TFT_Write16(0x0005);
	 TFT_Write16(0x0010);
	 TFT_Write16(0x0009);
	 TFT_Write16(0x003a);
	 TFT_Write16(0x0078);
	 TFT_Write16(0x004d);
	 TFT_Write16(0x0005);
	 TFT_Write16(0x0018);
	 TFT_Write16(0x000d);
	 TFT_Write16(0x0038);
	 TFT_Write16(0x003a);
	 TFT_Write16(0x001f);
	 #endif
	 */
	TFT_WriteRegister8(ILI9341_SLEEPOUT, 0);
	TFT_MilliSleep(150);
	TFT_WriteRegister8(ILI9341_DISPLAYON, 0);
	TFT_MilliSleep(500);

	// Set screen to be rotated to landscape mode
	TFT_WriteRegister8(ILI9341_MADCTL,
			ILI9341_MADCTL_MX | ILI9341_MADCTL_MY | ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR); // MADCTL

	// Set full screen as write region (width<->height swapped due to landscape)
	TFT_WriteRegister32(ILI9341_COLADDRSET, 0 << 16 | (TFTHEIGHT - 1));
	TFT_WriteRegister32(ILI9341_PAGEADDRSET, 0 << 16 | (TFTWIDTH - 1));

	// Clear screen to black
	CD_COMMAND;
	TFT_Write8(0x2C);
	CD_DATA;
	for (i = TFTWIDTH * TFTHEIGHT; i; i--)
	{
		TFT_Write16(0);
	}

	CS_IDLE;
}

// Pass 8-bit (each) R,G,B, get back 16-bit packed color
uint16_t I_MakeColor565(uint8_t r, uint8_t g, uint8_t b)
{
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit(void)
{
	// what is this?
}

//
// I_FinishUpdate
//
void I_FinishUpdate(void)
{
//#define PROFILE_FRAME
#ifdef PROFILE_FRAME
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	double startTime = (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;
#endif

	CS_ACTIVE;
	CD_COMMAND;
	TFT_Write8(0x2C);
	CD_DATA;

	uint8_t* s = screens[0];
	int i;
	for (i = SCREENWIDTH * SCREENHEIGHT; i; i--)
	{
		TFT_Write16(g_palette[*s++]);
	}

	CS_IDLE;

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
void I_ReadScreen(byte* scr)
{
	memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
void I_SetPalette(byte* palette)
{
	int i;
	for (i = 0; i < 256; i++)
	{
		uint8_t r, g, b;
		r = gammatable[usegamma][*palette++];
		g = gammatable[usegamma][*palette++];
		b = gammatable[usegamma][*palette++];
		g_palette[i] = I_MakeColor565(r, g, b);
	}
}

void I_InitGraphics(void)
{
	fprintf(stderr, "I_InitGraphics: ");

	// Initialize GPIO pins
	g_csPinCtx = TFT_InitPin(TFT_CS);
	g_cdPinCtx = TFT_InitPin(TFT_CD);
	g_wrPinCtx = TFT_InitPin(TFT_WR);
	g_rdPinCtx = TFT_InitPin(TFT_RD);
	int i;
	for (i = 0; i < 8; ++i)
	{
		g_dataPinCtx[i] = TFT_InitPin(TFT_DATA[i]);
	}

	TFT_InitMmap();

	TFT_InitDisplay();

	fprintf(stderr, " display ready\n");
}

void I_ShutdownGraphics(void)
{
	mraa_gpio_close(g_csPinCtx);
	mraa_gpio_close(g_cdPinCtx);
	mraa_gpio_close(g_wrPinCtx);
	mraa_gpio_close(g_rdPinCtx);
	int i;
	for (i = 0; i < 8; ++i)
	{
		mraa_gpio_close(g_dataPinCtx[i]);
	}
}
