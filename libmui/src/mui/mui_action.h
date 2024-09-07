/*
 * mui_action.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>

/*!
 * Actions are the provided way to add custom response to events for the
 * application; action handlers are called for a variety of things, from clicks
 * in controls, to menu selections, to window close etc.
 *
 * The 'what' parameter is a 4 character code, that can be used to identify
 * the action, and the 'param' is a pointer to a structure that depends on
 * the 'what' action (hopefully documented with that action constant)
 *
 * the 'cb_param' is specific to this action function pointer and is passed as
 * is to the callback, this is the pointer you pass to mui_window_add_action()
 */
typedef int (*mui_window_action_p)(
				struct mui_window_t * win,
				void * 		cb_param,
				uint32_t 	what,
				void * 		param);
typedef int (*mui_control_action_p)(
				struct mui_control_t *c,
				void * 		cb_param,
				uint32_t 	what,
				void * 		param);
/*
 * This is a standardized way of installing 'action' handlers onto windows
 * and controls. The 'current' field is used to prevent re-entrance. This structure
 * is opaque and is not accessible by the application, typically.
 */
typedef struct mui_action_t {
	STAILQ_ENTRY(mui_action_t) 	self;
	uint32_t 					current; // prevents re-entrance
	union {
		mui_window_action_p 	window_cb;
		mui_control_action_p 	control_cb;
	};
	void *						cb_param;
} mui_action_t;

typedef STAILQ_HEAD(, mui_action_t) mui_action_queue_t;