/*
 * mii_analog.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "mii_types.h"


typedef struct mii_analog_t {
	struct {
		uint8_t  		value;
		mii_cycles_t	decay;
	}	v[4];
} mii_analog_t;

struct mii_t;

void
mii_analog_init(
		struct mii_t *mii,
		mii_analog_t * analog );

void
mii_analog_access(
		mii_t *mii,
		mii_analog_t * analog,
		uint16_t addr,
		uint8_t * byte,
		bool write);
