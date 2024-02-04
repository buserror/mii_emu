/*
 * mii_65c02_disasm.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mii_65c02.h"
#include "mii_65c02_ops.h"
#include "mii_65c02_disasm.h"

int
mii_cpu_disasm_one(
	const uint8_t *prog,
	uint16_t addr,
	char *out,
	size_t out_len,
	uint16_t flags)
{
	uint16_t pc = addr;
	int i = 0;

	uint8_t op = prog[i];
	mii_op_desc_t d = mii_cpu_op[op].desc;
	if (!d.pc)
		d.pc = 1;
	// special case for JSR, it is marked as IMPLIED for execution, but is
	// in fact ABSOLUTE for PC calculation
	if (op == 0x20)
		d.mode = ABS;
	*out = 0;
	int len = out_len;
	if (flags & MII_DUMP_DIS_PC)
		len -= snprintf(out + strlen(out), len, "%04X: ", pc + i);
	if (flags & MII_DUMP_DIS_DUMP_HEX) {
		char hex[32] = {0};
		for (int oi = 0; oi < d.pc; oi++)
			sprintf(hex + (oi * 3), "%02X ", prog[i + oi]);
		len -= snprintf(out + strlen(out), len, "%-9s ", hex);
	}
	len -= snprintf(out + strlen(out), len, "%.4s ",
			mii_cpu_op[op].name[0] ?
			mii_cpu_op[op].name : "???");
	i++;
	switch (d.mode) {
		case IMM:
			len -= snprintf(out + strlen(out), len,
						"#$%02X", prog[i++]);
			break;
		case BRANCH:
		case ZP_REL:
			if ((op & 0xf) == 0xf) {
				uint16_t base = pc + i + 1;
				uint16_t dest = base + (int8_t)prog[i++];
				len -= snprintf(out + strlen(out), len,
						"%d,$%04X", d.s_bit, dest);
			} else if (d.branch) {
				uint16_t base = pc + i + 1;
				uint16_t dest = base + (int8_t)prog[i++];
				len -= snprintf(out + strlen(out), len,
						"$%04X", dest);
			} else
				len -= snprintf(out + strlen(out), len,
						"$%02X", prog[i++]);
			break;
		case ZP_X:
			len -= snprintf(out + strlen(out), len,
						"$%02X,X", prog[i++]);
			break;
		case ZP_Y:
			len -= snprintf(out + strlen(out), len,
						"$%02X,Y", prog[i++]);
			break;
		case ABS:
			len -= snprintf(out + strlen(out), len,
						"$%02X%02X", prog[i + 1], prog[i]);
			i += 2;
			break;
		case ABS_X:
			len -= snprintf(out + strlen(out), len,
						"$%02X%02X,X", prog[i + 1], prog[i]);
			i += 2;
			break;
		case ABS_Y:
			len -= snprintf(out + strlen(out), len,
						"$%02X%02X,Y", prog[i + 1], prog[i]);
			i += 2;
			break;
		case IND_X:
			len -= snprintf(out + strlen(out), len,
						"($%02X,X)", prog[i++]);
			break;
		case IND_AX:
			len -= snprintf(out + strlen(out), len,
						"($%02X%02X,X)",  prog[i + 1], prog[i]);
			i += 2;
			break;
		case IND_Y:
			len -= snprintf(out + strlen(out), len,
						"($%02X),Y", prog[i++]);
			break;
		case IND_Z:
			len -= snprintf(out + strlen(out), len,
						"($%02X)", prog[i++]);
			break;
		case IND:
			len -= snprintf(out + strlen(out), len,
						"($%02X%02X)", prog[i + 1], prog[i]);
			i += 2;
			break;
		case IMPLIED:
			break;
	}
	return d.pc;
}

void
mii_cpu_disasm(
	const uint8_t *prog,
	uint16_t addr,
	uint16_t len)
{
	uint16_t pc = addr;
	int i = 0;
	while (i < len) {
		char out[256] = {0};
		i += mii_cpu_disasm_one(prog + i, pc + i, out, sizeof(out),
					MII_DUMP_DIS_PC | MII_DUMP_DIS_DUMP_HEX);
		printf("%s\n", out);
	}
}
