/*
 * button.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_window.h>


enum mui_button_style_e {
	MUI_BUTTON_STYLE_NORMAL = 0,
	MUI_BUTTON_STYLE_DEFAULT = 1,
	MUI_BUTTON_STYLE_RADIO,
	MUI_BUTTON_STYLE_CHECKBOX,
};

mui_control_t *
mui_button_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		uint8_t			style,	// one of mui_button_style_e
		const char *	title,
		uint32_t 		uid );
// "align" is not implemented yet
void
mui_button_set_icon(
		mui_control_t * c,
		const char * icon,
		mui_text_e align );
