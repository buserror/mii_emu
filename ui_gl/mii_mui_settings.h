/*
 * mii_settings.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This file defines the configuration data model for the emulator, it is
 * made so it can be included without having to include the emulator's
 * own headers.
 * This way the dialog/ui can still be tested without having to link
 * against the emulator, using the mui_playground test.
 */
#pragma once

#include <stdint.h>

#include "mui.h"

#define MII_PATH_SIZE_MAX 256

typedef struct mii_config_line_t {
	unsigned int 					ignore : 1,
									section : 1,
									number; // line number in file
	char * 							key;
	char * 							value;
	char 							line[];
} mii_config_line_t;

DECLARE_C_ARRAY(mii_config_line_t*, mii_config_array, 16);

typedef struct mii_config_file_t {
	char * 					path;
	mii_config_array_t		line;
} mii_config_file_t;

typedef struct mii_drive_conf_t {
	unsigned long 			wp : 1, ro_file : 1, ro_format : 1,
							flags;
	char 					disk[MII_PATH_SIZE_MAX];
} mii_drive_conf_t;

typedef struct mii_1mb_conf_t {
	unsigned long 			slot_id : 3,
							use_default : 1,
							reserved;
	mii_drive_conf_t 		drive;
} mii_1mb_conf_t;

typedef struct mii_2dsk_conf_t {
	unsigned long 			slot_id : 3, reserved;
	mii_drive_conf_t 		drive[2];
} mii_2dsk_conf_t;

enum {
	MII_SSC_KIND_DEVICE = 0,
	MII_SSC_KIND_PTY,
	MII_SSC_KIND_SOCKET,
};

typedef struct mii_ssc_conf_t {
	uint32_t 				slot_id : 3,
							kind : 3, // device, pty, socket
							hw_handshake : 1;
	int 					socket_port;
	char 					device[MII_PATH_SIZE_MAX];
	// rom/card configuration
	unsigned int 			baud, bits, parity, stop;
} mii_ssc_conf_t;

typedef struct mii_joystick_conf_t {
	// /dev/input/js0 if empty
	char 					device[MII_PATH_SIZE_MAX];
	unsigned int 			buttons[2];
	unsigned int 			axes[2];
} mii_joystick_conf_t;

typedef struct mii_loadbin_conf_t {
	uint16_t 				active: 1, auto_reload : 1;
	uint16_t 				bank;		// unused for now
	uint16_t 				addr;		// address in bank
	char 					path[MII_PATH_SIZE_MAX];
} mii_loadbin_conf_t;

enum mii_mui_driver_e {
	MII_SLOT_DRIVER_NONE = 0,
	MII_SLOT_DRIVER_SMARTPORT,
	MII_SLOT_DRIVER_DISK2,
	MII_SLOT_DRIVER_MOUSE,
	MII_SLOT_DRIVER_SSC,
	MII_SLOT_DRIVER_ROM1MB,
	MII_SLOT_DRIVER_MOCKINGBOARD,
#ifdef MII_DANII
	MII_SLOT_DRIVER_DANII,
#endif
	MII_SLOT_DRIVER_COUNT
};

typedef struct mii_slot_driver_t {
	const char * driver;
	const char * label;
	const char * description;
} mii_slot_driver_t;

extern const mii_slot_driver_t mii_slot_driver[MII_SLOT_DRIVER_COUNT];

// This is obviously not made to be terribly compact.
typedef struct mii_machine_config_t {
	uint32_t				reboot_on_load : 1,
							load_defaults : 1,
							audio_muted : 1,
							no_slot_clock : 1,
							titan_accelerator : 1;
	uint32_t				video_mode;
	float					audio_volume;
	mii_joystick_conf_t 	joystick;
	mii_loadbin_conf_t		loadbin;
	struct {
		uint16_t				driver;
		union {
			mii_2dsk_conf_t 		smartport;
			mii_2dsk_conf_t 		disk2;
			mii_1mb_conf_t	 		rom1mb;
			mii_ssc_conf_t 			ssc;
		} conf;
	}						slot[7];
} mii_machine_config_t;

struct mui_window_t;
struct mui_t;

/*
 * These are passed to the window action proc when the dialogs have
 * been validated and closed. The matching config structures have been
 * updated and can be used to update the emulator's configuration.
 */
enum mii_mui_dialog_e {
	MII_MUI_SLOTS_SAVE 		= FCC('S','L','O','T'),
	MII_MUI_LOADBIN_SAVE 	= FCC('B','I','N',' '),
	MII_MUI_1MB_SAVE 		= FCC('1','M','B',' '),
	MII_MUI_DISK2_SAVE 		= FCC('2','D','S','K'),
	MII_MUI_SMARTPORT_SAVE 	= FCC('S','M','P','T'),
	MII_MUI_SSC_SAVE 		= FCC('S','S','C',' '),
};

struct mui_window_t *
mii_mui_configure_slots(
		struct mui_t *mui,
		mii_machine_config_t *config);
mui_window_t *
mii_mui_configure_slot(
		struct mui_t *mui,
		mii_machine_config_t *config,
		int slot_id);

struct mui_window_t *
mii_mui_load_binary(
		struct mui_t *mui,
		mii_loadbin_conf_t *config);
struct mui_window_t *
mii_mui_load_1mbrom(
		struct mui_t *mui,
		mii_1mb_conf_t *config);
struct mui_window_t *
mii_mui_about(
		struct mui_t *mui );

enum mii_mui_2dsk_e {
	MII_2DSK_DISKII = 0,
	MII_2DSK_SMARTPORT,
};

struct mui_window_t *
mii_mui_load_2dsk(
		struct mui_t *mui,
		mii_2dsk_conf_t *config,
		uint8_t drive_kind);
struct mui_window_t *
mii_mui_configure_ssc(
		struct mui_t *mui,
		mii_ssc_conf_t *config);

/*
 * Config file related
 */
mii_config_line_t *
mii_config_get_section(
	mii_config_file_t *		cf,
	const char * 			section,
	bool 					add );
mii_config_line_t *
mii_config_get(
	mii_config_file_t *		cf,
	mii_config_line_t *		section,
	const char * 			key);
mii_config_line_t *
mii_config_set(
	mii_config_file_t *		cf,
	mii_config_line_t *		section,
	const char * 			key,
	const char * 			value);
int
mii_settings_save(
	mii_config_file_t *		cf);
int
mii_settings_load(
	mii_config_file_t *		cf,
	const char * 			path,
	const char * 			file );
int
mii_emu_save(
	mii_config_file_t *		cf,
	mii_machine_config_t *	config );
int
mii_emu_load(
	mii_config_file_t *		cf,
	mii_machine_config_t *	config );
