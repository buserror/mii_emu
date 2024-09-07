/*
 * mui_timer.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>


typedef uint64_t mui_time_t;

struct mui_t;

/*!
 * Timer callback definition. Behaves in a pretty standard way; the timer
 * returns 0 to be cancelled (for one shot timers for example) or return
 * the delay to the next call (that will be added to 'now' to get the next)
 */
typedef mui_time_t (*mui_timer_p)(
				struct mui_t * mui,
				mui_time_t 	now,
				void * 		param);


enum mui_time_e {
	MUI_TIME_RES		= 1,
	MUI_TIME_SECOND		= 1000000,
	MUI_TIME_MS			= (MUI_TIME_SECOND/1000),
};
mui_time_t
mui_get_time();

#define MUI_TIMER_COUNT 	64
#define MUI_TIMER_NONE		0xff

typedef uint8_t mui_timer_id_t;

typedef struct mui_timer_group_t {
	uint64_t 					map;
	struct {
		mui_time_t 					when;
		mui_timer_p 				cb;
		void * 						param;
	} 						timers[MUI_TIMER_COUNT];
} mui_timer_group_t;

/*
 * Register 'cb' to be called after 'delay'. Returns a timer id (0 to 63)
 * or MUI_TIMER_NONE if no timer is available.
 * The timer function cb can return 0 for a one shot timer, or another
 * delay that will be added to the current stamp for a further call
 * of the timer.
 * 'param' will be also passed to the timer callback.
 */
mui_timer_id_t
mui_timer_register(
		struct mui_t *	ui,
		mui_timer_p 	cb,
		void *			param,
		uint32_t 		delay);
/*
 * Reset timer 'id' if 'cb' matches what was registered. Set a new delay,
 * or cancel the timer if delay is 0.
 * Returns the time that was left on the timer, or 0 if the timer was
 * not found.
 */
mui_time_t
mui_timer_reset(
		struct mui_t *	ui,
		mui_timer_id_t 	id,
		mui_timer_p 	cb,
		mui_time_t 		delay);
