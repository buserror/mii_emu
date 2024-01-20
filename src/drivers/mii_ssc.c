/*
 * mii_ssc.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

/*
			THIS IS A PLACEHOLDER DO NOT USE
 */
#include "mii.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_sw.h"

#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX mii_
#include "incbin.h"

INCBIN(ssc_rom, "roms/mii_rom_scc_3410065a.bin");

typedef struct mii_card_ssc_t {
	struct mii_slot_t *	slot;
	struct mii_bank_t * rom;
	mii_t *				mii;
	uint8_t 			slot_offset;
} mii_card_ssc_t;

static bool
_mii_ssc_select(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	mii_card_ssc_t *c = param;
	printf("%s selected:%d\n", __func__, c->slot->aux_rom_selected);
	if (c->slot->aux_rom_selected)
		return false;
	mii_bank_write(c->rom, 0xc800, mii_ssc_rom_data, 2048);
	c->slot->aux_rom_selected = true;
	SW_SETSTATE(c->mii, SLOTAUXROM, 1);
	c->mii->mem_dirty = true;
	return false;
}

static int
_mii_ssc_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_ssc_t *c = calloc(1, sizeof(*c));
	c->slot = slot;
	slot->drv_priv = c;
	c->mii = mii;

	printf("%s: THIS IS A PLACEHOLDER DO NOT USE\n", __func__);
	printf("%s loading in slot %d\n", __func__, slot->id + 1);

	c->slot_offset = slot->id + 1 + 0xc0;

	uint16_t addr = 0xc100 + (slot->id * 0x100);
	c->rom = &mii->bank[MII_BANK_CARD_ROM];
	mii_bank_write(c->rom, addr, mii_ssc_rom_data + 7*256, 256);
	/*
	 * install a callback that will be called for every access to the
	 * ROM area, we need this to re-install the secondary part of the ROM
	 * when the card 'slot' rom is accessed.
	 */
	mii_bank_install_access_cb(c->rom,
			_mii_ssc_select, c, addr >> 8, addr >> 8);

	return 0;
}

static void
_mii_ssc_dispose(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_ssc_t *c = slot->drv_priv;
	free(c);
	slot->drv_priv = NULL;
}

static uint8_t
_mii_ssc_access(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint16_t addr,
		uint8_t byte,
		bool write)
{
//	mii_card_ssc_t *c = slot->drv_priv;

	int psw = addr & 0x0F;
//	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	switch (psw) {
		default:
			printf("%s PC:%04x addr %04x %02x wr:%d\n", __func__,
					mii->cpu.PC, addr, byte, write);
			break;
	}
	return 0;
}

static int
_mii_ssc_command(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint8_t cmd,
		void * param)
{
//	mii_card_ssc_t *c = slot->drv_priv;
	switch (cmd) {
		case MII_SLOT_SSC_SET_TTY: {
			const char * tty = param;
			printf("%s: set tty %s\n", __func__, tty);
		}	break;
	}
	return -1;
}

static mii_slot_drv_t _driver = {
	.name = "ssc",
	.desc = "Super Serial card",
	.init = _mii_ssc_init,
	.dispose = _mii_ssc_dispose,
	.access = _mii_ssc_access,
	.command = _mii_ssc_command,
};
MI_DRIVER_REGISTER(_driver);
