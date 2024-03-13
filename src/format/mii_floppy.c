/*
 * mii_floppy.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "mii_floppy.h"
#include "mii_woz.h"

void
mii_floppy_init(
		mii_floppy_t *f)
{
	f->motor 		= 0;
	f->stepper 		= 0;
	// see spec for this.. 32 is the default for 4us.

	f->bit_timing 	= 32;
	f->qtrack 		= 15;	// just to see something at seek time
	f->bit_position = 0;
	f->seed_dirty = f->seed_saved = 0;
	f->write_protected &= ~MII_FLOPPY_WP_MANUAL;// keep the manual WP bit
	/* this will look like this; ie half tracks are 'random'
		0: 0   1: 0   2:35   3: 1
		4: 1   5: 1   6:35   7: 2
		8: 2   9: 2  10:35  11: 3
	*/
	for (int i = 0; i < (int)sizeof(f->track_id); i++)
		f->track_id[i] = ((i + 1) % 4) == 3 ?
								MII_FLOPPY_NOISE_TRACK : ((i + 2) / 4);
	/* generate a buffer with about 30% one bits */
	uint8_t *random = f->track_data[MII_FLOPPY_NOISE_TRACK];
	memset(random, 0, 256);
	uint32_t bits = 256 * 8;
	uint32_t ones = bits * 0.3; // 30% ones
	// set 'ones' random bits in that random track
	while (ones) {
		uint32_t bit = rand() % bits;
		if (random[bit >> 3] & (1 << (bit & 7)))
			continue;
		random[bit >> 3] |= (1 << (bit & 7));
		ones--;
	}
	// copy all that random stuff across the rest of the 'track'
	int rbi = 0;
	for (int bi = 256; bi < MII_FLOPPY_DEFAULT_TRACK_SIZE; bi++)
		random[bi] = random[rbi++ % 256];
	// important, the +1 means we initialize the random track too
	for (int i = 0; i < MII_FLOPPY_TRACK_COUNT + 1; i++) {
		f->tracks[i].dirty = 0;
		f->tracks[i].virgin = 1;
		f->tracks[i].bit_count = 6550 * 8;
		// fill the whole array up to the end..
		uint8_t *track = f->track_data[i];
		if (i != MII_FLOPPY_NOISE_TRACK) {
#if 0
			memset(track, 0, MII_FLOPPY_DEFAULT_TRACK_SIZE);
#else
			for (int bi = 0; bi < MII_FLOPPY_DEFAULT_TRACK_SIZE; bi++)
				track[bi] = random[rbi++ % 256];
#endif
		}
	}
}

static void
mii_track_write_bits(
	mii_floppy_track_t * dst,
	uint8_t * track_data,
	uint32_t bits,
	uint8_t count )
{
	while (count--) {
		uint32_t 	byte_index 	= dst->bit_count >> 3;
		uint8_t 	bit_index 	= 7 - (dst->bit_count & 7);
		track_data[byte_index] &= ~(1 << bit_index);
		track_data[byte_index] |= !!(bits & (1L << (count))) << bit_index;
		dst->bit_count++;
	}
}

static uint32_t
mii_track_read_bits(
	mii_floppy_track_t * src,
	uint8_t * track_data,
	uint32_t pos,
	uint8_t count )
{
	uint32_t bits = 0;
	while (count--) {
		pos = pos % src->bit_count;
		uint32_t 	byte_index 	= pos >> 3;
		uint8_t 	bit_index 	= 7 - (pos & 7);
		bits <<= 1;
		bits |= !!(track_data[byte_index] & (1 << bit_index));
		// we CAN have a wrap around here, but it's ok
		pos++;
	}
	return bits;
}

/*
 * given a track a a starting position, look for a run of 0b1111111100,
 * return the number of sync bits found, and also update the position to
 * the end of the sync bits.
 */
static uint32_t
mii_floppy_find_next_sync(
		mii_floppy_track_t * src,
		uint8_t * track_data,
		uint32_t *io_pos)
{
	/* First we need to sync ourselves by finding 5 * 0b1111111100 's */
	uint32_t window = 0;
	/* get one bit at a time until we get one sync word */
	uint32_t wi = 0;
	uint32_t pos = *io_pos;
	// give up after 2000 bits really, it's either there, or not
	// otherwise we could be 'fooled' looping over the whole track
	int tries = 10000;
	do {
		do {
			window = (window << 1) |
							mii_track_read_bits(src, track_data, pos, 1);
			pos++;
			if ((window & 0x3ff) == 0b1111111100)
				break;
		} while (tries-- > 0 );
		wi = 10;
		if (mii_track_read_bits(src, track_data, pos, 1) == 0) {
			pos++;
			wi++;
		}
		do {
			uint16_t w = mii_track_read_bits(src, track_data, pos + wi, 9);
			if (w == 0b111111110)
				wi += 9;
			else if ((w & 0b111111110) == 0b111111110) {
				wi += 8;
				break;
			}
			if (mii_track_read_bits(src, track_data, pos + wi, 1) == 0) {
				wi++;
			} else
				break;
			if (mii_track_read_bits(src, track_data, pos + wi, 1) == 0) {
				wi++;
			}
		} while (tries-- > 0 &&  wi < 2000);
		/* if this is a sector header, we're in sync here! */
		if (wi >= 2 * 10)
			break;
		else {
			wi = 0;
		}
	} while (tries-- > 0 &&  wi < 2000);
	// this CAN overflow the bit count, but it's ok
	pos += wi;
	*io_pos = pos;
	return wi;
}

#define DE44(a, b) 	((((a) & 0x55) << 1) | ((b) & 0x55))


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
		uint8_t flags )
{
	mii_floppy_track_t * src = &f->tracks[track_id];
	uint8_t * track_data = f->track_data[track_id];

	uint16_t hmap = 0, dmap = 0;
	uint32_t pos = 0;
	uint32_t wi = 0;
	int sect_count = 0;
	int sect_current = -1; // current sector
	// do 2 passes, in case a data sector appears before it's header
	int pass = 0;
	do {
		wi = mii_floppy_find_next_sync(src, track_data, &pos);
		uint32_t header = mii_track_read_bits(src, track_data, pos, 24);
		if (wi == 0) {
			printf("T%2d pos:%5d hmap:%04x dmap:%04x done?\n",
					track_id, pos, hmap, dmap);
			return -1;
		}
		if (header == 0xd5aaad) {	// data sector, update current sector
			if (sect_current == -1) {
				if (flags & 1)
					printf("%s: track %2d data sector before header\n",
							__func__, track_id);
			} else {
				dmap |= 1 << sect_current;
				map->sector[sect_current].dsync = wi;
				map->sector[sect_current].data = pos;
			}
			int skippy = 3 + 342 + 1 + 3; // header, data nibble, chk, tailer
			pos += skippy * 8;
			goto get_new_sync;
		}
		if (header != 0xd5aa96) {	// not a header section? maybe DOS3.2?
			if (flags & 1)
				printf("%s: track %2d bizare sync found %06x\n",
							__func__, track_id, header);
			pos += 10;	// first 10 bits aren't sync anyway
			goto get_new_sync;
		}
		if (flags & 1)
			printf("Track %2d sync %d sync bits at bit %d/%d next %08x\n",
				track_id, wi, pos, src->bit_count, header);
		uint8_t hb[8];
		for (int hi = 0; hi < 8; hi++)
			hb[hi] = mii_track_read_bits(src, track_data,
						pos + 24 + (hi * 8), 8);
		uint32_t tailer = mii_track_read_bits(src, track_data,
						pos + 24 + (8 * 8), 20);
		uint8_t vol 	= DE44(hb[0], hb[1]);
		uint8_t track 	= DE44(hb[2], hb[3]);
		uint8_t sector 	= DE44(hb[4], hb[5]);
		uint8_t chk 	= DE44(hb[6], hb[7]);
		uint8_t want	= vol ^ track ^ sector;

		if (chk != want) {
			if (flags & 1)
				printf("T%2d S%2d V%2d chk:%2x/%02x tailer %06x INVALID header\n",
						track, sector, vol, chk, want, tailer);
			goto get_new_sync;
		}
		sect_current = sector;
		sect_count++;
		// if we already have a header for this sector (with it's matching data)
		if ((hmap & (1 << sector)) && !(dmap & (1 << sector))) {
			printf("T%2d S%2d DUPLICATE sector pos:%5d hmap:%04x dmap:%04x\n",
					track, sector, pos, hmap, dmap);
			printf("\thsync: %3d pos:%5d dsync: %3d pos:%5d\n",
					map->sector[sector].hsync, map->sector[sector].header,
					map->sector[sector].dsync, map->sector[sector].data);
			return -1;
		}
		hmap |= 1 << sector;
		map->sector[sector].hsync = wi;
		map->sector[sector].header = pos;
		map->sector[sector].data = 0;
		if (flags & 1)
			printf("T%2d S%2d V%2d chk:%2x/%02x pos %5d tailer %06x hm:%04x dm:%04x\n",
					track, sector, vol, chk, want, pos, tailer, hmap, dmap);
		if (sect_count > 16) {
			// something fishy going on there, too many sector found,
			// and none of them is zero? let's bail.
			printf("T%2d S%2d Too many sectors\n",
					track, sector);
			return -1;
		}
//		printf("T%2d S%2d V%2d chk:%2x/%02x tailer %06x Skipping sector\n",
//				track, sector, vol, chk, want, tailer);
		pos += 24 + 8 * 8 + 24;	// skip the whole header

		if (track_id == 0 && sector == 0) {
			printf("pos %5d/%5d\n", pos, src->bit_count);
			for (int bi = 0; bi < 10; bi++) {
				uint32_t bits = mii_track_read_bits(src, track_data,
									pos + (bi * 10), 10);
				printf("%010b\n", bits);
			}
			printf("\n");
		}
get_new_sync:
		if (hmap == 0xffff && dmap == 0xffff)
			break;
		if (pos >= src->bit_count) {
			if (pass == 0) {
				printf("%s: T%2d has %d sectors hmap:%04x dmap:%04x LOOPING\n",
						__func__, track_id, sect_count, hmap, dmap);
				pass++;
				pos = pos % src->bit_count;
			}
		}
	} while (pos < src->bit_count);
	int res = hmap == 0xffff && dmap == 0xffff ? 0 : -1;
	if (res != 0) {
	//	if (flags & 1)
			printf("%s: T%2d has %d sectors hmap:%04x dmap:%04x\n",
					__func__, track_id, sect_count, hmap, dmap);
	}
	return res;
}

/* This reposition the sector 0 to the beginning of the track,
  hopefully also realign the nibbles to something readable.
  See Sather 9-28 for details
 */
void
mii_floppy_resync_track(
		mii_floppy_t *f,
		uint8_t track_id,
		uint8_t flags )
{
	mii_floppy_track_map_t map = {};

	if (mii_floppy_map_track(f, track_id, &map, flags) != 0) {
		printf("%s: track %2d has no sync\n", __func__, track_id);
		return;
	}
	int32_t pos = map.sector[0].header;
	int32_t wi = map.sector[0].hsync;
	/* We got a sector zero, we know the number of sync bits in front, and we
		know it's header position, so we can reposition it at the beginning
		of the track by basically reconstructing it */
	pos -= wi;
	if (pos <= 10) {	// already at the beginning, really.
		if (flags & 1)
			printf("T%2d Sector 0 at pos %d\n", track_id, pos);
		return;
	}
	mii_floppy_track_t * src = &f->tracks[track_id];
	uint8_t * track_data = f->track_data[track_id];

	if (flags & 1)
		printf("%s: track %2d resync from bit %5d/%5d\n",
				__func__, track_id, pos, src->bit_count);

	mii_floppy_track_t new = {.dirty = 1, .virgin = 0, .bit_count = 0};
	uint8_t *new_track = malloc(6656);
	while (new.bit_count < src->bit_count)	{
		int cnt = src->bit_count - new.bit_count > 32 ? 32 : src->bit_count - new.bit_count;
		uint32_t bits = mii_track_read_bits(src, track_data, pos, cnt);
		mii_track_write_bits(&new, new_track, bits, cnt);
		pos += cnt;
	}
//		printf("%s: Track %2d has been resynced!\n", __func__, track_id);
	memcpy(track_data, new_track, MII_FLOPPY_DEFAULT_TRACK_SIZE);
	free(new_track);
	src->dirty = 1;

}

/*
 * NIB isn't ideal to use with our bitstream, as it's lacking the sync
 * bits.
 * Anyway, We can recreate the proper bitstream by finding sectors headers,
 * filling up a few 'correct' 10 bits sync uint8_ts, then plonk said sector
 * as is.
 */
static void
mii_nib_rebit_track(
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
	do {
		window = (window << 8) | src_track[srci++];
		switch (state) {
			case 0: {
				if (window != 0xffd5aa96)
					break;
			//	uint32_t pos = dst->bit_count;
				for (int i = 0; i < (seccount == 0 ? 40 : 20); i++)
					mii_track_write_bits(dst, dst_track, 0xff << 2, 10);
				uint8_t * h = src_track + srci - 4;	// incs first 0xff
				tid = DE44(h[6], h[7]);
				sid = DE44(h[8], h[9]);
			//	printf("Track %2d sector %2d pos %5d\n", tid, sid, pos);
				hmap |= 1 << sid;
				memcpy(dst_track + (dst->bit_count >> 3), h, 15);
				dst->bit_count += 15 * 8;
				srci += 11;
				state = 1;
			}	break;
			case 1: {
				if (window != 0xffd5aaad)
					break;
				for (int i = 0; i < 4; i++)
					mii_track_write_bits(dst, dst_track, 0xff << 2, 10);
			//	printf("\tdata at %d\n", dst->bit_count);
				dmap |= 1 << sid;
				uint8_t *h = src_track + srci - 4;
				memcpy(dst_track + (dst->bit_count >> 3), h, 4 + 342 + 4);
				dst->bit_count += (4 + 342 + 4) * 8;
				srci += 4 + 342;
				seccount++;
				state = 0;
			}	break;
		}
	} while (srci < 6656);
	printf("%s %d sectors found hmap %04x dmap %04x - %5d bits\n",
			__func__, seccount, hmap, dmap, dst->bit_count);
	if (hmap != 0xffff || dmap != 0xffff)
		printf("%s: track %2d incomplete? (header 0x%04x data 0x%04x)\n",
				__func__, tid, ~hmap, ~dmap);
}

static int
mii_floppy_load_nib(
	mii_floppy_t *f,
	mii_dd_file_t *file )
{
	const char *filename = basename(file->pathname);
	printf("%s: loading NIB %s\n", __func__, filename);
	for (int i = 0; i < 35; i++) {
		uint8_t *track = file->map + (i * 6656);
		mii_nib_rebit_track(track, &f->tracks[i], f->track_data[i]);
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

static int
mii_floppy_write_track_woz(
	mii_floppy_t *f,
	mii_dd_file_t *file,
	int track_id )
{
	mii_woz_header_t *header = (mii_woz_header_t *)file->map;

	int version = !strncmp((char*)header, "WOZ", 3);
	if (!version) {
		printf("%s: not a WOZ file %4.4s\n", __func__, (char*)&header->magic_le);
		return 0;
	}
	version += !strncmp((char*)header, "WOZ2", 4);

	/* I don't really want to recalculate the CRC. Seems pointless in a file
	  like this, and i'd have to walk 250KB+ of data each time I update
	  anything.
	  Mark is as cleared, perhapps I need a tool to 'fix' it later, or JUST
	  at closing time ?*/
	header->crc_le = 0;

	mii_woz_tmap_t *tmap = NULL;
	if (version == 1) {
		mii_woz1_info_t *info = (mii_woz1_info_t *)(header + 1);
		tmap = (mii_woz_tmap_t *)((uint8_t *)info +
					le32toh(info->chunk.size_le) + sizeof(mii_woz_chunk_t));
		mii_woz1_trks_t *trks = (mii_woz1_trks_t *)((uint8_t *)tmap +
					le32toh(tmap->chunk.size_le) + sizeof(mii_woz_chunk_t));

		trks->track[track_id].bit_count_le = htole32(f->tracks[track_id].bit_count);
		uint32_t byte_count = (le32toh(trks->track[track_id].bit_count_le) + 7) >> 3;
		memcpy(trks->track[track_id].bits,
				f->track_data[track_id], byte_count);
		trks->track[track_id].byte_count_le = htole16(byte_count);
	} else {
		mii_woz2_info_t *info = (mii_woz2_info_t *)(header + 1);
		tmap = (mii_woz_tmap_t *)((uint8_t *)info +
					le32toh(info->chunk.size_le) + sizeof(mii_woz_chunk_t));
		mii_woz2_trks_t *trks = (mii_woz2_trks_t *)((uint8_t *)tmap +
					le32toh(tmap->chunk.size_le) + sizeof(mii_woz_chunk_t));

		uint8_t *track = file->map +
					(le16toh(trks->track[track_id].start_block_le) << 9);

		trks->track[track_id].bit_count_le = htole32(f->tracks[track_id].bit_count);
		uint32_t byte_count = (le32toh(trks->track[track_id].bit_count_le) + 7) >> 3;
		memcpy(track, f->track_data[track_id], byte_count);
	}
	f->tracks[track_id].dirty = 0;
	return 0;
}

static uint64_t
mii_floppy_woz_load_tmap(
	mii_floppy_t *f,
	mii_woz_tmap_t *tmap )
{
	uint64_t used_tracks = 0;
	int tmap_size = le32toh(tmap->chunk.size_le);
	for (int ti = 0; ti < (int)sizeof(f->track_id) && ti < tmap_size; ti++) {
		f->track_id[ti] = tmap->track_id[ti] == 0xff ?
							MII_FLOPPY_NOISE_TRACK : tmap->track_id[ti];
		if (tmap->track_id[ti] != 0xff)
			used_tracks |= 1L << f->track_id[ti];
	}
	return used_tracks;
}

static int
mii_floppy_load_woz(
	mii_floppy_t *f,
	mii_dd_file_t *file )
{
	const char *filename = basename(file->pathname);
	printf("%s: loading WOZ %s\n", __func__, filename);
	mii_woz_header_t *header = (mii_woz_header_t *)file->map;

	int version = !strncmp((char*)header, "WOZ", 3);
	if (!version) {
		printf("%s: not a WOZ file %4.4s\n", __func__, (char*)&header->magic_le);
		return 0;
	}
	version += !strncmp((char*)header, "WOZ2", 4);
	mii_woz_tmap_t *tmap = NULL;
	uint64_t used_tracks = 0;

	if (version == 1) {
		mii_woz1_info_t *info = (mii_woz1_info_t *)(header + 1);
		tmap = (mii_woz_tmap_t *)((uint8_t *)info +
					le32toh(info->chunk.size_le) + sizeof(mii_woz_chunk_t));
		mii_woz1_trks_t *trks = (mii_woz1_trks_t *)((uint8_t *)tmap +
					le32toh(tmap->chunk.size_le) + sizeof(mii_woz_chunk_t));
		used_tracks = mii_floppy_woz_load_tmap(f, tmap);
#if 1
		printf("WOZ: version %d, type %d\n",
				info->version, info->disk_type );
		printf("WOZ: creator '%s'\n", info->creator);
		printf("WOZ: track map %4.4s size %d\n",
				(char*)&tmap->chunk.id_le,
				le32toh(tmap->chunk.size_le));
		printf("WOZ: Track chunk %4.4s size %d\n",
				(char*)&trks->chunk.id_le, le32toh(trks->chunk.size_le));
#endif
		int max_track = le32toh(trks->chunk.size_le) / sizeof(trks->track[0]);
		for (int i = 0; i < MII_FLOPPY_TRACK_COUNT && i < max_track; i++) {
			uint8_t *track = trks->track[i].bits;
			if (!(used_tracks & (1L << i))) {
		//		printf("WOZ: Track %d not used\n", i);
				continue;
			}
			f->tracks[i].virgin = 0;
			memcpy(f->track_data[i], track, le16toh(trks->track[i].byte_count_le));
			f->tracks[i].bit_count = le32toh(trks->track[i].bit_count_le);
		}
	} else {
		mii_woz2_info_t *info = (mii_woz2_info_t *)(header + 1);
		tmap = (mii_woz_tmap_t *)((uint8_t *)info +
					le32toh(info->chunk.size_le) + sizeof(mii_woz_chunk_t));
		mii_woz2_trks_t *trks = (mii_woz2_trks_t *)((uint8_t *)tmap +
					le32toh(tmap->chunk.size_le) + sizeof(mii_woz_chunk_t));
		used_tracks = mii_floppy_woz_load_tmap(f, tmap);
#if 1
		printf("WOZ: version %d, type %d, sides %d, largest track %d, optimal bit timing: %d\n",
				info->version, info->disk_type, info->sides,
				le16toh(info->largest_track_le) * 512,
				info->optimal_bit_timing);
		printf("WOZ: creator '%s'\n", info->creator);
		printf("WOZ: track map %4.4s size %d\n",
				(char*)&tmap->chunk.id_le,
				le32toh(tmap->chunk.size_le));
		printf("WOZ: Track chunk %4.4s size %d\n",
				(char*)&trks->chunk.id_le, le32toh(trks->chunk.size_le));
#endif
		/* TODO: this doesn't work yet... */
		//f->bit_timing = info->optimal_bit_timing;
		for (int i = 0; i < MII_FLOPPY_TRACK_COUNT; i++) {
			if (!(used_tracks & (1L << i))) {
			//	printf("WOZ: Track %d not used\n", i);
				continue;
			}
			uint8_t *track = file->map +
						(le16toh(trks->track[i].start_block_le) << 9);
			uint32_t byte_count = (le32toh(trks->track[i].bit_count_le) + 7) >> 3;
			f->tracks[i].virgin = 0;
			memcpy(f->track_data[i], track, byte_count);
			f->tracks[i].bit_count = le32toh(trks->track[i].bit_count_le);
		}
	}
	return version;
}

#define NIBBLE_SECTOR_SIZE  416
#define NIBBLE_TRACK_SIZE   6656
#define DSK_SECTOR_SIZE     256
#define MAX_SECTORS         16
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

static const uint8_t TRANS62[] = {
	0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
	0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
	0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
	0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
	0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
	0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
	0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
	0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

// This function is derived from Scullin Steel Co.'s apple2js code
// https://github.com/whscullin/apple2js/blob/e280c3d/js/formats/format_utils.ts#L140
/* Further recycled for MII .DSK decoding, using 10 bits sync words etc. */
static void
mii_floppy_nibblize_sector(
		uint8_t vol, uint8_t track, uint8_t sector,
		const uint8_t *data,
		mii_floppy_track_t *dst,
		uint8_t * track_data )
{
	unsigned int gap;

	if (track == 0 )
		printf("NIB: vol %d track %d sector %d pos %5d\n",
				vol, track, sector, dst->bit_count);
	gap = sector == 0 ? 120 : track == 0 ? 30 : 20;
	for (uint8_t i = 0; i < gap; ++i)
		mii_track_write_bits(dst, track_data, 0xFF << 2, 10);
	// Address Field
	const uint8_t checksum = vol ^ track ^ sector;
	mii_track_write_bits(dst, track_data, 0xd5aa96, 24);
	mii_track_write_bits(dst, track_data, (vol >> 1) | 0xAA, 8);
	mii_track_write_bits(dst, track_data, vol | 0xAA, 8);
	mii_track_write_bits(dst, track_data, (track >> 1)  | 0xAA, 8);
	mii_track_write_bits(dst, track_data, track  | 0xAA, 8);
	mii_track_write_bits(dst, track_data, (sector >> 1)  | 0xAA, 8);
	mii_track_write_bits(dst, track_data, sector | 0xAA, 8);
	mii_track_write_bits(dst, track_data, (checksum >> 1) | 0xAA, 8);
	mii_track_write_bits(dst, track_data, checksum | 0xAA, 8);
	mii_track_write_bits(dst, track_data, 0xdeaaeb, 24);
	// Gap 2 (5)
	for (int i = 0; i < 5; ++i)
		mii_track_write_bits(dst, track_data, 0xFF << 2, 10);
	// Data Field
	mii_track_write_bits(dst, track_data, 0xd5aaad, 24);
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
	for (int i = 0; i != 0x156; ++i) {
		const uint8_t val = nibbles[i];
		mii_track_write_bits(dst, track_data, TRANS62[last ^ val], 8);
		last = val;
	}
	mii_track_write_bits(dst, track_data, TRANS62[last], 8);
	mii_track_write_bits(dst, track_data, 0xdeaaeb, 24);
	// Gap 3
	mii_track_write_bits(dst, track_data, 0xFF << 2, 10);
}

static int
mii_floppy_load_dsk(
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
		for (int phys_sector = 0; phys_sector < MAX_SECTORS; phys_sector++) {
			const uint8_t dos_sector = secmap[phys_sector];
			uint32_t off = ((MAX_SECTORS * i + dos_sector) * DSK_SECTOR_SIZE);
			uint8_t *src = file->map + off;
			mii_floppy_nibblize_sector(VOLUME_NUMBER, i, phys_sector,
						src, dst, track_data);
		}
	//	printf("%s: track %2d has %d bits %d bytes\n",
	//			__func__, i, dst->bit_count, dst->bit_count >> 3);
	}
	// DSK is read only
	f->write_protected |= MII_FLOPPY_WP_RO_FORMAT;

	return 0;
}

int
mii_floppy_update_tracks(
		mii_floppy_t *f,
		mii_dd_file_t *file )
{
	if (f->write_protected & MII_FLOPPY_WP_RO_FORMAT)
		return -1;
	if (f->write_protected & MII_FLOPPY_WP_RO_FILE)
		return -1;
	if (f->seed_dirty == f->seed_saved)
		return 0;
	for (int i = 0; i < MII_FLOPPY_TRACK_COUNT; i++) {
		if (!f->tracks[i].dirty)
			continue;
//		printf("%s: track %d is dirty, saving\n", __func__, i);
		switch (file->format) {
			case MII_DD_FILE_NIB:
				break;
			case MII_DD_FILE_WOZ:
				mii_floppy_write_track_woz(f, file, i);
//				printf("%s: WOZ track %d updated\n", __func__, i);
				break;
		}
		f->tracks[i].dirty = 0;
	}
	f->seed_saved = f->seed_dirty;
	return 0;
}

int
mii_floppy_load(
		mii_floppy_t *f,
		mii_dd_file_t *file )
{
	if (!file)
		return -1;
	int res = -1;
	switch (file->format) {
		case MII_DD_FILE_NIB:
			res = mii_floppy_load_nib(f, file);
			break;
		case MII_DD_FILE_WOZ:
			res = mii_floppy_load_woz(f, file);
			break;
		case MII_DD_FILE_DSK:
		case MII_DD_FILE_PO:
		case MII_DD_FILE_DO:
			res = mii_floppy_load_dsk(f, file);
			break;
		default:
			printf("%s: unsupported format %d\n", __func__, file->format);
	}
	// update write protection in case file is opened read only
	if (file->read_only)
		f->write_protected |= MII_FLOPPY_WP_RO_FILE;
	else
		f->write_protected &= ~MII_FLOPPY_WP_RO_FILE;
	f->seed_dirty = f->seed_saved = rand();
	return res;
}
