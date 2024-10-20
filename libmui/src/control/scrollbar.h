/*
 * scrollbar.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_control.h>

/*
 * Page step and line step are optional, they default to '30' pixels and the
 * 'visible' area of the scrollbar, respectively.
 *
 * If you want to -for example- have a scrollbar that scrolls by 5 when you
 * click the arrows, and by 20 when you click the bar, you would set the
 * line_step to 5, and the page_step to 20.
 */
mui_control_t *
mui_scrollbar_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		uint32_t 		uid,
		uint32_t 		line_step,
		uint32_t 		page_step);
uint32_t
mui_scrollbar_get_max(
		mui_control_t * c);
void
mui_scrollbar_set_max(
		mui_control_t * c,
		uint32_t 		max);
void
mui_scrollbar_set_page(
		mui_control_t * c,
		uint32_t 		page);

