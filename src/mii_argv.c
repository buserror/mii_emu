/*
 * mii_argv.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii.h"

extern mii_slot_drv_t * mii_slot_drv_list;

static void
_mii_usage(
	const char *progname)
{
	printf("Usage: %s [options]\n", progname);
	printf("Options:\n");
	printf("  -h, --help\tThis help\n");
	printf("  -v, --verbose\tVerbose output\n");
	printf("  -fs, --full-screen\tStart in full screen mode\n");
	printf("  -hide, --hide-ui, --no-ui\tHide the UI\n");
	printf("  --list-drivers\tList available drivers, exit\n");
	printf("  --list-roms\tList available ROMs, exit\n");
	printf("  --video-rom <name>\tLoad a video ROM\n");
	printf("  -m, --mute\tMute the speaker\n");
	printf("  -vol, --volume <volume>\tSet speaker volume (0.0 to 10.0)\n");
	printf("  --audio-off, --no-audio, --silent\tDisable audio output\n");
	printf("  -speed, --speed <speed>\tSet the CPU speed in MHz\n");
	printf("  -s, --slot <slot>:<driver>\tSpecify a slot and driver\n");
	printf("\t\tSlot id is 1..7\n");
	printf("  -d, --drive <slot>:<drive>:<filename>\tLoad a drive\n");
	printf("\t\tSlot id is 1..7, drive is 1..2\n");
	printf("\t\tAlternate syntax: <slot>:<drive> <filename>\n");
	printf("  -def, --default\tUse a set of default cards:\n");
	printf("\t\tSlot 4: mouse\n");
	printf("\t\tSlot 6: disk2\n");
	printf("\t\tSlot 7: smartport\n");
	printf("  -nsc[=0|1]\tEnable/Disable No Slot Clock:\n");
	printf("\t\t0: disable\n");
	printf("\t\t1: enable [Enabled by default]\n");
	printf("  -titan[=0|1]\tEnable/Disable Titan Accelerator IIe:\n");
	printf("\t\t0: disable [default]\n");
	printf("\t\t1: enable [Start at 3.58MHz]\n");
}

int
mii_argv_parse(
	mii_t *mii,
	int argc,
	const char *argv[],
	int *index,
	uint32_t *ioFlags)
{
	if (*index == 0)
		*index += 1;
	for (int i = *index; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			_mii_usage(argv[0]);
			exit(0);
		} else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
		//	mii->verbose++;
		//	continue;
		} else if (!strcmp(arg, "-fs") || !strcmp(arg, "--full-screen")) {
			*ioFlags |= MII_INIT_FULLSCREEN;
		} else if (!strcmp(arg, "-hide") || !strcmp(arg, "--hide-ui") ||
					!strcmp(arg, "--no-ui")) {
			*ioFlags |= MII_INIT_HIDE_UI;
		} else if ((!strcmp(arg, "-s") || !strcmp(arg, "--slot")) && i < argc-1) {
			// for mat for slot is 1..8:<name> where name is the driver name
			const char *p = argv[++i];
			int slot = 0;
			char *drv = NULL;
			if (sscanf(p, "%d:%ms", &slot, &drv) != 2) {
				printf("mii: invalid slot specification %s\n", p);
				return 1;
			}
			if (slot < 1 || slot > 8) {
				printf("mii: invalid slot number %d\n", slot);
				return 1;
			}
			if (drv == NULL) {
				printf("mii: missing driver name for slot %d\n", slot);
				return 1;
			}
			if (mii_slot_drv_register(mii, slot, drv) < 0) {
				printf("mii: failed to register driver %s for slot %d\n", drv, slot);
				return 1;
			}
		} else if ((!strcmp(arg, "-d") || !strcmp(arg, "--drive")) && i < argc-1) {
			// drive takes 2 following arguments, the <slot>:<drive> and a filename
			const char *p = argv[++i];
			int slot = 0;
			int drive = 0;
			const char *filename = NULL;
			int got = sscanf(p, "%d:%d:%ms", &slot, &drive, &filename);
			if (got == 2) {
				if (i < argc-1) {
					filename = argv[++i];
					got = 3;
				} else {
					printf("mii: missing filename for drive %d:%d\n", slot, drive);
					return 1;
				}
			} else if (got != 3) {
				printf("mii: invalid drive specification %s\n", p);
				return 1;
			}
			if (slot < 1 || slot > 8) {
				printf("mii: invalid slot number %d\n", slot);
				return 1;
			}
			if (drive < 1 || drive > 2) {
				printf("mii: invalid drive number %d\n", drive);
				return 1;
			}
			if (filename == NULL) {
				printf("mii: missing filename for drive %d:%d\n", slot, drive);
				return 1;
			}
			mii_slot_command(mii, slot,
					MII_SLOT_DRIVE_LOAD + drive - 1,
					(void*)filename);
		} else if (!strcmp(arg, "-def") || !strcmp(arg, "--default")) {
			mii_slot_drv_register(mii, 4, "mouse");
			mii_slot_drv_register(mii, 6, "disk2");
			mii_slot_drv_register(mii, 7, "smartport");
		} else if (!strcmp(arg, "-2c") || !strcmp(arg, "--2c") ||
						!strcmp(arg, "--iic")) {
			mii->emu = MII_EMU_IIC;
			mii_slot_drv_register(mii, 1, "ssc");
			mii_slot_drv_register(mii, 2, "ssc");
			mii_slot_drv_register(mii, 4, "mouse");
			mii_slot_drv_register(mii, 5, "smartport");
			mii_slot_drv_register(mii, 6, "disk2");
		} else if (!strcmp(arg, "-L") || !strcmp(arg, "--list-drivers")) {
			mii_slot_drv_t * drv = mii_slot_drv_list;
			printf("mii: available drivers:\n");
			while (drv) {
				printf("%10.10s - %s\n", drv->name, drv->desc);
				drv = drv->next;
			}
			exit(0);
		} else if (!strcmp(arg, "--list-roms")) {
			mii_rom_t * rom = mii_rom_get(NULL);
			while (rom) {
				printf("rom: %-20s %-12s %7d %s\n", rom->name, rom->class,
						rom->len, rom->description);
				rom = SLIST_NEXT(rom, self);
			}
			exit(0);
		} else if (!strcmp(arg, "--video-rom") && i < argc-1) {
			const char *name = argv[++i];
			mii_rom_t *rom = mii_rom_get_class(NULL, "video");
			while (rom) {
				if (!strcmp(rom->name, name)) {
					mii->video.rom = rom;
					break;
				}
				rom = SLIST_NEXT(rom, self);
			}
			if (!rom) {
				printf("mii: video rom %s not found\n", name);
				return 1;
			}
		} else if (!strcmp(arg, "-m") || !strcmp(arg, "--mute")) {
			mii->audio.muted = true;
		} else if (!strcmp(arg, "--audio-off") ||
					!strcmp(arg, "--no-audio") ||
					!strcmp(arg, "--silent")) {
			mii->audio.drv = NULL;
			*ioFlags |= MII_INIT_SILENT;
		} else if (!strcmp(arg, "-vol") || !strcmp(arg, "--volume")) {
			if (i < argc-1) {
				float vol = atof(argv[++i]);
				if (vol < 0) vol = 0;
				else if (vol > 10) vol = 10;
				mii_audio_volume(&mii->speaker.source, vol);
			} else {
				printf("mii: missing volume value\n");
				return 1;
			}
		} else if (!strcmp(arg, "-speed") || !strcmp(arg, "--speed")) {
			if (i < argc-1) {
				mii->speed = atof(argv[++i]);
				if (mii->speed <= 0.0)
					mii->speed = MII_SPEED_NTSC;
			} else {
				printf("mii: missing speed value\n");
				return 1;
			}
		} else {
			if (argv[i][0] == '-') {
				char dup[128];
				snprintf(dup, sizeof(dup), "%s", argv[i] + 1);
				char *equal = dup;
				char *name = strsep(&equal, "=");
				int enable = 1;
				if (equal && *equal) {
					if (!strcmp(equal, "0"))
						enable = 0;
					else if (!strcmp(equal, "1"))
						enable = 1;
					else {
						printf("mii: invalid flag %s\n", argv[i]);
						return 1;
					}
				}
			//	printf("%s lookup driver %s to %s\n", __func__,
			//				name, enable ? "enable" : "disable");
				mii_slot_drv_t * drv = mii_slot_drv_list;
				int done = 0;
				while (drv) {
					printf("%10.10s - %s\n", drv->name, drv->desc);
					if (drv->enable_flag) {
						if (!strcmp(name, drv->name)) {
							*ioFlags = (*ioFlags & ~drv->enable_flag) |
								(enable ? drv->enable_flag : 0);
							printf("mii: %s %s\n", name, enable ? "enabled" : "disabled");
							break;
						}
					}
					drv = drv->next;
				}
				if (!done && equal) {
					printf("mii: no driver found %s\n", argv[i]);
					return 1;
				}
				continue;
			}
			printf("mii: unknown argument %s\n", argv[i]);
			return 1;
		}
	}
	*index = argc;
	return 1;
}