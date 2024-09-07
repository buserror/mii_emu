/*
 * mui_alert.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_window.h>


/*
 * Alert dialog
 */
enum mui_alert_flag_e {
	MUI_ALERT_FLAG_OK 		= (1 << 0),
	MUI_ALERT_FLAG_CANCEL 	= (1 << 1),

	MUI_ALERT_ICON_INFO 	= (1 << 8),

	MUI_ALERT_INFO 			= (MUI_ALERT_FLAG_OK | MUI_ALERT_ICON_INFO),
	MUI_ALERT_WARN 			= (MUI_ALERT_FLAG_OK | MUI_ALERT_FLAG_CANCEL),
};

enum {
	MUI_ALERT_BUTTON_OK		= FCC('o','k',' ',' '),
	MUI_ALERT_BUTTON_CANCEL = FCC('c','a','n','c'),
};

mui_window_t *
mui_alert(
		struct mui_t * 	ui,
		c2_pt_t 		where, // (0,0) will center it
		const char * 	title,
		const char * 	message,
		uint16_t 		flags );
		