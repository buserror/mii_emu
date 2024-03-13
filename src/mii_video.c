/*
 * mii_video.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_rom_iiee_video.h"
#include "mii_sw.h"
#include "minipt.h"


enum {
	// https://rich12345.tripod.com/aiivideo/vbl.html
	MII_VBL_DOWN_CYCLES = 12480,
	MII_VBL_UP_CYCLES = 4550,
	MII_VIDEO_H_CYCLES = 40,
	MII_VIDEO_HB_CYCLES = 25,
};

/*
 * Colors were lifted from
 * https://comp.sys.apple2.narkive.com/lTSrj2ZI/apple-ii-colour-rgb
 * and
 * https://www.mrob.com/pub/xapple2/colors.html
 */
typedef struct mii_color_t {
	uint32_t 	rgb;
	uint32_t 	l : 8, index : 8;
} mii_color_t;

#define HI_LUMA(r,g,b) \
		((uint8_t)(0.2126 * (r) + 0.7152 * (g) + 0.0722 * (b)))

/*
 * You migth have to tweak this for performance reasons. At least on nVidia
 * cards, GL_BGRA is faster than GL_RGBA.
 */
#define HI_RGB(r,g,b)	{ \
		.rgb = (0xff000000 | ((b) << 16) | ((g) << 8) | (r)), \
		.l = HI_LUMA(r,g,b) \
	}

/* this 'dims' the colors for every second line of pixels
 * This is a very very cheap filter but it works really well!
 */
#define C_SCANLINE_MASK 0xffc0c0c0

typedef struct mii_video_clut_t {
	mii_color_t 	lores[2][16];	// lores (main, and aux page)
	mii_color_t 	dhires[16];
	mii_color_t 	hires[10];
	mii_color_t 	text[2];		// text
	mii_color_t 	mono[2];		// DHRES mono mode
} mii_video_clut_t;

/* These are the 'official' RGB colors for apple II,
 * Well not really, it is just ONE interpreation of many, we could possibly
 * make some sort of color lookup table to allow switching them on the fly?
 */
#define	C_BLACK		HI_RGB(0x00, 0x00, 0x00)
#define	C_PURPLE 	HI_RGB(0xff, 0x44, 0xfd)
#define	C_GREEN 	HI_RGB(0x14, 0xf5, 0x3c)
#define	C_BLUE		HI_RGB(0x14, 0xcf, 0xfd)
#define	C_ORANGE 	HI_RGB(0xff, 0x6a, 0x3c)
#define	C_WHITE		HI_RGB(0xff, 0xff, 0xff)
#define C_MAGENTA	HI_RGB(0xe3, 0x1e, 0x60)
#define C_DARKBLUE	HI_RGB(0x60, 0x4e, 0xbd)
#define C_DARKGREEN HI_RGB(0x00, 0xa3, 0x60)
#define C_GRAY1 	HI_RGB(0x9c, 0x9c, 0x9c)
#define C_GRAY2 	HI_RGB(0x9c, 0x9c, 0x9c)
#define C_LIGHTBLUE HI_RGB(0xd0, 0xc3, 0xff)
#define C_BROWN 	HI_RGB(0x60, 0x72, 0x03)
#define C_PINK  	HI_RGB(0xff, 0xa0, 0xd0)
#define C_YELLOW 	HI_RGB(0xd0, 0xdd, 0x8d)
#define C_AQUA  	HI_RGB(0x72, 0xff, 0xd0)

enum mii_video_color_mode_e {
	CI_BLACK = 0,
	CI_PURPLE, CI_GREEN, CI_BLUE, CI_ORANGE, CI_WHITE, CI_MAGENTA,
	CI_DARKBLUE,CI_DARKGREEN,CI_GRAY1,CI_GRAY2,CI_LIGHTBLUE,
	CI_BROWN,CI_PINK,CI_YELLOW,CI_AQUA,
};

static const mii_color_t base_color[16] = {
	[CI_BLACK] 		= C_BLACK,
	[CI_PURPLE] 	= C_PURPLE,
	[CI_GREEN] 		= C_GREEN,
	[CI_BLUE] 		= C_BLUE,
	[CI_ORANGE] 	= C_ORANGE,
	[CI_WHITE] 		= C_WHITE,
	[CI_MAGENTA] 	= C_MAGENTA,
	[CI_DARKBLUE] 	= C_DARKBLUE,
	[CI_DARKGREEN] 	= C_DARKGREEN,
	[CI_GRAY1] 		= C_GRAY1,
	[CI_GRAY2] 		= C_GRAY2,
	[CI_LIGHTBLUE] 	= C_LIGHTBLUE,
	[CI_BROWN] 		= C_BROWN,
	[CI_PINK] 		= C_PINK,
	[CI_YELLOW] 	= C_YELLOW,
	[CI_AQUA] 		= C_AQUA,
};


// this is not an official color, just 'my' interpretation of an amber screen
#define C_AMBER 	HI_RGB(0xfd, 0xcf, 0x14)	// amber

static const mii_color_t lores_colors[2][16] = { {
[0x0] = C_BLACK,	[0x1] = C_MAGENTA,	[0x2] = C_DARKBLUE,	[0x3] = C_PURPLE,
[0x4] = C_DARKGREEN,[0x5] = C_GRAY1,	[0x6] = C_BLUE,		[0x7] = C_LIGHTBLUE,
[0x8] = C_BROWN,	[0x9] = C_ORANGE,	[0xa] = C_GRAY2,	[0xb] = C_PINK,
[0xc] = C_GREEN,	[0xd] = C_YELLOW,	[0xe] = C_AQUA,		[0xf] = C_WHITE,
},{
[0x0] = C_BLACK,	[0x1] = C_DARKBLUE,	[0x2] = C_DARKGREEN,[0x3] = C_BLUE,
[0x4] = C_BROWN,	[0x5] = C_GRAY2,	[0x6] = C_GREEN,	[0x7] = C_AQUA,
[0x8] = C_MAGENTA,	[0x9] = C_PURPLE,	[0xa] = C_GRAY1,	[0xb] = C_LIGHTBLUE,
[0xc] = C_ORANGE,	[0xd] = C_PINK,		[0xe] = C_YELLOW,	[0xf] = C_WHITE,
} };
static const mii_color_t dhires_colors[16] = {
[0x0] = C_BLACK,	[0x1] = C_MAGENTA,	[0x2] = C_BROWN,	[0x3] = C_ORANGE,
[0x4] = C_DARKGREEN,[0x5] = C_GRAY1,	[0x6] = C_GREEN,	[0x7] = C_YELLOW,
[0x8] = C_DARKBLUE,	[0x9] = C_PURPLE,	[0xa] = C_GRAY2,	[0xb] = C_PINK,
[0xc] = C_BLUE,		[0xd] = C_LIGHTBLUE,[0xe] = C_AQUA,		[0xf] = C_WHITE,
};
static const mii_color_t hires_colors[10] = {
	C_BLACK, C_PURPLE, C_GREEN, C_GREEN, C_PURPLE,
	C_BLUE, C_ORANGE, C_ORANGE, C_BLUE, C_WHITE,
};
static const mii_color_t mono[3][2] = {
	{ C_BLACK, C_WHITE },
	{ C_BLACK, C_GREEN },
	{ C_BLACK, C_AMBER },
};


// TODO redo the hires decoder by reversing bits line by line...
// Used for DHRES decoding
static inline uint8_t reverse4(uint8_t b) {
	b = (b & 0b0001) << 3 | (b & 0b0010) << 1 |
		(b & 0b0100) >> 1 | (b & 0b1000) >> 3;
	return b;
}
static inline uint8_t reverse8(uint8_t b) {
	b = reverse4(b) << 4 | reverse4(b >> 4);
	return b;
}
static inline uint16_t
_mii_line_to_video_addr(
		uint16_t addr,
		uint8_t line)
{
	addr += ((line & 0x07) << 10) |
				 (((line >> 3) & 7) << 7) |
					((line >> 6) << 5) | ((line >> 6) << 3);
	return addr;
}

#define MII_VIDEO_BANK MII_BANK_AUX_BASE

static void
_mii_line_render_dhires_mono(
		mii_t *mii,
		uint8_t mode,
		bool page2 )
{
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii_bank_t * aux = &mii->bank[MII_VIDEO_BANK];

	uint16_t a = (0x2000 + (0x2000 * page2));
	a = _mii_line_to_video_addr(a, mii->video.line);
	uint32_t * screen = mii->video.pixels +
						(mii->video.line * MII_VRAM_WIDTH * 2);
	uint32_t * l2 = screen + MII_VRAM_WIDTH;

	const uint32_t clut[2] = {
			mono[mode][0].rgb,
			mono[mode][1].rgb };
	for (int x = 0; x < 40; x++) {
		uint32_t ext = (mii_bank_peek(aux, a + x) & 0x7f) |
						((mii_bank_peek(main, a + x) & 0x7f) << 7);
		for (int bi = 0; bi < 14; bi++) {
			uint8_t pixel = (ext >> bi) & 1;
			uint32_t col = clut[pixel];
			*screen++ = col;
			*l2++ = col & C_SCANLINE_MASK;
		}
	}
}

/* get exactly 4 bits from position bit from the buffer */
static inline uint8_t
_mii_get_1bits(
		uint8_t * buffer,
		int bit)
{
	int in_byte = (bit) / 8;
	int in_bit 	= 7 - ((bit) % 8);
	uint8_t b = (buffer[in_byte] >> in_bit) & 1;
	return b;
}


static void
_mii_line_render_dhires_color(
		mii_t *mii,
		uint8_t mode,
		bool page2 )
{
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii_bank_t * aux = &mii->bank[MII_VIDEO_BANK];

	uint16_t a = (0x2000 + (0x2000 * page2));
	a = _mii_line_to_video_addr(a, mii->video.line);
	uint32_t * screen = mii->video.pixels +
						(mii->video.line * MII_VRAM_WIDTH * 2);
	uint32_t * l2 = screen + MII_VRAM_WIDTH;

	uint8_t bits[71] = { 0 };

	for (int x = 0; x < 80; x++) {
		uint8_t b = mii_bank_peek(x & 1 ? main : aux, a + (x / 2));
		// this reverse the 7 bits of each bytes into the bit buffer
		for (int i = 0; i < 7; i++) {
			int out_index = 2 + (x * 7) + i;
			int out_byte = out_index / 8;
			int out_bit = 7 - (out_index % 8);
			int bit = (b >> i) & 1;
			bits[out_byte] |= bit << out_bit;
		}
	}
	for (int i = 0, d = 2; i < 560; i++, d++) {
        const uint8_t pixel =
			(_mii_get_1bits(bits, i + 3) << (3 - ((d + 3) % 4))) +
			(_mii_get_1bits(bits, i + 2) << (3 - ((d + 2) % 4))) +
			(_mii_get_1bits(bits, i + 1) << (3 - ((d + 1) % 4))) +
			(_mii_get_1bits(bits, i) << (3 - (d % 4)));
		uint32_t col = dhires_colors[pixel].rgb;
		*screen++ = col;
		*l2++ = col & C_SCANLINE_MASK;
	}
}


static void
_mii_line_render_hires(
		mii_t *mii,
		uint8_t mode,
		bool page2 )
{
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	uint16_t a = (0x2000 + (0x2000 * page2));
	a = _mii_line_to_video_addr(a, mii->video.line);
	uint32_t * screen = mii->video.pixels +
						(mii->video.line * MII_VRAM_WIDTH * 2);
	uint32_t * l2 = screen + MII_VRAM_WIDTH;

	uint8_t b0 = 0;
	uint8_t b1 = mii_bank_peek(main, a + 0);
	for (int x = 0; x < 40; x++) {
		uint8_t b2 	= mii_bank_peek(main, a + x + 1);
		// last 2 pixels, current 7 pixels, next 2 pixels
		uint16_t run =  ((b0 & 0x60) >> ( 5 )) |
						((b1 & 0x7f) << ( 2 )) |
						((b2 & 0x03) << ( 9 ));
		int odd = (x & 1) << 1;
		int offset = (b1 & 0x80) >> 5;
		if (mode == MII_VIDEO_COLOR) {
			for (int i = 0; i < 7; i++) {
				uint8_t left = (run >> (1 + i)) & 1;
				uint8_t pixel = (run >> (2 + i)) & 1;
				uint8_t right = (run >> (3 + i)) & 1;

				int idx = 0;	// black
				if (pixel) {
					if (left || right) {
						idx = 9;	// white
					} else {
						idx = offset + odd + (i & 1) + 1;
					}
				} else {
					if (left && right) {
						idx = offset + odd + 1 - (i & 1) + 1;
					}
				}
				uint32_t col = hires_colors[idx].rgb;
				*screen++ = col;
				*screen++ = col;
				*l2++ = col & C_SCANLINE_MASK;
				*l2++ = col & C_SCANLINE_MASK;
			}
		} else {
			for (int i = 0; i < 7; i++) {
				uint8_t pixel = (run >> (2 + i)) & 1;
				uint32_t col = mono[mode][pixel].rgb;
				*screen++ = col;
				*screen++ = col;
				*l2++ = col & C_SCANLINE_MASK;
				*l2++ = col & C_SCANLINE_MASK;
			}
		}
		b0 = b1;
		b1 = b2;
	}
}

static void
_mii_line_render_text(
		mii_t *mii,
		uint8_t mode,
		bool page2 )
{
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii_bank_t * aux = &mii->bank[MII_VIDEO_BANK];

	uint16_t a = (0x400 + (0x400 * page2));
	int i = mii->video.line >> 3;
	a += ((i & 0x07) << 7) | ((i >> 3) << 5) | ((i >> 3) << 3);

	bool 	col80 	= SW_GETSTATE(mii, SW80COL);
	uint32_t * screen = mii->video.pixels +
						(mii->video.line * MII_VRAM_WIDTH * 2);
	uint32_t * l2 = screen + MII_VRAM_WIDTH;

	for (int x = 0; x < 40 + (40 * col80); x++) {
		uint8_t c = 0;
		if (col80)
			c = mii_bank_peek(x & 1 ? main : aux, a + (x >> 1));
		else
			c = mii_bank_peek(main, a + x);

		bool altset = SW_GETSTATE(mii, SWALTCHARSET);
		int flash = mii->video.frame_count & 0x10;
		if (!altset) {
			if (c >= 0x40 && c <= 0x7f) {
				if (flash)
					c -= 0x40;
				else
					c += 0x40;
			}
		}
		const uint8_t * rom = iie_enhanced_video + (c << 3);
		uint8_t bits = rom[mii->video.line & 0x07];
		for (int pi = 0; pi < 7; pi++) {
			uint8_t pixel = (bits >> pi) & 1;
			uint32_t col = mono[mode][!pixel].rgb;
			*screen++ = col;
			*l2++ = col & C_SCANLINE_MASK;
			if (!col80) {
				*screen++ = col;
				*l2++ = col & C_SCANLINE_MASK;
			}
		}
	}
}

static void
_mii_line_render_lores(
		mii_t *mii,
		uint8_t mode,
		bool page2 )
{
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii_bank_t * aux = &mii->bank[MII_VIDEO_BANK];

	uint16_t a = (0x400 + (0x400 * page2));
	int i = mii->video.line >> 3;
	a += ((i & 0x07) << 7) | ((i >> 3) << 5) | ((i >> 3) << 3);

	bool 	col80 	= SW_GETSTATE(mii, SW80COL);
	uint32_t * screen = mii->video.pixels +
						(mii->video.line * MII_VRAM_WIDTH * 2);
	uint32_t * l2 = screen + MII_VRAM_WIDTH;

	for (int x = 0; x < 40 + (40 * col80); x++) {
		uint8_t c = 0;
		if (col80)
			c = mii_bank_peek(x & 1 ? main : aux, a + (x >> 1));
		else
			c = mii_bank_peek(main, a + x);

		int lo_line = mii->video.line / 4;
		c = c >> ((lo_line & 1) * 4);
		c |= (c << 4);
		if (mode == MII_VIDEO_COLOR) {
			uint32_t pixel = lores_colors[(x & col80) ^ col80][c & 0x0f].rgb;
			for (int pi = 0; pi < 7; pi++) {
				*screen++ = pixel;
				*l2++ = pixel & C_SCANLINE_MASK;
				if (!col80) {
					*screen++ = pixel;
					*l2++ = pixel & C_SCANLINE_MASK;
				}
			}
		} else {
			/* Not sure at all this should be rendered like this,
				* but I can't find a reference on how to render low
				* res graphics in mono */
			for (int pi = 0; pi < 7; pi++) {
				uint32_t pixel = mono[mode][c & 1].rgb;
				c >>= 1;
				*screen++ = pixel;
				*l2++ = pixel & C_SCANLINE_MASK;
				if (!col80) {
					*screen++ = pixel;
					*l2++ = pixel & C_SCANLINE_MASK;
				}
			}
		}
	}
}

/*
 * This return the correct line drawing function callback for the mode
 * and softswitches
 */
static mii_video_line_drawing_cb
_mii_video_get_line_render_cb(
		mii_t *mii,
		uint32_t sw_state)
{
	bool	text 	= SWW_GETSTATE(sw_state, SWTEXT);
	bool 	col80 	= SWW_GETSTATE(sw_state, SW80COL);
	bool 	hires 	= SWW_GETSTATE(sw_state, SWHIRES);
	bool 	dhires 	= SWW_GETSTATE(sw_state, SWDHIRES);

	mii_video_line_drawing_cb res = _mii_line_render_lores;
	if (hires && !text && col80 && dhires) {
		mii_bank_t * sw = &mii->bank[MII_BANK_SW];
		uint8_t reg = mii_bank_peek(sw, SWAN3_REGISTER);
		if (reg != 0 && mii->video.color_mode == MII_VIDEO_COLOR)
			res = _mii_line_render_dhires_color;
		else
			res = _mii_line_render_dhires_mono;
	} else if (hires && !text) {
		res = _mii_line_render_hires;
	} else if (text)
		res = _mii_line_render_text;
	return res;
}

/*
 * This is called when the video mode changes, and we need to update the
 * line drawing callback
 */
static void
_mii_video_mode_changed(
		mii_t *mii)
{
	uint32_t sw_state = mii->sw_state;

	mii->video.line_drawing = _mii_video_get_line_render_cb(mii, sw_state);
}

/*
 * This is the state of the video output
 * All timings lifted from https://rich12345.tripod.com/aiivideo/vbl.html
 *
 * This is a 'protothread' basically cooperative scheduling using an
 * old compiler trick. It's not a real thread, but it's a way to
 * write code that looks like a thread, and is easy to read.
 * The 'pt_start' macro starts the thread, and pt_yield() yields
 * the thread to the main loop.
 * The pt_end() macro ends the thread.
 * Remeber you cannot have locals in the thread, they must be
 * static or global.
 * *everything* before the pt_start call is ran every time, so you can use
 * that to reload some sort of state, as here, were we reload all the
 * video mode softswitches.
 *
 * This function is also a 'cycle timer' it returns the number of 6502
 * cycles to wait until being called again, so it mostly returns the
 * number of cycles until the next horizontal blanking between each lines,
 * but also the number of cycles until the next vertical blanking once
 * the last line is drawn.
 */
static uint64_t
mii_video_timer_cb(
	mii_t *mii,
	void *param)
{
	uint64_t res = MII_VIDEO_H_CYCLES * mii->speed;
	mii_bank_t * sw = &mii->bank[MII_BANK_SW];
	uint32_t sw_state = mii->sw_state;
	bool 	store80 = SWW_GETSTATE(sw_state, SW80STORE);
	bool 	page2 	= store80 ? 0 : SWW_GETSTATE(sw_state, SWPAGE2);

	pt_start(mii->video.state);
	/*
		We cheat and draw a whole line at a time, then 'wait' until
		horizontal blanking, then wait until vertical blanking.
	*/
	do {
		// 'clear' VBL flag. Flag is 0 during retrace
		mii_bank_poke(sw, SWVBL, 0x80);

		mii_video_line_drawing_cb line_drawing = mii->video.line_drawing;
		/* If we are in mixed mode past line 160, check if we need to
		 * switch from the 'main mode' callback to the text callback */
		const int mixed_line = 192 - (4 * 8);
		if (mii->video.line >= mixed_line) {
			bool	mixed	= SWW_GETSTATE(sw_state, SWMIXED);
			if (mixed) {
				uint32_t sw 	= sw_state;
				SWW_SETSTATE(sw, SWTEXT, 1);
				if (sw != sw_state)
					line_drawing = _mii_video_get_line_render_cb(mii, sw);
			}
		}
		const int mode = mii->video.color_mode;
		line_drawing(mii, mode, page2);

		mii->video.line++;
		if (mii->video.line == 192) {
			mii->video.line = 0;
			res = MII_VIDEO_H_CYCLES * mii->speed;
			pt_yield(mii->video.state);
			mii_bank_poke(sw, SWVBL, 0x00);
			res = MII_VBL_UP_CYCLES * mii->speed;
			mii->video.frame_count++;
			pt_yield(mii->video.state);
			// check if we need to switch the video mode, in case the UI switches
			// Color/mono palette etc
			_mii_video_mode_changed(mii);
		} else {
			res = (MII_VIDEO_H_CYCLES + MII_VIDEO_HB_CYCLES) *
									mii->speed;
			pt_yield(mii->video.state);
		}
	} while (1);
	pt_end(mii->video.state);
	return res;
}

bool
mii_access_video(
		mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	bool res = false;
	mii_bank_t * sw = &mii->bank[MII_BANK_SW];
	switch (addr) {
		case SWALTCHARSETOFF:
		case SWALTCHARSETON:
			if (!write) break;
			res = true;
			SW_SETSTATE(mii, SWALTCHARSET, addr & 1);
			mii_bank_poke(sw, SWALTCHARSET, (addr & 1) << 7);
			break;
		case SWVBL:
		case SW80COL:
		case SWTEXT:
		case SWMIXED:
		case SWPAGE2:
		case SWHIRES:
		case SWALTCHARSET:
		case SWRDDHIRES:
			res = true;
			if (!write)
				*byte = mii_bank_peek(sw, addr);
			break;
		case SW80COLOFF:
		case SW80COLON:
			if (!write) break;
			res = true;
			SW_SETSTATE(mii, SW80COL, addr & 1);
			mii_bank_poke(sw, SW80COL, (addr & 1) << 7);
			_mii_video_mode_changed(mii);
			break;
		case SWDHIRESOFF: //  0xc05f,
		case SWDHIRESON: { // = 0xc05e,
			res = true;
			uint8_t an3 = !!mii_bank_peek(sw, SWAN3);
			bool an3_on = !!(addr & 1); // 5f is ON, 5e is OFF
			uint8_t reg = mii_bank_peek(sw, SWAN3_REGISTER);
			if (an3_on && !an3) {
				uint8_t bit = !!mii_bank_peek(sw, SW80COL);
				reg = ((reg << 1) | bit) & 3;
				printf("VIDEO 80:%d REG now %x\n", bit, reg);
				mii_bank_poke(sw, SWAN3_REGISTER, reg);
			}
			mii_bank_poke(sw, SWAN3, an3_on);
			printf("DHRES IS %s mode:%d\n",
					(addr & 1) ? "OFF" : "ON", reg);
			mii->sw_state = (mii->sw_state & ~M_SWDHIRES) |
							(!(addr & 1) << B_SWDHIRES);
			SW_SETSTATE(mii, SWDHIRES, !(addr & 1));
			mii_bank_poke(sw, SWRDDHIRES, (!(addr & 1)) << 7);
			_mii_video_mode_changed(mii);
		}	break;
		case SWTEXTOFF:
		case SWTEXTON:
			res = true;
			SW_SETSTATE(mii, SWTEXT, addr & 1);
			mii_bank_poke(sw, SWTEXT, (addr & 1) << 7);
			_mii_video_mode_changed(mii);
			break;
		case SWMIXEDOFF:
		case SWMIXEDON:
			res = true;
			SW_SETSTATE(mii, SWMIXED, addr & 1);
			mii_bank_poke(sw, SWMIXED, (addr & 1) << 7);
			_mii_video_mode_changed(mii);
			break;
	}
	return res;
}

void
mii_video_init(
	mii_t *mii)
{
	mii->video.timer_id = mii_timer_register(mii,
				mii_video_timer_cb, NULL, MII_VIDEO_H_CYCLES, __func__);
	// start the DHRES in color
//	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii_bank_t * sw = &mii->bank[MII_BANK_SW];
	mii_bank_poke(sw, SWAN3_REGISTER, 1);
	_mii_video_mode_changed(mii);
}

/* given a RGB color r,g,b, print a table of 16 RGB colors that are graded
   from luminance 0 to 1 in that particular shade of color.
*/
void
mii_video_print_color_table(
		uint32_t rgb)
{
	uint8_t b = (rgb >> 16) & 0xff;
	uint8_t g = (rgb >> 8) & 0xff;
	uint8_t r = (rgb >> 0) & 0xff;
	uint8_t l = HI_LUMA(r, g, b);
	printf("// LUMA %d start color %02x %02x %02x\n{ ", l, r, g, b);
	for (int i = 0; i < 16; i++) {
		uint8_t ll = (l * i) / 15;
		uint8_t rr = (r * ll) / l;
		uint8_t gg = (g * ll) / l;
		uint8_t bb = (b * ll) / l;
		printf("%01x: %02x %02x %02x\n", i, rr, gg, bb);
	}
}


static void
_mii_mish_video(
		void * param,
		int argc,
		const char * argv[])
{
//	mii_t * mii = param;

	if (!argv[1] || !strcmp(argv[1], "list")) {
		for (int i = 0; i < 16; i++) {
			printf("%01x: %08x %08x %08x\n", i,
					lores_colors[0][i].rgb,
					lores_colors[1][i].rgb,
					dhires_colors[i].rgb);
		}
		return;
	}
	if (!strcmp(argv[1], "gradient")) {
		mii_video_print_color_table(lores_colors[0][1].rgb);

		return;
	}
}

#include "mish.h"

MISH_CMD_NAMES(video, "video");
MISH_CMD_HELP(video,
		"video: test patterns generator",
		" <default>: dump color tables"
		);
MII_MISH(video, _mii_mish_video);
