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
		f->tracks[i].bit_count = 6500 * 8;
		// fill the whole array up to the end..
		uint8_t *track = f->track_data[i];
		if (i != MII_FLOPPY_NOISE_TRACK) {
#if 1
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
	uint8_t bits,
	uint8_t count )
{
	while (count--) {
		uint32_t 	byte_index 	= dst->bit_count >> 3;
		uint8_t 	bit_index 	= 7 - (dst->bit_count & 7);
		track_data[byte_index] &= ~(1 << bit_index);
		track_data[byte_index] |= (!!(bits >> 7) << bit_index);
		dst->bit_count++;
		bits <<= 1;
	}
}

/*
 * NIB isn't ideal to use with our bitstream, as it's lacking the sync
 * bits. It was made to use in something like our previous emulator that
 * was just iterating uint8_ts.
 * Anyway, We can recreate the proper bitstream by finding sectors headers,
 * filling up a few 'correct' 10 bits sync uint8_ts, then plonk said sector
 * as is.
 */

static uint8_t _de44(uint8_t a, uint8_t b) {
	return ((a & 0x55) << 1) | (b & 0x55);
}

static void
mii_nib_rebit_track(
	uint8_t *src_track,
	mii_floppy_track_t * dst,
	uint8_t * dst_track)
{
	dst->bit_count = 0;
	uint32_t window = 0;
	int srci = 0;
	int seccount = 0;
	int state = 0;		// look for address field
	do {
		window = (window << 8) | src_track[srci++];
		switch (state) {
			case 0: {
				if (window != 0xffd5aa96)
					break;
				for (int i = 0; i < (seccount == 0 ? 40 : 20); i++)
					mii_track_write_bits(dst, dst_track, 0xff, 10);
				uint8_t * h = src_track + srci - 4;	// incs first 0xff
			//	int tid = _de44(h[6], h[7]);
			//	int sid = _de44(h[8], h[9]);
			//	printf("Track %2d sector %2d\n", tid, sid);
				memcpy(dst_track + (dst->bit_count >> 3), h, 15);
				dst->bit_count += 15 * 8;
				srci += 11;
				state = 1;
			}	break;
			case 1: {
				if (window != 0xffd5aaad)
					break;
				for (int i = 0; i < 4; i++)
					mii_track_write_bits(dst, dst_track, 0xff, 10);
				uint8_t *h = src_track + srci - 4;
				memcpy(dst_track + (dst->bit_count >> 3), h, 4 + 342 + 4);
				dst->bit_count += (4 + 342 + 4) * 8;
				srci += 4 + 342;
				seccount++;
				state = 0;
			}	break;
		}
	} while (srci < 6656);
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
		// f->bit_timing = info->optimal_bit_timing;
		for (int i = 0; i < MII_FLOPPY_TRACK_COUNT; i++) {
			if (!(used_tracks & (1L << i))) {
			//	printf("WOZ: Track %d not used\n", i);
				continue;
			}
			uint8_t *track = file->map +
						(le16toh(trks->track[i].start_block_le) << 9);
			uint32_t byte_count = (le32toh(trks->track[i].bit_count_le) + 7) >> 3;
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

/* Further recycled for MII .DSK decoding
 * We use this function to convert the sector from byte to nibble (8 bits), then
 * we pass that track to the mii_nib_rebit_track() to add 10 bit headers and
 * such. It could be done in one pass, but really, it's easier to reuse it as is.
 */
static void
mii_floppy_nibblize_sector(
		uint8_t vol, uint8_t track, uint8_t sector,
		uint8_t **nibSec, const uint8_t *data)
{
	uint8_t *wr = *nibSec;
	unsigned int gap;

	// Gap 1/3 (40/0x28 uint8_ts)
	if (sector == 0) // Gap 1
		gap = 0x80;
	else { // Gap 3
		gap = track == 0? 0x28 : 0x26;
	}
	for (uint8_t i = 0; i != gap; ++i)
		*wr++ = 0xFF;
	// Address Field
	const uint8_t checksum = vol ^ track ^ sector;
	*wr++ = 0xD5; *wr++ = 0xAA; *wr++ = 0x96; // Address Prolog D5 AA 96
	*wr++ = (vol >> 1) | 0xAA; *wr++ = vol | 0xAA;
	*wr++ = (track >> 1)  | 0xAA; *wr++ = track  | 0xAA;
	*wr++ = (sector >> 1) | 0xAA; *wr++ = sector | 0xAA;
	*wr++ = (checksum >> 1) | 0xAA; *wr++ = checksum | 0xAA;
	*wr++ = 0xDE; *wr++ = 0xAA; *wr++ = 0xEB; // Epilogue DE AA EB
	// Gap 2 (5 uint8_ts)
	for (int i = 0; i != 5; ++i)
		*wr++ = 0xFF;
	// Data Field
	*wr++ = 0xD5; *wr++ = 0xAA; *wr++ = 0xAD; // Data Prolog D5 AA AD

	uint8_t *nibbles = wr;
	const unsigned ptr2 = 0;
	const unsigned ptr6 = 0x56;

	for (int i = 0; i != 0x156; ++i)
		nibbles[i] = 0;

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
		nibbles[i] = TRANS62[last ^ val];
		last = val;
	}
	wr += 0x156; // advance write-pointer
	*wr++ = TRANS62[last];
	*wr++ = 0xDE; *wr++ = 0xAA; *wr++ = 0xEB; // Epilogue DE AA EB
	// Gap 3
	*wr++ = 0xFF;
	*nibSec = wr;
}

static int
mii_floppy_load_dsk(
	mii_floppy_t *f,
	mii_dd_file_t *file )
{
	uint8_t *nibbleBuf = malloc(NIBBLE_TRACK_SIZE);
	const char *filename = basename(file->pathname);

    const char *ext = rindex(filename, '.');
    ext = ext ? ext+1 : "";
	const uint8_t * secmap = DO;
    if (!strcasecmp(ext, "PO"))  {
		printf("%s Opening %s as PO.\n", __func__, filename);
   		secmap = PO;
    } else {
		printf("%s Opening %s as DO.\n", __func__, filename);
    }
	for (int i = 0; i < 35; ++i) {
		memset(nibbleBuf, 0xff, NIBBLE_TRACK_SIZE);
		uint8_t *writePtr = nibbleBuf;
		for (int phys_sector = 0; phys_sector < MAX_SECTORS; ++phys_sector) {
			const uint8_t dos_sector = secmap[phys_sector];
			uint32_t off = ((MAX_SECTORS * i + dos_sector) * DSK_SECTOR_SIZE);
			uint8_t *track = file->map + off;
			mii_floppy_nibblize_sector(VOLUME_NUMBER, i, phys_sector,
						  &writePtr, track);
		}
		mii_nib_rebit_track(nibbleBuf, &f->tracks[i], f->track_data[i]);
	}
	free(nibbleBuf);
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
