/*
 * mii_rom.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

/*
 * ROM and rom queue declaration. The ROM queue is just a single list of ROM
 * that auto-add themselves to the list. There is a small utility function to
 * locate a rom by name, or by class.
 */

#include <stdint.h>
#include "bsd_queue.h"

typedef struct mii_rom_t {
	SLIST_ENTRY(mii_rom_t) self;
	char *			name;
	char *			class;
	char *			description;
	const uint8_t *	rom;
	uint32_t 		len;
} mii_rom_t;


void
mii_rom_register(
		struct mii_rom_t *rom);

mii_rom_t *
mii_rom_get(
		const char *name);

mii_rom_t *
mii_rom_get_class(
		mii_rom_t * prev,
		const char *class);

#define MII_ROM(_d) \
	__attribute__((constructor,used)) \
	static void _mii_rom_register_##_d() { \
		mii_rom_register(&_d); \
	}
