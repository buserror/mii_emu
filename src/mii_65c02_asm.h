/*
 * mui_65c02_asm.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>


typedef struct mii_cpu_asm_line_t {
	struct mii_cpu_asm_line_t *next;
	struct mii_cpu_asm_line_t *sym_next;
	uint16_t			line_index;
	uint8_t				symbol : 1,
						label_resolved : 1,
						addr_set : 1, op_low : 1, op_high : 1;
	uint16_t			addr;		// address of the instruction (if resolved)
	uint8_t				mode;		// mode of the instruction
	uint8_t				opcode_count; // number of bytes for the opcode
	uint8_t				opcodes[32];	// or .byte statements

	char 				label[64];
	char				mnemonic[64];
	char				operand[64];

	char 				op_name[64];
	int					op_value;

	char 				line[];		// actual line read ends up here, untouched
} mii_cpu_asm_line_t;

typedef struct mii_cpu_asm_program_t {
	uint8_t 			verbose;
	uint16_t 			org;		// origin, can be set before, or with .org
	mii_cpu_asm_line_t *sym;
	mii_cpu_asm_line_t *sym_tail;

	mii_cpu_asm_line_t *prog;
	mii_cpu_asm_line_t *prog_tail;

	uint8_t *			output;
	uint16_t			output_len;
} mii_cpu_asm_program_t;

int
mii_cpu_asm(
	mii_cpu_asm_program_t *p,
	const char *prog);
void
mii_cpu_asm_free(
	mii_cpu_asm_program_t *p);
