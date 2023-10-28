/*
 * mii_analog.c
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
#include "mii_analog.h"

void
mii_analog_init(
		struct mii_t *mii,
		mii_analog_t * a )
{
	memset(a, 0, sizeof(*a));
}

/*
 * https://retrocomputing.stackexchange.com/questions/15093/how-do-i-read-the-position-of-an-apple-ii-joystick
 */
void
mii_analog_access(
		mii_t *mii,
		mii_analog_t * a,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (write)
		return;
	switch (addr) {
		case 0xc070: {
			// multiplying by mii->speed allows reading joystick in 'fast' mode,
			// this basically simulate slowing down just for the joystick reading

			/* TODO: According to various artivles, the multiplier ought
			 * to be 11, but we're not making the count here, which means it's
			 * likely the emulated core is missing a cycle for one instruction
			 * somewhere... */
			for (int i = 0; i < 4; i++) {
				a->v[i].decay = mii->cycles +
									((a->v[i].value * 10.10) * mii->speed);
			//	printf("joystick %d: %d\n", i, a->v[i].value);
			}
		}	break;
		case 0xc064 ... 0xc067: {
			addr -= 0xc064;
			*byte = mii->cycles <= a->v[addr].decay ? 0x80 : 0x00;
		}	break;
	}
}

