/*
 * mii_epromcard.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * This is a driver for these eprom/flash cards from
 * Terence J. Boldt and the likes
 */
#define _GNU_SOURCE // for asprintf
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

typedef struct mii_card_ee_t {
	mii_dd_t 	drive[1];
	uint8_t * 	file;
	uint16_t  	latch;
} mii_card_ee_t;


static int
_mii_ee_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_ee_t *c = calloc(1, sizeof(*c));

	slot->drv_priv = c;
	printf("%s loading in slot %d\n", __func__, slot->id + 1);

	for (int i = 0; i < 1; i++) {
		mii_dd_t *dd = &c->drive[i];
		dd->slot_id = slot->id + 1;
		dd->drive = i + 1;
		dd->slot = slot;
		dd->ro = 1; dd->wp = 1;
		asprintf((char **)&dd->name, "EE1MB S:%d D:%d",
				dd->slot_id, dd->drive);
	}
	mii_dd_register_drives(&mii->dd, c->drive, 1);

#if 1
	mii_rom_t *rom = mii_rom_get("epromcard");
	c->file = (uint8_t*)rom->rom;
#else
	const char *fname = "disks/GamesWithFirmware.po";

	mii_dd_file_t *file = mii_dd_file_load(&mii->dd, fname, 0);
	mii_dd_drive_load(&c->drive[0], file);
	c->file = file->map;
#endif
	if (c->file) {
		uint16_t addr = 0xc100 + (slot->id * 0x100);
		mii_bank_write(
				&mii->bank[MII_BANK_CARD_ROM],
				addr, c->file + 0x300, 256);
	}
	return 0;
}

static uint8_t
_mii_ee_access(
	mii_t * mii, struct mii_slot_t *slot,
	uint16_t addr, uint8_t byte, bool write)
{
	mii_card_ee_t *c = slot->drv_priv;

//	printf("%s PC:%04x addr %04x %02x wr:%d\n", __func__,
//			mii->cpu.PC, addr, byte, write);
	int psw = addr & 0x0F;
	if (write) {
		switch (psw) {
			case 0:
				c->latch = (c->latch & 0xff00) | byte;
				break;
			case 1:
				c->latch = (c->latch & 0x00ff) | (byte << 8);
				break;
		}
	} else {
		return c->file ? c->file[(c->latch << 4) + psw] : 0xff;
	}
	return 0;
}

static int
_mii_ee_command(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint32_t cmd,
		void * param)
{
	mii_card_ee_t *c = slot->drv_priv;
	int res = -1;
	switch (cmd) {
		case MII_SLOT_DRIVE_COUNT:
			if (param) {
				*(int *)param = 1;
				res = 0;
			}
			break;
		case MII_SLOT_DRIVE_LOAD: {
			const char *filename = param;
			mii_dd_file_t *file = NULL;
			if (filename && *filename) {
				file = mii_dd_file_load(&mii->dd, filename, 0);
				if (!file)
					return -1;
			}
			mii_dd_drive_load(&c->drive[0], file);
			mii_rom_t *rom = mii_rom_get("epromcard");
			c->file = file ? file->map : (uint8_t*)rom->rom;
			res = 0;
		}	break;
	}
	return res;
}

static mii_slot_drv_t _driver = {
	.name = "eecard",
	.desc = "EEPROM 1MB card",
	.init = _mii_ee_init,
	.access = _mii_ee_access,
	.command = _mii_ee_command,
};
MI_DRIVER_REGISTER(_driver);
