/*
 * mui_control_group.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>

#include "mui.h"
#include "mui_priv.h"


void
mui_controls_init(
		mui_controls_t * group)
{
	TAILQ_INIT(&group->controls);
}

void
mui_control_group_init(
		struct mui_control_group_t * group,
		mui_controls_t * attach)
{
	group->flags.hidden = false;
	TAILQ_INIT(&group->controls);
	if (attach)
		TAILQ_INSERT_TAIL(&attach->controls, group, self);
}

struct mui_control_t *
mui_control_group_first(
		struct mui_control_group_t * group,
		mui_controls_flags_e flags)
{
	if (!group)
		return NULL;
	bool all = flags == MUI_CONTROLS_ALL;
	struct mui_control_t * c = TAILQ_FIRST(&group->controls);
	while (c) {
		if (all || !c->flags.hidden)
			return c;
		c = TAILQ_NEXT(c, self);
	}
	return NULL;
}

struct mui_control_t *
mui_control_group_last(
		struct mui_control_group_t * group,
		mui_controls_flags_e flags)
{
	if (!group)
		return NULL;
	bool all = flags == MUI_CONTROLS_ALL;
	struct mui_control_t * c = TAILQ_LAST(&group->controls, controls);
	while (c) {
		if (all || !c->flags.hidden)
			return c;
		c = TAILQ_PREV(c, controls, self);
	}
	return NULL;
}

struct mui_control_t *
mui_control_group_next(
		struct mui_control_t * c,
		mui_controls_flags_e flags)
{
	if (!c)
		return NULL;
	bool all = flags == MUI_CONTROLS_ALL;
	struct mui_control_t * next = TAILQ_NEXT(c, self);
	while (next) {
		if (all || !next->flags.hidden)
			return next;
		next = TAILQ_NEXT(next, self);
	}
	return NULL;
}

struct mui_control_t *
mui_control_group_prev(
		struct mui_control_t * c,
		mui_controls_flags_e flags)
{
	if (!c)
		return NULL;
	bool all = flags == MUI_CONTROLS_ALL;
	struct mui_control_t * prev = TAILQ_PREV(c, controls, self);
	while (prev) {
		if (all || !prev->flags.hidden)
			return prev;
		prev = TAILQ_PREV(prev, controls, self);
	}
	return NULL;
}

struct mui_control_t *
mui_controls_first(
		mui_controls_t * group,
		mui_controls_flags_e flags)
{
	if (!group)
		return NULL;
	bool all = flags == MUI_CONTROLS_ALL;
	struct mui_control_group_t * g;
	TAILQ_FOREACH(g, &group->controls, self) {
		if (!all && g->flags.hidden)
			continue;
		mui_control_t * c = mui_control_group_first(g, flags);
		if (c)
			return c;
	}
	return NULL;
}

struct mui_control_t *
mui_controls_last(
		mui_controls_t * group,
		mui_controls_flags_e flags)
{
	if (!group)
		return NULL;
	bool all = flags == MUI_CONTROLS_ALL;
	struct mui_control_group_t * g;
	TAILQ_FOREACH_REVERSE(g, &group->controls, list, self) {
		if (!all && g->flags.hidden)
			continue;
		mui_control_t * c = mui_control_group_last(g, flags);
		if (c)
			return c;
	}
	return NULL;
}

struct mui_control_group_t *
mui_controls_current_group(
		mui_controls_t * group)
{
	if (!group)
		return NULL;
	return TAILQ_LAST(&group->controls, list);
}

struct mui_control_t *
mui_controls_next(
		struct mui_control_t * control,
		mui_controls_flags_e flags)
{
	if (!control)
		return NULL;
	struct mui_control_group_t * g = control->group;
	if (!g)
		return NULL;
	struct mui_control_t * c = mui_control_group_next(control, flags);
	if (c)
		return c;
	do {
		g = TAILQ_NEXT(g, self);
		c = mui_control_group_first(g, flags);
		if (c)
			return c;
	} while (g);
	return NULL;
}

struct mui_control_t *
mui_controls_prev(
		struct mui_control_t * control,
		mui_controls_flags_e flags)
{
	if (!control)
		return NULL;
	struct mui_control_group_t * g = control->group;
	if (!g)
		return NULL;
	struct mui_control_t * c = TAILQ_PREV(control, controls, self);
	if (c)
		return c;
	do {
		g = TAILQ_PREV(g, list, self);
		c = mui_control_group_last(g, flags);
		if (c)
			return c;
	} while (g);
	return NULL;
}
