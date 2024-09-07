/*
 * mii_disk2_mish.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE // for asprintf
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "mii.h"
#include "mii_bank.h"
#include "mii_disk2.h"


void
mii_hexdump(
		const char * prompt,
		unsigned int display_offset,
		const void *buffer,
		unsigned int len);

extern mii_card_disk2_t *_mish_d2;

uint32_t
mii_floppy_read_track_bits(
	mii_floppy_track_t * src,
	uint8_t * track_data,
	uint32_t pos,
	uint8_t count );

static void
_mii_mish_d2(
		void * param,
		int argc,
		const char * argv[])
{
//	mii_t * mii = param;
	if (!_mish_d2) {
		printf("No Disk ][ card installed\n");
		return;
	}
	static int sel = 0;
	if (!argv[1] || !strcmp(argv[1], "list")) {
		printf("LSS Status %02x Q6:%d Q7:%d\n", _mish_d2->lss_mode,
				!!(_mish_d2->lss_mode & (1 << Q6_LOAD_BIT)),
				!!(_mish_d2->lss_mode & (1 << Q7_WRITE_BIT)));
		mii_card_disk2_t *c = _mish_d2;
		for (int i = 0; i < 2; i++) {
			mii_floppy_t *f = &c->floppy[i];
			printf("Drive %d %s\n", f->id, f->write_protected ? "WP" : "RW");
			printf("\tMotor: %3s qtrack:%3d Bit %6d/%6d\n",
					f->motor ? "ON" : "OFF", f->qtrack,
					f->bit_position, f->tracks[0].bit_count);
		}
		return;
	}
	if (!strcmp(argv[1], "sel")) {
		if (argv[2]) {
			sel = atoi(argv[2]);
		}
		printf("Selected drive: %d\n", sel);
		return;
	}
	if (!strcmp(argv[1], "wp")) {
		if (argv[2]) {
			int wp = atoi(argv[2]);
			mii_card_disk2_t *c = _mish_d2;
			mii_floppy_t *f = &c->floppy[sel];
			f->write_protected = wp;
		}
		printf("Drive %d Write protected: %d\n", sel,
				_mish_d2->floppy[sel].write_protected);
		return;
	}
	// dump a track, specify track number and number of bytes
	if (!strcmp(argv[1], "track")) {
		mii_card_disk2_t *c = _mish_d2;
		mii_floppy_t *f = &c->floppy[sel];
		if (argv[2]) {
			int track = atoi(argv[2]);
			if (track < 0 || track >= MII_FLOPPY_TRACK_COUNT) {
				printf("Invalid track %d\n", track);
				return;
			}
			int count = 256;
			if (argv[3]) {
				if (!strcmp(argv[3], "save")) {
					// save one binary file in tmp with just that track
					uint8_t *data = f->track_data[track];
					char *filename;
					asprintf(&filename, "/tmp/track_%02d.bin", track);
					int fd = open(filename, O_CREAT | O_WRONLY, 0666);
					write(fd, data, MII_FLOPPY_MAX_TRACK_SIZE);
					close(fd);
					printf("Saved track %d to %s\n", track, filename);
					free(filename);
					return;
				}
				count = atoi(argv[3]);
			}
			uint8_t *data = f->track_data[track];

			for (int i = 0; i < count; i += 8) {
				uint8_t *line = data + i;
			#if 1
				for (int bi = 0; bi < 8; bi++) {
					uint8_t b = line[bi];
					for (int bbi = 0; bbi < 8; bbi++) {
						printf("%c", (b & 0x80) ? '1' : '0');
						b <<= 1;
					}
				}
				printf("\n");
			#endif
				for (int bi = 0; bi < 8; bi++)
					printf("%8x", line[bi]);
				printf("\n");
			}
		} else {
			printf("track <track 0-34> [count]\n");
		}
		return;
	}
	if (!strcmp(argv[1], "sector")) {
		// parameters are track then sector... dump the result as hex
		if (argv[2] && argv[3]) {
			mii_card_disk2_t *c = _mish_d2;
			mii_floppy_t *f = &c->floppy[sel];
			int track = atoi(argv[2]);
			int sector = atoi(argv[3]);
			if (track < 0 || track >= MII_FLOPPY_TRACK_COUNT) {
				printf("Invalid track %d\n", track);
				return;
			}
			if (sector < 0 || sector >= 16) {
				printf("Invalid sector %d\n", sector);
				return;
			}
			mii_floppy_track_map_t map = {};
			int r = mii_floppy_map_track(f, track, &map, 0);
			if (r != 0) {
				printf("Invalid track %d\n", track);
				return;
			}
			uint8_t sector_data[256];
			r = mii_floppy_read_sector(
						&f->tracks[track], f->track_data[track], &map,
						sector, sector_data);
			printf("Track %d Sector %d %s\n",
					track, sector, r == 0 ? "OK" : "Invalid");
			mii_hexdump("Sector", 0, sector_data, 256);
		} else {
			printf("sector <track 0-34> <sector 0-15>\n");
		}
	}
	if (!strcmp(argv[1], "dirty")) {
		mii_card_disk2_t *c = _mish_d2;
		mii_floppy_t *f = &c->floppy[sel];
		f->seed_dirty = f->seed_saved = rand();
		return;
	}
	if (!strcmp(argv[1], "resync")) {
		mii_card_disk2_t *c = _mish_d2;
		mii_floppy_t *f = &c->floppy[sel];
		printf("Resyncing tracks\n");
		for (int i = 0; i < MII_FLOPPY_TRACK_COUNT; i++)
			mii_floppy_resync_track(f, i, 0);
		return;
	}
	if (!strcmp(argv[1], "map")) {
		mii_card_disk2_t *c = _mish_d2;
		mii_floppy_t *f = &c->floppy[sel];

		printf("Disk map:\n");
		for (int i = 0; i < MII_FLOPPY_TRACK_COUNT; i++) {
			mii_floppy_track_map_t map = {};
			int r = mii_floppy_map_track(f, i, &map, 0);
			printf("Track %2d: %7s\n",
					i, r == 0 ? "OK" : "Invalid");
			if (r != 0) {
				printf("Track %d has %5d bits\n", i, f->tracks[i].bit_count);
				for (int si = 0; si < 16; si++)
					printf("[%2d hs:%4d h:%5d ds:%3d d:%5d]%s",
							si, map.sector[si].hsync,
							map.sector[si].header,
							map.sector[si].dsync,
							map.sector[si].data,
							si & 1 ? "\n" : " ");
			}
			mii_floppy_track_map_t map2 = f->tracks[i].map;
			// do a deeper compare, not using memcmp
			for (int si = 0; si < 16; si++) {
				if (map.sector[si].header != map2.sector[si].header ||
					map.sector[si].data != map2.sector[si].data) {
					printf("Track %2d sector %2d mismatch\n", i, si);
					// display details
					printf("  %2d: h: %4d:%5d d: %3d:%5d %08x:%08x\n", si,
							map.sector[si].hsync, map.sector[si].header,
							map.sector[si].dsync, map.sector[si].data,
							mii_floppy_read_track_bits(&f->tracks[i], f->track_data[i],
									map.sector[si].header, 32),
							mii_floppy_read_track_bits(&f->tracks[i], f->track_data[i],
									map.sector[si].data, 32));
					printf("  %2d: h: %4d:%5d d: %3d:%5d %08x:%08x\n", si,
							map2.sector[si].hsync, map2.sector[si].header,
							map2.sector[si].dsync, map2.sector[si].data,
							mii_floppy_read_track_bits(&f->tracks[i], f->track_data[i],
									map2.sector[si].header, 32),
							mii_floppy_read_track_bits(&f->tracks[i], f->track_data[i],
									map2.sector[si].data, 32));
				}
			}
		}
		return;
	}
	if (!strcmp(argv[1], "vcd")) {
		mii_card_disk2_t *c = _mish_d2;
		_mii_disk2_vcd_debug(c, !c->vcd);
		return;
	}
}

#include "mish.h"

MISH_CMD_NAMES(d2, "d2");
MISH_CMD_HELP(d2,
		"d2: disk ][ internals",
		" <default>: dump status",
		" list: list drives",
		" sel [0-1]: select drive",
		" wp [0-1]: write protect",
		" track <track 0-34> [count]: dump track",
		" track <track 0-34> save: save in /tmp/trackXX.bin",
		" dirty: mark track as dirty",
		" resync: resync all tracks",
		" map: show track map",
		" trace: toggle debug trace",
		" vcd: toggle VCD debug"
		);
MII_MISH(d2, _mii_mish_d2);
