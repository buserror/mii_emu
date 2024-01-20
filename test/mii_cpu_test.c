/*
 * mii_cpu_test.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror+git@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * This was heavily inspired and converted from
 * https://github.com/ct6502/apple2ts/blob/main/src/emulator/instructions.test.ts
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii_65c02.h"
#include "mii_65c02_asm.h"
#include "mii_65c02_disasm.h"
#include "mii_65c02_ops.h"


static void
_run_one_dump_state(
	mii_cpu_t *cpu,
	mii_cpu_state_t s,
	void *p)
{
	printf("PC:%04X A:%02X X:%02X Y:%02X S:%02x #%d %s AD:%04X D:%02x %s ",
		cpu->PC, cpu->A, cpu->X, cpu->Y, cpu->S, cpu->cycle,
		s.sync ? "I" : " ", s.addr, s.data, s.w ? "W" : "R");
	// display the S flags
	static const char *s_flags = "CZIDBRVN";
	for (int i = 0; i < 8; i++)
		printf("%c", MII_GET_P_BIT(cpu, i) ? s_flags[i] : tolower(s_flags[i]));
	if (s.sync) {
		mii_op_t d = mii_cpu_op[cpu->ram[cpu->PC]];
		printf(" ");
		char dis[64];
		mii_cpu_disasm_one(cpu->ram + cpu->PC, cpu->PC, dis, sizeof(dis),
			MII_DUMP_DIS_PC | MII_DUMP_DIS_DUMP_HEX);
		printf("%s", dis);
		if (d.desc.branch) {
			if (MII_GET_P_BIT(cpu, d.desc.s_bit) == d.desc.s_bit_value)
				printf(" ; taken");
		}
		printf("\n");
	} else
		printf("\n");
}


static int
_run_one_test(
	const char *prog,
	int verbose)
{
	uint8_t *ram = calloc(1, 0x10000);
	mii_cpu_asm_program_t p = {};
	{
		p.verbose = verbose;
		if (mii_cpu_asm(&p, prog) == 0) {
		//	uint8_t *o = p.output;
			if (verbose || p.verbose)
				mii_cpu_disasm(p.output, p.org, p.output_len);
			memcpy(ram + p.org, p.output, p.output_len);
		} else {
			printf("error\n");
			exit(1);
		}
	}
	mii_cpu_t cpu = {0};
	mii_cpu_state_t s = {0};
	verbose += p.verbose;
	cpu.ram = ram;
	if (verbose) {
	//	cpu.debug = _run_one_dump_state;
	}
	cpu.P._R = 1;
  	cpu.S = 0xFF;
	cpu.PC = p.org;
	cpu.A = 0x01;
	int count = 1000;
	int error = 1;
	do {
		s = mii_cpu_run(&cpu, s);
		if (s.w)	ram[s.addr] = s.data;
		else 		s.data = ram[s.addr];
		if (verbose)
			_run_one_dump_state(&cpu, s, NULL);
		if (s.sync && !s.w && s.data == 0x00) {
			mii_cpu_asm_line_t *l = p.prog;
			while (l) {
				if (l->addr == s.addr)
					break;
				l = l->next;
			}
			if (l && !strcasecmp(l->label, "pass")) {
				printf("TEST %-40.40s: PASS\n", p.prog->line + 2);
				error = 0;
			} else if (l) {
				printf("TEST %-40.40s: FAIL\n", p.prog->line + 2);
				printf("\t%s\n", l->line);
				_run_one_dump_state(&cpu, s, NULL);
			} else {
				printf("TEST %-40.40s: FAIL (out of program!)\n",
					p.prog->line + 2);
				_run_one_dump_state(&cpu, s, NULL);
			}
			break;
		}
	} while (count--);
	if (!count) {
		printf("TEST %-40.40s: FAIL (out of _run_this cycles!)\n",
			p.prog->line + 2);
		_run_one_dump_state(&cpu, s, NULL);
	}
	free(ram);
	return error;
}

int
_run_this_one(
	const char *desc,
	const char *prog,
	uint8_t expexted_A,
	uint8_t expected_flags,
	int verbose )
{
	uint8_t *ram = calloc(1, 0x10000);
	mii_cpu_asm_program_t p = {.org = 0x2000};
	{
		if (mii_cpu_asm(&p, prog) == 0) {
		//	uint8_t *o = p.output;
			if (verbose || p.verbose)
				mii_cpu_disasm(p.output, p.org, p.output_len);
			memcpy(ram + p.org, p.output, p.output_len);
		} else {
			printf("error\n");
			exit(1);
		}
	}
	mii_cpu_t cpu = {0};
	mii_cpu_state_t s = {0};
	verbose += p.verbose;
	cpu.ram = ram;
	cpu.P._R = 1;
	expected_flags |= (1 << B_X);
  	cpu.S = 0xFF;
	cpu.PC = p.org;
	cpu.A = 0x01;
	int count = 1000;
	do {
		s = mii_cpu_run(&cpu, s);
		if (s.w)	ram[s.addr] = s.data;
		else 		s.data = ram[s.addr];
		if (verbose)
			_run_one_dump_state(&cpu, s, NULL);
		if (s.sync && !s.w && s.data == 0x00) {
			if (cpu.A == expexted_A) {
				int err = 0;
				static const char *s_flags = "CZIDBRVN";
				for (int i = 0; i < 8; i++)
					if (MII_GET_P_BIT(&cpu, i) != ((expected_flags >> i) & 1)) {
						printf("** S bit %c mismatch %d want %d\n",
							s_flags[i], MII_GET_P_BIT(&cpu, i),
							(expected_flags >> i) & 1);
						err++;
					}
				if (err)
					goto fail;
				printf("TEST %-40.40s: PASS\n", desc);
				break;
			} else {
				printf("** A Mismatch %02x vs %02x\n", cpu.A, expexted_A);
fail:
				printf("TEST %-40.40s: FAIL\n", desc);
				return -1;
			}
		}
	} while (count--);
	free(ram);
	return 0;
}

void
_run_this(
	const char *desc,
	const char *prog,
	uint8_t expexted_A,
	uint8_t expected_flags,
	int verbose )
{
	int e = _run_this_one(desc, prog, expexted_A, expected_flags, verbose);
	if (e && !verbose) {
		if (_run_this_one(desc, prog, expexted_A, expected_flags, 1))
			exit(1);
	}
}

static char * indirect(char *prefix, uint8_t A, uint8_t mem, char *suffix) {
	char *s = calloc(1, 256);
	sprintf(s,
		" %s\n"
		" LDA #$01\n"
		" STA $12\n"
		" LDA #$30\n"
		" STA $13\n"
		" LDA #$%02x\n"
		" STA $3001\n"
		" LDA #$%02x\n"
		" %s\n", prefix, mem, A, suffix);
	return s;
}
static char * doSED_ADC(uint8_t a, uint8_t b) {
	char *s = calloc(1, 256);
	sprintf(s,
		" SED\n"
		" LDA #$%02x\n"
		" ADC #$%02x\n",
		a, b);
	return s;
}

#include <glob.h>
int main()
{
	printf("Sizeof mii_op_desc_t %d\n", (int)sizeof(mii_op_desc_t));
	printf("Sizeof mii_op_t %d\n", (int)sizeof(mii_op_t));
	printf("Sizeof mii_cpu_t %d\n", (int)sizeof(mii_cpu_t));
	printf("Sizeof mii_cpu_state_t %d\n", (int)sizeof(mii_cpu_state_t));
	printf("Sizeof mii_cpu_op %d\n", (int)sizeof(mii_cpu_op));

	glob_t globbuf;
	glob("test/asm/0*.asm", 0, NULL, &globbuf);
	for (int i = 0; i < (int)globbuf.gl_pathc; i++) {
		printf("Loading %s\n", globbuf.gl_pathv[i]);
		FILE *f = fopen(globbuf.gl_pathv[i], "r");
		if (f) {
			char *prog = calloc(1, 65536);
			fread(prog, 1, 65536, f);
			fclose(f);
			_run_one_test(prog, 0);
			free(prog);
		}
	}
	globfree(&globbuf);
	enum {
		C = (1 << B_C),
		Z = (1 << B_Z),
		I = (1 << B_I),
		D = (1 << B_D),
		B = (1 << B_B),
		X = (1 << B_X),
		V = (1 << B_V),
		N = (1 << B_N),
	};
	// https://github.com/AppleWin/AppleWin/issues/1257
	_run_one_test(
			"; Test of JSR *in* the stack\n"
			"  .org $0100\n"
			"  jmp test\n"
			"pass:  .org $0155\n"
			"test:  .org $0178\n"
			"  LDX #$7D\n"
			"  TXS\n"
			"  JSR $1355\n",
			0);
	_run_this("RMB $12",
			" lda #$FF\n"
			" sta $12\n"
			" rmb1 $12\n"
			" lda $12\n",
			0xfd, N, 0);
	_run_this("RMB1 $ea",
			" ldx     $EA\n"
			" lda     #$FF\n"
			" sta     $EA\n"
			" rmb1    $EA\n"
			" cmp     $EA\n"
			" stx     $EA\n",
			0xff, C, 0);
	_run_this("ASL Carry",
			" clc\n lda #$80\n asl\n",
			0x00, Z|C, 0);
	_run_this("ROL Base",
			" sec\n lda #$80\n rol\n",
			0x01, C, 0);

	_run_this("SBC Over+",
			" sec\n lda #$d0\n sbc #$70\n",
			0x60, V|C, 0);
	_run_this("SBC Neg3",
			" sec\n lda #$d0\n sbc #$30\n",
			0xA0, N|C, 0);
	_run_this("LDA ($aa,X)",
		"  LDX #$E9\n"
		"  LDY #$81\n"
		"  STY $3104\n"
		"  LDY #$04\n"
		"  STY $003A\n"
		"  LDY #$31\n"
		"  STY $003B\n"
		"  LDA ($51,X)\n",
		0x81, N, 0);
	_run_this("LDA ($aa),Y",
		" LDY #$E9\n"
		"  LDX #$BB\n"
		"  STX $403A\n"
		"  LDX #$51\n"
		"  STX $00A4\n"
		"  LDX #$3F\n"
		"  STX $00A5\n"
		"  LDA ($A4),Y\n",
		0xBB, N, 0);
	_run_this("JMP ($3000,X)",
		" JMP skip\n"
		" LDA #$99   ; $2003\n"
		" JMP done\n"
		"skip LDA #$03\n"
		" STA $3002\n"
		" LDA #$20\n"
		" STA $3003\n"
		" LDX #$02\n"
		" JMP ($3000,X)   ; JMP -> $2003\n"
		" LDA #$01    ; should not reach here\n"
		"done  NOP ;\n",
		0x99, N, 0);
	_run_this("DEC",
		"  LDA #99\n" "  DEC\n",
		0x98, N, 0);
	_run_this("PLX",
		" LDX #$97\n" " PHX\n" " PLX\n" " TXA\n",
		0x97, N, 0);

	_run_this("ASL", " LDA #$80\n ASL\n", 0x00, Z | C, 0);
	_run_this("ROR", " LDA #$81\n ROR\n", 0x40, C, 0);
	_run_this("ROL Z", " LDA #$80\n ROL\n", 0x00, Z | C, 0);
	_run_this("ROL", " LDA #$81\n ROL\n", 0x02, C, 0);


	_run_this("LDA ($12)",
		indirect("", 0x99, 0xFC, "LDA ($12)"),
		0xFC, N, 0);
	_run_this("ADC ($12)",
		indirect("", 0x7E, 0xAF, "ADC ($12)"),
		0x2D, C, 0);
	_run_this("ADC ($12) SED",
		indirect("SED", 0x75, 0x25, "ADC ($12)"),
		0x00, V | C | D, 0);
	_run_this("AND ($12)",
		indirect("", 0x3F, 0xFC, "AND ($12)"),
		0x3C, 0, 0);
	_run_this("CMP ($12)",
		indirect("", 0xFC, 0x3F, "CMP ($12)"),
		0xFC, C | N, 0);
	_run_this("EOR ($12)",
		indirect("", 0x3F, 0xFC, "EOR ($12)"),
		0xC3, N, 0);
	_run_this("ORA ($12)",
		indirect("", 0x0E, 0xFC, "ORA ($12)"),
		0xFE, N, 0);
	_run_this("SBC ($12)",
		indirect("SEC", 0xFF, 0xC0, "SBC ($12)"),
		0x3F, C, 0);
	_run_this("SBC ($12) SED",
		indirect("SEC\n  SED", 0x75, 0x25, "SBC ($12)"),
		0x50, C | D | V, 0);
	_run_this("STA ($12)",
		indirect("", 0xF1, 0xC0, "STA ($12)\n LDA $3001"),
		0xF1, N, 0);

	_run_this("BIT #$F0",
		" LDA #$0F\n BIT #$F0\n",
		0x0F, Z , 0);
	_run_this("BIT #$80",
		" LDA #$0F\n BIT #$80", 0x0F, Z, 0);
	_run_this("BIT #$70",
		" LDA #$0F\n BIT #$70",
		0x0F, Z, 0);
	_run_this("BIT #$FF",
		" LDA #$0F\n BIT #$FF",
		0x0F, 0, 0);
	_run_this("BIT $12,X",
		"  LDY #$F0\n"
		"  STY $14\n"
		"  LDA #$0F\n"
		"  LDX #$02\n"
		"  BIT $12,X\n",
		0x0F, Z | V | N, 0);
	_run_this("BIT $1234,X",
		"  LDY #$F0\n"
		"  STY $1236\n"
		"  LDA #$0F\n"
		"  LDX #$02\n"
		"  BIT $1234,X\n",
		0x0F, Z | V | N, 0);
	_run_this("DEC $xx",
		"  LDA #$FF\n"
		"  STA $12\n"
		"  DEC $12\n"
		"  LDA $12\n",
		0xFE, N, 0);
	_run_this("INC $xxxx",
		"  LDA #$FF\n"
		"  STA $3000\n"
		"  INC $3000\n"
		"  LDA $3000\n",
		0x00, Z, 0);

	_run_this("LDA ($9d),Y",
		"  LDX #$90\n"
		"  STX $90\n"
		"  LDY #$D\n"
		"  STZ $91\n"
		"  LDX #$de\n"
		"  STX $9D\n"
		"  LDA ($90),y\n",
		0xde, N, 0);

	_run_this("LDA ($aa,X)",
		"  LDX #$E9\n"
		"  LDY #$81\n"
		"  STY $3104\n"
		"  LDY #$04\n"
		"  STY $003A\n"
		"  LDY #$31\n"
		"  STY $003B\n"
		"  LDA ($51,X)\n",
		0x81, N, 0);
	_run_this("LDA ($aa),Y",
		"  LDY #$E9\n"
		"  LDX #$BB\n"
		"  STX $403A\n"
		"  LDX #$51\n"
		"  STX $00A4\n"
		"  LDX #$3F\n"
		"  STX $00A5\n"
		"  LDA ($A4),Y\n",
		0xBB, N, 0);
	_run_this("LDA ($aa)",
		"  LDX #$AA\n"
		"  STX $3F51\n"
		"  LDX #$51\n"
		"  STX $00A4\n"
		"  LDX #$3F\n"
		"  STX $00A5\n"
		"  LDA ($A4)\n",
		0xAA, N, 0);

	_run_this("SED ADC 0", doSED_ADC(0, 0), 0x0, Z | D, 0);
	_run_this("SED ADC 1", doSED_ADC(1, 0), 0x01, D, 0);
	_run_this("SED ADC 9", doSED_ADC(9, 0), 0x09, D, 0);
	_run_this("SED ADC 10", doSED_ADC(0x10, 0), 0x10, D, 0);
	_run_this("SED ADC 1D", doSED_ADC(0x1D, 0), 0x23, D, 0);
	_run_this("SED ADC 99", doSED_ADC(0x99, 0), 0x99, N | D, 0);
	_run_this("SED ADC 99", doSED_ADC(0x99, 1), 0x00, C | D, 0);
	_run_this("SED ADC BD", doSED_ADC(0xBD, 0), 0x23, C | D, 0);
	_run_this("SED ADC FF", doSED_ADC(0xFF, 0), 0x65, C | D , 0);

	_run_this("SED ADC 0,1", doSED_ADC(0, 1), 0x01, D, 0);
	_run_this("SED ADC 0,9", doSED_ADC(0, 9), 0x09, D, 0);
	_run_this("SED ADC 0,10", doSED_ADC(0, 0x10), 0x10, D, 0);
	_run_this("SED ADC 0,1D", doSED_ADC(0, 0x1D), 0x23, D, 0);
	_run_this("SED ADC 0,99", doSED_ADC(0, 0x99), 0x99, N | D, 0);
	_run_this("SED ADC 0,BD", doSED_ADC(0, 0xBD), 0x23, C | D, 0);
	_run_this("SED ADC 0,FF", doSED_ADC(0, 0xFF), 0x65, C | D, 0);

	_run_this("SED ADC 99,1", doSED_ADC(0x99, 1), 0x0, C | D, 0);
	_run_this("SED ADC 35,35", doSED_ADC(0x35, 0x35), 0x70, D, 0);
	_run_this("SED ADC 45,45", doSED_ADC(0x45, 0x45), 0x90, N | V | D, 0);
	_run_this("SED ADC 50,50", doSED_ADC(0x50, 0x50), 0x0, V | C | D, 0);
	_run_this("SED ADC 99,99", doSED_ADC(0x99, 0x99), 0x98, N | V | C | D, 0);
	_run_this("SED ADC B1,C1", doSED_ADC(0xB1, 0xC1), 0xD2, N | V | C | D, 0);

	// create an emulator, load the binary file 6502_functional_test.bin at $0000
	// and run it until we hit a BRK
	const char *bigtest[] = {
	//	"test/asm/6502_functional_test.bin",
	//	"test/asm/65C02_extended_opcodes_test.bin",
		NULL,
	};
	for (int ti = 0; bigtest[ti]; ti++) {
		FILE *f = fopen(bigtest[ti], "r");
		if (!f) {
			fprintf(stderr, "Cannot open %s\n", bigtest[ti]);
			continue;
		}
		printf("TEST %s\n", bigtest[ti]);
		uint8_t *ram = calloc(1, 0x10000);
		fread(ram + 0x000, 1, 0x10000 - 0x400, f);
		fclose(f);
		mii_cpu_t cpu = {0};
		mii_cpu_state_t s = {0};
		cpu.ram = ram;
//		cpu.debug = _run_one_dump_state;
		cpu.P._R = 1;
		cpu.P.I = 1;
		cpu.S = 0xFF;
		cpu.PC = 0x400;
		cpu.A = 0x00;
		int count = 100000000;
		int same_pc_count = 0;
		uint16_t prev_pc = 0;
		do {
			s = mii_cpu_run(&cpu, s);
			if (s.w)	ram[s.addr] = s.data;
			else 		s.data = ram[s.addr];
			if (s.sync && !s.w) {
				if (cpu.PC == prev_pc) {
					same_pc_count++;
					if (same_pc_count > 3) {
						if (ram[cpu.PC] == 0x4c)
							printf("TEST %s: PASS\n", bigtest[ti]);
						else {
							printf("TEST %s: FAIL (stuck at %04X)\n",
								bigtest[ti], cpu.PC);
							_run_one_dump_state(&cpu, s, NULL);
							printf("  Failed instruction is %c%c%c\n",
								cpu.A, cpu.X, cpu.Y);
						}
						break;
					}
				} else {
					same_pc_count = 0;
					prev_pc = cpu.PC;
				}
			}
		//	if (s.sync)
		//		_run_one_dump_state(&cpu, s, NULL);
		} while (count--);
		printf("TEST run with %d spare\n", count);
		_run_one_dump_state(&cpu, s, NULL);
		if (!count) {
			printf("TEST %s: FAIL (out of _run_this cycles!)\n",
					bigtest[ti]);
			_run_one_dump_state(&cpu, s, NULL);
		}
		free(ram);
	}
}
