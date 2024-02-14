/*
 * mii_disk2.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE // for asprintf
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "mii.h"
#include "mii_bank.h"
#include "mii_disk2.h"
#include "mii_rom_disk2.h"
#include "mii_woz.h"
#include "mii_floppy.h"

enum {
	// bits used to address the LSS ROM using lss_mode
	WRITE_BIT	= 0,
	LOAD_BIT	= 1,
	QA_BIT		= 2,
	RP_BIT		= 3,
};

typedef struct mii_card_disk2_t {
	mii_dd_t 		drive[2];
	mii_floppy_t	floppy[2];
	uint8_t 		selected;

	uint8_t 		timer_off;
	uint8_t 		timer_lss;

	uint8_t 		write_register;
	uint8_t 		head : 4;		// bits are shifted in there
	uint16_t 		clock;			// LSS clock cycles, read a bit when 0
	uint8_t 		lss_state : 4,	// Sequence state
					lss_mode : 4;	// WRITE/LOAD/SHIFT/QA/RP etc
	uint8_t 		lss_prev_state;	// for write bit
	uint8_t 		lss_skip;
	uint8_t 		data_register;
} mii_card_disk2_t;


static void
_mii_disk2_lss_tick(
	mii_card_disk2_t *c );

// debug, used for mish, only supports one card tho (yet)
static mii_card_disk2_t *_mish_d2 = NULL;

/*
 * This timer is used to turn off the motor after a second
 */
static uint64_t
_mii_floppy_motor_off_cb(
		mii_t * mii,
		void * param )
{
	mii_card_disk2_t *c = param;
	mii_floppy_t *f 	= &c->floppy[c->selected];
	printf("%s drive %d off\n", __func__, c->selected);
	if (c->drive[c->selected].file && f->tracks_dirty)
		mii_floppy_update_tracks(f, c->drive[c->selected].file);
	f->motor 		= 0;
	return 0;
}

/*
 * This (tries) to be called every cycle, it never happends in practice,
 * as all the instructions can use multiple cycles by CPU runs.
 * But I can retreive the overshoot cycles and call the LSS as many times
 * as needed to 'catch up'
 */
static uint64_t
_mii_floppy_lss_cb(
		mii_t * mii,
		void * param )
{
	mii_card_disk2_t *c = param;
	mii_floppy_t *f 	= &c->floppy[c->selected];
	if (!f->motor)
		return 0;	// stop timer, motor is now off
	// delta is ALWAYS negative, or zero here
	int32_t delta = mii_timer_get(mii, c->timer_lss);
	uint64_t ret = -delta + 1;
	do {
		_mii_disk2_lss_tick(c);
		_mii_disk2_lss_tick(c);
	} while (delta++ < 0);
	return ret;
}

static uint8_t
_mii_disk2_switch_track(
		mii_t *mii,
		mii_card_disk2_t *c,
		int delta)
{
	mii_floppy_t *f = &c->floppy[c->selected];
	int qtrack = f->qtrack + delta;
	if (qtrack < 0) qtrack = 0;
	if (qtrack >= 35 * 4) qtrack = (35 * 4) -1;

	if (qtrack == f->qtrack)
		return f->qtrack;

	uint8_t track_id = f->track_id[f->qtrack];
	if (track_id != MII_FLOPPY_RANDOM_TRACK_ID)
		printf("NEW TRACK D%d: %d\n", c->selected, track_id);
	uint8_t track_id_new = f->track_id[qtrack];

	/* adapt the bit position from one track to the others, from WOZ specs */
	uint32_t track_size = f->tracks[track_id].bit_count;
	uint32_t new_size = f->tracks[track_id_new].bit_count;
	uint32_t new_pos = f->bit_position * new_size / track_size;
	f->bit_position = new_pos;
	f->qtrack = qtrack;
	return f->qtrack;
}

static int
_mii_disk2_init(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_disk2_t *c = calloc(1, sizeof(*c));
	slot->drv_priv = c;

	printf("%s loading in slot %d\n", __func__, slot->id + 1);
	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr, mii_rom_disk2, 256);

	for (int i = 0; i < 2; i++) {
		mii_dd_t *dd = &c->drive[i];
		dd->slot_id = slot->id + 1;
		dd->drive = i + 1;
		dd->slot = slot;
		asprintf((char **)&dd->name, "Disk ][ S:%d D:%d",
				dd->slot_id, dd->drive);
		mii_floppy_init(&c->floppy[i]);
		c->floppy[i].id = i;
	}
	mii_dd_register_drives(&mii->dd, c->drive, 2);

	c->timer_off 	= mii_timer_register(mii,
							_mii_floppy_motor_off_cb, c, 0,
							"Disk ][ motor off");
	c->timer_lss 	= mii_timer_register(mii,
							_mii_floppy_lss_cb, c, 0,
							"Disk ][ LSS");
	_mish_d2 = c;
	return 0;
}

static uint8_t
_mii_disk2_access(
	mii_t * mii, struct mii_slot_t *slot,
	uint16_t addr, uint8_t byte, bool write)
{
	mii_card_disk2_t *c = slot->drv_priv;
	mii_floppy_t * f = &c->floppy[c->selected];
	uint8_t ret = 0;

	if (write) {
	//	printf("WRITE PC:%04x %4.4x: %2.2x\n", mii->cpu.PC, addr, byte);
		c->write_register = byte;
	}
	int psw = addr & 0x0F;
	int p = psw >> 1, on = psw & 1;
	switch (psw) {
		case 0x00 ... 0x07: {
			if (on) {
				if ((f->stepper + 3) % 4 == p)
					_mii_disk2_switch_track(mii, c, -2);
				else if ((f->stepper + 1) % 4 == p)
					_mii_disk2_switch_track(mii, c, 2);
				f->stepper = p;
			}
		}	break;
		case 0x08:
		case 0x09: {
			// motor on/off
			if (on) {
				mii_timer_set(mii, c->timer_off, 0);
				mii_timer_set(mii, c->timer_lss, 1);
				f->motor = 1;
			} else {
				if (!mii_timer_get(mii, c->timer_off)) {
					mii_timer_set(mii, c->timer_off, 1000000); // one second
				}
			}
		}	break;
		case 0x0A:
		case 0x0B: {
			if (on != c->selected) {
				c->selected = on;
				printf("SELECTED DRIVE: %d\n", c->selected);
				c->floppy[on].motor = f->motor;
				f->motor = 0;
			}
		}	break;
		case 0x0C:
		case 0x0D:
			c->lss_mode = (c->lss_mode & ~(1 << LOAD_BIT)) | (!!on << LOAD_BIT);
			break;
		case 0x0E:
		case 0x0F:
			c->lss_mode = (c->lss_mode & ~(1 << WRITE_BIT)) | (!!on << WRITE_BIT);
			break;
	}
	/*
	 * Here we run one LSS cycle ahead of 'schedule', just because it allows
	 * the write protect bit to be loaded if needed, it *has* to run before
	 * we return this value, so we marked a skip and run it here.
	 */
	_mii_disk2_lss_tick(c);
	c->lss_skip++;
	ret = on ? byte : c->data_register;

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
		case MII_SLOT_DRIVE_WP ... MII_SLOT_DRIVE_WP + 2 - 1: {
			int drive = cmd - MII_SLOT_DRIVE_WP;
			int *wp = param;
			if (wp) {
				printf("Drive %d WP: 0x%x set %s\n", drive,
							c->floppy[drive].write_protected,
							*wp ? "ON" : "OFF");
				c->floppy[drive].write_protected =
						(c->floppy[drive].write_protected &
											~(MII_FLOPPY_WP_MANUAL))|
						(*wp ? MII_FLOPPY_WP_MANUAL : 0);
			}
		}	break;
		case MII_SLOT_DRIVE_LOAD ... MII_SLOT_DRIVE_LOAD + 2 - 1: {
			int drive = cmd - MII_SLOT_DRIVE_LOAD;
			const char *pathname = param;
			mii_dd_file_t *file = NULL;
			if (pathname && *pathname) {
				if (c->drive[drive].file &&
						!strcmp(c->drive[drive].file->pathname, pathname)) {
					printf("%s D%d Same file, not reloading\n",
							__func__, drive);
					return 0;
				}
				file = mii_dd_file_load(&mii->dd, pathname, O_RDWR);
				if (!file)
					return -1;
			}
			// reinit all tracks, bits, maps etc
			mii_floppy_init(&c->floppy[drive]);
			mii_dd_drive_load(&c->drive[drive], file);
			mii_floppy_load(&c->floppy[drive], file);
		}	break;
	}
	return 0;
}

static mii_slot_drv_t _driver = {
	.name = "disk2",
	.desc = "Apple Disk ][",
	.init = _mii_disk2_init,
	.access = _mii_disk2_access,
	.command = _mii_disk2_command,
};
MI_DRIVER_REGISTER(_driver);


/* Sather Infamous Table, pretty much verbatim
									WRITE
	-----------READ PULSE---------		-—------NO READ PULSE--------
	----SHIFT-----	-----LOAD-----		----SHIFT-----	-----LOAD-----
SEQ --QA'-	--QA--	--QA'-	--QA--		 --QA'-	--QA--	--QA'-	--QA--
0-	18-NOP	18-NOP	18-NOP	18-NOP		18-NOP	18-NOP	18-NOP	18-NOP
1-	28-NOP	28-NOP	28-NOP	28-NOP		28-NOP	28-NOP	28-NOP	28-NOP
2-	39-SL0	39-SL0	3B-LD	3B-LD		39-SL0	39-SL0	3B-LD	3B-LD
3-	48-NOP	48-NOP	48-NOP	48-NOP		48-NOP	48-NOP	48-NOP	48-NOP
4-	58-NOP	58-NOP	58-NOP	58-NOP		58-NOP	58-NOP	58-NOP	58-NOP
5-	68-NOP	68-NOP	68-NOP	68-NOP		68-NOP	68-NOP	68-NOP	68-NOP
6-	78-NOP	78-NOP	78-NOP	78-NOP		78-NOP	78-NOP	78-NOP	78-NOP
7-	08-NOP	88-NOP	08-NOP	88-NOP		08-NOP	88-NOP	08-NOP	88-NOP
8-	98-NOP	98-NOP	98-NOP	98-NOP		98-NOP	98-NOP	98-NOP	98-NOP
9-	A8-NOP	A8-NOP	A8-NOP	A8-NOP		A8-NOP	A8-NOP	A8-NOP	A8-NOP
A-	B9-SL0	B9-SL0	BB-LD	BB-LD		B9-SL0	B9-SL0	BB-LD	BB-LD
B-	C8-NOP	C8-NOP	C8-NOP	C8-NOP		C8-NOP	C8-NOP	C8-NOP	C8-NOP
C-	D8-NOP	D8-NOP	D8-NOP	D8-NOP		D8-NOP	D8-NOP	D8-NOP	D8-NOP
D-	E8-NOP	E8-NOP	E8-NOP	E8-NOP		E8-NOP	E8-NOP	E8-NOP	E8-NOP
E-	F8-NOP	F8-NOP	F8-NOP	F8-NOP		F8-NOP	F8-NOP	F8-NOP	F8-NOP
F-	88-NOP	08-NOP	88-NOP	08-NOP		88-NOP	08-NOP	88-NOP	08-NOP
									READ
	------------SHIFT------------		-------------LOAD------------
	-----QA'------	------QA------		-----QA'------	-----QA------
	--RP--	-NORP-	--RP--	-NORP-		--RP--	-NORP-	--RP--	-NORP-
0-	18-NOP	18-NOP	18-NOP	18-NOP		0A-SR	0A-SR	0A-SR	0A-SR
1-	2D-SL1	2D-SL1	38-NOP	38-NOP		0A-SR	0A-SR	0A-SR	0A-SR
2-	D8-NOP	38-NOP	08-NOP	28-NOP		0A-SR	0A-SR	0A-SR	0A-SR
3-	D8-NOP	48-NOP	48-NOP	48-NOP		0A-SR	0A-SR	0A-SR	0A-SR
4-	D8-NOP	58-NOP	D8-NOP	58-NOP		0A-SR	0A-SR	0A-SR	0A-SR
5-	D8-NOP	68-NOP	D8-NOP	68-NOP		0A-SR	0A-SR	0A-SR	0A-SR
6-	D8-NOP	78-NOP	D8-NOP	78-NOP		0A-SR	0A-SR	0A-SR	0A-SR
7-	D8-NOP	88-NOP	D8-NOP	88-NOP		0A-SR	0A-SR	0A-SR	0A-SR
8-	D8-NOP	98-NOP	D8-NOP	98-NOP		0A-SR	0A-SR	0A-SR	0A-SR
9-	D8-NOP	29-SL0	D8-NOP	A8-NOP		0A-SR	0A-SR	0A-SR	0A-SR
A-	CD-SL1	BD-SL1	D8-NOP	B8-NOP		0A-SR	0A-SR	0A-SR	0A-SR
B-	D9-SL0	59-SL0	D8-NOP	C8-NOP		0A-SR	0A-SR	0A-SR	0A-SR
C-	D9-SL0	D9-SL0	D8-NOP	A0-CLR		0A-SR	0A-SR	0A-SR	0A-SR
D-	D8-NOP	08-NOP	E8-NOP	E8-NOP		0A-SR	0A-SR	0A-SR	0A-SR
E-	FD-SL1	FD-SL1	F8-NOP	F8-NOP		0A-SR	0A-SR	0A-SR	0A-SR
F-	DD-SL1	4D-SL1	E0-CLR	E0-CLR		0A-SR	0A-SR	0A-SR	0A-SR
*/
enum {
	WRITE 	= (1 << WRITE_BIT),
	LOAD 	= (1 << LOAD_BIT),
	QA1 	= (1 << QA_BIT),
	RP1 	= (1 << RP_BIT),
	/* This keeps the transposed table more readable, as it looks like the book */
	READ = 0, SHIFT = 0, QA0 = 0, RP0 = 0,
};
// And this is the same Sather table (Figure 9.11, page 9-20) but transposed
static const uint8_t lss_rom16s[16][16] = {
[WRITE|RP0|SHIFT|QA0]={	0x18,0x28,0x39,0x48,0x58,0x68,0x78,0x08,0x98,0xA8,0xB9,0xC8,0xD8,0xE8,0xF8,0x88 },
[WRITE|RP0|SHIFT|QA1]={	0x18,0x28,0x39,0x48,0x58,0x68,0x78,0x88,0x98,0xA8,0xB9,0xC8,0xD8,0xE8,0xF8,0x08 },
[WRITE|RP0|LOAD|QA0]={	0x18,0x28,0x3B,0x48,0x58,0x68,0x78,0x08,0x98,0xA8,0xBB,0xC8,0xD8,0xE8,0xF8,0x88 },
[WRITE|RP0|LOAD|QA1]={	0x18,0x28,0x3B,0x48,0x58,0x68,0x78,0x88,0x98,0xA8,0xBB,0xC8,0xD8,0xE8,0xF8,0x08 },
[WRITE|RP1|SHIFT|QA0]={	0x18,0x28,0x39,0x48,0x58,0x68,0x78,0x08,0x98,0xA8,0xB9,0xC8,0xD8,0xE8,0xF8,0x88 },
[WRITE|RP1|SHIFT|QA1]={	0x18,0x28,0x39,0x48,0x58,0x68,0x78,0x88,0x98,0xA8,0xB9,0xC8,0xD8,0xE8,0xF8,0x08 },
[WRITE|RP1|LOAD|QA0]={	0x18,0x28,0x3B,0x48,0x58,0x68,0x78,0x08,0x98,0xA8,0xBB,0xC8,0xD8,0xE8,0xF8,0x88 },
[WRITE|RP1|LOAD|QA1]={	0x18,0x28,0x3B,0x48,0x58,0x68,0x78,0x88,0x98,0xA8,0xBB,0xC8,0xD8,0xE8,0xF8,0x08 },
[READ|SHIFT|QA0|RP1]={	0x18,0x2D,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xCD,0xD9,0xD9,0xD8,0xFD,0xDD },
[READ|SHIFT|QA0|RP0]={	0x18,0x2D,0x38,0x48,0x58,0x68,0x78,0x88,0x98,0x29,0xBD,0x59,0xD9,0x08,0xFD,0x4D },
[READ|SHIFT|QA1|RP1]={	0x18,0x38,0x08,0x48,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xD8,0xE8,0xF8,0xE0 },
[READ|SHIFT|QA1|RP0]={	0x18,0x38,0x28,0x48,0x58,0x68,0x78,0x88,0x98,0xA8,0xB8,0xC8,0xA0,0xE8,0xF8,0xE0 },
[READ|LOAD|QA0|RP1]={	0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A },
[READ|LOAD|QA0|RP0]={	0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A },
[READ|LOAD|QA1|RP1]={	0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A },
[READ|LOAD|QA1|RP0]={	0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A,0x0A },
};

static void
_mii_disk2_lss_tick(
	mii_card_disk2_t *c )
{
	if (c->lss_skip) {
		c->lss_skip--;
		return;
	}
	mii_floppy_t *f = &c->floppy[c->selected];

	c->lss_mode = (c->lss_mode & ~(1 << QA_BIT)) |
					(!!(c->data_register & 0x80) << QA_BIT);
	c->clock += 4;	// 4 is 0.5us.. we run at 2MHz
	if (c->clock >= f->bit_timing) {
		c->clock -= f->bit_timing;
		uint8_t 	track_id = f->track_id[f->qtrack];

		uint32_t 	byte_index 	= f->bit_position >> 3;
		uint8_t 	bit_index 	= 7 - (f->bit_position & 7);
		if (!(c->lss_mode & (1 << WRITE_BIT))) {
			uint8_t 	bit 	= f->tracks[track_id].data[byte_index];
			bit = (bit >> bit_index) & 1;
			c->head = (c->head << 1) | bit;
			// see WOZ spec for how we do this here
			if ((c->head & 0xf) == 0) {
				bit = f->tracks[MII_FLOPPY_RANDOM_TRACK_ID].data[byte_index];
				bit = (bit >> bit_index) & 1;
//				printf("RANDOM TRACK %2d %2d %2d : %d\n",
//						track_id, byte_index, bit_index, bit);
			} else {
				bit = (c->head >> 1) & 1;
			}
			c->lss_mode = (c->lss_mode & ~(1 << RP_BIT)) | (bit << RP_BIT);
		}
		if ((c->lss_mode & (1 << WRITE_BIT))) {
			uint8_t msb = c->data_register >> 7;

			if (!f->tracks[track_id].dirty) {
				printf("DIRTY TRACK %2d \n", track_id);
				f->tracks[track_id].dirty = 1;
				f->tracks_dirty = 1;
			}
			f->tracks[track_id].data[byte_index] &= ~(1 << bit_index);
			f->tracks[track_id].data[byte_index] |= (msb << bit_index);
		}
		f->bit_position = (f->bit_position + 1) % f->tracks[track_id].bit_count;
	}
	const uint8_t *rom 	= lss_rom16s[c->lss_mode];
	uint8_t cmd 	= rom[c->lss_state];
	uint8_t next 	= cmd >> 4;
	uint8_t action 	= cmd & 0xF;

	if (action & 0b1000) {	// Table 9.3 in Sather's book
		switch (action & 0b0011) {
			case 1:	// SL0/1
				c->data_register <<= 1;
				c->data_register |= !!(action & 0b0100);
				break;
			case 2:	// SR
				c->data_register = (c->data_register >> 1) |
									(!!f->write_protected << 7);
				break;
			case 3:	// LD
				c->data_register = c->write_register;
				break;
		}
	} else {	// CLR
		c->data_register = 0;
	}
	c->lss_state = next;
	// read pulse only last one cycle..
	c->lss_mode &= ~(1 << RP_BIT);
}


static void
_mii_mish_d2(
		void * param,
		int argc,
		const char * argv[])
{
//	mii_t * mii = param;
	if (!_mish_d2) {
		printf("No Disk ][ card installed\n");
		return;
	}
	static int sel = 0;
	if (!argv[1] || !strcmp(argv[1], "list")) {
		mii_card_disk2_t *c = _mish_d2;
		for (int i = 0; i < 2; i++) {
			mii_floppy_t *f = &c->floppy[i];
			printf("Drive %d %s\n", f->id, f->write_protected ? "WP" : "RW");
			printf("\tMotor: %3s qtrack:%d Bit %6d\n",
					f->motor ? "ON" : "OFF", f->qtrack, f->bit_position);
		}
		return;
	}
	if (!strcmp(argv[1], "sel")) {
		if (argv[2]) {
			sel = atoi(argv[2]);
		}
		printf("Selected drive: %d\n", sel);
		return;
	}
	if (!strcmp(argv[1], "wp")) {
		if (argv[2]) {
			int wp = atoi(argv[2]);
			mii_card_disk2_t *c = _mish_d2;
			mii_floppy_t *f = &c->floppy[sel];
			f->write_protected = wp;
		}
		printf("Drive %d Write protected: %d\n", sel,
				_mish_d2->floppy[sel].write_protected);
		return;
	}
	// dump a track, specify track number and number of bytes
	if (!strcmp(argv[1], "track")) {
		if (argv[2]) {
			int track = atoi(argv[2]);
			int count = 256;
			if (argv[3])
				count = atoi(argv[3]);
			mii_card_disk2_t *c = _mish_d2;
			mii_floppy_t *f = &c->floppy[sel];
			uint8_t *data = f->tracks[track].data;

			for (int i = 0; i < count; i += 8) {
				uint8_t *line = data + i;
			#if 0
				for (int bi = 0; bi < 8; bi++) {
					uint8_t b = line[bi];
					for (int bbi = 0; bbi < 8; bbi++) {
						printf("%c", (b & 0x80) ? '1' : '0');
						b <<= 1;
					}
				}
				printf("\n");
			#endif
				for (int bi = 0; bi < 8; bi++)
					printf("%8x", line[bi]);
				printf("\n");
			}
		} else {
			printf("track <track 0-36> [count]\n");
		}
		return;
	}
}

#include "mish.h"

MISH_CMD_NAMES(d2, "d2");
MISH_CMD_HELP(d2,
		"d2: disk ][ internals",
		" <default>: dump status"
		);
MII_MISH(d2, _mii_mish_d2);
