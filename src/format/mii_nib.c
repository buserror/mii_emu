/*
 * mii_nib.c
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

/*
 * NIB isn't ideal to use with our bitstream, as it's lacking the sync
 * bits.
 * Anyway, We can recreate the proper bitstream by finding sectors headers,
 * filling up a few 'correct' 10 bits sync uint8_ts, then plonk said sector
 * as is.
 */
static void
mii_floppy_nib_render_track(
		uint8_t *src_track,
		mii_floppy_track_t * dst,
		uint8_t * dst_track)
{
	dst->bit_count = 0;
	dst->virgin = 0;
	uint32_t window = 0;
	int srci = 0;
	int seccount = 0;
	int state = 0;		// look for address field
	int tid = 0, sid;
	uint16_t hmap = 0, dmap = 0;
	dst->has_map = 0;	// we are redoing it all
	do {
		window = (window << 8) | src_track[srci++];
		switch (state) {
			case 0: {
				if (window != 0xffd5aa96)
					break;
				uint32_t pos = dst->bit_count;
				for (int i = 0; i < (seccount == 0 ? 40 : 20); i++)
					mii_floppy_write_track_bits(dst, dst_track, 0xff << 2, 10);
			//	mii_floppy_write_track_bits(dst, dst_track, 0xff, 8);
				// Points to the last sync 0xff of sync (which is 8 bits)
				uint8_t * h = src_track + srci - 4;
				tid = DE44(h[6], h[7]);
				sid = DE44(h[8], h[9]);
			//	printf("Track %2d sector %2d pos %5d\n", tid, sid, pos);
				hmap |= 1 << sid;
				dst->map.sector[sid].hsync = dst->bit_count - pos;
				// points to the 0xd5
				dst->map.sector[sid].header = dst->bit_count + 8;
				memcpy(dst_track + (dst->bit_count >> 3), h, 15);
				dst->bit_count += 15 * 8;
				srci += 11;
				state = 1;
			}	break;
			case 1: {
				if (window != 0xffd5aaad)
					break;
				uint32_t pos = dst->bit_count;
				for (int i = 0; i < 4; i++)
					mii_floppy_write_track_bits(dst, dst_track, 0xff << 2, 10);
			//	printf("\tdata at %d\n", dst->bit_count);
				dmap |= 1 << sid;
				uint8_t *h = src_track + srci - 4;
				dst->map.sector[sid].dsync = dst->bit_count - pos;
				// keep the position in track to be able to save sectors back
				// this points to the first byte of data
				dst->map.sector[sid].nib_position = srci;
				// points to the 0xd5
				dst->map.sector[sid].data = dst->bit_count + 8;
				memcpy(dst_track + (dst->bit_count >> 3), h, 4 + 342 + 4);
				dst->map.sector[sid].crc = mii_floppy_crc(-1, src_track + srci, 342);
				dst->bit_count += (4 + 342 + 4) * 8;
				srci += 4 + 342;
				seccount++;
				state = 0;
			}	break;
		}
	} while (srci < 6656);
//	printf("%s %d sectors found hmap %04x dmap %04x - %5d bits\n",
//			__func__, seccount, hmap, dmap, dst->bit_count);
	if (hmap != 0xffff || dmap != 0xffff)
		printf("%s: track %2d incomplete? (header 0x%04x data 0x%04x)\n",
				__func__, tid, ~hmap, ~dmap);
	else
		dst->has_map = 1;
}

/*
 * This one is easy, just copy the nibble back where they came from
 */
void
_mii_floppy_nib_write_sector(
		mii_dd_file_t *file,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t track_id,
		uint8_t sector,
		uint8_t data_sector[342 + 1] )
{
	printf("%s: T %2d S %2d has changed, writing sector\n",
			__func__, track_id, sector);
	uint8_t *dst = file->map + (track_id * 6656) +
					map->sector[sector].nib_position;
	memcpy(dst, data_sector, 342 + 1);
}

int
mii_floppy_nib_load(
		mii_floppy_t *f,
		mii_dd_file_t *file )
{
	const char *filename = basename(file->pathname);
	printf("%s: loading NIB %s\n", __func__, filename);
	for (int i = 0; i < 35; i++) {
		uint8_t *track = file->map + (i * 6656);
		mii_floppy_nib_render_track(track, &f->tracks[i], f->track_data[i]);
		if (f->tracks[i].bit_count < 100) {
			printf("%s: %s: Invalid track %d has zero bits!\n", __func__,
					filename, i);
			return -1;
		}
	//	printf("Track %d converted to %d bits\n", i, f->tracks[i].bit_count);
		f->tracks[i].dirty = 0;
	}
	return 0;
}
