/*
 * mii.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii_rom_iiee.h"
#include "mii.h"
#include "mii_bank.h"
#include "mii_video.h"
#include "mii_sw.h"
#include "mii_65c02.h"
#include "minipt.h"


mii_slot_drv_t * mii_slot_drv_list = NULL;

static const mii_bank_t	_mii_banks_init[MII_BANK_COUNT] = {
	[MII_BANK_MAIN] = {
		.name = "MAIN",
		.base = 0x0000,
		.size = 0xd0, // 208 pages, 48KB
	},
	[MII_BANK_BSR] = {
		.name = "BSR",
		.base = 0xd000,
		.size = 64,
	},
	[MII_BANK_BSR_P2] = {
		.name = "BSR P2",
		.base = 0xd000,
		.size = 16,
	},
	[MII_BANK_AUX] = {
		.name = "AUX",
		.base = 0x0000,
		.size = 0xd0, // 208 pages, 48KB
	},
	[MII_BANK_AUX_BSR] = {
		.name = "AUX BSR",
		.base = 0xd000,
		.size = 64,
	},
	[MII_BANK_AUX_BSR_P2] = {
		.name = "AUX BSR P2",
		.base = 0xd000,
		.size = 16,
	},
	[MII_BANK_ROM] = {
		.name = "ROM",
		.base = 0xc000,
		.size = 0x40, // 64 pages, 16KB
		.ro = 1,
	},
	[MII_BANK_CARD_ROM] = {
		.name = "CARD ROM",
		.base = 0xc100,
		.size = 15,
		.ro = 1,
	},
};


#include "mii_65c02_ops.h"
#include "mii_65c02_disasm.h"

void
mii_dump_trace_state(
	mii_t *mii)
{
	mii_cpu_t * cpu = &mii->cpu;
	mii_cpu_state_t s = mii->cpu_state;
	printf("PC:%04X A:%02X X:%02X Y:%02X S:%02x #%d %c AD:%04X D:%02x %c ",
		cpu->PC, cpu->A, cpu->X, cpu->Y, cpu->S, cpu->cycle,
		s.sync ? 'I' : ' ', s.addr, s.data, s.w ? 'W' : 'R');
	// display the S flags
	static const char *s_flags = "CZIDBRVN";
	for (int i = 0; i < 8; i++)
		printf("%c", MII_GET_P_BIT(cpu, i) ? s_flags[i] : tolower(s_flags[i]));
	if (s.sync) {
		uint8_t op[16];
		for (int i = 0; i < 4; i++) {
			mii_mem_access(mii, mii->cpu.PC + i, op + i, false, false);
		}
		mii_op_t d = mii_cpu_op[op[0]];
		printf(" ");
		char dis[32];
		mii_cpu_disasm_one(op, cpu->PC, dis, sizeof(dis),
					MII_DUMP_DIS_DUMP_HEX);
		printf(": %s", dis);
		if (d.desc.branch) {
			if (MII_GET_P_BIT(cpu, d.desc.s_bit) == d.desc.s_bit_value)
				printf(" ; taken");
		}
		printf("\n");
	} else
		printf("\n");
}

void
mii_dump_run_trace(
	mii_t *mii)
{
	// walk all the previous PC values in mii->trace, and display a line
	// of disassembly for all of them
	for (int li = 0; li < MII_PC_LOG_SIZE; li++) {
		int idx = (mii->trace.idx + li) & (MII_PC_LOG_SIZE - 1);
		uint16_t pc = mii->trace.log[idx];
		uint8_t op[16];
		for (int i = 0; i < 4; i++)
			mii_mem_access(mii, pc + i, op + i, false, false);
//		mii_op_t d = mii_cpu_op[op[0]];
		char dis[64];
		mii_cpu_disasm_one(op, pc, dis, sizeof(dis),
						MII_DUMP_DIS_PC | MII_DUMP_DIS_DUMP_HEX);
		printf("%s\n", dis);
	}
}

#define _SAME 0xf

static inline void
mii_page_set(
		mii_t * mii,
		uint8_t read,
		uint8_t write,
		uint8_t bank,
		uint8_t end )
{
	for (int i = bank; i <= end; i++) {
		if (read != _SAME)
			mii->mem[i].read = read;
		if (write != _SAME)
			mii->mem[i].write = write;
	}
}

static inline uint8_t
mii_sw(
		mii_t *mii,
		uint16_t sw)
{
	return mii_bank_peek(&mii->bank[MII_BANK_MAIN], sw);
}

static void
mii_page_table_update(
		mii_t *mii)
{
	if (likely(!mii->mem_dirty))
		return;
	mii->mem_dirty = 0;
	bool altzp 		= SW_GETSTATE(mii, SWALTPZ);
	bool page2 		= SW_GETSTATE(mii, SWPAGE2);
	bool store80 	= SW_GETSTATE(mii, SW80STORE);
	bool hires 		= SW_GETSTATE(mii, SWHIRES);
	bool ramrd 		= SW_GETSTATE(mii, SWRAMRD);
	bool ramwrt 	= SW_GETSTATE(mii, SWRAMWRT);
	bool intcxrom 	= SW_GETSTATE(mii, SWINTCXROM);
	bool slotc3rom 	= SW_GETSTATE(mii, SWSLOTC3ROM);
	bool slotauxrom	= SW_GETSTATE(mii, SLOTAUXROM);

	if (unlikely(mii->trace_cpu))
		printf("%04x: MEM update altzp:%d page2:%d store80:%d "
				"hires:%d ramrd:%d ramwrt:%d intcxrom:%d "
				"slotc3rom:%d\n", mii->cpu.PC,
			altzp, page2, store80, hires, ramrd, ramwrt, intcxrom, slotc3rom);
	// clean slate
	mii_page_set(mii, MII_BANK_MAIN, MII_BANK_MAIN, 0x00, 0xc0);
	mii_page_set(mii, MII_BANK_ROM, MII_BANK_ROM, 0xc1, 0xff);
	if (altzp)
		mii_page_set(mii, MII_BANK_AUX, MII_BANK_AUX, 0x00, 0x01);
	mii_page_set(mii,
		ramrd ? MII_BANK_AUX : MII_BANK_MAIN,
		ramwrt ? MII_BANK_AUX : MII_BANK_MAIN, 0x02, 0xbf);
	if (store80) {
		mii_page_set(mii,
			page2 ? MII_BANK_AUX : MII_BANK_MAIN,
			page2 ? MII_BANK_AUX : MII_BANK_MAIN, 0x04, 0x07);
		if (hires)
			mii_page_set(mii,
				page2 ? MII_BANK_AUX : MII_BANK_MAIN,
				page2 ? MII_BANK_AUX : MII_BANK_MAIN, 0x20, 0x3f);
	}
	if (!intcxrom) {
		mii_page_set(mii, MII_BANK_CARD_ROM, _SAME, 0xc1, 0xc7);
		if (slotauxrom)
			mii_page_set(mii, MII_BANK_CARD_ROM, _SAME, 0xc8, 0xcf);
	}
	mii_page_set(mii,
		slotc3rom ? MII_BANK_CARD_ROM : MII_BANK_ROM, _SAME, 0xc3, 0xc3);
	bool bsrread 	= SW_GETSTATE(mii, BSRREAD);
	bool bsrwrite 	= SW_GETSTATE(mii, BSRWRITE);
	bool bsrpage2 	= SW_GETSTATE(mii, BSRPAGE2);
	mii_page_set(mii,
		bsrread ?
			altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR :
					MII_BANK_ROM,
		bsrwrite ?
			altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR :
					MII_BANK_ROM,
				0xd0, 0xff);
	// BSR P2
	mii_page_set(mii,
		bsrread ?
			(altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR) + bsrpage2 :
					MII_BANK_ROM,
		bsrwrite ?
			(altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR) + bsrpage2 :
					MII_BANK_ROM,
				0xd0, 0xdf);
}

void
mii_set_sw_override(
		mii_t *mii,
		uint16_t sw_addr,
		mii_bank_access_cb cb,
		void *param)
{
	if (!mii->soft_switches_override)
		mii->soft_switches_override = calloc(256,
				sizeof(*mii->soft_switches_override));
	sw_addr &= 0xff;
	mii->soft_switches_override[sw_addr].cb = cb;
	mii->soft_switches_override[sw_addr].param = param;
}

/*
 * This watches for any write to 0xcfff -- if a card had it's aux rom
 * selected, it will deselect it.
 */
static bool
_mii_deselect_auxrom(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (addr != 0xcfff)
		return false;
	mii_t * mii = param;
//	printf("%s AUXROM:%d\n", __func__, !!(mii->sw_state & M_SLOTAUXROM));
	if (!(mii->sw_state & M_SLOTAUXROM))
		return false;
	for (int i = 0; i < 7; i++) {
		mii_slot_t * slot = &mii->slot[i];
		if (slot->aux_rom_selected) {
			printf("%s %d: %s\n", __func__,
					i, slot->drv ? slot->drv->name : "(none?)");
			slot->aux_rom_selected = false;
		}
	}
	mii->sw_state &= ~M_SLOTAUXROM;
	mii->mem_dirty = true;
	return false;
}

static bool
_mii_select_c3rom(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	mii_t * mii = param;
	printf("%s\n", __func__);
	if (mii->sw_state & M_SLOTAUXROM) {
	//	printf("%s: C3 aux rom re-selected\n", __func__);
		mii->sw_state &= ~M_SLOTAUXROM;
	}
	mii->mem_dirty = true;
	return false;
}

static bool
mii_access_soft_switches(
		mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (!(addr >= 0xc000 && addr <= 0xc0ff))
		return false;
	bool res = false;
	uint8_t on = 0;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];

	/*
	 * This allows driver (titan accelerator etc) to have their own
	 * soft switches, and override/supplement any default ones.
	 */
	if (mii->soft_switches_override && mii->soft_switches_override[addr & 0xff].cb) {
		res = mii->soft_switches_override[addr & 0xff].cb(
					main, mii->soft_switches_override[addr & 0xff].param,
					addr, byte, write);
		if (res)
			return res;
	}
	switch (addr) {
		case 0xc090 ... 0xc0ff: {
			res = true;
			int slot = ((addr >> 4) & 7) - 1;
#if 0
			printf("SLOT %d addr %04x write %d %02x drv %s\n",
				slot, addr, write, *byte,
				mii->slot[slot].drv ? mii->slot[slot].drv->name : "none");
#endif
			if (mii->slot[slot].drv) {
				on = mii->slot[slot].drv->access(mii,
							&mii->slot[slot], addr, *byte, write);
				if (!write)
					*byte = on;
			}
		}	break;
/*
 SATHER-SATHER-SATHER-SATHER-SATHER-SATHER-SATHER-SATHER-SATHER-SATHER

 Writing to high RAM is enabled when the HRAMWRT' soft switch is reset.
 The controlling MPU program must set the PRE-WRITE soft switch before
 it can reset HRAMWRT'. PRE-WRITE is set in the by odd read access ub the
 C08X range. It is reset by even read access or any write access in the
 $C08X range.
 HRAMWRT' is reset by odd read access in the $C08X range when PRE-WRITE is
 set. It is set by even acce.ss in the $C08X range. Any other type of access
 causes HRAMWRT' to hold its current state.
*/
		case 0xc080 ... 0xc08f: {
			res = true;
			uint8_t mode = addr & 0x0f;
			static const int write_modes[4] = { 0, 1, 0, 1, };
			static const int read_modes[4] = { 1, 0, 0, 1, };
			uint8_t rd = read_modes[mode & 3];
			uint8_t wr = write_modes[mode & 3];

			if (write) {
				SW_SETSTATE(mii, BSRPREWRITE, 0);
			} else {
				SW_SETSTATE(mii, BSRPREWRITE, mode & 1);
			}
		//	if (SW_GETSTATE(mii, BSRPREWRITE))
		//		;
			SW_SETSTATE(mii, BSRWRITE, wr);
			SW_SETSTATE(mii, BSRREAD, rd);
			SW_SETSTATE(mii, BSRPAGE2, !(mode & 0x08));	// A3
			mii->mem_dirty = 1;
//			mii->trace_cpu = 1;
//			mii->state = MII_STOPPED;
			if (unlikely(mii->trace_cpu))
				printf("%04x: BSR mode %c%04x pre:%d read:%s write:%s %s altzp:%02x\n",
					mii->cpu.PC, write ? 'W' : 'R',
					addr,
					SW_GETSTATE(mii, BSRPREWRITE),
					rd ? "BSR" : "ROM",
					wr ? "BSR" : "ROM",
					SW_GETSTATE(mii, BSRPAGE2) ? "page2" : "page1",
					mii_sw(mii, SWALTPZ));
		}	break;
		case SWPAGE2OFF:
		case SWPAGE2ON:
			res = true;
			SW_SETSTATE(mii, SWPAGE2, addr & 1);
			mii_bank_poke(main, SWPAGE2, (addr & 1) << 7);
			mii->mem_dirty = 1;
			break;
		case SWHIRESOFF:
		case SWHIRESON:
			res = true;
			SW_SETSTATE(mii, SWHIRES, addr & 1);
			mii_bank_poke(main, SWHIRES, (addr & 1) << 7);
			mii->mem_dirty = 1;
		//	printf("HIRES %s\n", (addr & 1) ? "ON" : "OFF");
			break;
		case SWSPEAKER:
			res = true;
			mii_speaker_click(&mii->speaker);
			break;
		case 0xc064 ... 0xc067: // Joystick, buttons
		case 0xc070: // Analog reset
			res = true;
			mii_analog_access(mii, &mii->analog, addr, byte, write);
			break;
		case 0xc068:
			res = true;
			// IIgs register, read by prodos tho
			break;
	}
	if (res && !mii->mem_dirty)
		return res;
	if (write) {
		switch (addr) {
			case SW80STOREOFF:
			case SW80STOREON:
				res = true;
				SW_SETSTATE(mii, SW80STORE, addr & 1);
				mii_bank_poke(main, SW80STORE, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWRAMRDOFF:
			case SWRAMRDON:
				res = true;
				SW_SETSTATE(mii, SWRAMRD, addr & 1);
				mii_bank_poke(main, SWRAMRD, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWRAMWRTOFF:
			case SWRAMWRTON:
				res = true;
				SW_SETSTATE(mii, SWRAMWRT, addr & 1);
				mii_bank_poke(main, SWRAMWRT, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWALTPZOFF:
			case SWALTPZON:
				res = true;
				SW_SETSTATE(mii, SWALTPZ, addr & 1);
				mii_bank_poke(main, SWALTPZ, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWINTCXROMOFF:
			case SWINTCXROMON:
				res = true;
				SW_SETSTATE(mii, SWINTCXROM, addr & 1);
				mii_bank_poke(main, SWINTCXROM, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWSLOTC3ROMOFF:
			case SWSLOTC3ROMON:
				res = true;
				SW_SETSTATE(mii, SWSLOTC3ROM, addr & 1);
				mii_bank_poke(main, SWSLOTC3ROM, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
		}
	} else {
		switch (addr) {
			case SWBSRBANK2:
				*byte = SW_GETSTATE(mii, BSRPAGE2) ? 0x80 : 0;
				res = true;
				break;
			case SWBSRREADRAM:
				*byte = SW_GETSTATE(mii, BSRREAD) ? 0x80 : 0;
				res = true;
				break;
			case SWRAMRD:
			case SWRAMWRT:
			case SW80STORE:
			case SWINTCXROM:
			case SWALTPZ:
			case SWSLOTC3ROM:
				res = true;
				*byte = mii_bank_peek(main, addr);
				break;
			case 0xc020: // toggle TAPE output ?!?!
				res = true;
				break;
			case 0xc068:
				res = true;
				// IIgs register, read by prodos tho
				break;
			default:
				res = true;
				/*
				 * this is moderately important, return some random value
				 * as it is supposed to represent what's on the bus at the time,
				 * typically video being decoded etc.
				 */
				*byte = mii->random[mii->random_index++];
				mii->random_index &= 0xff;
				break;
		}
	}
	if (!res) {
	//	printf("%s addr %04x write %d %02x\n", __func__, addr, write, *byte);
	//	mii->state = MII_STOPPED;
	}
	mii_page_table_update(mii);
	return res;
}

static bool
mii_access_keyboard(
		mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	bool res = false;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	switch (addr) {
		case SWKBD:
			if (!write) {
				res = true;
				*byte = mii_bank_peek(main, SWKBD);
			}
			break;
		case SWAKD: {
			res = true;
			uint8_t r = mii_bank_peek(main, SWAKD);
			if (!write)
				*byte = r;
			r &= 0x7f;
			mii_bank_poke(main, SWAKD, r);
			mii_bank_poke(main, SWKBD, r);
		}	break;
		case 0xc061 ... 0xc063: // Push Button 0, 1, 2 (Apple Keys)
			res = true;
			if (!write)
				*byte = mii_bank_peek(main, addr);
			break;
	}
	return res;
}

void
mii_keypress(
		mii_t *mii,
		uint8_t key)
{
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	key |= 0x80;
	mii_bank_poke(main, SWAKD, key);
	mii_bank_poke(main, SWKBD, key);
}


void
mii_init(
		mii_t *mii )
{
	memset(mii, 0, sizeof(*mii));
	mii->speed = 1.023;
	mii->timer.map = 0;
	for (int i = 0; i < MII_BANK_COUNT; i++)
		mii->bank[i] = _mii_banks_init[i];
	mii->bank[MII_BANK_ROM].mem = (uint8_t*)&iie_enhanced_rom_bin[0];
	for (int i = 0; i < MII_BANK_COUNT; i++)
		mii_bank_init(&mii->bank[i]);
	mii->cpu.trap = MII_TRAP;
	// these are called once, regardless of reset
	mii_dd_system_init(mii, &mii->dd);
	mii_analog_init(mii, &mii->analog);
	mii_video_init(mii);
	mii_speaker_init(mii, &mii->speaker);

	mii_reset(mii, true);
	mii->cpu_state = mii_cpu_init(&mii->cpu);
	for (int i = 0; i < 7; i++)
		mii->slot[i].id = i;

//	srandom(time(NULL));
	for (int i = 0; i < 256; i++)
		mii->random[i] = random();

	mii_bank_install_access_cb(&mii->bank[MII_BANK_CARD_ROM],
			_mii_deselect_auxrom, mii, 0xcf, 0xcf);
	mii_bank_install_access_cb(&mii->bank[MII_BANK_ROM],
			_mii_deselect_auxrom, mii, 0xcf, 0xcf);
	mii_bank_install_access_cb(&mii->bank[MII_BANK_ROM],
			_mii_select_c3rom, mii, 0xc3, 0xc3);
}

void
mii_prepare(
		mii_t *mii,
		uint32_t flags )
{
//	printf("%s driver table\n", __func__);
	mii_slot_drv_t * drv = mii_slot_drv_list;
	while (drv) {
		printf("%s driver: %s\n", __func__, drv->name);
		if (drv->probe && drv->probe(mii, flags)) {
		//	printf("%s %s probe done\n", __func__, drv->name);
		}
		drv = drv->next;
	}
}

void
mii_dispose(
		mii_t *mii )
{
	for (int i = 0; i < 7; i++) {
		if (mii->slot[i].drv && mii->slot[i].drv->dispose)
			mii->slot[i].drv->dispose(mii, &mii->slot[i]);
	}
	for (int i = 0; i < MII_BANK_COUNT; i++)
		mii_bank_dispose(&mii->bank[i]);
	mii_speaker_dispose(&mii->speaker);
	mii_dd_system_dispose(&mii->dd);
	mii->state = MII_INIT;
}

void
mii_reset(
		mii_t *mii,
		bool cold)
{
//	printf("%s cold %d\n", __func__, cold);
	mii->state = MII_RUNNING;
	mii->cpu_state.reset = 1;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii->sw_state = M_BSRWRITE | M_BSRPAGE2;
	mii_bank_poke(main, SWSLOTC3ROM, 0);
	mii_bank_poke(main, SWRAMRD, 0);
	mii_bank_poke(main, SWRAMWRT, 0);
	mii_bank_poke(main, SWALTPZ, 0);
	mii_bank_poke(main, SW80STORE, 0);
	mii_bank_poke(main, SW80COL, 0);
	mii->mem_dirty = 1;
	if (cold) {
		/*  these HAS to be reset in that state somehow */
		mii_bank_poke(main, SWINTCXROM, 0);
		uint8_t z[2] = {0x55,0x55};
		mii_bank_write(main, 0x3f2, z, 2);
	}
	mii->mem_dirty = 1;
	mii_page_table_update(mii);
	for (int i = 0; i < 7; i++) {
		if (mii->slot[i].drv && mii->slot[i].drv->reset)
			mii->slot[i].drv->reset(mii, &mii->slot[i]);
	}
}

void
mii_mem_access(
		mii_t *mii,
		uint16_t addr,
		uint8_t * d,
		bool wr,
		bool do_sw)
{
	if (!do_sw && addr >= 0xc000 && addr <= 0xc0ff)
		return;
	uint8_t done =
		mii_access_keyboard(mii, addr, d, wr) ||
		mii_access_video(mii, addr, d, wr) ||
		mii_access_soft_switches(mii, addr, d, wr);
	if (done)
		return;
	uint8_t page = addr >> 8;
	if (wr) {
		uint8_t m = mii->mem[page].write;
		mii_bank_t * b = &mii->bank[m];
		if (b->ro) {
		//	printf("%s write to RO bank %s %04x:%02x\n",
		//		__func__, b->name, addr, *d);
		} else
			mii_bank_write(b, addr, d, 1);
	} else {
		uint8_t m = mii->mem[page].read;
		mii_bank_t * b = &mii->bank[m];
		*d = mii_bank_peek(b, addr);
	}
}

static void
_mii_handle_trap(
		mii_t *mii)
{
//	printf("%s TRAP hit PC: %04x\n", __func__, mii->cpu.PC);
	mii->cpu_state.sync = 1;
	mii->cpu_state.trap = 0;
	mii->cpu.state = NULL;
	uint8_t trap = mii_read_one(mii, mii->cpu.PC);
	mii->cpu.PC += 1;
//	printf("%s TRAP %02x return PC %04x\n", __func__, trap, mii->cpu.PC);
	if (mii->trap.map & (1 << trap)) {
		if (mii->trap.trap[trap].cb)
			mii->trap.trap[trap].cb(mii, trap);
	} else {
		printf("%s TRAP %02x not handled\n", __func__, trap);
		mii->state = MII_STOPPED;
	}
//	mii->state = MII_STOPPED;
}

uint8_t
mii_register_trap(
		mii_t *mii,
		mii_trap_handler_cb cb)
{
	if (mii->trap.map == 0xffff) {
		printf("%s no more traps!!\n", __func__);
		return 0xff;
	}
	for (int i = 0; i < (int)sizeof(mii->trap.map) * 8; i++) {
		if (!(mii->trap.map & (1 << i))) {
			mii->trap.map |= 1 << i;
			mii->trap.trap[i].cb = cb;
			return i;
		}
	}
	return 0xff;
}

uint8_t
mii_timer_register(
		mii_t *mii,
		mii_timer_p cb,
		void *param,
		int64_t when,
		const char *name)
{
	if (mii->timer.map == (uint64_t)-1) {
		printf("%s no more timers!!\n", __func__);
		return 0xff;
	}
	int i = ffsll(~mii->timer.map) - 1;
	mii->timer.map |= 1ull << i;
	mii->timer.timers[i].cb = cb;
	mii->timer.timers[i].param = param;
	mii->timer.timers[i].when = when;
	mii->timer.timers[i].name = name;
	return i;
}

int64_t
mii_timer_get(
		mii_t *mii,
		uint8_t timer_id)
{
	if (timer_id >= (int)sizeof(mii->timer.map) * 8)
		return 0;
	return mii->timer.timers[timer_id].when;
}

int
mii_timer_set(
		mii_t *mii,
		uint8_t timer_id,
		int64_t when)
{
	if (timer_id >= (int)sizeof(mii->timer.map) * 8)
		return -1;
	mii->timer.timers[timer_id].when = when;
	return 0;
}

static void
mii_timer_run(
		mii_t *mii,
		uint64_t cycles)
{
	uint64_t timer = mii->timer.map;
	while (timer) {
		int i = ffsll(timer) - 1;
		timer &= ~(1ull << i);
		if (mii->timer.timers[i].when > 0) {
			mii->timer.timers[i].when -= cycles;
			if (mii->timer.timers[i].when <= 0) {
				if (mii->timer.timers[i].cb)
					mii->timer.timers[i].when += mii->timer.timers[i].cb(mii,
							mii->timer.timers[i].param);
			}
		}
	}
}

void
mii_run(
		mii_t *mii)
{
	/* this runs all cycles for one instruction */
	uint16_t cycle = mii->cpu.cycle;
	do {
		if (unlikely(mii->trace_cpu > 1))
			mii_dump_trace_state(mii);
		mii->cpu_state = mii_cpu_run(&mii->cpu, mii->cpu_state);

		mii_timer_run(mii,
					mii->cpu.cycle > cycle ? mii->cpu.cycle - cycle :
					mii->cpu.cycle);
		cycle = mii->cpu.cycle;

		// extract 16-bit address from pin mask
		const uint16_t addr = mii->cpu_state.addr;
//		const uint8_t data = mii->cpu_state.data;
		int wr = mii->cpu_state.w;
//		uint8_t d = data;
		if (unlikely(mii->debug.bp_map)) {
			for (int i = 0; i < (int)sizeof(mii->debug.bp_map) * 8; i++) {
				if (!(mii->debug.bp_map & (1 << i)))
					continue;
				if (addr >= mii->debug.bp[i].addr &&
						addr < mii->debug.bp[i].addr + mii->debug.bp[i].size) {
					if (((mii->debug.bp[i].kind & MII_BP_R) && !wr) ||
							((mii->debug.bp[i].kind & MII_BP_W) && wr)) {

						if (1 || !mii->debug.bp[i].silent) {
							printf("BREAKPOINT %d at %04x PC:%04x\n",
								i, addr, mii->cpu.PC);
							mii_dump_run_trace(mii);
							mii_dump_trace_state(mii);
							mii->state = MII_STOPPED;
						}
					}
					if (!(mii->debug.bp[i].kind & MII_BP_STICKY))
						mii->debug.bp_map &= ~(1 << i);
					mii->debug.bp[i].kind |= MII_BP_HIT;
				}
			}
		}
		mii_mem_access(mii, addr, &mii->cpu_state.data, wr, true);
		if (unlikely(mii->cpu_state.trap)) {
			_mii_handle_trap(mii);
		}
	} while (!(mii->cpu_state.sync));

	// log PC for the running disassembler display
	mii->trace.log[mii->trace.idx] = mii->cpu.PC;
	mii->trace.idx = (mii->trace.idx + 1) & (MII_PC_LOG_SIZE - 1);
	for (int i = 0; i < 7; i++) {
		if (mii->slot[i].drv && mii->slot[i].drv->run)
			mii->slot[i].drv->run(mii, &mii->slot[i]);
	}
}

//! Read one byte from and addres, using the current memory mapping
uint8_t
mii_read_one(
		mii_t *mii,
		uint16_t addr)
{
	uint8_t d = 0;
	mii_mem_access(mii, addr, &d, 0, false);
	return d;
}
//! Read a word from addr, using current memory mapping (little endian)
uint16_t
mii_read_word(
		mii_t *mii,
		uint16_t addr)
{
	uint8_t d = 0;
	uint16_t res = 0;
	mii_mem_access(mii, addr, &d, 0, false);
	res = d;
	mii_mem_access(mii, addr + 1, &d, 0, false);
	res |= d << 8;
	return res;
}
/* same accessors, for write
 */
void
mii_write_one(
		mii_t *mii,
		uint16_t addr,
		uint8_t d)
{
	mii_mem_access(mii, addr, &d, 1, false);
}
void
mii_write_word(
		mii_t *mii,
		uint16_t addr,
		uint16_t w)
{
	uint8_t d = w;
	mii_mem_access(mii, addr, &d, 1, false);
	d = w >> 8;
	mii_mem_access(mii, addr + 1, &d, 1, false);
}
