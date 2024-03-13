/*
 * mii_video.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// TODO move VRAM stuff to somewhere else
#define MII_VIDEO_WIDTH		(280 * 2)
#define MII_VIDEO_HEIGHT	(192 * 2)
// in case padding is needed, these can be changed

#if 0	// Loads of padding to align to power of 2 sizes
#define MII_VRAM_WIDTH		(1024 * 4)
#define MII_VRAM_HEIGHT		512
#else
#define MII_VRAM_WIDTH		(MII_VIDEO_WIDTH * 4)
#define MII_VRAM_HEIGHT		MII_VIDEO_HEIGHT
#endif

enum {
	MII_VIDEO_COLOR		= 0,
	MII_VIDEO_GREEN,
	MII_VIDEO_AMBER,
	MII_VIDEO_MODE_COUNT
};

struct mii_t;

typedef void (*mii_video_line_drawing_cb)(
						mii_t *mii, uint8_t mode, bool page2 );

typedef struct mii_video_t {
	void *			state;	// protothread state in mii_video.c
	uint8_t 		timer_id;	// timer id for the video thread
	uint8_t			line;	// current line
	uint32_t 		pixels[MII_VRAM_WIDTH * MII_VRAM_HEIGHT];
	uint32_t		frame_count; // incremented every frame
	uint32_t		frame_drawn;
	uint8_t 		color_mode;	// color, green, amber
	// function pointer to the line drawing function
	mii_video_line_drawing_cb	line_drawing;
} mii_video_t;

bool
mii_access_video(
		struct mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write);
void
mii_video_init(
		mii_t *mii);


