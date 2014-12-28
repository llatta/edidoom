// Graphics library by ladyada/adafruit with init code from Rossum
// MIT license

#include "Adafruit_TFTLCD.h"
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#if SIMPLE_PINS
	static const int TFT_CS = 8;
	static const int TFT_CD = 9;
	static const int TFT_WR = 10;
	static const int TFT_RD = 11;
	static const int TFT_DATA[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#else
	static const int TFT_CS = 9;
	static const int TFT_CD = 8;
	static const int TFT_WR = 7;
	static const int TFT_RD = 6;
	static const int TFT_DATA[8] = { 13, 10, 12, 11, 14, 15, 16, 17 };

	// First hardware pin number of the data bits, ie 40-47, and WR flag assumed in the same % 32 range
	static const int TFT_EDISON_DATA0 = 40;
	static const int TFT_EDISON_WR = 48;
#endif

#define TFTWIDTH   240
#define TFTHEIGHT  320

#define HIGH                1
#define LOW                 0

// When using the TFT breakout board, control pins are configurable.
#define RD_ACTIVE  mraa_gpio_write(m_rdPinCtx, LOW)
#define RD_IDLE    mraa_gpio_write(m_rdPinCtx, HIGH)
#define WR_ACTIVE  mraa_gpio_write(m_wrPinCtx, LOW)
#define WR_IDLE    mraa_gpio_write(m_wrPinCtx, HIGH)
#define CD_COMMAND mraa_gpio_write(m_cdPinCtx, LOW)
#define CD_DATA    mraa_gpio_write(m_cdPinCtx, HIGH)
#define CS_ACTIVE  mraa_gpio_write(m_csPinCtx, LOW)
#define CS_IDLE    mraa_gpio_write(m_csPinCtx, HIGH)

// Data write strobe, ~2 instructions and always inline
//#define WR_STROBE { WR_ACTIVE; WR_IDLE; }
#define WR_STROBE { uint32_t bit = (uint32_t)((uint32_t)1 << (TFT_EDISON_WR % 32)); \
	*(volatile uint32_t*)mmap_reg_clear = bit; \
	*(volatile uint32_t*)mmap_reg_set = bit; }

static void delay(int milliseconds)
{
	usleep(milliseconds * 1000);
}

static void delayMicroseconds(int microseconds)
{
	usleep(microseconds);
}

// LCD controller chip identifiers
#define ID_932X    0
#define ID_7575    1
#define ID_9341    2
#define ID_HX8357D    3
#define ID_UNKNOWN 0xFF

#include "registers.h"

/**
 * A structure representing a gpio pin.
 */
struct _gpio {
    /*@{*/
    int pin; /**< the pin number, as known to the os. */
    int phy_pin; /**< pin passed to clean init. -1 none and raw*/
    int value_fp; /**< the file pointer to the value of the gpio */
    void (* isr)(void *); /**< the interupt service request */
    void *isr_args; /**< args return when interupt service request triggered */
    pthread_t thread_id; /**< the isr handler thread id */
    int isr_value_fp; /**< the isr file pointer on the value */
    mraa_boolean_t owner; /**< If this context originally exported the pin */
    mraa_result_t (*mmap_write) (mraa_gpio_context dev, int value);
    int (*mmap_read) (mraa_gpio_context dev);
    /*@}*/
};

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

mraa_result_t mraa_intel_edison_mmap_writeX(mraa_gpio_context dev, int value)
{
    uint8_t offset = ((dev->pin / 32) * sizeof(uint32_t));
    uint8_t valoff;

    if (value) {
        valoff = 0x34;
    } else {
        valoff = 0x4c;
    }

    *(volatile uint32_t*) (mmap_reg + offset + valoff) =
        (uint32_t)(1 << (dev->pin % 32));

    return MRAA_SUCCESS;
}

// Requirements: dev points to the first of 8 consecutive pins. All pins need to be in the same % 32 range.
mraa_result_t mraa_intel_edison_mmap_write8(mraa_gpio_context dev, uint8_t value)
{
    uint8_t offset = ((dev->pin / 32) * sizeof(uint32_t));

    uint32_t set   = (uint32_t)((uint32_t)value << (dev->pin % 32));
    uint32_t clear = (uint32_t)((uint32_t)((uint8_t)~value) << (dev->pin % 32));
    *(volatile uint32_t*) (mmap_reg + offset + 0x34) = set;
    *(volatile uint32_t*) (mmap_reg + offset + 0x4c) = clear;

    return MRAA_SUCCESS;
}

// Constructor for breakout board (configurable LCD control lines).
Adafruit_TFTLCD::Adafruit_TFTLCD() {

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



#if 1//def USE_ADAFRUIT_SHIELD_PINOUT
	CS_IDLE; // Set all control bits to idle state
	WR_IDLE;
	RD_IDLE;
	CD_DATA;
#endif

	setWriteDir(); // Set up LCD data port(s) for WRITE operations
}

// Initialization command tables for different LCD controllers
#define TFTLCD_DELAY 0xFF

void Adafruit_TFTLCD::begin(uint16_t id) {
	reset();

	delay(200);

	if (id == 0x9341) {
		driver = ID_9341;
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

//		writeRegister16(0xF2, 0x00);    // 3Gamma Function Disable

//		writeRegister16(ILI9341_GAMMASET, 0x01);    //Gamma curve selected
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
		setAddrWindow(0, 0, TFTWIDTH-1, TFTHEIGHT-1);

		flood(0, TFTWIDTH * TFTHEIGHT);

		return;
	} else {
		driver = ID_UNKNOWN;
		return;
	}
}


void Adafruit_TFTLCD::reset(void) {

	CS_IDLE;
	//  CD_DATA;
	WR_IDLE;
	RD_IDLE;

	// Data transfer sync
	CS_ACTIVE;
	CD_COMMAND;
	write8(0x00);
	for(uint8_t i=0; i<3; i++) WR_STROBE; // Three extra 0x00s
	CS_IDLE;
}

// Sets the LCD address window (and address counter, on 932X).
// Relevant to rect/screen fills and H/V lines.  Input coordinates are
// assumed pre-sorted (e.g. x2 >= x1).
void Adafruit_TFTLCD::setAddrWindow(int x1, int y1, int x2, int y2) {
	CS_ACTIVE;

	uint32_t t;

	t = x1;
	t <<= 16;
	t |= x2;
	writeRegister32(ILI9341_COLADDRSET, t);  // HX8357D uses same registers!
	t = y1;
	t <<= 16;
	t |= y2;
	writeRegister32(ILI9341_PAGEADDRSET, t); // HX8357D uses same registers!

	CS_IDLE;
}

// Fast block fill operation for fillScreen, fillRect, H/V line, etc.
// Requires setAddrWindow() has previously been called to set the fill
// bounds.  'len' is inclusive, MUST be >= 1.
void Adafruit_TFTLCD::flood(uint16_t color, uint32_t len) {
	int blocks;
	int i;
	uint8_t hi = color >> 8,
			lo = color;

	CS_ACTIVE;
	CD_COMMAND;
	if (driver == ID_9341) {
		write8(0x2C);
	} else if (driver == ID_932X) {
		write8(0x00); // High byte of GRAM register...
		write8(0x22); // Write data to GRAM
	} else if (driver == ID_HX8357D) {
		write8(HX8357_RAMWR);
	} else {
		write8(0x22); // Write data to GRAM
	}

	CD_DATA;

	if(hi == lo) {
		// Write first pixel normally, decrement counter by 1
		write8(hi);
		write8(lo);
		len--;

		blocks = (uint16_t)(len / 64); // 64 pixels/block

		// High and low bytes are identical.  Leave prior data
		// on the port(s) and just toggle the write strobe.
		while(blocks--) {
			i = 16; // 64 pixels/block / 4 pixels/pass
			do {
				WR_STROBE; WR_STROBE; WR_STROBE; WR_STROBE; // 2 bytes/pixel
				WR_STROBE; WR_STROBE; WR_STROBE; WR_STROBE; // x 4 pixels
			} while(--i);
		}
		// Fill any remaining pixels (1 to 64)
		for(i = (uint8_t)len & 63; i--; ) {
			WR_STROBE;
			WR_STROBE;
		}
	}
	else
	{
#if 0
		blocks = (uint16_t)(len / 64); // 64 pixels/block

		while(blocks--) {
			i = 16; // 64 pixels/block / 4 pixels/pass
			do {
				write8(hi); write8(lo); write8(hi); write8(lo);
				write8(hi); write8(lo); write8(hi); write8(lo);
			} while(--i);
		}
		for(i = (uint8_t)len & 63; i--; ) {
			write8(hi);
			write8(lo);
		}
#elif 1
		for(int l = len; l; --l) {
			write16(color);
		}
#else
		blocks = (uint16_t)(len / 64); // 64 pixels/block

		while(blocks--) {
			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);

			write8(hi); write8(lo); write8(hi); write8(lo);
			write8(hi); write8(lo); write8(hi); write8(lo);
		}
		for(i = (uint8_t)len & 63; i--; ) {
			write8(hi);
			write8(lo);
		}

#endif
	}
	CS_IDLE;
}
// Issues 'raw' an array of 16-bit color values to the LCD; used
// externally by BMP examples.  Assumes that setWindowAddr() has
// previously been set to define the bounds.  Max 255 pixels at
// a time (BMP examples read in small chunks due to limited RAM).
void Adafruit_TFTLCD::pushColors(uint16_t *data, uint32_t len, bool first) {
	CS_ACTIVE;
	if(first == true) { // Issue GRAM write command only on first call
		CD_COMMAND;
		if(driver == ID_932X) write8(0x00);
		if ((driver == ID_9341) || (driver == ID_HX8357D)){
			write8(0x2C);
		}  else {
			write8(0x22);
		}
	}
	CD_DATA;
	while(len--) {
		uint16_t color = *data++;
		write16(color);
	}
	CS_IDLE;
}

void Adafruit_TFTLCD::setRotation(uint8_t rotation) {

	CS_ACTIVE;
	if (driver == ID_9341) {
		// MEME, HX8357D uses same registers as 9341 but different values
		uint16_t t = 0;

		switch (rotation) {
		case 2:
			t = ILI9341_MADCTL_MX | ILI9341_MADCTL_BGR;
			break;
		case 3:
			t = ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR;
			break;
		case 0:
			t = ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR;
			break;
		case 1:
			t = ILI9341_MADCTL_MX | ILI9341_MADCTL_MY | ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR;
			break;
		}
		writeRegister8(ILI9341_MADCTL, t ); // MADCTL
		// For 9341, init default full-screen address window:
		int _width, _height;
		switch(rotation) {
		case 0:
		case 2:
			_width  = TFTWIDTH;
			_height = TFTHEIGHT;
			break;
		case 1:
		case 3:
			_width  = TFTHEIGHT;
			_height = TFTWIDTH;
			break;
		}
		setAddrWindow(0, 0, _width - 1, _height - 1); // CS_IDLE happens here
	}
}


// Pass 8-bit (each) R,G,B, get back 16-bit packed color
uint16_t Adafruit_TFTLCD::color565(uint8_t r, uint8_t g, uint8_t b) {
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// For I/O macros that were left undefined, declare function
// versions that reference the inline macros just once:

#ifndef write8
void Adafruit_TFTLCD::write8(uint8_t value) {
#if 0
	for (int i = 0; i < 8; ++i)
	{
		mraa_intel_edison_mmap_writeX(m_dataPinCtx[i], (value >> i) & 1);
	}

	WR_STROBE;
#elif 0
	mraa_intel_edison_mmap_write8(m_dataPinCtx[0], value);

	WR_STROBE;
#elif 0
    uint8_t offset = ((TFT_EDISON_DATA0 / 32) * sizeof(uint32_t));

    uint32_t set   = (uint32_t)((uint32_t)value << (TFT_EDISON_DATA0 % 32));
    uint32_t clear = (uint32_t)((uint32_t)((uint8_t)~value) << (TFT_EDISON_DATA0 % 32))
    		| (uint32_t)1 << (TFT_EDISON_WR % 32);
    uint32_t set2   = (uint32_t)((uint32_t)1 << (TFT_EDISON_WR % 32));
    *(volatile uint32_t*) (mmap_reg + offset + 0x34) = set;
    *(volatile uint32_t*) (mmap_reg + offset + 0x4c) = clear;
    *(volatile uint32_t*) (mmap_reg + offset + 0x34) = set2;
#else
    uint32_t set   = (uint32_t)((uint32_t)value << (TFT_EDISON_DATA0 % 32));
    uint32_t clear = (uint32_t)((uint32_t)((uint8_t)~value) << (TFT_EDISON_DATA0 % 32))
    		| (uint32_t)1 << (TFT_EDISON_WR % 32);
    uint32_t set2   = (uint32_t)((uint32_t)1 << (TFT_EDISON_WR % 32));
    *(volatile uint32_t*)mmap_reg_set = set;
    *(volatile uint32_t*)mmap_reg_clear = clear;
    *(volatile uint32_t*)mmap_reg_set = set2;
#endif
}
#endif

void Adafruit_TFTLCD::write16(uint16_t value)
{
	uint8_t hi = value >> 8;
	uint8_t lo = value;

	uint32_t setHi   = (uint32_t)((uint32_t)hi << (TFT_EDISON_DATA0 % 32));
	uint32_t clearHi = (uint32_t)((uint32_t)((uint8_t)~hi) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t)1 << (TFT_EDISON_WR % 32);
	uint32_t setLo   = (uint32_t)((uint32_t)lo << (TFT_EDISON_DATA0 % 32));
	uint32_t clearLo = (uint32_t)((uint32_t)((uint8_t)~lo) << (TFT_EDISON_DATA0 % 32))
			| (uint32_t)1 << (TFT_EDISON_WR % 32);
	uint32_t setWr   = (uint32_t)((uint32_t)1 << (TFT_EDISON_WR % 32));

	*(volatile uint32_t*)mmap_reg_set = setWr | setHi;
	*(volatile uint32_t*)mmap_reg_clear = clearHi;
	*(volatile uint32_t*)mmap_reg_set = setWr | setLo;
	// *(volatile uint32_t*)mmap_reg_set = setLo;
	*(volatile uint32_t*)mmap_reg_clear = clearLo;
	//*(volatile uint32_t*)mmap_reg_set = setWr;
}





#define SYSFS_CLASS_GPIO "/sys/class/gpio"
#define MAX_SIZE 64

static unsigned int outputen[] = {248,249,250,251,252,253,254,255,256,257,258,259,260,261,232,233,234,235,236,237};
static mraa_gpio_context outputPins[sizeof(outputen) / sizeof(outputen[0])] = {0};
static mraa_gpio_context tristate;

mraa_result_t
mraa_intel_edison_gpio_dir_preX(mraa_gpio_context dev, gpio_dir_t dir)
{
    if (dev->phy_pin >= 0) {
        int pin = dev->phy_pin;

        if (outputPins[pin] == 0)
        {
        	outputPins[pin] = mraa_gpio_init_raw(outputen[pin]);
			if (mraa_gpio_dir(outputPins[pin], MRAA_GPIO_OUT) != MRAA_SUCCESS)
				return MRAA_ERROR_INVALID_RESOURCE;
        }

        int output_val = 0;
        if (dir == MRAA_GPIO_OUT)
            output_val = 1;
        if (mraa_gpio_write(outputPins[pin], output_val) != MRAA_SUCCESS)
            return MRAA_ERROR_INVALID_RESOURCE;
    }
    return MRAA_SUCCESS;
}

mraa_result_t
mraa_intel_edison_gpio_dir_postX(mraa_gpio_context dev, gpio_dir_t dir)
{
    return MRAA_SUCCESS;
}

mraa_result_t
mraa_gpio_dirX(mraa_gpio_context dev, gpio_dir_t dir)
{
	mraa_result_t pre_ret = mraa_intel_edison_gpio_dir_preX(dev,dir);
	if(pre_ret != MRAA_SUCCESS)
		return pre_ret;

    if (dev == NULL) {
        return MRAA_ERROR_INVALID_HANDLE;
    }
    if (dev->value_fp != -1) {
         close(dev->value_fp);
         dev->value_fp = -1;
    }
    char filepath[MAX_SIZE];
    snprintf(filepath, MAX_SIZE, SYSFS_CLASS_GPIO "/gpio%d/direction", dev->pin);

    int direction = open(filepath, O_RDWR);

    if (direction == -1) {
        return MRAA_ERROR_INVALID_RESOURCE;
    }

    char bu[MAX_SIZE];
    int length;
    switch(dir) {
        case MRAA_GPIO_OUT:
            length = snprintf(bu, sizeof(bu), "out");
            break;
        case MRAA_GPIO_IN:
            length = snprintf(bu, sizeof(bu), "in");
            break;
        default:
            close(direction);
            return MRAA_ERROR_FEATURE_NOT_IMPLEMENTED;
    }

    if (write(direction, bu, length*sizeof(char)) == -1) {
        close(direction);
        return MRAA_ERROR_INVALID_RESOURCE;
    }

    close(direction);
    return mraa_intel_edison_gpio_dir_postX(dev,dir);
}

#ifndef setWriteDir
void Adafruit_TFTLCD::setWriteDir(void) {
	if(tristate == nullptr)
	{
	    tristate = mraa_gpio_init_raw(214);
	    if (tristate == nullptr) {
	        printf("edison: Failed to initialise Arduino board TriState\n");
	    }
	    mraa_gpio_dir(tristate, MRAA_GPIO_OUT);
	}

    mraa_gpio_write(tristate, 0);

    for (int i = 0; i < 8; ++i)
	{
		mraa_gpio_dirX(m_dataPinCtx[i], MRAA_GPIO_OUT);
	}

    mraa_gpio_write(tristate, 1);
}
#endif

#ifndef setReadDir
void Adafruit_TFTLCD::setReadDir(void) {
	if(tristate == nullptr)
	{
	    tristate = mraa_gpio_init_raw(214);
	    if (tristate == nullptr) {
	        printf("edison: Failed to initialise Arduino board TriState\n");
	    }
	    mraa_gpio_dir(tristate, MRAA_GPIO_OUT);
	}

    mraa_gpio_write(tristate, 0);

    for (int i = 0; i < 8; ++i)
	{
		mraa_gpio_dirX(m_dataPinCtx[i], MRAA_GPIO_IN);
	}

    mraa_gpio_write(tristate, 1);
}
#endif

void Adafruit_TFTLCD::writeRegister8(uint8_t a, uint8_t d) {
	CD_COMMAND;
	write8(a);
	CD_DATA;
	write8(d);
}

void Adafruit_TFTLCD::writeRegister16(uint8_t a, uint16_t d) {
	CD_COMMAND;
	write8(a);
	CD_DATA;
	write16(d);
}

void Adafruit_TFTLCD::writeRegisterPair(uint8_t aH, uint8_t aL, uint16_t d) {
	// Set value of 2 TFT registers: Two 8-bit addresses (hi & lo), 16-bit value
	uint8_t hi = (d) >> 8, lo = (d);
	CD_COMMAND; write8(aH); CD_DATA; write8(hi);
	CD_COMMAND; write8(aL); CD_DATA; write8(lo);
}

void Adafruit_TFTLCD::writeRegister24(uint8_t r, uint32_t d) {
	CS_ACTIVE;
	CD_COMMAND;
	write8(r);
	CD_DATA;
	delayMicroseconds(10);
	write8(d >> 16);
	delayMicroseconds(10);
	write8(d >> 8);
	delayMicroseconds(10);
	write8(d);
	CS_IDLE;
}


void Adafruit_TFTLCD::writeRegister32(uint8_t r, uint32_t d) {
	CS_ACTIVE;
	CD_COMMAND;
	write8(r);
	CD_DATA;
	delayMicroseconds(10);
	write8(d >> 24);
	delayMicroseconds(10);
	write8(d >> 16);
	delayMicroseconds(10);
	write8(d >> 8);
	delayMicroseconds(10);
	write8(d);
	CS_IDLE;
}
