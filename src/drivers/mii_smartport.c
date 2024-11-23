/*
 * mii_smartport.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * This is shamelessly inspired from:
 * https:github.com/ct6502/apple2ts/blob/main/src/emulator/harddrivedata.ts
 * http://www.1000bit.it/support/manuali/apple/technotes/smpt/tn.smpt.1.html
 */
#define _GNU_SOURCE // for asprintf
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_dd.h"
#include "mii_slot.h"


#define MII_SM_DRIVE_COUNT 2

typedef struct mii_card_sm_t {
	mii_dd_t drive[MII_SM_DRIVE_COUNT];
	struct mii_slot_t *slot;
} mii_card_sm_t;

static void
_mii_hd_callback(
		mii_t *mii,
		uint8_t trap)
{
	int sid = ((mii->cpu.PC >> 8) & 0xf) - 1;
	mii_card_sm_t *c = mii->slot[sid].drv_priv;

	uint8_t command 	= mii_read_one(mii, 0x42);
	uint8_t unit 		= mii_read_one(mii, 0x43);
	uint16_t buffer 	= mii_read_word(mii, 0x44);
	uint16_t blk 		= mii_read_word(mii, 0x46);

	unit >>= 7; 		// last bit is the one we want, drive 0/1
	switch (command) {
		case 0: { // get status
			if (!c->drive[unit].file) {
				mii->cpu.X = mii->cpu.Y = 0;
				mii->cpu.P.C = 1;
			} else {
				int nblocks = (c->drive[unit].file->size + 511) / 512;
				mii->cpu.X = nblocks & 0xff;
				mii->cpu.Y = nblocks >> 8;
				mii->cpu.P.C = 0;
			}
		}	break;
		case 1: {// read block
			if (!c->drive[unit].file) {
				mii->cpu.P.C = 1;
				break;
			}
			if (blk >= c->drive[unit].file->size / 512) {
				mii->cpu.P.C = 1;
				break;
			}
			mii_bank_t * bank = &mii->bank[mii->mem[buffer >> 8].write];
			mii->cpu.P.C = mii_dd_read(
							&c->drive[unit], bank, buffer, blk, 1) != 0;
			// if Prodos is reading a block that happens to be video memory,
			// make sure the video driver knows about it
			mii_video_OOB_write_check(mii, buffer, 512);
		}	break;
		case 2: {// write block
			if (!c->drive[unit].file) {
				mii->cpu.P.C = 1;
				break;
			}
			if (blk >= c->drive[unit].file->size / 512) {
				mii->cpu.P.C = 1;
				break;
			}
			mii_bank_t * bank = &mii->bank[mii->mem[buffer >> 8].read];
			mii->cpu.P.C = mii_dd_write(
							&c->drive[unit], bank, buffer, blk, 1) != 0;
		}	break;
		default: {
			printf("%s cmd %02x unit %02x buffer %04x blk %04x\n", __func__,
					command, unit, buffer, blk);
			printf("*** %s: unhandled command %02x\n", __func__, command);
			mii->cpu.P.C = 1;
		}
	}
}

static void
_mii_sm_callback(
		mii_t *mii,
		uint8_t trap)
{
//	printf("%s\n", __func__);
	int sid = ((mii->cpu.PC >> 8) & 0xf) - 1;
	mii_card_sm_t *c = mii->slot[sid].drv_priv;

	uint16_t sp = 0x100 + mii->cpu.S + 1;
	uint16_t call_addr = mii_read_word(mii, sp);
	uint8_t	spCommand = mii_read_one(mii, call_addr + 1);
	uint16_t spParams = mii_read_word(mii, call_addr + 2);
	call_addr += 3;
	mii_write_word(mii, sp, call_addr);

	uint8_t spPCount = mii_read_one(mii, spParams + 0);
	uint8_t spUnit = mii_read_one(mii, spParams + 1);
	uint16_t spBuffer = mii_read_word(mii, spParams + 2);

//	printf("%s cmd %02x params %04x pcount %d unit %02x buffer %04x\n", __func__,
//			spCommand, spParams, spPCount, spUnit, spBuffer);
	switch (spCommand) {
		case 0: { // get status
			if (spPCount != 3) {
				mii->cpu.P.C = 1;
				break;
			}
			uint8_t status = mii_read_one(mii, spParams + 4);
		//	printf("%s: unit %d status %02x \n", __func__, spUnit, status);
			uint8_t st = 0x80 | 0x40 | 0x20;
			uint32_t bsize = 0;
			if (status == 0) {
				mii->cpu.P.C = 0;
				mii->cpu.A = 0;
				/* Apple IIc reference says this ought to be a status byte,
				 * but practice and A2Desktop says it ought to be a drive
				 * count, so here goes... */
//				mii_write_one(mii, spBuffer++, st);
				if (spUnit == 0) {
					mii_write_one(mii, spBuffer++, MII_SM_DRIVE_COUNT);
					mii_write_one(mii, spBuffer++, 0x00);
					mii_write_one(mii, spBuffer++, 0x01);
					mii_write_one(mii, spBuffer++, 0x13);
				} else if (spUnit <= MII_SM_DRIVE_COUNT) {
					if (c->drive[spUnit-1].file) {
						st |= 0x10;
						bsize = (c->drive[spUnit-1].file->size + 511) / 512;
					}
					mii_write_one(mii, spBuffer++, st);
					mii_write_one(mii, spBuffer++, bsize);
					mii_write_one(mii, spBuffer++, bsize >> 8);
					mii_write_one(mii, spBuffer++, bsize >> 16);
				} else {
					mii->cpu.P.C = 1;
					mii->cpu.A = 0x21; // bad status
				}
			} else if (status == 3) {
				mii->cpu.P.C = 0;
				mii->cpu.A = 0;
				if (spUnit > 0 && spUnit <= MII_SM_DRIVE_COUNT) {
					if (c->drive[spUnit-1].file) {
						st |= 0x10;
						bsize = (c->drive[spUnit-1].file->size + 511) / 512;
					}
					mii_write_one(mii, spBuffer++, st);
					mii_write_one(mii, spBuffer++, bsize);
					mii_write_one(mii, spBuffer++, bsize >> 8);
					mii_write_one(mii, spBuffer++, bsize >> 16);
					char dname[17] = "\x8MII HD 0        ";
					dname[8] = '0' + spUnit-1;
					for (int i = 0; i < 17; i++)
						mii_write_one(mii, spBuffer++, dname[i]);
					mii_write_one(mii, spBuffer++, 0x02); // Profile
					mii_write_one(mii, spBuffer++, 0x00); // Profile
					mii_write_one(mii, spBuffer++, 0x01); // Version
					mii_write_one(mii, spBuffer++, 0x13);
				} else {
					mii->cpu.P.C = 1;
					mii->cpu.A = 0x21; // bad status
				}
			} else {
				printf("%s: unit %d bad status %d\n",
						__func__, spUnit, status);
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x21; // bad status
			}
		}	break;
		case 1: { // read
			mii->cpu.P.C = 0;
			mii->cpu.A = 0;
			if (spPCount != 3) {
				printf("%s: unit %d bad pcount %d\n",
						__func__, spUnit, spPCount);
				mii->cpu.P.C = 1;
				break;
			}
			if (spUnit == 0 || spUnit >= MII_SM_DRIVE_COUNT) {
				printf("%s: unit %d out of range\n", __func__, spUnit);
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x28;
				break;
			}
			spUnit--;
			uint32_t blk = mii_read_one(mii, spParams + 4) |
								(mii_read_one(mii, spParams + 5) << 8) |
								(mii_read_one(mii, spParams + 6) << 16);
		//	printf("%s read block 0x%6x\n", __func__, blk);
			if (!c->drive[spUnit].file) {
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x2f;
				break;
			}
			if (blk >= c->drive[spUnit].file->size / 512) {
				printf("%s: block %d out of range\n",
						__func__, blk);
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x2d;
				break;
			}
			mii_bank_t * bank = &mii->bank[mii->mem[spBuffer >> 8].write];
			mii->cpu.P.C = mii_dd_read(
							&c->drive[spUnit], bank, spBuffer, blk, 1) != 0;
			if (mii->cpu.P.C)
				mii->cpu.A = 0x2d;
			// if Prodos is reading a block that happens to be video memory,
			// make sure the video driver knows about it
			mii_video_OOB_write_check(mii, spBuffer, 512);
		//	mii->cpu.P.C = 0;
		}	break;
		case 2: { // write
			mii->cpu.P.C = 0;
			mii->cpu.A = 0;
			if (spPCount != 3) {
				printf("%s: unit %d bad pcount %d\n",
						__func__, spUnit, spPCount);
				mii->cpu.P.C = 1;
				break;
			}
			if (spUnit >= MII_SM_DRIVE_COUNT) {
				printf("%s: unit %d out of range\n",
						__func__, spUnit);
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x28;
				break;
			}
			spUnit--;
			uint32_t blk = mii_read_one(mii, spParams + 4) |
								(mii_read_one(mii, spParams + 5) << 8) |
								(mii_read_one(mii, spParams + 6) << 16);
		//	printf("%s write block %x\n", __func__, blk);
			if (!c->drive[spUnit].file) {
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x2f;
				break;
			}
			if (blk >= c->drive[spUnit].file->size / 512) {
				printf("%s: block %d out of range\n",
						__func__, blk);
				mii->cpu.P.C = 1;
				mii->cpu.A = 0x2d;
				break;
			}
			mii_bank_t * bank = &mii->bank[mii->mem[spBuffer >> 8].read];
			mii->cpu.P.C = mii_dd_write(
							&c->drive[spUnit], bank, spBuffer, blk, 1) != 0;
			if (mii->cpu.P.C)
				mii->cpu.A = 0x2d;
		}	break;
	}
}

static int
_mii_sm_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_sm_t *c = calloc(1, sizeof(*c));
	c->slot = slot;
	slot->drv_priv = c;

//	printf("%s loading in slot %d\n", __func__, slot->id + 1);
	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_rom_t *rom = mii_rom_get("smartport");
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr, rom->rom, 256);

	uint8_t trap_hd = mii_register_trap(mii, _mii_hd_callback);
	uint8_t trap_sm = mii_register_trap(mii, _mii_sm_callback);
//	printf("%s: traps %02x %02x\n", __func__, trap_hd, trap_sm);
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr + 0xd2, &trap_hd, 1);
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr + 0xe2, &trap_sm, 1);

	for (int i = 0; i < MII_SM_DRIVE_COUNT; i++) {
		mii_dd_t *dd = &c->drive[i];
		dd->slot_id = slot->id + 1;
		dd->drive = i + 1;
		dd->slot = slot;
		asprintf((char **)&dd->name, "SmartPort S:%d D:%d",
				dd->slot_id, dd->drive);
	}
	mii_dd_register_drives(&mii->dd, c->drive, MII_SM_DRIVE_COUNT);

	return 0;
}

static void
_mii_sm_dispose(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_sm_t *c = slot->drv_priv;
	for (int i = 0; i < MII_SM_DRIVE_COUNT; i++) {
		free((char *)c->drive[i].name);
		c->drive[i].name = NULL;
	}
	// files attached to drives are automatically freed.
	free(c);
	slot->drv_priv = NULL;
}

static int
_mii_sm_command(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint32_t cmd,
		void * param)
{
	mii_card_sm_t *c = slot->drv_priv;
	int res = -1;
	switch (cmd) {
		case MII_SLOT_DRIVE_COUNT:
			if (param) {
				*(int *)param = MII_SM_DRIVE_COUNT;
				res = 0;
			}
			break;
		case MII_SLOT_DRIVE_LOAD ... MII_SLOT_DRIVE_LOAD + MII_SM_DRIVE_COUNT - 1: {
			int drive = cmd - MII_SLOT_DRIVE_LOAD;
			const char *filename = param;
			mii_dd_file_t *file = NULL;
			if (filename && *filename) {
				file = mii_dd_file_load(&mii->dd, filename, 0);
				if (!file)
					return -1;
			}
			mii_dd_drive_load(&c->drive[drive], file);
			res = 0;
		}	break;
	}
	return res;
}

static uint8_t
_mii_sm_access(
	mii_t * mii, struct mii_slot_t *slot,
	uint16_t addr, uint8_t byte, bool write)
{
	return 0;
}

static mii_slot_drv_t _driver = {
	.name = "smartport",
	.desc = "SmartPort card",
	.init = _mii_sm_init,
	.dispose = _mii_sm_dispose,
	.access = _mii_sm_access,
	.command = _mii_sm_command,
};
MI_DRIVER_REGISTER(_driver);
