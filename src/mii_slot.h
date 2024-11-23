/*
 * mii_slot.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

typedef struct mii_slot_t {
	uint8_t				aux_rom_selected: 1, id;
	void *				drv_priv;			// for driver use
	const struct mii_slot_drv_t * drv;
} mii_slot_t;


typedef struct mii_slot_drv_t {
	struct mii_slot_drv_t *next; // in global driver queue
	uint32_t enable_flag;		// if there is a MII_INIT_xxx flag
	const char * name;
	const char * desc;
	int (*probe)(
			struct mii_t * mii,
			uint32_t flags);
	int (*init)(
			struct mii_t * mii,
			struct mii_slot_t *slot);
	/* optional */
	void (*dispose)(
			struct mii_t * mii,
			struct mii_slot_t *slot);
	/* optional */
	void (*reset)(
			struct mii_t * mii,
			struct mii_slot_t *slot);
	// access to the slot's soft switches.
	uint8_t (*access)(
			struct mii_t * mii,
			struct mii_slot_t *slot,
			uint16_t addr, uint8_t data, bool write);
	// arbitrary command for load/save etc	/* optional */
	int (*command)(
			struct mii_t * mii,
			struct mii_slot_t *slot,
			uint32_t cmd,
			void * param);
} mii_slot_drv_t;

// get driver installed in slot_id
mii_slot_drv_t *
mii_slot_drv_get(
		struct mii_t *mii,
		uint8_t slot_id);
// install driver 'driver_name' in slot_id slot
int
mii_slot_drv_register(
		struct mii_t *mii,
		uint8_t slot_id,
		const char *driver_name);
// find a driver 'name' in the global list
mii_slot_drv_t *
mii_slot_drv_find(
		struct mii_t *mii,
		const char * name);

enum {
	MII_SLOT_DRIVE_COUNT 	= 0x01,
	MII_SLOT_DRIVE_LOAD		= 0x20, // + drive index 0...n
	MII_SLOT_DRIVE_WP		= 0x30, // + drive index 0...n

	MII_SLOT_SSC_SET_TTY	= 0x10, // param is a mii_ssc_setconf_t
	MII_SLOT_SSC_GET_TTY	= 0x11, // param is a mii_ssc_setconf_t
	// + drive index 0..1. Param is a mii_floppy_t **
	MII_SLOT_D2_GET_FLOPPY	= 0x40,
};

// send a command to a slot/driver. Return >=0 if ok, -1 if error
int
mii_slot_command(
		struct mii_t *mii,
		uint8_t slot_id,
		uint8_t cmd,
		void * param);
