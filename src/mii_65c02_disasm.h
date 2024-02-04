/*
 * mii_65c02_disasm.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>


enum {
	MII_DUMP_DIS_PC 		= (1 << 0),
	MII_DUMP_DIS_DUMP_HEX 	= (1 << 1),
};


int
mii_cpu_disasm_one(
	const uint8_t *prog,
	uint16_t addr,
	char *out,
	size_t out_len,
	uint16_t flags);

void
mii_cpu_disasm(
	const uint8_t *prog,
	uint16_t addr,
	uint16_t len);
