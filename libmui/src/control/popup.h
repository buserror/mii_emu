/*
 * popup.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_control.h>
#include <mui/mui_menu.h>

/*
 * Popup menu control.
 *
 * flags are MUI_TEXT_ALIGN_* -- however this corresponds to the margins
 * of the popup control itself when placed into it's 'frame' -- the
 * popup will be placed left,right,center of the frame rectangle depending
 * on these flags.
 */
mui_control_t *
mui_popupmenu_new(
		mui_window_t *	win,
		c2_rect_t 		frame,
		const char * 	title,
		uint32_t 		uid,
		uint32_t		flags);
mui_menu_items_t *
mui_popupmenu_get_items(
		mui_control_t * c);
void
mui_popupmenu_prepare(
		mui_control_t * c);
