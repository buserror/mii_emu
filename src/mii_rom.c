/*
 * mii_rom.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mii_rom.h"


typedef SLIST_HEAD(mii_rom_queue_t, mii_rom_t) mii_rom_queue_t;

static mii_rom_queue_t mii_rom_queue = SLIST_HEAD_INITIALIZER(mii_rom_queue);

void
mii_rom_register(
		struct mii_rom_t *rom)
{
	// insert in the queue, but keep it sorted by class, then name
	mii_rom_t *r, *prev = NULL;
	SLIST_FOREACH(r, &mii_rom_queue, self) {
		int class = strcmp(rom->class, r->class);
		if (class < 0 || (class == 0 && strcmp(rom->name, r->name) < 0))
			break;
		prev = r;
	}
	if (prev == NULL) {
		SLIST_INSERT_HEAD(&mii_rom_queue, rom, self);
	} else {
		SLIST_INSERT_AFTER(prev, rom, self);
	}
}

mii_rom_t *
mii_rom_get(
		const char *name)
{
	mii_rom_t *rom;
	// this one is for debug only, so we can pass NULL to get the first one
	if (!name)
		return SLIST_FIRST(&mii_rom_queue);

	SLIST_FOREACH(rom, &mii_rom_queue, self) {
		if (strcmp(rom->name, name) == 0)
			return rom;
	}
	fprintf(stderr, "%s: ROM %s not found\n", __func__, name);
	return NULL;
}

mii_rom_t *
mii_rom_get_class(
		mii_rom_t * prev,
		const char *class)
{
	mii_rom_t *rom;

	// if prev is NULL, we want the first that matches that class
	if (!prev) {
		SLIST_FOREACH(rom, &mii_rom_queue, self) {
			if (strcmp(rom->class, class) == 0)
				return rom;
		}
		fprintf(stderr, "%s: ROM class %s not found\n", __func__, class);
		return NULL;
	}
	// else we want the next one that matches that class.
	// list is sorted by class, so we can just check the next one
	rom = SLIST_NEXT(prev, self);
	if (rom && strcmp(rom->class, class) == 0)
		return rom;
	return NULL;
}
