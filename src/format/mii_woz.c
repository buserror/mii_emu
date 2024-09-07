/*
 * mii_woz.h
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


int
mii_floppy_woz_write_track(
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

int
mii_floppy_woz_load(
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
