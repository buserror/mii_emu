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

#define MII_FLOPPY_MAX_TRACK_SIZE		6656
#define MII_FLOPPY_TRACK_COUNT			35


#define DE44(a, b) 	((((a) & 0x55) << 1) | ((b) & 0x55))

/*
 * Reasons for write protect. Ie checkbox in the UI, or file format
 * doesn't support writes, or the file has no write permissions.
 */
enum {
	MII_FLOPPY_WP_MANUAL 		= (1 << 0),	// write protect by the user
	MII_FLOPPY_WP_RO_FILE		= (1 << 1),	// file is read only
	MII_FLOPPY_WP_RO_FORMAT		= (1 << 2),	// File format doesn't do writes
};

typedef struct mii_floppy_track_map_t {
	struct {
		int32_t  			hsync, dsync;	// number of sync bits
		uint32_t			header, data;	// position of the header and data
		uint16_t 			crc;			// crc of the data
		uint32_t			dsk_position;	// position in the DSK file
		uint32_t 			nib_position;	// position in the nibble stream
	}				sector[16];
} mii_floppy_track_map_t;

typedef struct mii_floppy_track_t {
	uint8_t					dirty : 1,		// track has been written to
							has_map : 1,	// track has a valid map
							virgin : 1;		// track is not loaded/formatted
	uint32_t				bit_count;
	mii_floppy_track_map_t 	map;			// position of all the sectors
} mii_floppy_track_t;


// 32 bytes of track data corresponds to one byte of heatmap
#define MII_FLOPPY_HM_HIT_SIZE 32
// thats 208 bytes per track or about 7KB*2 for the whole disk for read+write
// we align it on 16 bytes to make it easier to use in a shader
#define MII_FLOPPY_HM_TRACK_SIZE \
		(((MII_FLOPPY_MAX_TRACK_SIZE / MII_FLOPPY_HM_HIT_SIZE) + 15) & ~15)

typedef struct mii_track_heatmap_t {
	// 32 bytes of track data corresponds to one byte of heatmap
	// this needs to be aligned, otherwise SSE code will die horribly
	uint8_t 			map[MII_FLOPPY_TRACK_COUNT][MII_FLOPPY_HM_TRACK_SIZE]
			__attribute__((aligned(32)));
	uint32_t 			seed;	// increased each time the heatmap is touched
	uint8_t 			cleared;	// set to 1 when the heatmap is cleared
	uint32_t 			tex;	// texture handle for the UI
} mii_track_heatmap_t;

typedef struct mii_floppy_heatmap_t {
	mii_track_heatmap_t read, write;
} mii_floppy_heatmap_t;


// the last track is used for noise
#define MII_FLOPPY_NOISE_TRACK		MII_FLOPPY_TRACK_COUNT

typedef struct mii_floppy_t {
	// write_protected is a bitfield of MII_FLOPPY_WP_*
	uint8_t 			write_protected : 3,
						id : 2;
	uint8_t 			bit_timing;		// 0=32 (default)
	uint8_t				motor;			// motor is on
	uint8_t 			stepper;		// last step we did...
	uint8_t 			qtrack;			// quarter track we are on
	uint32_t			bit_position;
	// this two relate to what we do when LSS needs to return random bits
	uint32_t 			random_position;// position in the random data
	uint8_t				random;			// random data is used
	// this is incremented each time a track is marked dirty
	// used when deciding wether to save to disk (or update texture)
	uint32_t 			seed_dirty;
	uint32_t			seed_saved;		// last seed we saved at
	uint8_t 			track_id[MII_FLOPPY_TRACK_COUNT * 4];
	mii_floppy_track_t 	tracks[MII_FLOPPY_TRACK_COUNT + 1];
	// keep all the data together, we'll use it to make a texture
	// the last track is used for noise
	uint8_t 			track_data[MII_FLOPPY_TRACK_COUNT + 1]
									[MII_FLOPPY_MAX_TRACK_SIZE];
	/* This is set by the UI to track the head movements,
	 * no functional use */
	mii_floppy_heatmap_t * heat;	// optional heatmap
} mii_floppy_t;

/*
 * Initialize a floppy structure with random data. It is not formatted,
 * just ready to use for loading a disk image, or formatting as a
 * 'virgin' disk.
 */
void
mii_floppy_init(
		mii_floppy_t *f);

int
mii_floppy_load(
		mii_floppy_t *f,
		mii_dd_file_t *file );
// save any dirty track (if floppy is writeable)
int
mii_floppy_update_tracks(
		mii_floppy_t *f,
		mii_dd_file_t *file );
void
mii_floppy_resync_track(
		mii_floppy_t *f,
		uint8_t track_id,
		uint8_t flags );
int
mii_floppy_read_sector(
		mii_floppy_track_t *track,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t sector,
		uint8_t data[256] );

/*
 * this creates a sector+data map of a bitstream, and returns the positions
 * of header and data blocks, as well as how many sync bits were found.
 * Function return 0 if 16 headers + data were found, -1 if not.
 */
int
mii_floppy_map_track(
		mii_floppy_t *f,
		uint8_t track_id,
		mii_floppy_track_map_t *map,
		uint8_t flags );
void
mii_floppy_write_track_bits(
		mii_floppy_track_t * dst,
		uint8_t * track_data,
		uint32_t bits,
		uint8_t count );
uint16_t
mii_floppy_crc(	//
		uint16_t crc,				  // Initial value
		uint8_t *data,
		size_t	 length);
