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
	uint8_t * file;
	uint16_t  latch;
} mii_card_ee_t;

int mmapfile(const char *fname, uint8_t **buf, size_t *sz, int flags);

static int
_mii_ee_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	uint8_t * file;
	size_t sz;
	const char *fname = "disks/Apple IIe Diagnostic 2.1.po";
	//const char *fname = "disks/GamesWithFirmware.po";

	if (mmapfile(fname, &file, &sz, O_RDONLY) != 0) {
		printf("Failed to load %s\n", fname);
		return -1;
	}
	mii_card_ee_t *c = calloc(1, sizeof(*c));

	slot->drv_priv = c;
	c->file = file;
	printf("%s loading in slot %d\n", __func__, slot->id);
	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr, c->file + 0x300, 256);

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
		return c->file[(c->latch << 4) + psw];
	}
	return 0;
}

static mii_slot_drv_t _driver = {
	.name = "eecard",
	.desc = "EEPROM 1MB card",
	.init = _mii_ee_init,
	.access = _mii_ee_access,
};
MI_DRIVER_REGISTER(_driver);
