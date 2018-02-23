//  Copyright (c) 2018
//  Eric Tremblay <habscup@gmail.com>
//
//  License: MIT (see LICENSE for details)

#ifndef neoPixel_h
#define neoPixel_h

#define NEO_GRB  	82 //((1 << 6) | (1 << 4) | (0 << 2) | (2))
#define MAGIC_T0H  	6UL | (0x8000) // 0.375us
#define MAGIC_T1H  	13UL | (0x8000) // 0.8125us
#define CTOPVAL 	20UL // 1.25us

#define NEOPIXEL_COUNT	8
#define LED_PIN			26

struct nsec18_pixels {
	uint16_t numBytes;
	uint8_t rOffset;
	uint8_t gOffset;
	uint8_t bOffset;
	uint8_t *pixels;
};

int nsec_neopixel_init(void);
void nsec_neoPixel_clean(void);
void nsec_set_pixel_color(uint16_t n, uint8_t r, uint8_t g, uint8_t b);
void nsec_neopixel_show(void);

#endif /* neoPixel_h */
