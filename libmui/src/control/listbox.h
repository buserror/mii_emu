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


typedef struct mui_listbox_elem_t {
	uint32_t 					disabled : 1;
	// currently this is a UTF8 string using the 'icons' font
	char 						icon[8];	// UTF8 icon
	// default 'LDEF' is to draw the 'elem' string using normal font
	void * 						elem; // char * or... ?
} mui_listbox_elem_t;

DECLARE_C_ARRAY(mui_listbox_elem_t, mui_listbox_elems, 2);
IMPLEMENT_C_ARRAY(mui_listbox_elems);

enum mui_ldef_op_e {
	/*
	 * Initialize the LDEF, the 'ldef_param' is passed in, and the
	 */
	MUI_LDEF_OP_INIT,
	/*
	 * Allow the LDEF to dispose of ldef_param, if applicable
	 */
	MUI_LDEF_OP_DISPOSE,
	MUI_LDEF_OP_DRAW,
	MUI_LDEF_OP_GET_TEXT,
	MUI_LDEF_OP_EVENT,
	MUI_LDEF_OP_COUNT,
};

/* This is currently unused */
typedef void (*mui_ldef_p)(
		mui_control_t * 	c,
		enum mui_ldef_op_e 	op,
		void *				ldef_param,
		uint32_t 			elem_index,
		mui_listbox_elem_t * elem,
		void * 				io_result );


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
void
mui_listbox_set_ldef(
		mui_control_t * c,
		mui_ldef_p 		ldef,
		void * 			ldef_param);
