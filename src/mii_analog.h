/*
 * mii_analog.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

typedef struct mii_analog_t {
	struct {
		uint8_t  		value;
//		mii_cycles_t	decay;
		uint8_t			timer_id;
	}	v[4];
	bool		enabled;
} mii_analog_t;

struct mii_t;

void
mii_analog_init(
		struct mii_t *mii,
		mii_analog_t * analog );

void
mii_analog_access(
		struct mii_t *mii,
		mii_analog_t * analog,
		uint16_t addr,
		uint8_t * byte,
		bool write);
