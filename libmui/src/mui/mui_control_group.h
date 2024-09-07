/*
 * mui_control_group.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>

struct mui_control_t;

/*
 * The control group is a a list of controls, the subtelety is that
 * there is a list of groups as well, so that controls can be grouped
 * together, and hidden or shown as a group.
 * This is mostly done to allow for 'tab' controls, where the controls
 * are grouped together, and only one group is shown at a time.
 * For most windows, there is only one group, and all controls are
 * in that group.
 * The 'hidden' flag is used to hide the whole group, and all the controls
 * in it.
 * This API allows iterating over all the controls in a group, or all
 * the controls in all the groups. The iterator allows to skip hidden
 * controls, or to walk all controls.
 */
struct mui_control_t;
typedef struct mui_control_group_t {
	struct {
		uint hidden : 1;
	} flags;
	TAILQ_ENTRY(mui_control_group_t) self;
	TAILQ_HEAD(controls, mui_control_t) controls;
} mui_control_group_t;

typedef struct mui_controls_t {
	TAILQ_HEAD(list, mui_control_group_t) controls;
} mui_controls_t;

void
mui_controls_init(
		mui_controls_t * group_list);
// Return the 'current' group; this is more of a placeholder as it
// always return the 'last' group in the list.
struct mui_control_group_t *
mui_controls_current_group(
		mui_controls_t * group_list);
typedef enum mui_controls_flags_e {
	// only return visible controls
	MUI_CONTROLS_VISIBLE 	= 0,
	// return all controls, regardless of visibility
	MUI_CONTROLS_ALL 		= 1,
} mui_controls_flags_e;

// Initializes a control group, optionaly (if 'attach' is not NULL) attach
// it to the 'attach' group.
void
mui_control_group_init(
		struct mui_control_group_t * group,
		mui_controls_t * attach);
struct mui_control_t *
mui_control_group_first(
		struct mui_control_group_t * group,
		mui_controls_flags_e flags);
struct mui_control_t *
mui_control_group_last(
		struct mui_control_group_t * group,
		mui_controls_flags_e flags);
struct mui_control_t *
mui_control_group_next(
		struct mui_control_t * c,
		mui_controls_flags_e flags);
struct mui_control_t *
mui_control_group_prev(
		struct mui_control_t * c,
		mui_controls_flags_e flags);

// return first control in the list of groups.
struct mui_control_t *
mui_controls_first(
		mui_controls_t * group_list,
		mui_controls_flags_e flags);
// return last control in the list of groups. Only return visible controls
// if 'all' is false
struct mui_control_t *
mui_controls_last(
		mui_controls_t * group_list,
		mui_controls_flags_e flags);
/*
 * Return the next control relative to 'c'; it can be in the same group
 * or any following groups.
 */
struct mui_control_t *
mui_controls_next(
		struct mui_control_t * c,
		mui_controls_flags_e flags);
/* Return the previous control relative to 'c'; it can be in the same group
 * or any previous groups.
 */
struct mui_control_t *
mui_controls_prev(
		struct mui_control_t * control,
		mui_controls_flags_e flags);
