/*
 * mii_disk2.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

//  Shamelesly lifted from periph/disk2.c
//
//  Copyright (c) 2023 Micah John Cowan.
//  This code is licensed under the MIT license.
//  See the accompanying LICENSE file for details.

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_disk2.h"
#include "mii_rom_disk2.h"
#include "mii_disk_format.h"

//#define DISK_DEBUG
#ifdef DISK_DEBUG
# define D2DBG(...)  do { \
		if (c->pr_count == 1) \
			fprintf(stderr, __VA_ARGS__); \
	} while(0)
#else
# define D2DBG(...)
#endif

typedef struct mii_card_disk2_t {
	uint32_t timer;
	bool motor_on;
	bool drive_two;
	bool write_mode;
	union {
		struct {
			DiskFormatDesc disk1;
			DiskFormatDesc disk2;
		};
		DiskFormatDesc disk[2];
	};
	uint8_t data_register; // only used for write
	bool steppers[4];
	int cog1;
	int cog2;
//	int pr_count; // debug print
} mii_card_disk2_t;

//static const size_t dsk_disksz = 143360;

int disk2_debug = 0;

static DiskFormatDesc *
active_disk_obj(
		mii_card_disk2_t *c)
{
	return c->drive_two ? &c->disk2 : &c->disk1;
}

#if 0
bool
drive_spinning(
		mii_card_disk2_t *c)
{
	return c->motor_on;
}

int
active_disk(
		mii_card_disk2_t *c)
{
	return c->drive_two ? 2 : 1;
}
int
eject_disk(
		mii_card_disk2_t *c,
		int drive)
{
	if (c->motor_on && active_disk(c) == drive) {
		return -1;
	}
	init(c); // to make sure

	if (drive == 1) {
		c->disk1.eject(&c->disk1);
		c->disk1 = disk_format_load(NULL);
	} else if (drive == 2) {
		c->disk2.eject(&c->disk2);
		c->disk2 = disk_format_load(NULL);
	}
	return 0;
}

int
insert_disk(
		mii_card_disk2_t *c,
		int drive,
		const char *path)
{
	int err = eject_disk(c, drive);
	if (err != 0) return err;

	// Make sure we're inserted to slot 6
	// XXX should check for error/distinguish if we're already in that slot
	(void) periph_slot_reg(6, &disk2card);

	if (err) {
		return -1; // XXX should be a distinguishable err code
	}
	if (drive == 1) {
		c->disk1 = disk_format_load(path);
	} else if (drive == 2) {
		c->disk2 = disk_format_load(path);
	}

	return 0;
}
#endif

// NOTE: cog "left" and "right" refers only to the number line,
//       and not the physical cog or track head movement.
static bool
cogleft(
		mii_card_disk2_t *c,
		int *cog)
{
	int cl = ((*cog) + 3) % 4; // position to the "left" of cog
	return (!c->steppers[(*cog)] && c->steppers[cl]);
}

// NOTE: cog "left" and "right" refers only to the number line,
//       and not the physical cog or track head movement.
static bool
cogright(
		mii_card_disk2_t *c,
		int *cog)
{
	int cr = ((*cog) + 1) % 4; // position to the "right" of cog
	return (!c->steppers[(*cog)] && c->steppers[cr]);
}

static int *
active_cog(
		mii_card_disk2_t *c)
{
	return c->drive_two? &c->cog2 : &c->cog1;
}

static void
adjust_track(
		mii_card_disk2_t *c)
{
	DiskFormatDesc *disk = active_disk_obj(c);
	int *cog = active_cog(c);
	D2DBG("halftrack: ");
	if (cogleft(c, cog)) {
		*cog = ((*cog) + 3) % 4;
		if (disk->halftrack > 0) --disk->halftrack;
		D2DBG("dec to %d", disk->halftrack);
	} else if (cogright(c, cog)) {
		*cog = ((*cog) + 1) % 4;
		if (disk->halftrack < 69) ++disk->halftrack;
		D2DBG("inc to %d", disk->halftrack);
	} else {
		D2DBG("no change (%d)", disk->halftrack);
	}
}

static void
stepper_motor(
		mii_card_disk2_t *c,
		uint8_t psw)
{
	uint8_t phase = psw >> 1;
	bool onoff = (psw & 1) != 0;

	D2DBG("phase %d %s ", (int)phase, onoff? "on, " : "off,");
	c->steppers[phase] = onoff;
	adjust_track(c);
}

static inline uint8_t encode4x4(mii_card_disk2_t *c, uint8_t orig)
{
	return (orig | 0xAA);
}

static void turn_off_motor(mii_card_disk2_t *c)
{
	c->motor_on = false;
	DiskFormatDesc *disk = active_disk_obj(c);
	disk->spin(disk, false);
//    event_fire_disk_active(0);
}

static int
_mii_disk2_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_disk2_t *c = calloc(1, sizeof(*c));

	slot->drv_priv = c;
	c->disk1 = disk_format_load(NULL);
	c->disk2 = disk_format_load(NULL);
	printf("%s loading in slot %d\n", __func__, slot->id + 1);
	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr, mii_rom_disk2, 256);

	return 0;
}

static void
_mii_disk2_run(
		mii_t * mii,
		struct mii_slot_t *slot)
{
	mii_card_disk2_t *c = slot->drv_priv;
	if (c->timer && c->timer <= mii->video.frame_count) {
		printf("%s turning off motor\n", __func__);
		c->timer = 0;
		turn_off_motor(c);
	}
}

static uint8_t
_mii_disk2_access(
	mii_t * mii, struct mii_slot_t *slot,
	uint16_t addr, uint8_t byte, bool write)
{
	mii_card_disk2_t *c = slot->drv_priv;
	uint8_t ret = 0;

	int psw = addr & 0x0F;

	if (c->timer)
		c->timer = mii->video.frame_count + 60;
	uint8_t last = -1;
	if (disk2_debug && last != psw + (write? 0x10 : 0))
		printf("disk sw $%02X, PC = $%04X\n", psw, mii->cpu.PC);
	last = psw + (write? 0x10 : 0);
	if (write)
		c->data_register = byte; // ANY write sets data register
	if (psw < 8) {
		stepper_motor(c, psw);
	} else switch (psw) {
		case 0x08:
			if (c->motor_on) {
				c->timer = mii->video.frame_count + 60;
				//frame_timer(60, turn_off_motor);
			}
			break;
		case 0x09: {
//            frame_timer_cancel(turn_off_motor);
			c->timer = 0;
			c->motor_on = true;
			DiskFormatDesc *disk = active_disk_obj(c);
			disk->spin(disk, true);
            if (disk2_debug)
    			printf("%s turning on motor %d\n", __func__, c->drive_two);
	   //     event_fire_disk_active(drive_two? 2 : 1);
		}	break;
		case 0x0A:
			if (c->motor_on && c->drive_two) {
				c->disk2.spin(&c->disk2, false);
				c->disk1.spin(&c->disk1, true);
			}
			c->drive_two = false;
			if (c->motor_on) {
			//    event_fire_disk_active(1);
			}
			break;
		case 0x0B:
			if (c->motor_on && !c->drive_two) {
				c->disk1.spin(&c->disk1, false);
				c->disk2.spin(&c->disk2, true);
			}
			c->drive_two = true;
			if (c->motor_on) {
			//    event_fire_disk_active(2);
			}
			break;
		case 0x0C: {
			DiskFormatDesc *disk = active_disk_obj(c);
			if (!c->motor_on) {
				// do nothing
			} else if (c->write_mode) {
				// XXX ignores timing
				disk->write_byte(disk, c->data_register);
				c->data_register = 0; // "shifted out".
			} else {
				// XXX any even-numbered switch can be used
				//  to read a byte. But for now we do so only
				//  through the sanctioned switch for that purpose.
				ret = c->data_register = disk->read_byte(disk);
			//	printf("read byte %02X\n", ret);
			}
		}	break;
		case 0x0D:
#if 0
			if (!motor_on || drive_two) {
				// do nothing
			} else if (write_mode) {
				data_register = (val == -1? 0: val);
			} else {
				// XXX should return whether disk is write-protected...
			}
#endif
			break;
		case 0x0E:
			c->write_mode = false;
            if (disk2_debug)
                printf("%s write mode off\n", __func__);
			break;
		case 0x0F:
			c->write_mode = true;
            if (disk2_debug)
                printf("%s write mode on\n", __func__);
			break;
		default:
			;
	}

	D2DBG("\n");

	return ret;
}

static int
_mii_disk2_command(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint8_t cmd,
		void * param)
{
	mii_card_disk2_t *c = slot->drv_priv;
	switch (cmd) {
		case MII_SLOT_DRIVE_COUNT:
			if (param)
				*(int *)param = 2;
			break;
		case MII_SLOT_DRIVE_LOAD ... MII_SLOT_DRIVE_LOAD + 2 - 1:
			int drive = cmd - MII_SLOT_DRIVE_LOAD;
			if (c->disk[drive].privdat) {
				c->disk[drive].eject(&c->disk[drive]);
			}
			const char *filename = param;
			printf("%s: drive %d loading %s\n", __func__, drive,
						filename);
			c->disk[drive] = disk_format_load(
								filename && *filename ? filename : NULL);
			break;
	}
	return 0;
}

static mii_slot_drv_t _driver = {
	.name = "disk2",
	.desc = "Apple Disk ][",
	.init = _mii_disk2_init,
	.access = _mii_disk2_access,
	.run = _mii_disk2_run,
	.command = _mii_disk2_command,
};
MI_DRIVER_REGISTER(_driver);
