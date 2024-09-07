/*
 * mii_disk2.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "mii.h"
#include "mii_floppy.h"

enum {
	// bits used to address the LSS ROM using lss_mode
	Q7_WRITE_BIT	= 3,
	Q6_LOAD_BIT		= 2,
	QA_BIT			= 1,
	RP_BIT			= 0,
};

typedef struct mii_card_disk2_t {
	mii_t *			mii;
	mii_dd_t 		drive[2];
	mii_floppy_t	floppy[2];
	uint8_t 		selected;

	uint8_t 		timer_off;
	uint8_t 		timer_lss;

	uint8_t 		iwm_mode;		// IWM mode register -- fake for now
	uint8_t 		write_register;
	uint8_t 		head : 4;		// bits are shifted in there
	uint16_t 		clock;			// LSS clock cycles, read a bit when 0
	uint8_t 		lss_state : 4,	// Sequence state
					lss_mode : 4;	// WRITE/LOAD/SHIFT/QA/RP etc
	uint8_t 		lss_prev_state;	// for write bit
	uint8_t 		lss_skip;
	uint8_t 		data_register;

	uint64_t 		debug_last_write, debug_last_duration;
	mii_vcd_t 		*vcd;
	mii_signal_t 	*sig;
} mii_card_disk2_t;


void
_mii_disk2_vcd_debug(
	mii_card_disk2_t *c,
	int on);
