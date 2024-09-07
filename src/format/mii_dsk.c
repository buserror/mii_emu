/*
 * mii_dsk.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "mii_dsk.h"

#define NIBBLE_SECTOR_SIZE  416
#define NIBBLE_TRACK_SIZE   6656
#define DSK_SECTOR_SIZE     256
//#define MAX_SECTORS         16
#define VOLUME_NUMBER       254
#define DSK_TRACK_SIZE      (DSK_SECTOR_SIZE * MAX_SECTORS)

//static const size_t nib_disksz = 232960;
//static const size_t dsk_disksz = 143360;

//  DOS 3.3 Physical sector order (index is physical sector,
//  value is DOS sector)
static const uint8_t DO[] = {
	0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
	0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
};
// ProDOS Physical sector order (index is physical sector,
// value is ProDOS sector).
static const uint8_t PO[] = {
	0x0, 0x8, 0x1, 0x9, 0x2, 0xa, 0x3, 0xb,
	0x4, 0xc, 0x5, 0xd, 0x6, 0xe, 0x7, 0xf
};

const uint8_t TRANS62[] = {
	0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
	0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
	0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
	0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
	0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
	0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
	0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
	0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};
const int8_t  DETRANS62[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x02, 0x03, 0x00, 0x04, 0x05, 0x06,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x08,
	0x00, 0x00, 0x00, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
	0x00, 0x00, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
	0x00, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x1B, 0x00, 0x1C, 0x1D, 0x1E,
	0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x20, 0x21,
	0x00, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x2A, 0x2B,
	0x00, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
	0x00, 0x00, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
	0x00, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};


/*
 * take a normalized nibble sector, and decode it to a 256 byte sector.
 * return 0 if the checksum is correct, -1 if not.
 */
int
mii_floppy_decode_sector(
		uint8_t data_sector[342 + 1],
		uint8_t data[256])
{
	int res = 0;
	uint8_t data2[0x56] = {};
	uint8_t last = 0;
	int val = 0;
	uint8_t didx = 0;
	uint8_t *src = data_sector;

	for (int jdx = 0x55; jdx >= 0; jdx--) {
		didx = *src++ - 0x80;
		val = DETRANS62[didx] ^ last;
		data2[jdx] = val;
		last = val;
	}
	for (int jdx = 0; jdx < 0x100; jdx++) {
		didx = *src++ - 0x80;
		val = DETRANS62[didx] ^ last;
		data[jdx] = val;
		last = val;
	}
	didx = *src++ - 0x80;
	uint8_t checkSum = DETRANS62[didx] ^ last;
	if (checkSum)
		res = -1;
	for (int kdx = 0, jdx = 0x55; kdx < 0x100; kdx++) {
		data[kdx] <<= 1;
		if ((data2[jdx] & 0x01) != 0) {
			data[kdx] |= 0x01;
		}
		data2[jdx] >>= 1;
		data[kdx] <<= 1;
		if ((data2[jdx] & 0x01) != 0) {
			data[kdx] |= 0x01;
		}
		data2[jdx] >>= 1;
		if (--jdx < 0)
			jdx = 0x55;
	}
	return res;
}

// This function is derived from Scullin Steel Co.'s apple2js code
// https://github.com/whscullin/apple2js/blob/e280c3d/js/formats/format_utils.ts#L140
/* Further recycled for MII .DSK decoding, using 10 bits sync words etc. */
static void
mii_floppy_dsk_render_sector(
		uint8_t vol, uint8_t track, uint8_t sector,
		const uint8_t *data,
		mii_floppy_track_t *dst,
		uint8_t * track_data )
{
	unsigned int gap;

//	if (track == 0 )
//		printf("NIB: vol %d track %d sector %d pos %5d\n",
//				vol, track, sector, dst->bit_count);
	gap = sector == 0 ? 100 : track == 0 ? 20 : 20;
	uint32_t pos = dst->bit_count;
	for (uint8_t i = 0; i < gap; ++i)
		mii_floppy_write_track_bits(dst, track_data, 0xFF << 2, 10);
	dst->map.sector[sector].hsync = dst->bit_count - pos;
	dst->map.sector[sector].header = dst->bit_count;
	// Address Field
	const uint8_t checksum = vol ^ track ^ sector;
	mii_floppy_write_track_bits(dst, track_data, 0xd5aa96, 24);
	mii_floppy_write_track_bits(dst, track_data, (vol >> 1) | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, vol | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, (track >> 1)  | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, track  | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, (sector >> 1)  | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, sector | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, (checksum >> 1) | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, checksum | 0xAA, 8);
	mii_floppy_write_track_bits(dst, track_data, 0xdeaaeb, 24);
	pos = dst->bit_count;
	// Gap 2 (5)
	for (int i = 0; i < 5; ++i)
		mii_floppy_write_track_bits(dst, track_data, 0xFF << 2, 10);
	// total 48 bits sync, which helps keeping us byte aligned on the data
	mii_floppy_write_track_bits(dst, track_data, 0xFF, 8);
	dst->map.sector[sector].dsync = dst->bit_count - pos;
	dst->map.sector[sector].data = dst->bit_count;
//	printf("Track %2d sector %2d pos %5d %s\n", track, sector, dst->bit_count,
//			dst->bit_count % 8 == 0 ? "" : "NOT BYTE ALIGNED");
	// Data Field
	mii_floppy_write_track_bits(dst, track_data, 0xd5aaad, 24);
	uint8_t nibbles[0x156] = {};
	const unsigned ptr2 = 0;
	const unsigned ptr6 = 0x56;

	int i2 = 0x55;
	for (int i6 = 0x101; i6 >= 0; --i6) {
		uint8_t val6 = data[i6 % 0x100];
		uint8_t val2 = nibbles[ptr2 + i2];
		val2 = (val2 << 1) | (val6 & 1); val6 >>= 1;
		val2 = (val2 << 1) | (val6 & 1); val6 >>= 1;
		nibbles[ptr6 + i6] = val6;
		nibbles[ptr2 + i2] = val2;
		if (--i2 < 0)
			i2 = 0x55;
	}
	uint8_t last = 0;
	// get a CRC for that sector before we write it
	dst->map.sector[sector].crc = mii_floppy_crc(-1, nibbles, 342);
	for (int i = 0; i < 342; ++i) {
		const uint8_t val = nibbles[i];
		mii_floppy_write_track_bits(dst, track_data, TRANS62[last ^ val], 8);
		last = val;
	}
	mii_floppy_write_track_bits(dst, track_data, TRANS62[last], 8);
	mii_floppy_write_track_bits(dst, track_data, 0xdeaaeb, 24);
	// Gap 3
	mii_floppy_write_track_bits(dst, track_data, 0xFF << 2, 10);
}

void
_mii_floppy_dsk_write_sector(
		mii_dd_file_t *file,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t track_id,
		uint8_t sector,
		uint8_t data_sector[342 + 1] )
{
	uint8_t data[256];

	int errors = mii_floppy_decode_sector(data_sector, data);
	if (errors) {
		printf("%s: T %2d S %2d has errors -- not writing sector\n",
				__func__, track_id, sector);
	} else {
		printf("%s: T %2d S %2d has changed, writing sector\n",
				__func__, track_id, sector);
		memcpy(file->map + map->sector[sector].dsk_position, data, 256);
	}
}

int
mii_floppy_dsk_load(
		mii_floppy_t *f,
		mii_dd_file_t *file )
{
	const char *filename = basename(file->pathname);

    const char *ext = rindex(filename, '.');
    ext = ext ? ext+1 : "";
	const uint8_t * secmap = DO;
    if (!strcasecmp(ext, "PO"))  {
		printf("%s opening %s as PO.\n", __func__, filename);
   		secmap = PO;
    } else {
		printf("%s opening %s as DO.\n", __func__, filename);
    }
	for (int i = 0; i < 35; ++i) {
		mii_floppy_track_t *dst = &f->tracks[i];
		uint8_t *track_data = f->track_data[i];
		dst->bit_count = 0;
		dst->virgin = 0;
		dst->has_map = 1;	// being filled by nibblize_sector
		for (int phys_sector = 0; phys_sector < 16; phys_sector++) {
			const uint8_t dos_sector = secmap[phys_sector];
			uint32_t off = ((16 * i + dos_sector) * DSK_SECTOR_SIZE);
			uint8_t *src = file->map + off;
			mii_floppy_dsk_render_sector(VOLUME_NUMBER, i, phys_sector,
						src, dst, track_data);
			dst->map.sector[phys_sector].dsk_position = off;
		}
		if (i == 0)
			printf("%s: track %2d has %d bits %d bytes\n",
					__func__, i, dst->bit_count, dst->bit_count >> 3);
	}
	// DSK is read only
//	f->write_protected |= MII_FLOPPY_WP_RO_FORMAT;

	return 0;
}
