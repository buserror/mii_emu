/*
 * box.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_control.h>
#include <mui/mui_text.h>


/*
 * Create a static text box. Font is optional (default to the system main font),
 * flags corresponds to the MUI_TEXT_ALIGN_* * PLUS the extra(s) listed below.
 */
enum mui_textbox_e {
	// draw the frame around the text box
	MUI_CONTROL_TEXTBOX_FRAME		= (1 << (MUI_TEXT_FLAGS_COUNT+1)),
	MUI_CONTROL_TEXTBOX_FLAGS_COUNT = (MUI_TEXT_FLAGS_COUNT+1),
};
mui_control_t *
mui_textbox_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		const char *	text,
		const char * 	font,
		uint32_t		flags );

mui_control_t *
mui_groupbox_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		const char *	title,
		uint32_t		flags );
mui_control_t *
mui_separator_new(
		mui_window_t * 	win,
		c2_rect_t 		frame);
