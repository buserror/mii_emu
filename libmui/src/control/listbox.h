/*
 * listbox.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once


#include <mui/mui_types.h>
#include <mui/mui_control.h>

struct mui_control_t;
struct mui_window_t;
struct mui_listbox_elem_t;

/* This is currently unused */
typedef void (*mui_ldef_p)(
				struct mui_control_t * 	c,
				uint32_t 	elem_index,
				struct mui_listbox_elem_t * elem);


typedef struct mui_listbox_elem_t {
	uint32_t 					disabled : 1;
	// currently this is a UTF8 string using the 'icons' font
	char 						icon[8];	// UTF8 icon
	// default 'LDEF' is to draw the 'elem' string
	void * 						elem; // char * or... ?
} mui_listbox_elem_t;

DECLARE_C_ARRAY(mui_listbox_elem_t, mui_listbox_elems, 2);
IMPLEMENT_C_ARRAY(mui_listbox_elems);


mui_control_t *
mui_listbox_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		uint32_t 		uid );
void
mui_listbox_prepare(
		mui_control_t * c);
mui_listbox_elems_t *
mui_listbox_get_elems(
		mui_control_t * c);
