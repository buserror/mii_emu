/*
 * drawable.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_window.h>


/* Drawable control is just an offscreen buffer (icon, pixel view) */
mui_control_t *
mui_drawable_control_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		mui_drawable_t * dr,
		mui_drawable_t * mask,
		uint16_t 		flags);
mui_drawable_t *
mui_drawable_control_get_drawable(
		mui_control_t * c);
