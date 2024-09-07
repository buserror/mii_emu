/*
 * mui_control.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_event.h>
#include <mui/mui_control_group.h>
#include <mui/mui_ref.h>
#include <mui/mui_window.h>


enum mui_cdef_e {
	MUI_CDEF_INIT = 0,	// param is NULL
	MUI_CDEF_DISPOSE,	// param is NULL
	MUI_CDEF_DRAW,		// param is mui_drawable_t*
	MUI_CDEF_EVENT,		// param is mui_event_t*
	MUI_CDEF_SET_STATE,	// param is int*
	MUI_CDEF_SET_VALUE,	// param is int*
	MUI_CDEF_SET_FRAME,	// param is c2_rect_t*
	MUI_CDEF_SET_TITLE,	// param is char * (utf8)
	// Used when hot-key is pressed, change control value
	// to simulate a click
	MUI_CDEF_SELECT,
	// used when a window is selected, to set the focus to the
	// first control that can accept it
	MUI_CDEF_FOCUS,	// param is int* with 0,1
	MUI_CDEF_CAN_FOCUS,// param is NULL, return true or false
};
typedef bool (*mui_cdef_p)(
				struct mui_control_t * 	c,
				uint8_t 	what,
				void * 		param);


enum mui_control_state_e {
	MUI_CONTROL_STATE_NORMAL = 0,
	MUI_CONTROL_STATE_HOVER,
	MUI_CONTROL_STATE_CLICKED,
	MUI_CONTROL_STATE_DISABLED,
	MUI_CONTROL_STATE_COUNT
};

enum mui_control_action_e {
	MUI_CONTROL_ACTION_NONE = 0,
	MUI_CONTROL_ACTION_VALUE_CHANGED	= FCC('c','v','a','l'),
	MUI_CONTROL_ACTION_CLICKED			= FCC('c','l','k','d'),
	MUI_CONTROL_ACTION_SELECT			= FCC('c','s','e','l'),
	MUI_CONTROL_ACTION_DOUBLECLICK		= FCC('c','d','c','l'),
};

/*
 * Control record... this are the 'common' fields, most of the controls
 * have their own 'extended' record using their own fields.
 */
typedef struct mui_control_t {
	TAILQ_ENTRY(mui_control_t) 	self;
	mui_control_group_t *		group; 	// group we belong to
	struct mui_window_t *		win;
	mui_refqueue_t				refs;
	mui_control_ref_t 			lock;
	mui_cdef_p 					cdef;
	uint32_t					state;
	uint32_t 					type;
	uint32_t					style;
	struct {
		uint		 			hidden : 1, // combined with group->hidden
								hit_part : 8;
	}							flags;
	uint32_t					value;
	uint32_t 					uid;
	uint32_t 					uid_mask;		// for radio buttons
	c2_rect_t					frame;
	mui_key_equ_t				key_equ; // keystroke to select this control
	char *						title;
	mui_action_queue_t			actions;
} mui_control_t;

/*
 * Control related
 */
/*
 * This is the 'low level' control creation function, you can pass the
 * 'cdef' function pointer and a control 'type' that will be passed to it,
 * so you can implement variants of controls.
 * The instance_size is the size of the extended control record, if any.
 */
mui_control_t *
mui_control_new(
		mui_window_t * 	win,
		uint32_t 		type,	// specific to the CDEF
		mui_cdef_p 		cdef,
		c2_rect_t 		frame,
		const char *	title,
		uint32_t 		uid,
		uint32_t 		instance_size );
void
mui_control_dispose(
		mui_control_t * c );
uint32_t
mui_control_get_type(
		mui_control_t * c );
uint32_t
mui_control_get_uid(
		mui_control_t * c );
mui_control_t *
mui_control_locate(
		mui_window_t * 	win,
		c2_pt_t 		pt );
mui_control_t *
mui_control_get_by_id(
		mui_window_t * 	win,
		uint32_t 		uid );
void
mui_control_inval(
		mui_control_t * c );
void
mui_control_set_frame(
		mui_control_t * c,
		c2_rect_t *		frame );
void
mui_control_action(
		mui_control_t * c,
		uint32_t 		what,
		void * 			param );
void
mui_control_set_action(
		mui_control_t * c,
		mui_control_action_p	cb,
		void * 			param );
void
mui_control_set_state(
		mui_control_t * c,
		uint32_t 		state );
uint32_t
mui_control_get_state(
		mui_control_t * c );

int32_t
mui_control_get_value(
		mui_control_t * c);
int32_t
mui_control_set_value(
		mui_control_t * c,
		int32_t 		selected);
const char *
mui_control_get_title(
		mui_control_t * c );
void
mui_control_set_title(
		mui_control_t * c,
		const char * 	text );
/* Sets the focus to control 'c' in that window, return true if that
 * control was able to take the focus, or false if it wasn't (for example any
 * control that are not focusable will return false)
 */
bool
mui_control_set_focus(
		mui_control_t * c );
/* Returns true if the control has the focus */
bool
mui_control_has_focus(
		mui_control_t * c );
/* Switch focus to the next/previous control in the window */
mui_control_t *
mui_control_switch_focus(
		mui_window_t * win,
		int 			dir );
