// Graphics library by ladyada/adafruit with init code from Rossum
// MIT license

#ifndef _ADAFRUIT_TFTLCD_H_
#define _ADAFRUIT_TFTLCD_H_

#include <mraa/gpio.h>
#include <stdint.h>


class Adafruit_TFTLCD {

public:

	Adafruit_TFTLCD(void);

	void     begin(uint16_t id);
	void     reset(void);
	void     setRotation(uint8_t x);
	void     setAddrWindow(int x1, int y1, int x2, int y2);
	void     flood(uint16_t color, uint32_t len);
	void     pushColors(uint16_t *data, uint32_t len, bool first);

	uint16_t color565(uint8_t r, uint8_t g, uint8_t b),
			readPixel(int16_t x, int16_t y),
			readID(void);

private:

	// These items may have previously been defined as macros
	// in pin_magic.h.  If not, function versions are declared:
	inline void write8(uint8_t value);
	inline void write16(uint16_t value);

	void setWriteDir(void),
	setReadDir(void),

	writeRegister8(uint8_t a, uint8_t d),
	writeRegister16(uint8_t a, uint16_t d),
	writeRegister24(uint8_t a, uint32_t d),
	writeRegister32(uint8_t a, uint32_t d),
	writeRegisterPair(uint8_t aH, uint8_t aL, uint16_t d),
	setLR(void);

	uint8_t  driver;

	mraa_gpio_context m_csPinCtx;
	mraa_gpio_context m_cdPinCtx;
	mraa_gpio_context m_wrPinCtx;
	mraa_gpio_context m_rdPinCtx;
	mraa_gpio_context m_dataPinCtx[8];
};

#endif
