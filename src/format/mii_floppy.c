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
#include "mii_dsk.h"
#include "mii_nib.h"

typedef void (*mii_floppy_write_sector_cb)(
		mii_dd_file_t *file,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t track_id,
		uint8_t sector,
		uint8_t data_sector[342 + 1] );


uint16_t
mii_floppy_crc(	//
		uint16_t crc,				  // Initial value
		uint8_t *data,
		size_t	 length)
{
//	uint16_t crc = 0xFFFF;		  // Initial value
	const uint16_t polynomial = 0x1021; // Polynomial used in CRC16-CCITT

	for (size_t i = 0; i < length; i++) {
		crc ^= (data[i] << 8);	  // Move the byte into the CRC-16's high byte
		for (uint8_t bit = 0; bit < 8; bit++) {
			if (crc & 0x8000) {	  // If the top bit is set, shift left
				crc = (crc << 1) ^ polynomial; // and XOR with polynomial
			} else {
				crc = crc << 1;
			}
		}
	}
	return crc;
}

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
	for (int bi = 256; bi < MII_FLOPPY_MAX_TRACK_SIZE; bi++)
		random[bi] = random[rbi++ % 256];
	// important, the +1 means we initialize the random track too
	for (int i = 0; i < MII_FLOPPY_TRACK_COUNT + 1; i++) {
		f->tracks[i].dirty = 0;
		f->tracks[i].virgin = 1;
		// this affects the disk 'speed' -- larger number will slow down the
		// apparent speed of the disk, according to disk utilities. This value
		// gives 299-300 RPM, which is the correct speed for a 5.25" floppy.
		f->tracks[i].bit_count = 6400 * 8;
		// fill the whole array up to the end..
		uint8_t *track = f->track_data[i];
		if (i != MII_FLOPPY_NOISE_TRACK) {
#if 0
			memset(track, 0, MII_FLOPPY_MAX_TRACK_SIZE);
#else
			for (int bi = 0; bi < MII_FLOPPY_MAX_TRACK_SIZE; bi++)
				track[bi] = random[rbi++ % 256];
#endif
		}
	}
}

void
mii_floppy_write_track_bits(
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

uint32_t
mii_floppy_read_track_bits(
	mii_floppy_track_t * src,
	uint8_t * track_data,
	uint32_t pos,
	uint8_t count )
{
	uint32_t bits = 0;
	// align on 8 bits boudary
	while (count && (pos & 7) != 0) {
		pos = pos % src->bit_count;
		uint32_t 	byte_index 	= pos >> 3;
		uint8_t 	bit_index 	= 7 - (pos & 7);
		bits <<= 1;
		bits |= !!(track_data[byte_index] & (1 << bit_index));
		// we CAN have a wrap around here, but it's ok
		pos++;
		count--;
	}
	// now we can read 8 bits at a time
	while (count >= 8) {
		pos = pos % src->bit_count;
		uint32_t 	byte_index 	= pos >> 3;
		bits = (bits << 8) | track_data[byte_index];
		pos += 8;
		count -= 8;
	}
	// get any remaining bits
	while (count) {
		pos = pos % src->bit_count;
		uint32_t 	byte_index 	= pos >> 3;
		uint8_t 	bit_index 	= 7 - (pos & 7);
		bits <<= 1;
		bits |= !!(track_data[byte_index] & (1 << bit_index));
		// we CAN have a wrap around here, but it's ok
		pos++;
		count--;
	}
	return bits;
}

/*
 * At least PRODOS doesn't necessarily write the sector data in exactly
 * the same place as it was loaded from... so a valid sector map when loading
 * is not nessarily exact when saving.
 * This code check the data header, and if it is not 0xd5aaad, we look back
 * a few bit then forward until we find it again, and update the data position
 * in the sector map.
 */
static int
mii_floppy_realign_sector_map(
		mii_floppy_track_t *track,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t sector )
{
	// now it is entirely possible that the sector has moved a bit,
	// at least prodos does that, so we need to re-find it.
	uint32_t win = mii_floppy_read_track_bits(
						track, track_data, map->sector[sector].data, 24);
	if (win != 0xd5aaad) {
//			printf("%s: track %2d sector %2d has moved %08x\n",
//					__func__, track_id, i,
//					mii_floppy_read_track_bits(track, track_data, map->sector[sector]].data, 24));

		uint32_t pos = map->sector[sector].data;
		win = mii_floppy_read_track_bits(
						track, track_data, pos - 24, 24);
		for (int j = 0; j < 100; j++) {
			win = (win << 1) | mii_floppy_read_track_bits(track, track_data, pos, 1);
			win &= 0xffffff;
			if (win == 0xd5aaad) {
		//		printf("%s: track %2d sector %2d found at %d (was %d)\n",
		//				__func__, track_id, i, pos, map->sector[sector].data);
				map->sector[sector].data = pos - 23;
				break;
			}
			pos++;
		}
	}
	return win == 0xd5aaad ? 0 : -1;
}

/*
 * This reads the sector nibbles. I realign all to 8 bits, and also skips
 * any spurious zero bits that might be in the data.
 * Return the number of spurious zero bits found. This is not necessarily
 * an error.
 */
int
mii_floppy_read_sector_nibbles(
		mii_floppy_track_t *track,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t sector,
		uint8_t data_sector[342 + 1] )
{
	// if the data is not byte aligned, we have to
	// read the whole lot, this will align it to 8 bits
	int j = 0, bit = map->sector[sector].data + (3 * 8), errors = 0;
	while (j < 342 + 1) {
		uint8_t b = mii_floppy_read_track_bits(track, track_data, bit, 8);
		while (!(b & 0x80)) {
			errors++;
			bit++;
			b = mii_floppy_read_track_bits(track, track_data, bit, 8);
		}
		bit += 8;
		data_sector[j++] = b;
	}
	return errors;
}

int
mii_floppy_read_sector(
		mii_floppy_track_t *track,
		uint8_t *track_data,
		mii_floppy_track_map_t *map,
		uint8_t sector,
		uint8_t data[256] )
{
	int res = 0;

	uint8_t data_sector[342 + 1];

	int errors = mii_floppy_read_sector_nibbles(
						track, track_data, map, sector, data_sector);
	if (errors)
		printf("%s warning sector %d has %d spurious zero bits\n",
				__func__, sector, errors);
	res = mii_floppy_decode_sector(data_sector, data);

	return res;
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
							mii_floppy_read_track_bits(src, track_data, pos, 1);
			pos++;
			if ((window & 0x3ff) == 0b1111111100)
				break;
		} while (tries-- > 0 );
		wi = 10;
		if (mii_floppy_read_track_bits(src, track_data, pos, 1) == 0) {
			pos++;
			wi++;
		}
		do {
			uint16_t w = mii_floppy_read_track_bits(src, track_data, pos + wi, 9);
			if (w == 0b111111110)
				wi += 9;
			else if ((w & 0b111111110) == 0b111111110) {
				wi += 8;
				break;
			}
			if (mii_floppy_read_track_bits(src, track_data, pos + wi, 1) == 0) {
				wi++;
			} else
				break;
			if (mii_floppy_read_track_bits(src, track_data, pos + wi, 1) == 0) {
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
		uint32_t header = mii_floppy_read_track_bits(src, track_data, pos, 24);
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
			hb[hi] = mii_floppy_read_track_bits(src, track_data,
						pos + 24 + (hi * 8), 8);
		uint32_t tailer = mii_floppy_read_track_bits(src, track_data,
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

		if (0 && track_id == 0 && sector == 0) {
			printf("pos %5d/%5d\n", pos, src->bit_count);
			for (int bi = 0; bi < 10; bi++) {
				uint32_t bits = mii_floppy_read_track_bits(src, track_data,
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

/*
 * This reposition the sector 0 to the beginning of the track,
 * hopefully also realign the nibbles to something readable.
 * See Sather 9-28 for details
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
		uint32_t bits = mii_floppy_read_track_bits(src, track_data, pos, cnt);
		mii_floppy_write_track_bits(&new, new_track, bits, cnt);
		pos += cnt;
	}
//		printf("%s: Track %2d has been resynced!\n", __func__, track_id);
	memcpy(track_data, new_track, MII_FLOPPY_MAX_TRACK_SIZE);
	free(new_track);
	src->dirty = 1;

}

/*
 * This iterates all the sectors, looking for changed ones using the CRC
 * and call the callback to write them back to the file.
 * Callback can be DSK or NIB specific.
 */
static void
mii_floppy_write_track(
		mii_floppy_t *f,
		mii_dd_file_t *file,
		uint8_t track_id,
		mii_floppy_write_sector_cb cb )
{
	mii_floppy_track_t *track = &f->tracks[track_id];
	uint8_t *track_data = f->track_data[track_id];

	if (!track->has_map) {
		printf("%s: track %d has no map\n", __func__, track_id);
		return;
	}
	// look for changed sectors, re-calculate crc, when a sector is found changed,
	// convert it back from 6:2 encoding, write it using the corresponding sector
	// map for this format, then update the crc.
	for (int i = 0; i < 16; i++) {
		mii_floppy_track_map_t *map = &track->map;

		// now it is entirely possible that the sector has moved a bit,
		// at least prodos does that, so we need to re-find it.
		if (mii_floppy_realign_sector_map(track, track_data, map, i)) {
			printf("%s: track %2d sector %2d not found %08x\n",
					__func__, track_id, i,
					mii_floppy_read_track_bits(track, track_data, map->sector[i].data, 24));
			continue;
		}
		uint8_t data_sector[342 + 1];

		int errors = mii_floppy_read_sector_nibbles(
							track, track_data, map, i, data_sector);
		if (errors) {
			printf("%s: T %2d S %2d has %d spurious zeroes\n",
					__func__, track_id, i, errors);
		}
		uint8_t * src = data_sector;
		uint16_t crc = mii_floppy_crc(-1, src, 342);
		if (crc == map->sector[i].crc)
			continue;

		cb(file, track_data, map, track_id, i, data_sector);
		// update the crc
		map->sector[i].crc = crc;
	}
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
		printf("%s: track %d is dirty, saving\n", __func__, i);
		switch (file->format) {
			case MII_DD_FILE_NIB:
				mii_floppy_write_track(f, file, i, _mii_floppy_nib_write_sector);
				break;
			case MII_DD_FILE_WOZ:
				mii_floppy_woz_write_track(f, file, i);
//				printf("%s: WOZ track %d updated\n", __func__, i);
				break;
			case MII_DD_FILE_DSK:
			case MII_DD_FILE_PO:
			case MII_DD_FILE_DO:
				mii_floppy_write_track(f, file, i, _mii_floppy_dsk_write_sector);
//				printf("%s: DSK track %d updated\n", __func__, i);
				break;
			default:
				printf("%s: unsupported format %d\n", __func__, file->format);
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
			res = mii_floppy_nib_load(f, file);
			break;
		case MII_DD_FILE_WOZ:
			res = mii_floppy_woz_load(f, file);
			break;
		case MII_DD_FILE_DSK:
		case MII_DD_FILE_PO:
		case MII_DD_FILE_DO:
			res = mii_floppy_dsk_load(f, file);
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
#if 0
	// dump the floppy track maps to check on offsets etc
	for (int i = 0; i < MII_FLOPPY_TRACK_COUNT; i++) {
		mii_floppy_track_map_t *map = &f->tracks[i].map;
		if (!f->tracks[i].has_map)
			continue;
		printf("Track %2d\n", i);
		// also show the first 24 bits of data, to check for sync
		for (int j = 0; j < 16; j++) {
			printf("\tSector %2d header :%4d:%5d data %3d:%5d crc %04x sync %06x %06x\n",
					j, map->sector[j].hsync, map->sector[j].header,
					map->sector[j].dsync, map->sector[j].data, map->sector[j].crc,
					mii_floppy_read_track_bits(&f->tracks[i], f->track_data[i],
						map->sector[j].header, 24),
					mii_floppy_read_track_bits(&f->tracks[i], f->track_data[i],
						map->sector[j].data, 24));
//			printf("\n");
		}
	}
#endif
	return res;
}
