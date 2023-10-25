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
mii_bank_write(
		mii_bank_t *bank,
		uint16_t addr,
		const uint8_t *data,
		uint16_t len)
{
	if ((addr < bank->base) ||
			((addr + len) > (uint32_t)(bank->base + (bank->size * 256)))) {
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
	if (!bank->mem) {
		bank->alloc = 1;
		bank->mem = calloc(1, bank->size * 256);
	}
	addr -= bank->base;
	for (uint16_t i = 0; i < len; i++, addr++) {
		bank->mem[addr] = data[i];
	}
}

void
mii_bank_read(
		mii_bank_t *bank,
		uint16_t addr,
		uint8_t *data,
		uint16_t len)
{
	if (addr < bank->base ||
			(addr + len) > (uint32_t)(bank->base + (bank->size * 256))) {
		printf("%s %s INVALID read addr %04x len %d %04x-%04x\n",
					__func__, bank->name, addr, (int)len,
					bank->base, bank->base + (bank->size * 256));
		return;
	}
	uint8_t page_index = (addr - bank->base) >> 8;
	if (bank->access && bank->access[page_index].cb) {
		if (bank->access[page_index].cb(bank, bank->access[page_index].param,
					addr, data, false))
			return;
	}
	if (!bank->mem) {
		bank->alloc = 1;
		bank->mem = calloc(1, bank->size * 256);
	}
	addr -= bank->base;
	for (uint16_t i = 0; i < len; i++, addr++) {
		data[i] = bank->mem[addr];
	}
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
	printf("%s %s install access cb page %02x:%02x\n",
			__func__, bank->name, page, end);
	for (int i = page; i <= end; i++) {
		bank->access[i].cb = cb;
		bank->access[i].param = param;
	}
}
