/*
 * mii_video.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "mii_types.h"

// TODO move VRAM stuff to somewhere else
/*
	These are currently way bigger than needed, because they need to be
	a power of two to easily map to an opengl texture.
 */
#define MII_VRAM_WIDTH		1024
#define MII_VRAM_HEIGHT		512

#define MII_VIDEO_WIDTH		(280 * 2)
#define MII_VIDEO_HEIGHT	(192 * 2)

enum {
	MII_VIDEO_COLOR		= 0,
	MII_VIDEO_GREEN,
	MII_VIDEO_AMBER,
};

struct mii_t;

typedef struct mii_video_t {
		void *			state;	// protothread state in mii_video.c
		uint8_t			line;	// current line
		bool 			vbl_irq; // VBL IRQ emabled (set by mouse card)
		mii_cycles_t	wait;	// 'wait until' cycle marker
		uint32_t 		pixels[MII_VRAM_WIDTH * MII_VRAM_HEIGHT];
		uint32_t		frame_count; // incremented every frame
		uint32_t		frame_drawn;
		uint8_t 		color_mode;	// color, green, amber
} mii_video_t;

bool
mii_access_video(
		struct mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write);
void
mii_video_run(
	struct mii_t *mii);


