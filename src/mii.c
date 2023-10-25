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
		printf("%c", cpu->P.P[7-i] ? s_flags[7-i] : tolower(s_flags[7-i]));
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
			if (cpu->P.P[d.desc.s_bit] == d.desc.s_bit_value)
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
	// of disassebly for all of them
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
	if (!mii->mem_dirty)
		return;
	mii->mem_dirty = 0;
	int altzp = mii_sw(mii, SWALTPZ);
	int page2 = mii_sw(mii, SWPAGE2);
	int store80 = mii_sw(mii, SW80STORE);
	int hires = mii_sw(mii, SWHIRES);
	int ramrd = mii_sw(mii, SWRAMRD);
	int ramwrt = mii_sw(mii, SWRAMWRT);
	int intcxrom = mii_sw(mii, SWINTCXROM);
	int slotc3rom = mii_sw(mii, SWSLOTC3ROM);

	if (mii->trace_cpu)
		printf("%04x: page table update altzp:%02x page2:%02x store80:%02x hires:%02x ramrd:%02x ramwrt:%02x intcxrom:%02x slotc3rom:%02x\n",
			mii->cpu.PC,
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
	if (!intcxrom)
		mii_page_set(mii, MII_BANK_CARD_ROM, _SAME, 0xc1, 0xc7);
	mii_page_set(mii,
		slotc3rom ? MII_BANK_CARD_ROM : MII_BANK_ROM, _SAME, 0xc3, 0xc3);
	mii_page_set(mii,
		mii->bsr_mode.read ?
				altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR :
				MII_BANK_ROM,
		mii->bsr_mode.write ?
				altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR :
				MII_BANK_ROM,
				0xd0, 0xff);
	// BSR P2
	mii_page_set(mii,
		mii->bsr_mode.read ?
			(altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR) +
					mii->bsr_mode.page2 : MII_BANK_ROM,
		mii->bsr_mode.write ?
			(altzp ? MII_BANK_AUX_BSR : MII_BANK_BSR) +
					mii->bsr_mode.page2 : MII_BANK_ROM,
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

static bool
mii_access_soft_switches(
		mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write)
{
	if (!(addr >= 0xc000 && addr <= 0xc0ff) || addr == 0xcfff)
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
		case 0xc080 ... 0xc08f: {
			res = true;
			uint8_t mode = addr & 0x0f;
			static const int write_modes[4] = { 0, 1, 0, 1, };
			static const int read_modes[4] = { 1, 0, 0, 1, };
			uint8_t rd = read_modes[mode & 3];
			uint8_t wr = write_modes[mode & 3];
			mii->bsr_mode.write = wr;
			mii->bsr_mode.read = rd;
			mii->bsr_mode.page2 = mode & 0x08 ? 0 : 1;
			mii->mem_dirty = 1;
			if (mii->trace_cpu)
				printf("%04x: BSR mode addr %04x:%02x read:%s write:%s %s altzp:%02x\n",
					mii->cpu.PC, addr,
					mode,
					rd ? "BSR" : "ROM",
					wr ? "BSR" : "ROM",
					mii->bsr_mode.page2 ? "page2" : "page1",
					mii_sw(mii, SWALTPZ));
		}	break;
		case 0xcfff:
			res = true;
			mii->mem_dirty = 1;
			printf("%s TODO reset SLOT roms\n", __func__);
			break;
		case SWPAGE2OFF:
		case SWPAGE2ON:
			res = true;
			mii_bank_poke(main, SWPAGE2, (addr & 1) << 7);
			mii->mem_dirty = 1;
			break;
		case SWHIRESOFF:
		case SWHIRESON:
			res = true;
			mii_bank_poke(main, SWHIRES, (addr & 1) << 7);
			mii->mem_dirty = 1;
		//	printf("HIRES %s\n", (addr & 1) ? "ON" : "OFF");
			break;
		case SWSPEAKER:
			res = true;
			mii_speaker_click(&mii->speaker);
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
				mii_bank_poke(main, SW80STORE, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWRAMRDOFF:
			case SWRAMRDON:
				res = true;
				mii_bank_poke(main, SWRAMRD, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWRAMWRTOFF:
			case SWRAMWRTON:
				res = true;
				mii_bank_poke(main, SWRAMWRT, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWALTPZOFF:
			case SWALTPZON:
				res = true;
				mii_bank_poke(main, SWALTPZ, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWINTCXROMOFF:
			case SWINTCXROMON:
				res = true;
				mii_bank_poke(main, SWINTCXROM, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
			case SWSLOTC3ROMOFF:
			case SWSLOTC3ROMON:
				res = true;
				mii_bank_poke(main, SWSLOTC3ROM, (addr & 1) << 7);
				mii->mem_dirty = 1;
				break;
		}
	} else {
		switch (addr) {
			case SWBSRBANK2:
				*byte = mii->bsr_mode.page2 ? 0x80 : 0;
				res = true;
				break;
			case SWBSRREADRAM:
				*byte = mii->bsr_mode.read ? 0x80 : 0;
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
			case 0xc064: // Analog Input 0 (paddle 0)
			case 0xc065: // Analog Input 1 (paddle 1)
			case 0xc079: // Analog Input Reset
				res = true;
				break;
			case 0xc068:
				res = true;
				// IIgs register, read by prodos tho
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
				/* If fifo not empty, peek at the next key to process, it already
				 * has the 0x80 bit on --  otherwise, return 0 */
				res = true;
		#if 1
				*byte = mii_bank_peek(main, SWKBD);
		#else
				if (mii_key_fifo_isempty(&mii->keys))
					*byte = 0;
				else
					*byte = mii_key_fifo_read_at(&mii->keys, 0).key;
		#endif
			}
			break;
		case SWAKD:
			res = true;
#if 1
			{
				uint8_t r = mii_bank_peek(main, SWAKD);
				if (!write)
					*byte = r;
				r &= 0x7f;
				mii_bank_poke(main, SWAKD, r);
				mii_bank_poke(main, SWKBD, r);
			}
#else
		//	if (write) {
				/* clear latch, and replace it immediately with the new key
				 * if there's one in the FIFO */
				if (!mii_key_fifo_isempty(&mii->keys)) {
					mii_key_t k = mii_key_fifo_read(&mii->keys);
					mii_bank_poke(main, SWAKD, k.key);
				} else
					mii_bank_poke(main, SWAKD,
						mii_bank_peek(main, SWAKD) & ~0x80);
		//	} else
			if (!write)
				*byte = mii_bank_peek(main, SWAKD);
#endif
			break;
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
#if 0
	mii_key_t k = {
		.key = key | 0x80,
	};
	if (!mii_key_fifo_isfull(&mii->keys))
		mii_key_fifo_write(&mii->keys, k);
	else {
		printf("%s key fifo full\n", __func__);
	}
#endif
}


void
mii_init(
		mii_t *mii )
{
	memset(mii, 0, sizeof(*mii));
	mii->speed = 1.0;
	for (int i = 0; i < MII_BANK_COUNT; i++)
		mii->bank[i] = _mii_banks_init[i];
	mii->bank[MII_BANK_ROM].mem = (uint8_t*)&iie_enhanced_rom_bin[0];
	mii->cpu.trap = MII_TRAP;
	mii_reset(mii, true);
	mii_speaker_init(mii, &mii->speaker);
	mii->cpu_state = mii_cpu_init(&mii->cpu);
	for (int i = 0; i < 7; i++)
		mii->slot[i].id = i;
}

void
mii_prepare(
		mii_t *mii,
		uint32_t flags )
{
	printf("%s driver table\n", __func__);
	mii_slot_drv_t * drv = mii_slot_drv_list;
	while (drv) {
		printf("%s driver: %s\n", __func__, drv->name);
		if (drv->probe && drv->probe(mii, flags)) {
			printf("%s %s probe done\n", __func__, drv->name);
		}
		drv = drv->next;
	}
}

void
mii_reset(
		mii_t *mii,
		bool cold)
{
//	printf("%s cold %d\n", __func__, cold);
	mii->cpu_state.reset = 1;
	mii->bsr_mode.write = 1;
	mii->bsr_mode.read = 0;
	mii->bsr_mode.page2 = 1;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
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
	if (!done) {
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

void
mii_run(
		mii_t *mii)
{
	/* this runs all cycles for one instruction */
	do {
		if (mii->trace_cpu)
			mii_dump_trace_state(mii);
		mii->cpu_state = mii_cpu_run(&mii->cpu, mii->cpu_state);
		mii_video_run(mii);
		mii_speaker_run(&mii->speaker);
		// extract 16-bit address from pin mask
		const uint16_t addr = mii->cpu_state.addr;
		const uint8_t data = mii->cpu_state.data;
		int wr = mii->cpu_state.w;
		uint8_t d = data;
		if (mii->debug.bp_map) {
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
		mii_mem_access(mii, addr, &d, wr, true);
		if (!wr)
			mii->cpu_state.data = d;
		if (mii->cpu_state.trap) {
			_mii_handle_trap(mii);
		}
	} while (!(mii->cpu_state.sync));
	mii->cycles += mii->cpu.cycle;
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
