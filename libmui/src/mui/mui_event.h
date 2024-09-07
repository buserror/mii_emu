/*
 * mui_event.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_timer.h>


/*
 * Event description. pretty standard stuff here -- the 'when' field is
 * only used really to detect double clicks so far.
 *
 * Even handlers should return true if the event was handled, (in which case
 * even processing stops for that event) or false to continue passing the even
 * down the chain.
 *
 * Events are passed to the top window first, and then down the chain of
 * windows, until one of them returns true.
 * Implicitely, it means the menubar gets to see the events first, even clicks,
 * even if the click wasn't in the menubar. This is also true of key events of
 * course, which allows the menu to detect key combos, first.
 */
typedef struct mui_event_t {
	mui_event_e 		type;
	mui_time_t 			when;
	mui_modifier_e 		modifiers;
	union {
		struct key {
			uint32_t 		key;	// ASCII or mui_key_e
			bool 			up;
		} 				key;
		struct {
			uint32_t 		button : 4,
							count : 2; // click count
			c2_pt_t 		where;
		} 				mouse;
		struct {
			int32_t 		delta;
			c2_pt_t 		where;
		} 				wheel;
		struct {	// MUI_EVENT_TEXT is of variable size!
			uint32_t 		size;
			uint8_t  		text[0];
		}				text;
	};
} mui_event_t;

/* Just a generic buffer for UTF8 text */
DECLARE_C_ARRAY(uint8_t, mui_utf8, 8);
IMPLEMENT_C_ARRAY(mui_utf8);

/*
 * Key equivalent, used to match key events to menu items
 * Might be extended to controls, right now only the 'key' is checked,
 * mostly for Return and ESC.
 */
typedef union mui_key_equ_t {
	struct {
		uint16_t 		mod;
		uint16_t 		key;
	};
	uint32_t 		value;
} mui_key_equ_t;

#define MUI_KEY_EQU(_mask, _key) \
		(mui_key_equ_t){ .mod = (_mask), .key = (_key) }
