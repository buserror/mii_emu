/*
 * mui_controls.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>

#include "mui.h"


const mui_control_color_t mui_control_color[MUI_CONTROL_STATE_COUNT] = {
	[MUI_CONTROL_STATE_NORMAL] = {
		.fill = MUI_COLOR(0xeeeeeeff),
		.frame = MUI_COLOR(0x000000ff),
		.text = MUI_COLOR(0x000000ff),
	},
	[MUI_CONTROL_STATE_HOVER] = {
		.fill = MUI_COLOR(0xaaaaaaff),
		.frame = MUI_COLOR(0x000000ff),
		.text = MUI_COLOR(0x0000ffff),
	},
	[MUI_CONTROL_STATE_CLICKED] = {
		.fill = MUI_COLOR(0x777777ff),
		.frame = MUI_COLOR(0x000000ff),
		.text = MUI_COLOR(0xffffffff),
	},
	[MUI_CONTROL_STATE_DISABLED] = {
		.fill = MUI_COLOR(0xeeeeeeff),
		.frame = MUI_COLOR(0x666666ff),
		.text = MUI_COLOR(0xccccccff),
	},
};

void
mui_control_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	if (!c)
		return;
	if (c->cdef)
		c->cdef(c, MUI_CDEF_DRAW, dr);
}

mui_control_t *
mui_control_new(
		mui_window_t * 	win,
		uint32_t 		type,
		mui_cdef_p 		cdef,
		c2_rect_t 		frame,
		const char *	title,
		uint32_t 		uid,
		uint32_t 		instance_size )
{
	if (!win)
		return NULL;
	mui_control_t *c = calloc(1, instance_size >= sizeof(*c) ?
										instance_size : sizeof(*c));
	c->type = type;
	c->cdef = cdef;
	c->frame = frame;
	c->title = title ? strdup(title) : NULL;
	c->win = win;
	c->uid = uid;
	STAILQ_INIT(&c->actions);
	TAILQ_INSERT_TAIL(&win->controls, c, self);
	if (c->cdef)
		c->cdef(c, MUI_CDEF_INIT, NULL);
	return c;
}

void
_mui_control_free(
		mui_control_t * c )
{
	if (!c)
		return;
	if (c->title)
		free(c->title);
	c->title = NULL;
	if (c->cdef)
		c->cdef(c, MUI_CDEF_DISPOSE, NULL);
	free(c);
}

void
mui_control_dispose(
		mui_control_t * c )
{
	if (!c)
		return;
	if (c->flags.zombie) {
		printf("%s: DOUBLE delete %s\n", __func__, c->title);
		return;
	}
	TAILQ_REMOVE(&c->win->controls, c, self);
	if (c->win->flags.zombie || c->win->ui->action_active) {
		c->flags.zombie = true;
		TAILQ_INSERT_TAIL(&c->win->zombies, c, self);
	} else
		_mui_control_free(c);
}

uint32_t
mui_control_get_type(
		mui_control_t * c )
{
	if (!c)
		return 0;
	return c->type;
}
uint32_t
mui_control_get_uid(
		mui_control_t * c )
{
	if (!c)
		return 0;
	return c->uid;
}

mui_control_t *
mui_control_locate(
		mui_window_t * 	win,
		c2_pt_t 		pt )
{
	if (!win)
		return NULL;
	mui_control_t * c;
	TAILQ_FOREACH(c, &win->controls, self) {
		c2_rect_t f = c->frame;
		c2_rect_offset(&f, win->content.l, win->content.t);
		if (c2_rect_contains_pt(&f, &pt))
			return c;
	}
	return NULL;
}

static mui_time_t
_mui_control_highlight_timer_cb(
		struct mui_t * mui,
		mui_time_t now,
		void * param)
{
	mui_control_t * c = param;

	printf("%s: %s\n", __func__, c->title);
	mui_control_set_state(c, MUI_CONTROL_STATE_NORMAL);
	if (c->cdef)
		c->cdef(c, MUI_CDEF_SELECT, NULL);
	mui_control_action(c, MUI_CONTROL_ACTION_SELECT, NULL);

	return 0;
}

int32_t
mui_control_get_value(
		mui_control_t * c)
{
	if (!c)
		return 0;
	return c->value;
}

int32_t
mui_control_set_value(
		mui_control_t * c,
		int32_t 		value)
{
	if (!c)
		return 0;
	if (c->cdef && c->cdef(c, MUI_CDEF_SET_VALUE, &value))
		return c->value;
	if (value != (int)c->value)
		mui_control_inval(c);
	c->value = value;
	return c->value;
}

bool
mui_control_event(
		mui_control_t * c,
		mui_event_t * 	ev )
{
	if (!c)
		return false;
	bool res = false;

	res = c->cdef && c->cdef(c, MUI_CDEF_EVENT, ev);
	if (res || !c->key_equ.key)
		return res;
	switch (ev->type) {
		case MUI_EVENT_KEYDOWN:
			if (c->state != MUI_CONTROL_STATE_DISABLED &&
					mui_event_match_key(ev, c->key_equ)) {
				mui_control_set_state(c, MUI_CONTROL_STATE_CLICKED);
				mui_timer_register(
						c->win->ui, _mui_control_highlight_timer_cb,
						c, MUI_TIME_SECOND / 10);
				res = true;
			}
			break;
	}
	return res;
}

void
mui_control_inval(
		mui_control_t * c )
{
	if (!c)
		return;
	mui_window_inval(c->win, &c->frame);
}

void
mui_control_set_state(
		mui_control_t * c,
		uint32_t 		state )
{
	if (!c)
		return;
	if (c->cdef && c->cdef(c, MUI_CDEF_SET_STATE, &state))
		return;
	if (c->state == state)
		return;
	c->state = state;
	mui_control_inval(c);
}

uint32_t
mui_control_get_state(
		mui_control_t * c )
{
	if (!c)
		return 0;
	return c->state;
}

const char *
mui_control_get_title(
		mui_control_t * c )
{
	if (!c)
		return NULL;
	return c->title;
}

void
mui_control_set_title(
		mui_control_t * c,
		const char * 	text )
{
	if (!c)
		return;
	if (c->cdef && c->cdef(c, MUI_CDEF_SET_TITLE, (void*)text))
		return;
	if (text && c->title && !strcmp(text, c->title))
		return;
	if (c->title)
		free(c->title);
	c->title = text ? strdup(text) : NULL;
	mui_control_inval(c);
}

void
mui_control_action(
		mui_control_t * c,
		uint32_t 		what,
		void * 			param )
{
	if (!c)
		return;
	c->win->ui->action_active++;
	mui_action_t *a;
	STAILQ_FOREACH(a, &c->actions, self) {
		if (!a->control_cb)
			continue;
		a->control_cb(c, a->cb_param, what, param);
	}
	c->win->ui->action_active--;
}

void
mui_control_set_action(
		mui_control_t * c,
		mui_control_action_p 	cb,
		void * 			param )
{
	if (!c)
		return;
	mui_action_t *a = calloc(1, sizeof(*a));
	a->control_cb = cb;
	a->cb_param = param;
	STAILQ_INSERT_TAIL(&c->actions, a, self);
}

mui_control_t *
mui_control_get_by_id(
		mui_window_t * 	win,
		uint32_t 		uid )
{
	if (!win)
		return NULL;
	mui_control_t *c;
	TAILQ_FOREACH(c, &win->controls, self) {
		if (c->uid == uid)
			return c;
	}
	return NULL;
}
