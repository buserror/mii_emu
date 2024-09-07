/*
 * textedit.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_control.h>
#include <control/box.h>



/*
 * Text editor control
 */
enum {
	// do we handle multi-line text? If zero, we only handle one line
	MUI_CONTROL_TEXTEDIT_VERTICAL	= 1 << (MUI_CONTROL_TEXTBOX_FLAGS_COUNT+1),
	MUI_CONTROL_TEXTEDIT_FLAGS_COUNT = (MUI_CONTROL_TEXTBOX_FLAGS_COUNT+1),
};

mui_control_t *
mui_textedit_control_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		uint32_t 		flags);
void
mui_textedit_set_text(
		mui_control_t * c,
		const char * text);
void
mui_textedit_set_selection(
		mui_control_t * c,
		uint			start,
		uint			end);
/*
 * Get current selection
 */
void
mui_textedit_get_selection(
		mui_control_t * 			c,
		uint * 						glyph_start,
		uint * 						glyph_end);
uint
mui_textedit_get_text(
		mui_control_t * c,
		char *	 		text,
		uint  			len);