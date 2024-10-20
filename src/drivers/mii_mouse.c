/*
 * mii_mouse.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

// https://github.com/ivanizag/izapple2/blob/master/cardMouse.go
// https://hackaday.io/project/19925-aiie-an-embedded-apple-e-emulator/log/188017-entry-23-here-mousie-mousie-mousie
// https://github.com/ct6502/apple2ts/blob/main/src/emulator/mouse.ts

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mii.h"
#include "mii_bank.h"

/*
 * Coded against this information
 * http://stason.org/TULARC/pc/apple2/programmer/012-How-do-I-write-programs-which-use-the-mouse.html
 */

/*
 * Screen holes
 * $0478 + slot Low byte of absolute X position
 * $04F8 + slot Low byte of absolute Y position
 * $0578 + slot High byte of absolute X position
 * $05F8 + slot High byte of absolute Y position
 * $0678 + slot Reserved and used by the firmware
 * $06F8 + slot Reserved and used by the firmware
 * $0778 + slot Button 0/1 interrupt status byte
 * $07F8 + slot Mode byte
 *
 * Interrupt status byte:
 * Set by READMOUSE
 * Bit 7 6 5 4 3 2 1 0
 *     | | | | | | | |
 *     | | | | | | | `--- Previously, button 1 was up (0) or down (1)
 *     | | | | | | `----- Movement interrupt
 *     | | | | | `------- Button 0/1 interrupt
 *     | | | | `--------- VBL interrupt
 *     | | | `----------- Currently, button 1 is up (0) or down (1)
 *     | | `------------- X/Y moved since last READMOUSE
 *     | `--------------- Previously, button 0 was up (0) or down (1)
 *     `----------------- Currently, button 0 is up (0) or down (1)
 *
 * Mode byte
 * Valid after calling SERVEMOUSE, cleared with READMOUSE
 * Bit 7 6 5 4 3 2 1 0
 *     | | | | | | | |
 *     | | | | | | | `--- Mouse off (0) or on (1)
 *     | | | | | | `----- Interrupt if mouse is moved
 *     | | | | | `------- Interrupt if button is pressed
 *     | | | | `--------- Interrupt on VBL
 *     | | | `----------- Reserved
 *     | | `------------- Reserved
 *     | `--------------- Reserved
 *     `----------------- Reserved
 */

enum {
	MOUSE_STATUS_PREV_BUT1	= (1 << 0),
	MOUSE_STATUS_MOVE_IRQ	= (1 << 1),
	MOUSE_STATUS_BUT_IRQ	= (1 << 2),
	MOUSE_STATUS_VBL_IRQ	= (1 << 3),
	MOUSE_STATUS_BUT1		= (1 << 4),
	MOUSE_STATUS_MOVED		= (1 << 5),
	MOUSE_STATUS_PREV_BUT0	= (1 << 6),
	MOUSE_STATUS_BUT0		= (1 << 7),

	MOUSE_MODE_ON			= (1 << 0),
	MOUSE_MODE_MOVE_IRQ		= (1 << 1),
	MOUSE_MODE_BUT_IRQ		= (1 << 2),
	MOUSE_MODE_VBL_IRQ		= (1 << 3),
};

enum {
	CLAMP_MIN_LO    = 0x478,
	CLAMP_MIN_HI    = 0x578,
	CLAMP_MAX_LO    = 0x4F8,
	CLAMP_MAX_HI    = 0x5F8,

	// RAM Locations
	// (Add $Cn where n is slot to these)
	MOUSE_X_LO      = 0x03B8,
	MOUSE_X_HI      = 0x04B8,
	MOUSE_Y_LO      = 0x0438,
	MOUSE_Y_HI      = 0x0538,
	MOUSE_STATUS    = 0x06B8,
	MOUSE_MODE      = 0x0738,
};

typedef struct mii_card_mouse_t {
	STAILQ_ENTRY(mii_card_mouse_t) self;
	struct mii_slot_t *	slot;
	mii_t *				mii;
	uint8_t 			irq_num;	// MII IRQ line
	uint8_t 			timer_id;	// 60hz timer
	uint8_t 			slot_offset;
	uint8_t				mode; 		// cached mode byte
	uint8_t 			status; 	// cached status byte
	struct {
		uint16_t 			x, y;
		bool 				button;
	}					last;
} mii_card_mouse_t;

STAILQ_HEAD(, mii_card_mouse_t)
		_mii_card_mouse = STAILQ_HEAD_INITIALIZER(_mii_card_mouse);

static uint64_t
_mii_mouse_vbl_handler(
		mii_t * mii,
		void *param)
{
	mii_card_mouse_t *c = param;

	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
//	mii_bank_t * sw = &mii->bank[MII_BANK_SW];
	uint8_t status = c->status;
	uint8_t old = status;

	if (c->mode & MOUSE_MODE_MOVE_IRQ) {
		if ((mii->mouse.x != c->last.x) || (mii->mouse.y != c->last.y)) {
			mii_irq_raise(mii, c->irq_num);
			status |= MOUSE_STATUS_MOVE_IRQ;
		}
	}
	if (c->mode & MOUSE_MODE_BUT_IRQ) {
		if (mii->mouse.button && !c->last.button) {
			mii_irq_raise(mii, c->irq_num);
			status |= MOUSE_STATUS_BUT_IRQ;
		}
	}
	if (c->mode & MOUSE_MODE_VBL_IRQ && !(status & MOUSE_STATUS_VBL_IRQ)) {
		mii_irq_raise(mii, c->irq_num);
		status |= MOUSE_STATUS_VBL_IRQ;
	}
//	if (mii->cpu_state.irq) mii->trace_cpu = true;
	if (status != old) {
		mii_bank_poke(main, MOUSE_STATUS + c->slot_offset, status);
		c->status = status;
	}
	return 1000000 / 60;
}

static int
_mii_mouse_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_mouse_t *c = calloc(1, sizeof(*c));
	c->slot = slot;
	slot->drv_priv = c;
	c->mii = mii;

	printf("%s loading in slot %d\n", __func__, slot->id + 1);

	c->slot_offset = slot->id + 1 + 0xc0;

	c->timer_id = mii_timer_register(mii,
					_mii_mouse_vbl_handler, c,
					1000000 / 60, __func__);
	STAILQ_INSERT_TAIL(&_mii_card_mouse, c, self);
	c->irq_num = mii_irq_register(mii, "mouse");

	/*
	 * The mouse card ROM is a 256 bytes, and has entry points for the
	 * various calls, and a signature at the start.
	 * This re-create it as a bare minimum - the entry points poke at the
	 * softswitches, which in turn calls back the emulator.
	 */
	uint8_t data[256] = {};
	// Identification as a mouse card
	// From Technical Note Misc #8, "Pascal 1.1 Firmware Protocol ID Bytes":
	data[0x05] = 0x38;
	data[0x07] = 0x18;
	data[0x0b] = 0x01;
	data[0x0c] = 0x20;
	// From "AppleMouse // User's Manual", Appendix B:
	//data[0x0c] = 0x20
	data[0xfb] = 0xd6;

	// Set 8 entrypoints to sofstwitches 2 to 1f
	for (int i = 0; i < 14; i++) {
		uint8_t base = 0x30 + 0x05 * i;
		data[0x12+i] = base;
		data[base+0] = 0x8D; // STA $C0x2
		data[base+1] = 0x82 + i + ((slot->id + 1) << 4);
		data[base+2] = 0xC0;
		data[base+3] = 0x18; // CLC ;no error
		data[base+4] = 0x60; // RTS
	}
	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr, data, 256);
	return 0;
}

static void
_mii_mouse_dispose(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_mouse_t *c = slot->drv_priv;

//	mii_timer_unregister(mii, c->timer_id);
	mii_irq_unregister(mii, c->irq_num);
	STAILQ_REMOVE(&_mii_card_mouse, c, mii_card_mouse_t, self);
	free(c);
	slot->drv_priv = NULL;
}

static uint8_t
_mii_mouse_access(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint16_t addr,
		uint8_t byte,
		bool write)
{
	mii_card_mouse_t *c = slot->drv_priv;

	int psw = addr & 0x0F;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];

	switch (psw) {
		case 2: {
			if (write) {
				byte &= 0xf;
				mii_bank_poke(main, MOUSE_MODE + c->slot_offset, byte);
				mii->mouse.enabled = byte & MOUSE_MODE_ON;
				printf("%s: mode %02x: %s Move:%d Button:%d VBL:%d\n", __func__,
						byte, mii->mouse.enabled ? "ON " : "OFF",
						byte & MOUSE_MODE_MOVE_IRQ ? 1 : 0,
						byte & MOUSE_MODE_BUT_IRQ ? 1 : 0,
						byte & MOUSE_MODE_VBL_IRQ ? 1 : 0);
				c->mode = byte;
			}
		}	break;
		case 3: {// service mouse
			// call the timer now, and reset it
		//	mii_timer_set(mii, c->timer_id, 1000000 / 60);
		//	_mii_mouse_vbl_handler(mii, c);
			// now they've been read, clear the flags
			uint8_t status = c->status;
			status &= ~(MOUSE_STATUS_BUT_IRQ|
								MOUSE_STATUS_MOVE_IRQ|MOUSE_STATUS_VBL_IRQ);
			mii_bank_poke(main, MOUSE_STATUS + c->slot_offset, status);
			c->status = status;
			mii_irq_clear(mii, c->irq_num);
		}	break;
		case 4: {// read mouse
			if (!mii->mouse.enabled)
				break;
			mii_bank_poke(main, MOUSE_X_HI + c->slot_offset, mii->mouse.x >> 8);
			mii_bank_poke(main, MOUSE_Y_HI + c->slot_offset, mii->mouse.y >> 8);
			mii_bank_poke(main, MOUSE_X_LO + c->slot_offset, mii->mouse.x);
			mii_bank_poke(main, MOUSE_Y_LO + c->slot_offset, mii->mouse.y);
			// update the status byte. IRQ flags were set by timer
			uint8_t status = c->status;
			status &= ~MOUSE_STATUS_MOVED;
			if ((mii->mouse.x != c->last.x) || (mii->mouse.y != c->last.y))
				status |= MOUSE_STATUS_MOVED;
			status = (status & ~MOUSE_STATUS_PREV_BUT0) |
							(c->last.button ? MOUSE_STATUS_PREV_BUT0 : 0);
			status = (status & ~MOUSE_STATUS_BUT0) |
							(mii->mouse.button ? MOUSE_STATUS_BUT0 : 0);
			mii_bank_poke(main, MOUSE_STATUS + c->slot_offset, status);
			c->status = status;
			c->last.x = mii->mouse.x;
			c->last.y = mii->mouse.y;
			c->last.button = mii->mouse.button;
		}	break;
		case 5: // clear mouse
			break;
		case 7: // set mouse
			if (byte == 0) {
				mii->mouse.min_x = mii_bank_peek(main, CLAMP_MIN_LO) |
									(mii_bank_peek(main, CLAMP_MIN_HI) << 8);
				mii->mouse.max_x = mii_bank_peek(main, CLAMP_MAX_LO) |
									(mii_bank_peek(main, CLAMP_MAX_HI) << 8);
			} else if (byte == 1) {
				mii->mouse.min_y = mii_bank_peek(main, CLAMP_MIN_LO) |
									(mii_bank_peek(main, CLAMP_MIN_HI) << 8);
				mii->mouse.max_y = mii_bank_peek(main, CLAMP_MAX_LO) |
									(mii_bank_peek(main, CLAMP_MAX_HI) << 8);
			}
			printf("Mouse clamp to %d,%d - %d,%d\n",
					mii->mouse.min_x, mii->mouse.min_y,
					mii->mouse.max_x, mii->mouse.max_y);
			break;
		case 8: // home mouse
			mii->mouse.x = mii->mouse.min_x;
			mii->mouse.y = mii->mouse.min_y;
			break;
		case 0xc: // init mouse
			mii->mouse.min_x = mii->mouse.min_y = 0;
			mii->mouse.max_x = mii->mouse.max_y = 1023;
			mii->mouse.enabled = 0;
			mii_bank_poke(main, MOUSE_MODE + c->slot_offset, 0x0);
			break;
		default:
			printf("%s PC:%04x addr %04x %02x wr:%d\n", __func__,
					mii->cpu.PC, addr, byte, write);
			break;
	}
	return 0;
}

static mii_slot_drv_t _driver = {
	.name = "mouse",
	.desc = "Mouse card",
	.init = _mii_mouse_init,
	.dispose = _mii_mouse_dispose,
	.access = _mii_mouse_access,
};
MI_DRIVER_REGISTER(_driver);



#include "mish.h"

static void
_mii_mish_mouse(
		void * param,
		int argc,
		const char * argv[])
{
	mii_t * mii = param;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];

	if (!argv[1] || !strcmp(argv[1], "status")) {
		mii_card_mouse_t *c;
		printf("mouse: cards:\n");
		STAILQ_FOREACH(c, &_mii_card_mouse, self) {
			printf("mouse %d:\n", c->slot->id + 1);

		#define MCM(__n) { .a = __n, .s = (#__n)+6 }
			static const struct {
				uint16_t a;
				const char * s;
			} mouse_regs[] = {
				MCM(MOUSE_X_LO), MCM(MOUSE_X_HI), MCM(MOUSE_Y_LO),
				MCM(MOUSE_Y_HI), MCM(MOUSE_STATUS), MCM(MOUSE_MODE),
			};
			for (int i = 0; i < 6; i++) {
				uint8_t val = mii_bank_peek(main, mouse_regs[i].a + c->slot_offset);
				printf(" $%04x: %-10s: $%02x\n",
				mouse_regs[i].a, mouse_regs[i].s, val);
			}
			uint8_t status = mii_bank_peek(main, MOUSE_STATUS + c->slot_offset);
			// printf individual status bits
			static char * status_bits[] = {
				"PREV_BUT1", "MOVE_IRQ", "BUT_IRQ", "VBL_IRQ",
				"BUT1", "MOVED", "PREV_BUT0", "BUT0",
			};
			printf("  status: ");
			for (int i = 0; i < 8; i++)
				printf("%s ", (status & (1 << i)) ? status_bits[i] : ".");
			printf("\n");
			// printf individual mode bits
			uint8_t mode = mii_bank_peek(main, MOUSE_MODE + c->slot_offset);
			static char * mode_bits[] = {
				"ON", "MOVE_IRQ", "BUT_IRQ", "VBL_IRQ",
			};
			printf("  mode: ");
			for (int i = 0; i < 4; i++)
				printf("%s ", (mode & (1 << i)) ? mode_bits[i] : ".");
		}
		return;
	}
}

MISH_CMD_NAMES(_mouse, "mouse");
MISH_CMD_HELP(_mouse,
		"mouse: Mouse card internals",
		" <default>: dump status"
		);
MII_MISH(_mouse, _mii_mish_mouse);
