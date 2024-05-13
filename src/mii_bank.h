/*
 * mii_bank.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

struct mii_bank_t;
/*
 * Bank access callback can be register with banks and will be called
 * when a specific page (or pages) are read/written to.
 * Parameter will be the bank specified, the 'real' address beind read/written
 * and the 'param' passed when the callback was registered.
 * The callback should return true if it handled the access, false otherwise.
 * If it return true, the bank will not be accessed for this read/write.
 *
 * Note: the callback will be called once with bank == NULL when the bank
 * is being disposed, this allow any memory allocated by the callback to be
 * freed.
 */
typedef bool (*mii_bank_access_cb)(
		struct mii_bank_t *bank,
		void *param,
		uint16_t addr,
		uint8_t * byte,
		bool write);

typedef struct mii_bank_access_t {
	mii_bank_access_cb cb;
	void *param;
} mii_bank_access_t;

typedef struct mii_bank_t {
	uint64_t	base : 16, 		// base address
				size : 9,		// in pages
				no_alloc: 1,	// don't allocate memory
				alloc : 1,		// been malloced()
				ro : 1;			// read only
	char *		name;
	mii_bank_access_t * access;
	uint8_t		*mem;
	uint32_t 	mem_offset;
} mii_bank_t;

void
mii_bank_init(
		mii_bank_t *bank);
void
mii_bank_dispose(
		mii_bank_t *bank);
void
mii_bank_write(
		mii_bank_t *bank,
		uint16_t addr,
		const uint8_t *data,
		uint16_t len);
void
mii_bank_read(
		mii_bank_t *bank,
		uint16_t addr,
		uint8_t *data,
		uint16_t len);
bool
mii_bank_access(
		mii_bank_t *bank,
		uint16_t addr,
		const uint8_t *data,
		uint16_t len,
		bool write);

/* return the number of pages dirty (written into since last time) between
 * addr1 and addr2 (inclusive) */
uint8_t
mii_bank_is_dirty(
		mii_bank_t *bank,
		uint16_t addr1,
		uint16_t addr2);
void
mii_bank_install_access_cb(
		mii_bank_t *bank,
		mii_bank_access_cb cb,
		void *param,
		uint8_t page,
		uint8_t end);

static inline void
mii_bank_poke(
		mii_bank_t *bank,
		uint16_t addr,
		const uint8_t data)
{
	mii_bank_write(bank, addr, &data, 1);
}

static inline uint8_t
mii_bank_peek(
		mii_bank_t *bank,
		uint16_t addr)
{
	uint8_t res = 0;
	mii_bank_read(bank, addr, &res, 1);
	return res;
}
