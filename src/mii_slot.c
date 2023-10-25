/*
 * mii_slot.c
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

mii_slot_drv_t *
mii_slot_drv_find(
		mii_t *mii,
		const char * name)
{
	mii_slot_drv_t * drv = mii_slot_drv_list;
	while (drv) {
		if (!strcmp(drv->name, name))
			return drv;
		drv = drv->next;
	}
	return NULL;
}

int
mii_slot_drv_register(
		mii_t *mii,
		uint8_t slot_id,
		const char *driver_name)
{
	if (!mii || !driver_name) {
		printf("%s invalid args\n", __func__);
		return -1;
	}
	if (slot_id < 1 || slot_id > 7) {
		printf("%s invalid slot id %d\n", __func__, slot_id);
		return -1;
	}
	if (mii->slot[slot_id - 1].drv) {
		printf("%s slot %d already has a driver (%s)\n",
				__func__, slot_id, mii->slot[slot_id - 1].drv->name);
		return -1;
	}
	mii_slot_drv_t * drv = mii_slot_drv_find(mii, driver_name);
	if (!drv) {
		printf("%s driver %s not found\n", __func__, driver_name);
		return -1;
	}
	if (drv->init) {
		if (drv->init(mii, &mii->slot[slot_id - 1]) != 0) {
			printf("%s driver %s init failed\n", __func__, driver_name);
			return -1;
		}
	}
	mii->slot[slot_id - 1].drv = drv;
	return 0;
}

mii_slot_drv_t *
mii_slot_drv_get(
		mii_t *mii,
		uint8_t slot_id)
{
	if (!mii || slot_id < 1 || slot_id > 7)
		return NULL;
	return (mii_slot_drv_t *)mii->slot[slot_id - 1].drv;
}

int
mii_slot_command(
		mii_t *mii,
		uint8_t slot_id,
		uint8_t cmd,
		void * param)
{
	mii_slot_drv_t * drv = mii_slot_drv_get(mii, slot_id);
	if (!drv)
		return -1;
	if (drv->command)
		return drv->command(mii, &mii->slot[slot_id - 1], cmd, param);
	return -1;
}
