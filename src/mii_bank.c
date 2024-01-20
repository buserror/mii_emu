/*
 * mii_bank.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii.h"
#include "mii_bank.h"

void
mii_bank_init(
		mii_bank_t *bank)
{
	if (bank->mem)
		return;
	bank->mem = calloc(1, bank->size * 256);
	bank->alloc = 1;
}

void
mii_bank_dispose(
		mii_bank_t *bank)
{
	if (bank->alloc)
		free(bank->mem);
	bank->mem = NULL;
	bank->alloc = 0;
	if (bank->access) {
		// Allow callback to free anything it wants
		for (int i = 0; i < bank->size; i++)
			if (bank->access[i].cb)
				bank->access[i].cb(NULL, bank->access[i].param, 0, NULL, false);
		free(bank->access);
	}
	bank->access = NULL;
}

void
mii_bank_write(
		mii_bank_t *bank,
		uint16_t addr,
		const uint8_t *data,
		uint16_t len)
{
	uint32_t end = bank->base + (bank->size << 8);
	if (unlikely(addr < bank->base) || unlikely((addr + len) > end)) {
		printf("%s %s INVALID write addr %04x len %d %04x:%04x\n",
					__func__, bank->name, addr, (int)len,
					bank->base, bank->base + (bank->size * 256));
		return;
	}
	uint8_t page_index = (addr - bank->base) >> 8;
	if (bank->access && bank->access[page_index].cb) {
		if (bank->access[page_index].cb(bank, bank->access[page_index].param,
					addr, (uint8_t *)data, true))
			return;
	}
	addr -= bank->base;
	do {
		bank->mem[addr++] = *data++;
	} while (unlikely(--len));
}

void
mii_bank_read(
		mii_bank_t *bank,
		uint16_t addr,
		uint8_t *data,
		uint16_t len)
{
	#if 0 // rather expensive test when profiling!
	uint32_t end = bank->base + (bank->size << 8);
	if (unlikely(addr < bank->base) || unlikely((addr + len) > end)) {
		printf("%s %s INVALID read addr %04x len %d %04x-%04x\n",
					__func__, bank->name, addr, (int)len,
					bank->base, bank->base + (bank->size * 256));
		return;
	}
	#endif
	uint8_t page_index = (addr - bank->base) >> 8;
	if (bank->access && bank->access[page_index].cb) {
		if (bank->access[page_index].cb(bank, bank->access[page_index].param,
					addr, data, false))
			return;
	}
	addr -= bank->base;
	do {
		*data++ = bank->mem[addr++];
	} while (unlikely(--len));
}


void
mii_bank_install_access_cb(
		mii_bank_t *bank,
		mii_bank_access_cb cb,
		void *param,
		uint8_t page,
		uint8_t end)
{
	if (!end)
		end = page;
	if ((page << 8) < bank->base || (end << 8) > (bank->base + bank->size * 256)) {
		printf("%s %s INVALID install access cb %p param %p page %02x-%02x\n",
					__func__, bank->name, cb, param, page, end);
		return;
	}
	page -= bank->base >> 8;
	end -= bank->base >> 8;
	if (!bank->access) {
		bank->access = calloc(1, bank->size * sizeof(bank->access[0]));
	}
//	printf("%s %s install access cb page %02x:%02x\n",
//			__func__, bank->name, page, end);
	for (int i = page; i <= end; i++) {
		bank->access[i].cb = cb;
		bank->access[i].param = param;
	}
}
