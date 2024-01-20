/*
 * mii_mui.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
/*
	This tries to contains a structure that is the MUI interface over the MII
	video, but without any attachment to x11 or opengl. Basically hopefully
	segregating the relevant logic without tying it to a specific windowing system.

	Hopefully with a bit more work this OUGHT to allow Windows/macOS port
	with a native frontend.
 */

#include <stdbool.h>
#include <stdint.h>
#include "mii.h"
#include "mui.h"
#include "mii_mui_settings.h"

enum mii_mui_transition_e {
	MII_MUI_TRANSITION_NONE,
	MII_MUI_TRANSITION_HIDE_UI,
	MII_MUI_TRANSITION_SHOW_UI,
};

typedef struct mii_mui_t {
	mui_t 					mui;		// mui interface
	mii_t 					mii;		// apple II emulator

	bool	 				mui_visible;
	uint8_t					transition;

	mii_machine_config_t	config;
	mii_loadbin_conf_t		loadbin_conf;

	mii_config_file_t		cf;
} mii_mui_t;


void
mii_mui_menus_init(
	mii_mui_t * ui);
void
mii_mui_menu_slot_menu_update(
	mii_mui_t * ui);
// slot can be <= 0 to open the machine dialog instead
void
mii_config_open_slots_dialog(
		mii_mui_t * ui);
