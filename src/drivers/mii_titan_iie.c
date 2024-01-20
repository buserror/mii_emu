/*
 * mii_titan_iie.c
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
#include "mii_bank.h"

/*
 * This is a mini driver for the Titan Accelerator IIe, not very common,
 * but it's a nice card, and it's a good example of a driver that needs
 * to use a softswitch ovverride to work.
 * Also, I own one of these, and none of the other fancy ones, so this one
 * gets the love.
 */
static bool
_mii_titan_access(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	mii_t *mii = param;
	bool res = false;
	mii_bank_t *main = &mii->bank[MII_BANK_MAIN];
	if (write) {
		printf("titan: write %02x to %04x\n", *byte, addr);
		switch (*byte) {
			case 5:
				mii->speed = 3.58;
				mii_bank_poke(main, 0xc086, *byte);
			 	break;
			case 1:
				mii_bank_poke(main, 0xc086, *byte);
				mii->speed = 1;
				break;
			case 0xa:	// supposed to lock it too...
				mii_bank_poke(main, 0xc086, *byte);
				mii->speed = 1;
				break;
			default:
				printf("titan: unknown speed %02x\n", *byte);
				break;
		}
	}
	return res;
}

static int
_mii_titan_probe(
		mii_t *mii,
		uint32_t flags)
{
//	printf("%s %s\n", __func__, flags & MII_INIT_TITAN ? "enabled" : "disabled");
	if (!(flags & MII_INIT_TITAN))
		return 0;
	// this override a read-only soft switch, but we only handle writes
	// so it's fine
	mii_set_sw_override(mii, 0xc086, _mii_titan_access, mii);
	mii->speed = 3.58;
	return 1;
}

static mii_slot_drv_t _driver = {
	.name = "titan",
	.desc = "Titan Accelerator IIe",
	.enable_flag = MII_INIT_TITAN,
	.probe = _mii_titan_probe,
};
MI_DRIVER_REGISTER(_driver);
