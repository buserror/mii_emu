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

#include "mii_woz.h"
#include "mii_disk2.h"


enum {
	SIG_DR,			// data register
	SIG_WR,			// write register
	SIG_DRIVE,		// selected drive
	SIG_MOTOR,		// motor on/off
	SIG_READ_DR,
	SIG_LSS_CLK,
	SIG_LSS_SEQ,
	SIG_LSS_CMD,
	SIG_LSS_WRITE,
	SIG_LSS_LOAD,
	SIG_LSS_QA,
	SIG_LSS_RP,		// read pulse
	SIG_LSS_WB,		// write bit
	SIG_LSS_RANDOM,
	SIG_WRITE_CYCLE,
	SIG_COUNT
};
const char *sig_names[] = {
	"D2_DR", "D2_WR", "DRIVE", "MOTOR", "READ_DR",
	"LSS_CLK", "LSS_SEQ", "LSS_CMD", "LSS_WRITE", "LSS_LOAD",
	"LSS_QA", "LSS_RP", "LSS_WB", "LSS_RANDOM", "WRITE_CYCLE",
};


static void
_mii_disk2_lss_tick(
	mii_card_disk2_t *c );

// debug, used for mish, only supports one card tho (yet)
mii_card_disk2_t *_mish_d2 = NULL;

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
//	printf("%s drive %d off\n", __func__, c->selected);
	if (c->drive[c->selected].file && f->seed_dirty != f->seed_saved)
		mii_floppy_update_tracks(f, c->drive[c->selected].file);
	f->motor = 0;
	mii_raise_signal(c->sig + SIG_MOTOR, 0);
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
	if (qtrack >= MII_FLOPPY_TRACK_COUNT * 4)
			qtrack = (MII_FLOPPY_TRACK_COUNT * 4) -1;

	if (qtrack == f->qtrack)
		return f->qtrack;

	uint8_t track_id = f->track_id[f->qtrack];
//	if (track_id != 0xff)
//		printf("NEW TRACK D%d: %d\n", c->selected, track_id);
	uint8_t track_id_new = f->track_id[qtrack];
	if (track_id != track_id_new && track_id != MII_FLOPPY_NOISE_TRACK) {
		if (track_id == 0 && c->vcd)
			_mii_disk2_vcd_debug(c, 0);
		if (f->seed_dirty != f->seed_saved) {
		//	mii_floppy_resync_track(f, track_id, 0);
		}
	}
	if (track_id_new >= MII_FLOPPY_TRACK_COUNT)
		track_id_new = MII_FLOPPY_NOISE_TRACK;
	/* adapt the bit position from one track to the others, from WOZ specs */
	if (track_id_new != MII_FLOPPY_NOISE_TRACK) {
		uint32_t track_size = f->tracks[track_id].bit_count;
		uint32_t new_size = f->tracks[track_id_new].bit_count;
		uint32_t new_pos = f->bit_position * new_size / track_size;
		f->bit_position = new_pos;
	}
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
	c->mii = mii;
	printf("%s loading in slot %d\n", __func__, slot->id + 1);
	uint16_t addr = 0xc100 + (slot->id * 0x100);
	mii_rom_t * rom = mii_rom_get("disk2_p5");
	mii_bank_write(
			&mii->bank[MII_BANK_CARD_ROM],
			addr, rom->rom, 256);

	c->sig = mii_alloc_signal(&mii->sig_pool, 0, SIG_COUNT, sig_names);
	for (int i = 0; i < SIG_COUNT; i++)
		c->sig[i].flags |= SIG_FLAG_FILTERED;
	for (int i = 0; i < 2; i++) {
		mii_dd_t *dd = &c->drive[i];
		dd->slot_id = slot->id + 1;
		dd->drive = i + 1;
		dd->slot = slot;
		asprintf((char **)&dd->name, "Disk ][ S:%d D:%d",
				dd->slot_id, dd->drive);
		mii_floppy_init(&c->floppy[i]);
		c->floppy[i].id = i;
		dd->floppy = &c->floppy[i];
	}
	mii_dd_register_drives(&mii->dd, c->drive, 2);
	char *n;
	asprintf(&n, "Disk ][ S:%d motor off", slot->id + 1);
	c->timer_off 	= mii_timer_register(mii, _mii_floppy_motor_off_cb, c, 0, n);
	asprintf(&n, "Disk ][ S:%d LSS", slot->id + 1);
	c->timer_lss 	= mii_timer_register(mii, _mii_floppy_lss_cb, c, 0, n);
	_mish_d2 = c;
	return 0;
}

static void
_mii_disk2_reset(
		mii_t * mii,
		struct mii_slot_t *slot )
{
	mii_card_disk2_t *c = slot->drv_priv;
//	printf("%s\n", __func__);
	c->selected = 1;
	_mii_floppy_motor_off_cb(mii, c);
	c->selected = 0;
	_mii_floppy_motor_off_cb(mii, c);
	mii_raise_signal(c->sig + SIG_DRIVE, 0);
}

static uint8_t
_mii_disk2_access(
	mii_t * mii, struct mii_slot_t *slot,
	uint16_t addr, uint8_t byte, bool write)
{
	mii_card_disk2_t *c = slot->drv_priv;
	mii_floppy_t * f = &c->floppy[c->selected];
	uint8_t ret = 0;

	int psw = addr & 0x0F;
	int p = psw >> 1, on = psw & 1;
	switch (psw) {
		case 0x00 ... 0x07: {
			static const int8_t delta[4][4] = {
				{0, 1, 2, -1}, {-1, 0, 1, 2}, {-2, -1, 0, 1}, {1, -2, -1, 0},
			};
			if (on) {
				_mii_disk2_switch_track(mii, c, delta[f->stepper][p] * 2);
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
				mii_raise_signal(c->sig + SIG_MOTOR, 1);
			} else {
				int32_t timer = mii_timer_get(mii, c->timer_off);
				mii_timer_set(mii, c->timer_off,
						timer + (1000000 * mii->speed)); // one second
			}
		}	break;
		case 0x0A:
		case 0x0B: {
			if (on != c->selected) {
				c->selected = on;
			//	printf("SELECTED DRIVE: %d\n", c->selected);
				c->floppy[on].motor = f->motor;
				f->motor = 0;
				mii_raise_signal(c->sig + SIG_MOTOR, 0);
			}
		}	break;
		case 0x0C:
		case 0x0D:
			c->lss_mode = (c->lss_mode & ~(1 << Q6_LOAD_BIT)) | (!!on << Q6_LOAD_BIT);
			if (!(c->lss_mode & (1 << Q7_WRITE_BIT)) && f->heat) {
				uint8_t 	track_id = f->track_id[f->qtrack];
				uint32_t 	byte_index 	= f->bit_position >> 3;
				unsigned int dstb = byte_index / MII_FLOPPY_HM_HIT_SIZE;
				f->heat->read.map[track_id][dstb] = 255;
				f->heat->read.seed++;
				mii_raise_signal(c->sig + SIG_READ_DR, c->data_register);
			}
			break;
		case 0x0E:
		case 0x0F:
			c->lss_mode = (c->lss_mode & ~(1 << Q7_WRITE_BIT)) | (!!on << Q7_WRITE_BIT);
			break;
	}
	switch (c->lss_mode & ((1 << Q6_LOAD_BIT) | (1 << Q7_WRITE_BIT))) {
		// off | off | 	Read data register
		case 0:
			ret = c->data_register;
			break;
		// on  | off | 	Read status register
		case (1 << Q6_LOAD_BIT):
			if (write) {
			//	printf("%s: IMW Write something register? %2x\n", __func__,byte);
			}
			ret = c->iwm_mode;
			break;
		// on  | on  | 	Write mode register (if drive is off)
		//				data register       (if drive is on)
		case (1 << Q6_LOAD_BIT) | (1 << Q7_WRITE_BIT):
			if (f->motor) {
				if (write) {
				//	if (!(byte &0x80))
				//		printf("WRITE PC:%04x %4.4x: %2.2x\n",
				//				mii->cpu.PC, addr, byte);
					c->write_register = byte;
					if (c->vcd) {
						mii_raise_signal(c->sig + SIG_WR, byte);
						uint8_t delta = c->vcd->cycle - c->debug_last_write;

						mii_raise_signal_float(
								c->sig + SIG_WRITE_CYCLE, c->debug_last_duration != delta, 0);
						c->debug_last_duration = delta;
						c->debug_last_write = c->vcd->cycle;
					}
				}
				ret = c->data_register;
			} else {
				if (write) {
					c->iwm_mode = byte;
				}
				ret  = c->iwm_mode;
			}
			break;
		// off | on  | Read handshake register
		case (1 << Q7_WRITE_BIT):
		//	printf("%s read handshake register\n", __func__);
			// IWM ready and No Underrun
			ret = (1 << 7) | (1 << 6);
			break;
	}
//	ret = on ? byte : c->data_register;

	return ret;
}

static int
_mii_disk2_command(
		mii_t * mii,
		struct mii_slot_t *slot,
		uint32_t cmd,
		void * param)
{
	mii_card_disk2_t *c = slot->drv_priv;
	int res = -1;
	switch (cmd) {
		case MII_SLOT_DRIVE_COUNT:
			if (param) {
				*(int *)param = 2;
				res = 0;
			}
			break;
		case MII_SLOT_DRIVE_WP ... MII_SLOT_DRIVE_WP + 2 - 1: {
			int drive = cmd - MII_SLOT_DRIVE_WP;
			int *wp = param;
			if (wp) {
				res = 0;
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
			res = 0;
		}	break;
		case MII_SLOT_D2_GET_FLOPPY: {
			if (param) {
				mii_floppy_t ** fp = param;
				fp[0] = &c->floppy[0];
				fp[1] = &c->floppy[1];
				res = 0;
			}
		}	break;
	}
	return res;
}

static mii_slot_drv_t _driver = {
	.name = "disk2",
	.desc = "Apple Disk ][",
	.init = _mii_disk2_init,
	.reset = _mii_disk2_reset,
	.access = _mii_disk2_access,
	.command = _mii_disk2_command,
};
MI_DRIVER_REGISTER(_driver);

void
_mii_disk2_vcd_debug(
	mii_card_disk2_t *c,
	int on)
{
	if (c->vcd) {
		mii_vcd_close(c->vcd);
		c->vcd = NULL;
		printf("VCD OFF\n");
	} else {
		c->vcd = calloc(1, sizeof(*c->vcd));
		// 2Mhz clock
		mii_vcd_init(c->mii, "/tmp/disk2.vcd", c->vcd, 500);
		printf("VCD ON\n");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_DR, 8, "DR");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_WR, 8, "WR");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_DRIVE, 1, "DRIVE");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_MOTOR, 1, "MOTOR");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_READ_DR, 8, "READ");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_CLK, 1, "LSS_CLK");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_SEQ, 4, "LSS_SEQ");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_CMD, 4, "LSS_CMD");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_WRITE, 1, "LSS_W/R");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_LOAD, 1, "LSS_L/S");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_QA, 1, "LSS_QA");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_RP, 1, "LSS_RP");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_WB, 1, "LSS_WB");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_LSS_RANDOM, 1, "LSS_RANDOM");
		mii_vcd_add_signal(c->vcd, c->sig + SIG_WRITE_CYCLE, 1, "WRITE_CYCLE");
		mii_vcd_start(c->vcd);
		// put the LSS state in the VCD to start with
		mii_raise_signal(c->sig + SIG_LSS_QA,
					!!(c->lss_mode & (1 << QA_BIT)));
		mii_raise_signal(c->sig + SIG_LSS_WRITE,
					!!(c->lss_mode & (1 << Q7_WRITE_BIT)));
		mii_raise_signal(c->sig + SIG_LSS_LOAD,
					!!(c->lss_mode & (1 << Q6_LOAD_BIT)));
	}
}

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
	WRITE 	= (1 << Q7_WRITE_BIT),
	LOAD 	= (1 << Q6_LOAD_BIT),
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

static const uint8_t SEQUENCER_ROM_16[256] __attribute__((unused)) = {
    // See Understanding the Apple IIe, Figure 9.11 The DOS 3.3 Logic State Sequencer
    // Note that the column order here is NOT the same as in Figure 9.11 for Q7 H (Write).
    //
    //                Q7 L (Read)                                     Q7 H (Write)
    //    Q6 L (Shift)            Q6 H (Load)             Q6 L (Shift)             Q6 H (Load)
    //  QA L        QA H        QA L        QA H        QA L        QA H        QA L        QA H
    // 1     0     1     0     1     0     1     0     1     0     1     0     1     0     1     0
    0x18, 0x18, 0x18, 0x18, 0x0A, 0x0A, 0x0A, 0x0A, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, // 0
    0x2D, 0x2D, 0x38, 0x38, 0x0A, 0x0A, 0x0A, 0x0A, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, // 1
    0xD8, 0x38, 0x08, 0x28, 0x0A, 0x0A, 0x0A, 0x0A, 0x39, 0x39, 0x39, 0x39, 0x3B, 0x3B, 0x3B, 0x3B, // 2
    0xD8, 0x48, 0x48, 0x48, 0x0A, 0x0A, 0x0A, 0x0A, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, // 3
    0xD8, 0x58, 0xD8, 0x58, 0x0A, 0x0A, 0x0A, 0x0A, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, // 4
    0xD8, 0x68, 0xD8, 0x68, 0x0A, 0x0A, 0x0A, 0x0A, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, // 5
    0xD8, 0x78, 0xD8, 0x78, 0x0A, 0x0A, 0x0A, 0x0A, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, // 6
    0xD8, 0x88, 0xD8, 0x88, 0x0A, 0x0A, 0x0A, 0x0A, 0x08, 0x08, 0x88, 0x88, 0x08, 0x08, 0x88, 0x88, // 7
    0xD8, 0x98, 0xD8, 0x98, 0x0A, 0x0A, 0x0A, 0x0A, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98, // 8
    0xD8, 0x29, 0xD8, 0xA8, 0x0A, 0x0A, 0x0A, 0x0A, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, // 9
    0xCD, 0xBD, 0xD8, 0xB8, 0x0A, 0x0A, 0x0A, 0x0A, 0xB9, 0xB9, 0xB9, 0xB9, 0xBB, 0xBB, 0xBB, 0xBB, // A
    0xD9, 0x59, 0xD8, 0xC8, 0x0A, 0x0A, 0x0A, 0x0A, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, // B
    0xD9, 0xD9, 0xD8, 0xA0, 0x0A, 0x0A, 0x0A, 0x0A, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, // C
    0xD8, 0x08, 0xE8, 0xE8, 0x0A, 0x0A, 0x0A, 0x0A, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, // D
    0xFD, 0xFD, 0xF8, 0xF8, 0x0A, 0x0A, 0x0A, 0x0A, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, // E
    0xDD, 0x4D, 0xE0, 0xE0, 0x0A, 0x0A, 0x0A, 0x0A, 0x88, 0x88, 0x08, 0x08, 0x88, 0x88, 0x08, 0x08  // F
	// 0     1     1     3     4	 5     6     7     8     9     A     B     C     D     E     F
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

	if (c->vcd)
		c->vcd->cycle += 1;
	c->clock += 4;	// 4 is 0.5us.. we run at 2MHz

	uint8_t 	track_id 	= f->track_id[f->qtrack];
	uint8_t * 	track 		= f->track_data[track_id];
	uint32_t 	byte_index 	= f->bit_position >> 3;
	uint8_t 	bit_index 	= 7 - (f->bit_position & 7);
	uint8_t 	rp 			= 0;

	mii_raise_signal(c->sig + SIG_LSS_CLK, c->clock >= f->bit_timing);
	if (c->clock >= f->bit_timing) {
		uint8_t 	bit 	= track[byte_index];
		bit = (bit >> bit_index) & 1;
		c->head = (c->head << 1) | bit;
		// see WOZ spec for how we do this here
		if ((c->head & 0xf) == 0) {
			/* pick a random bit position for the random data */
			if (!f->random) {
				f->random = 1;
				f->random_position = random() % f->tracks[track_id].bit_count;
			}
			bit = f->track_data[MII_FLOPPY_NOISE_TRACK][f->random_position / 8];
			rp = (bit >> (f->random_position % 8)) & 1;
			f->random_position = (f->random_position + 1) % \
						f->tracks[track_id].bit_count;
		//		printf("RANDOM TRACK %2d %2d %2d : %d\n",
		//				track_id, byte_index, bit_index, rp);
			mii_raise_signal_float(c->sig + SIG_LSS_RANDOM, rp, 0);
		} else {
			f->random = 0;
			rp = (c->head >> 1) & 1;
			mii_raise_signal_float(c->sig + SIG_LSS_RANDOM, rp, 1);
		}
	}
	c->lss_mode = (c->lss_mode & ~(1 << RP_BIT)) | (rp << RP_BIT);
	c->lss_mode = (c->lss_mode & ~(1 << QA_BIT)) |
						(!!(c->data_register & 0x80) << QA_BIT);

	mii_raise_signal(c->sig + SIG_LSS_RP, rp);
	mii_raise_signal(c->sig + SIG_LSS_QA,
				!!(c->lss_mode & (1 << QA_BIT)));
	mii_raise_signal(c->sig + SIG_LSS_WRITE,
				!!(c->lss_mode & (1 << Q7_WRITE_BIT)));
	mii_raise_signal(c->sig + SIG_LSS_LOAD,
				!!(c->lss_mode & (1 << Q6_LOAD_BIT)));

	const uint8_t *rom 	= lss_rom16s[c->lss_mode];
	uint8_t cmd 	= rom[c->lss_state];
	uint8_t next 	= cmd >> 4;
	uint8_t action 	= cmd & 0xF;

	mii_raise_signal(c->sig + SIG_LSS_SEQ, c->lss_state);
	mii_raise_signal(c->sig + SIG_LSS_CMD, action);

	if (action & 0b1000) {	// Table 9.3 in Sather's book
		switch (action & 0b0011) {
			case 1:	// SL0/1
				c->data_register <<= 1;
				c->data_register |= !!(action & 0b0100);
				mii_raise_signal(c->sig + SIG_DR, c->data_register);
				break;
			case 2:	// SR
				c->data_register = (c->data_register >> 1) |
									(!!f->write_protected << 7);
				mii_raise_signal(c->sig + SIG_DR, c->data_register);
				break;
			case 3:	{// LD
				uint8_t 	track_id = f->track_id[f->qtrack];
				c->data_register = c->write_register;
				mii_raise_signal(c->sig + SIG_DR, c->data_register);
				f->seed_dirty++;
				if (f->heat && track_id < MII_FLOPPY_TRACK_COUNT) {
					uint32_t 	byte_index 	= f->bit_position >> 3;
					unsigned int dstb = byte_index/MII_FLOPPY_HM_HIT_SIZE;
					f->heat->write.map[track_id][dstb] = 255;
					f->heat->write.seed++;
				}
			}	break;
		}
	} else {	// CLR
		c->data_register = 0;
		mii_raise_signal(c->sig + SIG_DR, c->data_register);
	}
	if ((c->lss_mode & (1 << Q7_WRITE_BIT)) &&
					track_id < MII_FLOPPY_TRACK_COUNT) {
		// on state 0 and 8 we write a bit...
		if ((c->lss_state & 0b0111) == 0) {
			uint8_t bit = c->data_register >> 7;
//			uint8_t bit = !!(c->lss_state & 0x8);
			mii_raise_signal_float(c->sig + SIG_LSS_WB, 1, 0);
			if (!f->tracks[track_id].dirty) {
			//	printf("DIRTY TRACK %2d \n", track_id);
				f->tracks[track_id].dirty = 1;
				/*
				* This little trick allows to have all the track neatly aligned
				* on bit zero when formatting a floppy or doing a copy, this helps
				* debug quite a bit.
				*/
				if (f->tracks[track_id].virgin) {
					f->tracks[track_id].virgin = 0;
					f->bit_position = 0;
				//	if (track_id == 0)
				//		_mii_disk2_vcd_debug(c, 1);
				}
				f->seed_dirty++;
			}
			f->track_data[track_id][byte_index] &= ~(1 << bit_index);
			f->track_data[track_id][byte_index] |= (bit << bit_index);
		} else {
			mii_raise_signal_float(c->sig + SIG_LSS_WB, 0, 0);
		}
	}

	c->lss_state = next;
	if (c->clock >= f->bit_timing) {
		c->clock -= f->bit_timing;
		f->bit_position = (f->bit_position + 1) % f->tracks[track_id].bit_count;
	}
}
