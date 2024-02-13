/*
 * mii_floppy.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include "mii_dd.h"

// for NIB and others. can be bigger on .WOZ
#define MII_FLOPPY_DEFAULT_TRACK_SIZE	6656
// track containing random bits
#define MII_FLOPPY_RANDOM_TRACK_ID		35

enum {
	MII_FLOPPY_WP_MANUAL 		= (1 << 0),	// write protect by the user
	MII_FLOPPY_WP_RO_FILE		= (1 << 1),	// file is read only
	MII_FLOPPY_WP_RO_FORMAT		= (1 << 2),	// File format doesn't do writes
};

typedef struct mii_floppy_track_t {
	uint8_t			dirty : 1;	// track has been written to
	uint32_t		bit_count;
	uint8_t 		data[6680];	// max suggested by WOZ spec
} mii_floppy_track_t;

typedef struct mii_floppy_t {
	uint8_t 		write_protected : 3, id : 2;
	uint8_t 		bit_timing;		// 0=32 (default)
	uint8_t			motor;			// motor is on
	uint8_t 		stepper;		// last step we did...
	uint8_t 		qtrack;			// quarter track we are on
	uint32_t		bit_position;
	uint8_t			tracks_dirty;	// needs saving
	uint8_t 		track_id[35 * 4];
	mii_floppy_track_t tracks[36];
} mii_floppy_t;

/*
 * Initialize a floppy structure with random data. It is not formatted, just
 * ready to use for loading a disk image, or formatting as a 'virgin' disk.
 */
void
mii_floppy_init(
		mii_floppy_t *f);

int
mii_floppy_load(
	mii_floppy_t *f,
	mii_dd_file_t *file );

int
mii_floppy_update_tracks(
		mii_floppy_t *f,
		mii_dd_file_t *file );
