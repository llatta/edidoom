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

#define TFTWIDTH   240
#define TFTHEIGHT  320

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


uint16_t g_palette[256];

mraa_gpio_context m_csPinCtx;
mraa_gpio_context m_cdPinCtx;
mraa_gpio_context m_wrPinCtx;
mraa_gpio_context m_rdPinCtx;
mraa_gpio_context m_dataPinCtx[8];

// When using the TFT breakout board, control pins are configurable.
#define RD_ACTIVE  mraa_gpio_write(m_rdPinCtx, 0)
#define RD_IDLE    mraa_gpio_write(m_rdPinCtx, 1)
#define WR_ACTIVE  mraa_gpio_write(m_wrPinCtx, 0)
#define WR_IDLE    mraa_gpio_write(m_wrPinCtx, 1)
#define CD_COMMAND mraa_gpio_write(m_cdPinCtx, 0)
#define CD_DATA    mraa_gpio_write(m_cdPinCtx, 1)
#define CS_ACTIVE  mraa_gpio_write(m_csPinCtx, 0)
#define CS_IDLE    mraa_gpio_write(m_csPinCtx, 1)

// Data write strobe, ~2 instructions and always inline
#define WR_STROBE { uint32_t bit = (uint32_t)((uint32_t)1 << (TFT_EDISON_WR % 32)); \
	*(volatile uint32_t*)mmap_reg_clear = bit; \
	*(volatile uint32_t*)mmap_reg_set = bit; }

// This is an absolute path to a resource file found within sysfs.
// Might not always be correct. First thing to check if mmap stops
// working. Check the device for 0x1199 and Intel Vendor (0x8086)
#define MMAP_PATH "/sys/devices/pci0000:00/0000:00:0c.0/resource0"

//MMAP
static uint8_t *mmap_reg = NULL;
static uint8_t *mmap_reg_set = NULL;
static uint8_t *mmap_reg_clear = NULL;
static int mmap_fd = 0;
static int mmap_size;

static void delay(int milliseconds)
{
	usleep(milliseconds * 1000);
}

void write8(uint8_t value) {
    uint32_t set   = (uint32_t)value << (TFT_EDISON_DATA0 % 32);
    uint32_t clear = ((uint32_t)((uint8_t)~value) << (TFT_EDISON_DATA0 % 32))
    		| (uint32_t)1u << (TFT_EDISON_WR % 32);
    uint32_t setWr   = (uint32_t)1 << (TFT_EDISON_WR % 32);
    *(volatile uint32_t*)mmap_reg_set = set;
    *(volatile uint32_t*)mmap_reg_clear = clear;
    *(volatile uint32_t*)mmap_reg_set = setWr;
}

void write16(uint16_t value)
{
	uint8_t hi = value >> 8;
	uint8_t lo = value;

	uint32_t setHi   = (uint32_t)hi << (TFT_EDISON_DATA0 % 32);
	uint32_t clearHi = ((uint32_t)((uint8_t)~hi) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t)1 << (TFT_EDISON_WR % 32);
	uint32_t setLo   = (uint32_t)lo << (TFT_EDISON_DATA0 % 32);
	uint32_t clearLo = ((uint32_t)((uint8_t)~lo) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t)1 << (TFT_EDISON_WR % 32);
	uint32_t setWr   = (uint32_t)1 << (TFT_EDISON_WR % 32);

#if RUN_WITHIN_SPEC
	// The display controller spec wants us to hold the data lines at their steady value
	// on the transition of WR from low to high for at least 10 ns.
	// This version will do that:
	*(volatile uint32_t*)mmap_reg_set = setHi;
	*(volatile uint32_t*)mmap_reg_clear = clearHi;
	*(volatile uint32_t*)mmap_reg_set = setWr;
	*(volatile uint32_t*)mmap_reg_set = setLo;
	*(volatile uint32_t*)mmap_reg_clear = clearLo;
	*(volatile uint32_t*)mmap_reg_set = setWr;
#else
	// Disregarding the spec, it seems to "currently" work to merge the WR switch with
	// the subsequent data write, shaving off two of six write cycles.
	// If this stops working in the future, it might be fixable by delaying the WR signal
	// a little, maybe with a capacitor/low pass filterish circuit.
	*(volatile uint32_t*)mmap_reg_set = setWr | setHi;
	*(volatile uint32_t*)mmap_reg_clear = clearHi;
	*(volatile uint32_t*)mmap_reg_set = setWr | setLo;
	*(volatile uint32_t*)mmap_reg_clear = clearLo;
#endif
}

void writeRegister8(uint8_t a, uint8_t d) {
	CD_COMMAND;
	write8(a);
	CD_DATA;
	write8(d);
}

void writeRegister16(uint8_t a, uint16_t d) {
	CD_COMMAND;
	write8(a);
	CD_DATA;
	write16(d);
}

void writeRegister32(uint8_t r, uint32_t d) {
	CS_ACTIVE;
	CD_COMMAND;
	write8(r);
	CD_DATA;
	write8(d >> 24);
	write8(d >> 16);
	write8(d >> 8);
	write8(d);
	CS_IDLE;
}

void setAddrWindow(int x1, int y1, int x2, int y2) {
	CS_ACTIVE;

	uint32_t t;

	t = x1;
	t <<= 16;
	t |= x2;
	writeRegister32(ILI9341_COLADDRSET, t);
	t = y1;
	t <<= 16;
	t |= y2;
	writeRegister32(ILI9341_PAGEADDRSET, t);

	CS_IDLE;
}


// Pass 8-bit (each) R,G,B, get back 16-bit packed color
uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
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
//#define PROFILE_FRAME
#ifdef PROFILE_FRAME
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	double startTime = (double)t.tv_sec + (double)t.tv_nsec / 1.0e9;
#endif

	CS_ACTIVE;
	CD_COMMAND;
	write8(0x2C);
	CD_DATA;

	uint8_t* s = screens[0];
	for (int i = SCREENWIDTH * SCREENHEIGHT; i; i--)
	{
		write16(g_palette[*s++]);
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
		g_palette[i] = color565(r, g, b);
	}
}

void I_InitGraphics(void)
{
	m_csPinCtx = mraa_gpio_init(TFT_CS);
	if (m_csPinCtx == nullptr) {
		fprintf (stderr, "Are you sure that pin%d you requested is valid on your platform?", TFT_CS);
		exit (1);
	}
	m_cdPinCtx = mraa_gpio_init(TFT_CD);
	if (m_cdPinCtx == nullptr) {
		fprintf (stderr, "Are you sure that pin%d you requested is valid on your platform?", TFT_CD);
		exit (1);
	}
	m_wrPinCtx = mraa_gpio_init(TFT_WR);
	if (m_wrPinCtx == nullptr) {
		fprintf (stderr, "Are you sure that pin%d you requested is valid on your platform?", TFT_WR);
		exit (1);
	}
	m_rdPinCtx = mraa_gpio_init(TFT_RD);
	if (m_rdPinCtx == nullptr) {
		fprintf (stderr, "Are you sure that pin%d you requested is valid on your platform?", TFT_RD);
		exit (1);
	}
	for (int i = 0; i < 8; ++i)
	{
		m_dataPinCtx[i] = mraa_gpio_init(TFT_DATA[i]);
		if (m_dataPinCtx[i] == nullptr) {
			fprintf (stderr, "Are you sure that pin%d you requested is valid on your platform?", TFT_DATA[i]);
			exit (1);
		}

		mraa_gpio_use_mmaped(m_dataPinCtx[i], 1);
	}

	mraa_gpio_dir(m_csPinCtx, MRAA_GPIO_OUT);
	mraa_gpio_dir(m_cdPinCtx, MRAA_GPIO_OUT);
	mraa_gpio_dir(m_wrPinCtx, MRAA_GPIO_OUT);
	mraa_gpio_dir(m_rdPinCtx, MRAA_GPIO_OUT);
    for (int i = 0; i < 8; ++i)
	{
		mraa_gpio_dir(m_dataPinCtx[i], MRAA_GPIO_OUT);
	}

	mraa_gpio_use_mmaped(m_csPinCtx, 1);
	mraa_gpio_use_mmaped(m_cdPinCtx, 1);
	mraa_gpio_use_mmaped(m_wrPinCtx, 1);
	mraa_gpio_use_mmaped(m_rdPinCtx, 1);

    if (mmap_reg == NULL) {
        if ((mmap_fd = open(MMAP_PATH, O_RDWR)) < 0) {
            printf("edison map: unable to open resource0 file\n");
        }

        struct stat fd_stat;
        fstat(mmap_fd, &fd_stat);
        mmap_size = fd_stat.st_size;

        mmap_reg = (uint8_t*) mmap(NULL, fd_stat.st_size,
                                   PROT_READ | PROT_WRITE,
                                   MAP_FILE | MAP_SHARED,
                                   mmap_fd, 0);
        if (mmap_reg == MAP_FAILED) {
            printf("edison mmap: failed to mmap\n");
            mmap_reg = NULL;
            close(mmap_fd);
        }

        uint8_t offset = ((TFT_EDISON_DATA0 / 32) * sizeof(uint32_t));
        mmap_reg_set = mmap_reg + offset + 0x34;
        mmap_reg_clear = mmap_reg + offset + 0x4c;
    }

	WR_IDLE;
	RD_IDLE;

	// Data transfer sync
	CS_ACTIVE;
	CD_COMMAND;
	write8(0x00);
	for(uint8_t i=0; i<3; i++) WR_STROBE; // Three extra 0x00s
	CS_IDLE;

	delay(200);

	CS_ACTIVE;
	writeRegister8(ILI9341_SOFTRESET, 0);
	delay(50);
	writeRegister8(ILI9341_DISPLAYOFF, 0);

	writeRegister8(ILI9341_POWERCONTROL1, 0x23);
	writeRegister8(ILI9341_POWERCONTROL2, 0x10);
	writeRegister16(ILI9341_VCOMCONTROL1, 0x2B2B);
	writeRegister8(ILI9341_VCOMCONTROL2, 0xC0);
	writeRegister8(ILI9341_MEMCONTROL, ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR);
	writeRegister8(ILI9341_PIXELFORMAT, 0x55);
	writeRegister16(ILI9341_FRAMECONTROL, 0x001B);

	writeRegister8(ILI9341_ENTRYMODE, 0x07);
	/* writeRegister32(ILI9341_DISPLAYFUNC, 0x0A822700);*/

//	writeRegister16(0xF2, 0x00);    // 3Gamma Function Disable

//	writeRegister16(ILI9341_GAMMASET, 0x01);    //Gamma curve selected
/*
	CD_COMMAND;
	write8(ILI9341_GMCTRP1);    //Set Gamma
	CD_DATA;
#ifdef ADAFRUIT_GAMMA
	write16(0x0F);
	write16(0x31);
	write16(0x2B);
	write16(0x0C);
	write16(0x0E);
	write16(0x08);
	write16(0x4E);
	write16(0xF1);
	write16(0x37);
	write16(0x07);
	write16(0x10);
	write16(0x03);
	write16(0x0E);
	write16(0x09);
	write16(0x00);
#else
	// From MI0283QT-11 spec
	write16(0x001f);
	write16(0x001a);
	write16(0x0018);
	write16(0x000a);
	write16(0x000f);
	write16(0x0006);
	write16(0x0045);
	write16(0x0087);
	write16(0x0032);
	write16(0x000a);
	write16(0x0007);
	write16(0x0002);
	write16(0x0007);
	write16(0x0005);
	write16(0x0000);
#endif

	CD_COMMAND;
	write8(ILI9341_GMCTRN1);    //Set Gamma
	CD_DATA;
#ifdef ADAFRUIT_GAMMA
	write16(0x00);
	write16(0x0E);
	write16(0x14);
	write16(0x03);
	write16(0x11);
	write16(0x07);
	write16(0x31);
	write16(0xC1);
	write16(0x48);
	write16(0x08);
	write16(0x0F);
	write16(0x0C);
	write16(0x31);
	write16(0x36);
	write16(0x0F);
#else
	// From MI0283QT-11 spec
	write16(0x0000);
	write16(0x0025);
	write16(0x0027);
	write16(0x0005);
	write16(0x0010);
	write16(0x0009);
	write16(0x003a);
	write16(0x0078);
	write16(0x004d);
	write16(0x0005);
	write16(0x0018);
	write16(0x000d);
	write16(0x0038);
	write16(0x003a);
	write16(0x001f);
#endif
*/
	writeRegister8(ILI9341_SLEEPOUT, 0);
	delay(150);
	writeRegister8(ILI9341_DISPLAYON, 0);
	delay(500);

	// Set screen to be rotated to landscape mode
	CS_ACTIVE;
	writeRegister8(ILI9341_MADCTL, ILI9341_MADCTL_MX | ILI9341_MADCTL_MY | ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR); // MADCTL
	setAddrWindow(0, 0, TFTHEIGHT - 1, TFTWIDTH - 1); // CS_IDLE happens here

	// Clear screen to black
	CS_ACTIVE;
	CD_COMMAND;
	write8(0x2C);
	CD_DATA;
	for (int i = TFTWIDTH * TFTHEIGHT; i; i--)
	{
		write16(0);
	}
	CS_IDLE;
}


void I_ShutdownGraphics(void)
{
}

} // extern C
