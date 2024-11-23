/*
 * mii_mish.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_sw.h"
#include "mii_65c02_ops.h"
#include "mii_65c02_disasm.h"
#include "mii_rom.h"

void
mii_hexdump(
		const char * prompt,
		unsigned int display_offset,
		const void *buffer,
		unsigned int len)
{
	const uint8_t *buf = buffer;
	static const char *hex = "0123456789abcdef";
	char line[80];
	unsigned int o = 0;
	const int line_l = 16;
	if (prompt)
		printf("%s (%d/0x%x):\n", prompt, len, len);
	for (unsigned int i = 0; i < len; ) {
		memset(line, ' ', 80 - 1);
		char *dst = line;
		char *txt = dst + (line_l * 3);
		unsigned int so = o;
		for (int c = 0; c < line_l && i < len; c++, i++, o++) {
			uint8_t b = *buf++;
			*dst++ = hex[b >> 4];
			*dst++ = hex[b & 0xf];
			dst++; // space
			*txt++ = b < ' ' || b > 126 ? '.' : b;
		}
		*txt = 0;
		printf("%04x: %s\n", display_offset + so, line);
	}
}

static const char * apple2_charset =
"@ABCDEFGHIJKLMNO"
"PQRSTUVWXYZ[\\]^_"
" !\"#$%&'()*+,-./"
"0123456789:;<=>?"
"@ABCDEFGHIJKLMNO"
"PQRSTUVWXYZ[\\]^_"
"`abcdefghijklmno"
"pqrstuvwxyz{|}~";


void
_mii_mish_text(
		void * param,
		int argc,
		const char * argv[])
{
	// load 0x400, calculate the 24 line addresses from the code in video
	// and show the 40 or 80 chars, depending on col80
	mii_t * mii = param;
	mii_bank_t * main = &mii->bank[MII_BANK_MAIN];
	mii_bank_t * aux = &mii->bank[MII_BANK_AUX_BASE];
	uint16_t a = 0x400;
	bool page2 	= SW_GETSTATE(mii, SW80STORE) ? 0 : SW_GETSTATE(mii, SWPAGE2);
	int col80 = SW_GETSTATE(mii, SW80COL);
	for (int li = 0; li < 24; li++) {
		int i = li;
		a = (0x400 + (0x400 * page2));
		a += ((i & 0x07) << 7) | ((i >> 3) << 5) | ((i >> 3) << 3);
		printf("%04x: ", a);
		for (int x = 0; x < 40 + (40 * col80); x++) {
			uint8_t c = 0;
			if (col80)
				c = mii_bank_peek(x & 1 ? main : aux, a + (x >> 1));
			else
				c = mii_bank_peek(main, a + x);
			c = c & 0x7f;
			printf("%c", apple2_charset[c]);
		}
		printf("\n");
	}
}

static void
_mii_mish_cmd(
		void * param,
		int argc,
		const char * argv[])
{
	const char * state[] = {
			"INIT", "RUNNING", "STOPPED", "STEP", "TERMINATE"};
	mii_t * mii = param;
	if (!argv[1]) {
show_state:
		printf("mii: %s Target speed: %.3fMHz\n",
				state[mii->state], mii->speed);
		mii_dump_run_trace(mii);
		mii_dump_trace_state(mii);

		for (int i = 0; mii_sw_names[i]; i++) {
			char buf[32];
			sprintf(buf, "%s:%d", mii_sw_names[i],
					!!(mii->sw_state & (1 << i)));
			printf("%-13.13s%s", buf, !((i+1) % 6) ? "\n" : " ");
		}
		printf("\n");
		printf("RGB Video Mode: %d\n", mii->video.color_mode);
		return;
	}
	if (!strcmp(argv[1], "reset")) {
		mii_reset(mii, 0);
		return;
	}
	if (!strcmp(argv[1], "rgb")) {
		if (argc < 3) {
			printf("rgb: missing argument\n");
			return;
		}
		uint8_t val = strtol(argv[2], NULL, 16);
		mii_video_set_mode(mii, val);
		return;
	}

	if (!strcmp(argv[1], "mem")) {
		printf("mii: memory map:\n");
		for (int i = 0; i < MII_BANK_COUNT; i++)
			printf("%0d:%s ", i, mii->bank[i].name);
		printf("\n");
		for (int i = 0; i < 16; i++) {
			printf("%02x: ", i * 16);
			for (int j = 0; j < 16; j++)
				printf("%d:%d ", mii->mem[(i * 16) + j].read,
					mii->mem[(i * 16) + j].write);
			printf("\n");
		}
		return;
	}
	if (!strcmp(argv[1], "analog")) {
		printf("analog: %3d %3d %3d %3d\n", mii->analog.v[0].value,
				mii->analog.v[1].value, mii->analog.v[2].value,
				mii->analog.v[3].value);
		return;
	}
	if (!strcmp(argv[1], "trace")) {
		mii->trace_cpu = !mii->trace_cpu;
		printf("trace_cpu %d\n", mii->trace_cpu);
		return;
	}
	if (!strcmp(argv[1], "poke")) {
		if (argc < 4) {
			printf("poke: missing argument\n");
			return;
		}
		uint16_t addr = strtol(argv[2], NULL, 16);
		uint8_t val = strtol(argv[3], NULL, 16);
		mii_mem_access(mii, addr, &val, true, true);
		return;
	}
	if (!strcmp(argv[1], "peek")) {
		if (argc < 3) {
			printf("peek: missing argument\n");
			return;
		}
		uint16_t addr = strtol(argv[2], NULL, 16);
		uint8_t val;
		mii_mem_access(mii, addr, &val, false, true);
		printf("%04x: %02x\n", addr, val);
		return;
	}
	if (!strcmp(argv[1], "speed")) {
		if (argc < 3) {
			printf("speed: missing argument\n");
			return;
		}
		float speed = atof(argv[2]);
		if (speed < 0.1 || speed > 30.0) {
			printf("speed: speed must be between 0.0 and 30.0\n");
			return;
		}
		mii->speed = speed;
		printf("speed: %.3fMHz\n", speed);
		return;
	}
	if (!strcmp(argv[1], "stop")) {
		mii->state = MII_STOPPED;
		goto show_state;
	}
	if (!strcmp(argv[1], "quit") || !strcmp(argv[1], "exit")) {
		mii->state = MII_TERMINATE;
		printf("mii: terminating\n");
		return;
	}
	if (!strcmp(argv[1], "timers")) {
		uint64_t timer = mii->timer.map;
		printf("mii: %d cycle timers\n", __builtin_popcountll(timer));
		while (timer) {
			int i = ffsll(timer) - 1;
			timer &= ~(1ull << i);
			printf("%2d: %8ld %s\n", i, mii->timer.timers[i].when,
					mii->timer.timers[i].name);
		}
		return;
	}
	if (!strcmp(argv[1], "roms")) {
		mii_rom_t * rom = mii_rom_get(NULL);
		while (rom) {
			printf("rom: %-20s %-12s %7d %s\n", rom->name, rom->class,
					rom->len, rom->description);
			rom = SLIST_NEXT(rom, self);
		}
		return;
	}
	if (!strcmp(argv[1], "irq")) {
		uint16_t irq = mii->irq.map;
		printf("mii: %d IRQs\n", __builtin_popcount(irq));
		for (int i = 0; i < 16; i++) {
			if (irq & (1 << i))
				printf("%2d: %-10s %6d\n", i,
					mii->irq.irq[i].name, mii->irq.irq[i].count);
		}
		return;
	}
	printf("mii: unknown command %s\n", argv[1]);
}

void
_mii_mish_bp(
		void * param,
		int argc,
		const char * argv[])
{
	mii_t * mii = param;
	if (!argv[1] || !strcmp(argv[1], "list")) {
		printf("breakpoints: map %04x\n", mii->debug.bp_map);
		for (int i = 0; i < (int)sizeof(mii->debug.bp_map)*8; i++) {
			printf("%2d %c %04x %c%c%c size:%2d\n", i,
					(mii->debug.bp_map & (1 << i)) ? '*' : ' ',
					mii->debug.bp[i].addr,
					(mii->debug.bp[i].kind & MII_BP_R) ? 'r' : '-',
					(mii->debug.bp[i].kind & MII_BP_W) ? 'w' : '-',
					(mii->debug.bp[i].kind & MII_BP_STICKY) ? 's' : '-',
					mii->debug.bp[i].size);
		}
		return;
	}
	const char *p = argv[1];
	if (p[0] == '+') {
		p++;
		int addr = strtol(p, NULL, 16);
		int kind = 0;
		if (strchr(p, 'r'))
			kind |= MII_BP_R;
		if (strchr(p, 'w'))
			kind |= MII_BP_W;
		if (strchr(p, 's'))
			kind |= MII_BP_STICKY;
		if (!kind || kind == MII_BP_STICKY)
			kind |= MII_BP_R;
		int size = 1;
		if (argv[2])
			size = strtol(argv[2], NULL, 16);
		if (!size) size++;
		for (int i = 0; i < (int)sizeof(mii->debug.bp_map)*8; i++) {
			if (!(mii->debug.bp_map & (1 << i)) || mii->debug.bp[i].addr == addr) {
				mii->debug.bp_map |= 1 << i;
				mii->debug.bp[i].addr = addr;
				mii->debug.bp[i].kind = kind;
				mii->debug.bp[i].size = size;
				printf("breakpoint %d set at %04x size %d\n", i, addr, size);
				break;
			}
		}
		return;
	}
	if (p[0] == '-') {
		p++;
		int idx = strtol(p, NULL, 10);
		if (idx >= 0 && idx < 7) {
			mii->debug.bp_map &= ~(1 << idx);
			printf("breakpoint %d cleared\n", idx);
		}
	}
}


static void
_mii_mish_il(
		void * param,
		int argc,
		const char * argv[])
{
	static uint16_t addr = 0x800;
	if (argv[1]) {
		addr = strtol(argv[1], NULL, 16);
		if (addr >= 0xffff)
			addr = 0xfff0;
	}
	mii_t * mii = param;

	for (int li = 0; li < 20; li++) {
		uint8_t op[16];
		for (int bi = 0; bi < 4; bi++)
			mii_mem_access(mii, addr + bi, op + bi, false, false);
		char dis[64];
		addr += mii_cpu_disasm_one(op, addr, dis, sizeof(dis),
					MII_DUMP_DIS_PC | MII_DUMP_DIS_DUMP_HEX);
		printf("%s\n", dis);
	}
}


static void
_mii_mish_mm(
		void * param,
		int argc,
		const char * argv[])
{
	static uint32_t addr = 0x800;
	if (argv[1]) {
		addr = strtol(argv[1], NULL, 16);
		if (addr > 0x00100000)
			addr = 0x00fff0;
	}
	mii_t * mii = param;
	int bi = (addr >> 16) & 0xf;
	if (bi > MII_BANK_COUNT) {
		printf("mii: invalid bank index %d (0:7)\n", bi);
		return;
	}
	addr &= 0xffff;
	mii_bank_t * bank = &mii->bank[bi];
//	addr -= bank->base;
	if (!strcmp(argv[0], "mm")) {
		printf("%s %04x\n", bank->name, addr);
		for (int i = 0; i < 8; i++) {
			printf("%06x: ", addr);
			for (int j = 0; j < 16; j++)
				printf("%02x ", mii_bank_peek(bank, addr++));
			printf("\n");
		}
		return;
	}
	if (!strcmp(argv[0], "mb")) {
		printf("%s: %04x: ", bank->name, addr);
		printf("%02x ", mii_bank_peek(bank, addr++));
		printf("\n");
		return;
	}
	if (!strcmp(argv[0], "mw") || !strcmp(argv[0], "ma")) {
		printf("%s: %04x: ", bank->name, addr);
		uint8_t l = mii_bank_peek(bank, addr++);
		uint8_t h = mii_bank_peek(bank, addr++);
		printf("%02x%02x", h, l);
		printf("\n");
		return;
	}
}

static void
_mii_mish_dm(
		void * param,
		int argc,
		const char * argv[])
{
	static uint16_t addr = 0x800;
	if (argv[1]) {
		addr = strtol(argv[1], NULL, 16);
		if (addr >= 0xffff)
			addr = 0xfff0;
	}
	mii_t * mii = param;
	if (!strcmp(argv[0], "dm")) {
		printf("dm: %04x\n", addr);
		for (int i = 0; i < 8; i++) {
			printf("%04x: ", addr);
			for (int j = 0; j < 16; j++)
				printf("%02x ", mii_read_one(mii, addr++));
			printf("\n");
		}
		return;
	}
	if (!strcmp(argv[0], "db")) {
		printf("%s: %04x: ", argv[0], addr);
		printf("%02x ", mii_read_one(mii, addr++));
		printf("\n");
		return;
	}
	if (!strcmp(argv[0], "dw") || !strcmp(argv[0], "da")) {
		printf("%s: %04x: ", argv[0], addr);
		uint8_t l = mii_read_one(mii, addr++);
		uint8_t h = mii_read_one(mii, addr++);
		printf("%02x%02x", h, l);
		printf("\n");
		return;
	}
}


static void
_mii_mish_sr(
		void * param,
		int argc,
		const char * argv[])
{
	// set register
	mii_t * mii = param;
	if (argc < 3) {
		printf("sr: missing argument <A,X,Y,S,PC,P> <Value>)\n");
		return;
	}
	uint16_t val = strtol(argv[2], NULL, 16);
	if (!strcasecmp(argv[1], "A"))
		mii->cpu.A = val;
	else if (!strcasecmp(argv[1], "X"))
		mii->cpu.X = val;
	else if (!strcasecmp(argv[1], "Y"))
		mii->cpu.Y = val;
	else if (!strcasecmp(argv[1], "S"))
		mii->cpu.S = val;
	else if (!strcasecmp(argv[1], "PC"))
		mii->cpu.PC = val;
	else if (!strcasecmp(argv[1], "P")) {
		MII_SET_P(&mii->cpu, val);
	} else {
		printf("sr: unknown register %s\n", argv[1]);
		return;
	}
	mii_dump_trace_state(mii);
}

static void
_mii_mish_step(
		void * param,
		int argc,
		const char * argv[])
{
	if (argv[0][0] == 's') {
		mii_t * mii = param;
		if (mii->state != MII_STOPPED) {
			printf("mii: can't step/next, not stopped\n");
			return;
		}
		int count = 1;
		if (argv[1])
			count = strtol(argv[1], NULL, 10);
		mii_cpu_step(mii, count);
		return;
	}
	if (argv[0][0] == 'n') {
		mii_t * mii = param;
		mii_cpu_next(mii);
		return;
	}
	if (argv[0][0] == 'c') {
		mii_t * mii = param;
		mii->trace.step_inst = 0;
		mii->state = MII_RUNNING;
		return;
	}
	if (argv[0][0] == 'h') {
		mii_t * mii = param;
		mii->trace.step_inst = 0;
		mii->state = MII_STOPPED;
		return;
	}
}

#include <math.h>
extern int mii_speaker_debug;

static void
_mii_mish_audio(
		void * param,
		int argc,
		const char * argv[])
{
	mii_t * mii = param;
	if (argc < 2) {
		printf("audio volume: %.3f multiplier:%.3f muted:%d\n",
					mii->speaker.source.volume,
					mii->speaker.source.vol_multiplier,
					mii->audio.muted);
		return;
	}
	if (!strcmp(argv[1], "record")) {
#if 1
		if (mii_speaker_debug) {
//			close(mii_speaker_debug_fd);
//			mii_speaker_debug_fd = -1;
			mii_speaker_debug = 0;
			printf("audio: stop recording\n");
		} else {
			mii_speaker_debug = 1;
		//	mii_speaker_debug_fd = open("audio.raw",
		//								O_WRONLY | O_CREAT | O_TRUNC, 0644);
			printf("audio: start recording\n");
		}
#endif
	} else if (!strcmp(argv[1], "mute")) {
		if (argv[2] && !strcmp(argv[2], "off"))
			mii->audio.muted = false;
		else if (argv[2] && !strcmp(argv[2], "on"))
			mii->audio.muted = true;
		else if (!argv[2] || (argv[2] && !strcmp(argv[2], "toggle")))
			mii->audio.muted = !mii->audio.muted;
		printf("audio: %s\n", mii->audio.muted ? "muted" : "unmuted");
	} else if (!strcmp(argv[1], "volume")) {
		if (argc < 3) {
			printf("audio: missing volume\n");
			return;
		}
		// convert a linear volume from 0 to 10 into a float from 0.0 to 1.0
		float vol = atof(argv[2]);
		if (vol < 0) vol = 0;
		else if (vol > 10) vol = 10;
		mii_audio_volume(&mii->speaker.source, vol);
		printf("audio: volume %.3f (amp: %.4f)\n",
					vol, mii->speaker.source.vol_multiplier);
	} else {
		printf("audio: unknown command %s\n", argv[1]);
	}
}

static void
_mii_mish_bsave(
		void * param,
		int argc,
		const char * argv[])
{
	mii_t * mii = param;
	if (argc < 4) {
		printf("bsave: missing argument <file> <addr> <size>\n");
		return;
	}
	const char * file = argv[1];
	uint16_t addr = strtol(argv[2], NULL, 16);
	if (!strcmp(argv[0], "bsave")) {
		uint16_t size = strtol(argv[3], NULL, 16);
		if (addr + size > 0x10000) {
			printf("bsave: size too big\n");
			return;
		}
		FILE * f = fopen(file, "wb");
		if (!f) {
			printf("bsave: can't open %s\n", file);
			return;
		}
		mii_bank_t * bank = &mii->bank[MII_BANK_MAIN];

		fwrite(bank->mem + addr, size, 1, f);
		fclose(f);
		printf("bsave: %s saved %d bytes at %04x\n", file, size, addr);
	} else if (!strcmp(argv[0], "bload")) {
		FILE * f = fopen(file, "rb");
		if (!f) {
			printf("bsave: can't open %s\n", file);
			return;
		}
		fseek(f, 0, SEEK_END);
		uint16_t size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (addr + size > 0x10000) {
			printf("bsave: size too big\n");
			fclose(f);
			return;
		}
		mii_bank_t * bank = &mii->bank[MII_BANK_MAIN];
		fread(bank->mem + addr, size, 1, f);
		fclose(f);
		printf("bsave: %s loaded %d bytes at %04x\n", file, size, addr);
	} else {
		printf("%s: unknown command\n", argv[0]);
		return;
	}
}

#include "mish.h"

MISH_CMD_NAMES(mii, "mii");
MISH_CMD_HELP(mii,
		"mii: access internals, trace, reset, speed, etc",
		" <default> : dump current state",
		" rgb <val>: Set RGB mode to <val>(0:color,1:green,2:amber)",
		" reset : reset the cpu",
		" t|trace : toggle trace_cpu (WARNING HUGE traces!))",
		" mem : dump memory and bank map",
		" poke <addr> <val> : poke a value in memory (respect SW)",
		" peek <addr> : peek a value in memory (respect SW)",
		" speed <speed> : set speed in MHz",
		" stop : stop the cpu",
		" quit|exit : quit the emulator",
		" timers : list active timers",
		" roms : list loaded roms",
		" irq : list active IRQs"
		);
MII_MISH(mii, _mii_mish_cmd);

MISH_CMD_NAMES(bp, "bp");
MISH_CMD_HELP(bp,
		"mii: breakpoints. 'sticky' means the breakpoint is re-armed after hit",
		" <default> : dump state",
		" +<addr>[r|w][s] [size]: add at <addr> for read/write, sticky",
		" -<index> : disable (don't clear) breakpoint <index>"
		);
MII_MISH(bp, _mii_mish_bp);

MISH_CMD_NAMES(il, "il");
MISH_CMD_HELP(il,
		"mii: disassembly",
		" <default> : list next 20 instructions.",
		" [addr]: start at address addr"
		);
MII_MISH(il, _mii_mish_il);

MISH_CMD_NAMES(dm, "dm","db","dw","da");
MISH_CMD_HELP(dm,
		"mii: dump memory, byte, word, address",
		" <default>|dm <addr>: dump 64 bytes.",
		" db [<addr>]: dump one byte.",
		" dw [<addr>]: dump one word.",
		" da [<addr>]: dump one address.",
		" [addr]: start at address addr"
		);
MII_MISH(dm, _mii_mish_dm);

MISH_CMD_NAMES(mm, "mm","mb","mw","ma");
MISH_CMD_HELP(mm,
		"mii: dump memory, byte, word, [raw address]",
		" <default>|mm <addr>: dump 64 bytes.",
		" mb [<addr>]: dump one byte.",
		" mw [<addr>]: dump one word.",
		" ma [<addr>]: dump one address.",
		" [addr]: 24 bits address addr 00 main, 01 aux"
		);
MII_MISH(mm, _mii_mish_mm);

MISH_CMD_NAMES(step, "s","step","n","next","cont","h","halt");
MISH_CMD_HELP(step,
		"mii: step instructions",
		" s|step [num]: step [num, or one] instruction.",
		" n|next : step one instruction, skip subroutines.",
		" cont   :  continue execution."
		);
MII_MISH(step, _mii_mish_step);

MISH_CMD_NAMES(sr, "sr");
MISH_CMD_HELP(sr,
		"mii: set register",
		" <reg> <val>: set register to value",
		"    <reg>: A, X, Y, S, PC, P"
		);
MII_MISH(sr, _mii_mish_sr);

MISH_CMD_NAMES(text, "text");
MISH_CMD_HELP(text,
		"mii: show text page [buggy]",
		" <default> : that's it"
		);
MII_MISH(text, _mii_mish_text);

MISH_CMD_NAMES(audio, "audio");
MISH_CMD_HELP(audio,
		"audio: audio control/debug",
		" record: record/stop debug file.",
		" mute: mute/unmute audio.",
		" volume: set volume (0.0 to 1.0)."
		);
MII_MISH(audio, _mii_mish_audio);


MISH_CMD_NAMES(bsave, "bsave","bload");
MISH_CMD_HELP(bsave,
		"bsave/bload: save/load binary data from ram",
		" bsave <file> <addr> <size>: save binary data to file.",
		" bload <file> <addr>: load binary data from file."
		);
MII_MISH(bsave, _mii_mish_bsave);
