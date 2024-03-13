/*
 * mii_noslotclock.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */
// http://ctrl.pomme.reset.free.fr/index.php/hardware/no-slot-clock-ds1216e/
// Disassembly:
// https://gist.github.com/mgcaret/ae2860c754fd029d2640107c4fe0bffd


#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mii.h"
#include "mii_bank.h"
#include "minipt.h"

const uint64_t nsc_pattern = 0x5CA33AC55CA33AC5ULL;

typedef struct mii_nsc_t {
	mii_t *		mii;
	uint64_t 	pattern;
	void * 		state;		// protothread state
	int 		bitcount;
	int 		mode; 		// read or write;
} mii_nsc_t;

static void
_mii_nsc_maketime(
		mii_nsc_t *nsc)
{
	uint64_t ret = 0;

	time_t now = time(NULL);
	struct tm t;
	localtime_r(&now, &t);

	int year = t.tm_year % 100;
	ret = year / 10;
	ret <<= 4;
	ret += year % 10;
	ret <<= 4;

	int month = t.tm_mon + 1;
	ret += month / 10;
	ret <<= 4;
	ret += month % 10;
	ret <<= 4;

	int day = t.tm_mday;
	ret += day / 10;
	ret <<= 4;
	ret += day % 10;
	ret <<= 4;

	// Bits 4 and 5 of the day ret are used to control the RST and oscillator
	// functions. These bits are shipped from the factory set to logic 1.
	ret += 0x0; //0x3, but zero on read.
	ret <<= 4;
	ret += t.tm_wday + 1; // uint64(now.Weekday()) + 1
	ret <<= 4;

	int hour = t.tm_hour;
	ret += 0x0; // 0x8 for 24 hour mode, but zero on read.
	ret += hour / 10;
	ret <<= 4;
	ret += hour % 10;
	ret <<= 4;

	int minute = t.tm_min;
	ret += minute / 10;
	ret <<= 4;
	ret += minute % 10;
	ret <<= 4;

	int second = t.tm_sec;
	ret += second / 10;
	ret <<= 4;
	ret += second % 10;
	ret <<= 4;

	int centisecond = 0;
	ret += centisecond / 10;
	ret <<= 4;
	ret += centisecond % 10;
	nsc->pattern = ret;
}

/*
 * this is a protothread, so remember no locals that will survive a yield()
 */
static bool
_mii_nsc_access(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (!bank) {
	//	printf("%s: disposing of NSC\n", __func__);
		free(param);
		return false;
	}
	mii_nsc_t * nsc = param;
//	printf("%s PC:%04x access %s addr %04x byte %02x write %d\n",
//			__func__, nsc->mii->cpu.PC,
//			bank->name, addr, *byte, write);
	int rd = (addr & 0x4);
	int bit = addr & 1;
	bool res = false;
	pt_start(nsc->state);
	do {
		if (rd) {
			nsc->pattern = 0;
		} else {
			nsc->pattern = (nsc->pattern >> 1) | ((uint64_t)bit << 63);
			int match = nsc->pattern == nsc_pattern;
			if (match) {
			//	printf("pattern %016lx %s\n", nsc->pattern, match ? "match" : "");
				break;
			}
		}
		pt_yield(nsc->state);
	} while (1);
	nsc->bitcount = 0;
	pt_yield(nsc->state);
	nsc->mode = rd;
//	printf("%s in %s mode\n", __func__, rd ? "read" : "write");
	if (nsc->mode) {
		_mii_nsc_maketime(nsc);
	//	printf("time %016lx\n", nsc->pattern);
	}
	do {
		if (nsc->mode) {
			*byte = (nsc->pattern >> nsc->bitcount) & 1;
		} else {
			nsc->pattern |= ((uint64_t)bit << nsc->bitcount);
		}
		res = true; // don't call ROM handler
		pt_yield(nsc->state);
		nsc->bitcount++;
	} while (nsc->bitcount < 64 && nsc->mode == rd);
//	printf("%s done\n", __func__);
	pt_end(nsc->state);
	return res;
}

static int
_mii_nsc_probe(
		mii_t *mii,
		uint32_t flags)
{
//	printf("%s %s\n", __func__, flags & MII_INIT_NSC ? "enabled" : "disabled");
	if (!(flags & MII_INIT_NSC))
		return 0;
	mii_nsc_t * nsc = calloc(1, sizeof(*nsc));
	nsc->mii = mii;
	// This worked fine with NS.CLOCK.SYSTEM but...
//	mii_bank_install_access_cb(&mii->bank[MII_BANK_CARD_ROM],
//			_mii_nsc_access, nsc, 0xc1, 0);
	/* ... A2Desktop requires the NSC to be on the main rom, the source
	 * claims it probe the slots, but in fact, it doesnt */
	mii_bank_install_access_cb(&mii->bank[MII_BANK_ROM],
			_mii_nsc_access, nsc, 0xc8, 0);
	return 1;
}

static mii_slot_drv_t _driver = {
	.name = "nsc",
	.desc = "No Slot Clock",
	.enable_flag = MII_INIT_NSC,
	.probe = _mii_nsc_probe,
};
MI_DRIVER_REGISTER(_driver);
