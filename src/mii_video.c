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

#define VIDEO_RESOLUTION_X 280
#define VIDEO_RESOLUTION_Y 192

#define VIDEO_BYTES_PER_LINE 40
#define SCREEN_LINE_OFFSET 0x80
#define VIDEO_SEGMENT_OFFSET 0x28


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
	uint8_t 	l;
} mii_color_t;

#define HI_LUMA(r,g,b) \
		((uint8_t)(0.2126 * (r) + 0.7152 * (g) + 0.0722 * (b)))

#define HI_RGB(r,g,b)	{ \
		.rgb = (0xff000000 | ((b) << 16) | ((g) << 8) | (r)), \
		.l = HI_LUMA(r,g,b) \
	}

/* this 'dims' the colors for every second line of pixels
 * This is a very very cheap filter but it works really well!
 */
#define C_SCANLINE_MASK 0xffc0c0c0

/* These are the 'official' RGB colors for apple II,
 * Well not really, it is just ONE interpreation of many, we could possibly
 * make some sort of color lookup table to allow switching them on the fly?
 */
#define	C_BLACK		HI_RGB(0x00, 0x00, 0x00)	// black
#define	C_PURPLE 	HI_RGB(0xff, 0x44, 0xfd)	// purple
#define	C_GREEN 	HI_RGB(0x14, 0xf5, 0x3c)	// green
#define	C_BLUE		HI_RGB(0x14, 0xcf, 0xfd)	// blue
#define	C_ORANGE 	HI_RGB(0xff, 0x6a, 0x3c)	// orange
#define	C_WHITE		HI_RGB(0xff, 0xff, 0xff)	// white
#define C_MAGENTA	HI_RGB(0xe3, 0x1e, 0x60)	// magenta
#define C_DARKBLUE	HI_RGB(0x60, 0x4e, 0xbd)	// dark blue
#define C_DARKGREEN HI_RGB(0x00, 0xa3, 0x60)	// dark green
#define C_GRAY1 	HI_RGB(0x9c, 0x9c, 0x9c)	// gray 1
#define C_GRAY2 	HI_RGB(0x9c, 0x9c, 0x9c)	// gray 2
#define C_LIGHTBLUE HI_RGB(0xd0, 0xc3, 0xff)	// light blue
#define C_BROWN 	HI_RGB(0x60, 0x72, 0x03)	// brown
#define C_PINK  	HI_RGB(0xff, 0xa0, 0xd0)	// pink
#define C_YELLOW 	HI_RGB(0xd0, 0xdd, 0x8d)	// yellow
#define C_AQUA  	HI_RGB(0x72, 0xff, 0xd0)	// aqua

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
static const mii_color_t dhires_colors[] = {
[0x0] = C_BLACK,	[0x1] = C_MAGENTA,	[0x2] = C_BROWN,	[0x3] = C_ORANGE,
[0x4] = C_DARKGREEN,[0x5] = C_GRAY1,	[0x6] = C_GREEN,	[0x7] = C_YELLOW,
[0x8] = C_DARKBLUE,	[0x9] = C_PURPLE,	[0xa] = C_GRAY2,	[0xb] = C_PINK,
[0xc] = C_BLUE,		[0xd] = C_LIGHTBLUE,[0xe] = C_AQUA,		[0xf] = C_WHITE,
};

static const mii_color_t hires_colors[] = {
	C_BLACK,
	C_PURPLE,
	C_GREEN,
	C_GREEN,
	C_PURPLE,
	C_BLUE,
	C_ORANGE,
	C_ORANGE,
	C_BLUE,
	C_WHITE,
};

static const mii_color_t mono[3][2] = {
	{ C_BLACK, C_WHITE },
	{ C_BLACK, C_GREEN },
	{ C_BLACK, C_AMBER },
};


// TODO redo the hires decoder by reversing bits line by line...
static inline uint8_t reverse8(uint8_t b) {
	b = (b & 0b11110000) >> 4 | (b & 0b00001111) << 4;
	b = (b & 0b11001100) >> 2 | (b & 0b00110011) << 2;
	b = (b & 0b10101010) >> 1 | (b & 0b01010101) << 1;
	return b;
}
static inline uint8_t reverse4(uint8_t b) {
	b = (b & 0b0001) << 3 | (b & 0b0010) << 1 |
		(b & 0b0100) >> 1 | (b & 0b1000) >> 3;
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
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	bool	text 	= SW_GETSTATE(mii, SWTEXT);
	bool 	page2 	= SW_GETSTATE(mii, SWPAGE2);
	bool 	col80 	= SW_GETSTATE(mii, SW80COL);
	bool 	store80 = SW_GETSTATE(mii, SW80STORE);
	bool	mixed	= SW_GETSTATE(mii, SWMIXED);
	bool 	hires 	= SW_GETSTATE(mii, SWHIRES);
	bool 	dhires 	= SW_GETSTATE(mii, SWDHIRES);

	pt_start(mii->video.state);
	/*
		We cheat and draw a whole line at a time, then 'wait' until
		horizontal blanking, then wait until vertical blanking.
	*/
	do {
		// 'clear' VBL flag. Flag is 0 during retrace
		mii_bank_poke(main, SWVBL, 0x80);
		if (mixed && !text) {
			text = mii->video.line >= 192 - (4 * 8);
		}
		const int mode = mii->video.color_mode;
		// http://www.1000bit.it/support/manuali/apple/technotes/aiie/tn.aiie.03.html
		if (hires && !text && col80 && dhires) {
			if (store80)
				page2 = 0;
			uint8_t reg = mii_bank_peek(main, SWAN3_REGISTER);
			uint16_t a = (0x2000 + (0x2000 * page2));
			a = _mii_line_to_video_addr(a, mii->video.line);
			uint32_t * screen = mii->video.pixels +
								(mii->video.line * MII_VRAM_WIDTH * 2);
			uint32_t * l2 = screen + MII_VRAM_WIDTH;

			mii_bank_t * aux = &mii->bank[MII_BANK_AUX];

			if (reg == 0 || mode != MII_VIDEO_COLOR) {
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
			} else {	// color mode
				int x = 0, dx = 0;
				do {
					uint64_t run = 0;
					// get 8 bytes, so we get 8*7=56 bits for 14 pixels
					for (int bx = 0; bx < 8 && x < 80; bx++, x++) {
						uint8_t b = mii_bank_peek(
										x & 1 ? main : aux, a + (x / 2));
						run |= ((uint64_t)(b & 0x7f) << (bx * 7));
					}
					for (int px = 0; px < 14 && dx < 80*2; px++, dx++) {
						uint8_t pixel = reverse4(run & 0xf);
						run >>= 4;
						uint32_t col = dhires_colors[pixel].rgb;
						*screen++ = col;
						*screen++ = col;
						*screen++ = col;
						*screen++ = col;
						*l2++ = col & C_SCANLINE_MASK;
						*l2++ = col & C_SCANLINE_MASK;
						*l2++ = col & C_SCANLINE_MASK;
						*l2++ = col & C_SCANLINE_MASK;
					}
				} while (x < 80);
			}
		} else if (hires && !text) {
			if (store80)
				page2 = 0;
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
		} else {
			if (store80)
				page2 = 0;
			uint16_t a = (0x400 + (0x400 * page2));
			int i = mii->video.line >> 3;
			a += ((i & 0x07) << 7) | ((i >> 3) << 5) | ((i >> 3) << 3);

			mii_bank_t * aux = &mii->bank[MII_BANK_AUX];
			uint32_t * screen = mii->video.pixels +
								(mii->video.line * MII_VRAM_WIDTH * 2);
			uint32_t * l2 = screen + MII_VRAM_WIDTH;
			for (int x = 0; x < 40 + (40 * col80); x++) {
				uint8_t c = 0;
				if (col80)
					c = mii_bank_peek(
						x & 1 ? main : aux, a + (x >> 1));
				else
					c = mii_bank_peek(main, a + x);
				if (text) {
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
				} else { // lores graphics
					int lo_line = mii->video.line / 4;
					c = c >> ((lo_line & 1) * 4);

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
						c |= (c << 4);
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
		}
		mii->video.line++;
		if (mii->video.line == 192) {
			mii->video.line = 0;
			res = MII_VIDEO_H_CYCLES * mii->speed;
			pt_yield(mii->video.state);
			mii_bank_poke(main, SWVBL, 0x00);
			res = MII_VBL_UP_CYCLES * mii->speed;
			mii->video.frame_count++;
			pt_yield(mii->video.state);
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
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	switch (addr) {
		case SWALTCHARSETOFF:
		case SWALTCHARSETON:
			if (!write) break;
			res = true;
			SW_SETSTATE(mii, SWALTCHARSET, addr & 1);
			mii_bank_poke(main, SWALTCHARSET, (addr & 1) << 7);
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
				*byte = mii_bank_peek(main, addr);
			break;
		case SW80COLOFF:
		case SW80COLON:
			if (!write) break;
			res = true;
			SW_SETSTATE(mii, SW80COL, addr & 1);
			mii_bank_poke(main, SW80COL, (addr & 1) << 7);
			break;
		case SWDHIRESOFF: //  0xc05f,
		case SWDHIRESON: { // = 0xc05e,
			res = true;
			uint8_t an3 = !!mii_bank_peek(main, SWAN3);
			bool an3_on = !!(addr & 1); // 5f is ON, 5e is OFF
			uint8_t reg = mii_bank_peek(main, SWAN3_REGISTER);
			if (an3_on && !an3) {
				uint8_t bit = !!mii_bank_peek(main, SW80COL);
				reg = ((reg << 1) | bit) & 3;
			//	printf("VIDEO 80:%d REG now %x\n", bit, reg);
				mii_bank_poke(main, SWAN3_REGISTER, reg);
			}
			mii_bank_poke(main, SWAN3, an3_on);
		//	printf("DHRES IS %s mode:%d\n",
		//			(addr & 1) ? "OFF" : "ON", reg);
			mii->sw_state = (mii->sw_state & ~M_SWDHIRES) |
							(!(addr & 1) << B_SWDHIRES);
			SW_SETSTATE(mii, SWDHIRES, !(addr & 1));
			mii_bank_poke(main, SWRDDHIRES, (!(addr & 1)) << 7);
		}	break;
		case SWTEXTOFF:
		case SWTEXTON:
			res = true;
			SW_SETSTATE(mii, SWTEXT, addr & 1);
			mii_bank_poke(main, SWTEXT, (addr & 1) << 7);
			break;
		case SWMIXEDOFF:
		case SWMIXEDON:
			res = true;
			SW_SETSTATE(mii, SWMIXED, addr & 1);
			mii_bank_poke(main, SWMIXED, (addr & 1) << 7);
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
}



static void
_mii_mish_video(
		void * param,
		int argc,
		const char * argv[])
{
	mii_t * mii = param;

	if (!argv[1] || !strcmp(argv[1], "list")) {
		for (int i = 0; i < 16; i++) {
			printf("%01x: %08x %08x %08x\n", i,
					lores_colors[0][i].rgb,
					lores_colors[1][i].rgb,
					dhires_colors[i].rgb);
		}
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
