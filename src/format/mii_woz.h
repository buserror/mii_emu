/*
 * mii_woz.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/*
 * Woz format parser. 'le' fields are little-endian in the file.
 * Use the appropriate macros like le32toh() to convert to host-endian as needed.
 */
typedef struct mii_woz_header_t {
	uint32_t 		magic_le;	// 'WOZ2'
	uint8_t			padding[4];
	uint32_t 		crc_le;
} __attribute__((packed)) mii_woz_header_t;

typedef struct mii_woz_chunk_t {
	uint32_t 		id_le;
	uint32_t 		size_le;
} __attribute__((packed)) mii_woz_chunk_t;

// offset 12 in the file, 'version' at offset 20
typedef struct mii_woz2_info_t {
	mii_woz_chunk_t	chunk;				// 'INFO'
	uint8_t			version;			// version 3
	uint8_t			disk_type;			// 1=5.25" 2=3.5"
	uint8_t			write_protected;
	uint8_t			sync;				// 1=cross track sync was used when imaged
	uint8_t			cleaned;			// 1=MC3470 fake bits have been cleaned
	uint8_t			creator[32];
	uint8_t 		sides;				// 1 or 2 (3.5")
	uint8_t			boot_format; 		// boot sector format 1:16,2:13,3:both
	uint8_t			optimal_bit_timing;
	uint16_t 		compatible_hardware_le;
	uint16_t 		required_ram_le;	// apple II ram required (48, 64 etc)
	uint16_t 		largest_track_le;	// in units of 512 bytes
	uint16_t		flux_block_le;
	uint16_t 		flux_largest_track_le;
} __attribute__((packed)) mii_woz2_info_t;

// offset 80 in the file -- same for WOZ1 and WOZ2
typedef struct mii_woz_tmap_t {
	mii_woz_chunk_t	chunk;				// 'TMAP'
	// 140 bytes for 35*4 tracks; or 160 bytes for 80 tracks*2 sides
	uint8_t			track_id[160];		// 'TRKS' id for each quarter track
} __attribute__((packed)) mii_woz_tmap_t;

// offset 248 in the file
typedef struct mii_woz2_trks_t {
	mii_woz_chunk_t	chunk;				// 'TRKS'
	// offset 256 in the file
	struct {
		/* First block of BITS data. This value is relative to the start
		   of the file, so the first possible starting block is 3.
		   Multiply this value by 512 (x << 9) to get the starting byte
		   of the BITS data. */
		uint16_t 		start_block_le;		// starting block number
		uint16_t  		block_count_le;		// number of 512-byte blocks
		uint32_t 		bit_count_le;		// number of bits in the track
	}				track[160];
	uint8_t 		bits[];				// the actual bits
} __attribute__((packed)) mii_woz2_trks_t;

/* Same info, tmap and trks for WOZ 1 files */
typedef struct mii_woz1_info_t {
	mii_woz_chunk_t	chunk;				// 'INFO'
	uint8_t			version;			// version 1
	uint8_t			disk_type;			// 1=5.25" 2=3.5"
	uint8_t			write_protected;
	uint8_t			sync;				// 1=cross track sync was used when imaged
	uint8_t			cleaned;			// 1=MC3470 fake bits have been cleaned
	uint8_t			creator[32];
} __attribute__((packed)) mii_woz1_info_t;

// offset 248 in the file
typedef struct mii_woz1_trks_t {
	mii_woz_chunk_t	chunk;				// 'TRKS'
	// offset 256 in the file
	struct {
		uint8_t			bits[6646];
		uint16_t 		byte_count_le; 		// size in bytes
		uint16_t		bit_count_le;
		uint16_t 		splice_point_le;
		uint8_t 		splice_nibble;
		uint8_t 		splice_bit_count;
		uint16_t 		reserved;
	}				track[35];
} __attribute__((packed)) mii_woz1_trks_t;

struct mii_floppy_t;

int
mii_floppy_woz_write_track(
	struct mii_floppy_t *f,
	mii_dd_file_t *file,
	int track_id );
int
mii_floppy_woz_load(
	struct mii_floppy_t *f,
	mii_dd_file_t *file );
