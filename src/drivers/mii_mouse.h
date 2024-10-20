/*
 * mii_mouse.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

	// mins/max are set by the card, x,y,button are set by the UI
typedef struct mii_mouse_t {
	bool			enabled; // read only, set by driver
	int16_t 		min_x, max_x,
					min_y, max_y; // set by driver when enabled
	uint16_t 		x, y;
	bool 			button;
} mii_mouse_t;
