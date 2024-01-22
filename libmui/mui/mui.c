/*
 * mui.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "mui_priv.h"

void
mui_init(
		mui_t *ui)
{
	//memset(ui, 0, sizeof(*ui));
	ui->clear_color = MUI_COLOR(0xccccccff);
	TAILQ_INIT(&ui->windows);
	TAILQ_INIT(&ui->zombies);
	TAILQ_INIT(&ui->fonts);
	mui_font_init(ui);
	pixman_region32_init(&ui->redraw);
	c2_rect_t whole = C2_RECT(0, 0, ui->screen_size.x, ui->screen_size.y);
	pixman_region32_reset(&ui->inval, (pixman_box32_t*)&whole);
}

void
mui_dispose(
		mui_t *ui)
{
	pixman_region32_fini(&ui->inval);
	pixman_region32_fini(&ui->redraw);
	mui_font_dispose(ui);
	mui_window_t *w;
	while ((w = TAILQ_FIRST(&ui->windows))) {
		mui_window_dispose(w);
	}
}

void
mui_draw(
		mui_t *ui,
		mui_drawable_t *dr,
		uint16_t all)
{
	if (!(all || pixman_region32_not_empty(&ui->inval)))
		return;
	if (all) {
	//	printf("%s: all\n", __func__);
		c2_rect_t whole = C2_RECT(0, 0, dr->pix.size.x, dr->pix.size.y);
		pixman_region32_reset(&ui->inval, (pixman_box32_t*)&whole);
	}
	mui_drawable_set_clip(dr, NULL);

	/*
	 * Windows are drawn top to bottom, their area/rectangle is added to the
	 * done region, the done region (any windows that are overlaping others)
	 * is substracted to any other windows update region before drawing...
	 * once all windows are done, the 'done' region (sum of all the windows),
	 * is substracted from the 'desk' area and erased.
	 */
	pixman_region32_t done = {};

	mui_window_t * win;
	TAILQ_FOREACH_REVERSE(win, &ui->windows, windows, self) {
		pixman_region32_intersect_rect(&win->inval, &win->inval,
					win->frame.l, win->frame.t,
					c2_rect_width(&win->frame), c2_rect_height(&win->frame));

		mui_drawable_set_clip(dr, NULL);
		if (!all)
			mui_drawable_clip_push_region(dr, &win->inval);
		else
			mui_drawable_clip_push(dr, &win->frame);
		pixman_region32_clear(&win->inval);

		mui_drawable_clip_substract_region(dr, &done);
		mui_window_draw(win, dr);
	//	printf("  %s : %s\n", win->title, c2_rect_as_str(&win->frame));
		pixman_region32_union_rect(&done, &done,
			win->frame.l, win->frame.t,
			c2_rect_width(&win->frame), c2_rect_height(&win->frame));
	}

	mui_drawable_set_clip(dr, NULL);
	pixman_region32_t sect = {};
	c2_rect_t desk = C2_RECT(0, 0, dr->pix.size.x, dr->pix.size.y);
	pixman_region32_inverse(&sect, &done, (pixman_box32_t*)&desk);

	mui_drawable_clip_push_region(dr, &sect);

	pixman_image_fill_boxes(
			ui->clear_color.value ? PIXMAN_OP_SRC : PIXMAN_OP_CLEAR,
			mui_drawable_get_pixman(dr),
			&PIXMAN_COLOR(ui->clear_color), 1, (pixman_box32_t*)&desk);
	pixman_region32_fini(&sect);
	pixman_region32_fini(&done);

	pixman_region32_union(&ui->redraw, &ui->redraw, &ui->inval);
	pixman_region32_clear(&ui->inval);
	if (ui->draw_debug) {
		// save a png of the current screen
		ui->draw_debug = 0;
		printf("%s: saving debug.png\n", __func__);
	//	mui_drawable_save_to_png(dr, "debug.png");
	}
}

bool
mui_handle_event(
		mui_t *ui,
		mui_event_t *ev)
{
	bool res = false;
	if (!ev->when)
		ev->when = mui_get_time();
	ui->action_active++;
	switch (ev->type) {
		case MUI_EVENT_KEYUP:
		case MUI_EVENT_KEYDOWN: {
			if (ev->modifiers & MUI_MODIFIER_EVENT_TRACE)
				printf("%s modifiers %04x key %x\n", __func__,
							ev->modifiers, ev->key.key);
			mui_window_t *w, *safe;
			TAILQ_FOREACH_REVERSE_SAFE(w, &ui->windows, windows, self, safe) {
				if ((res = mui_window_handle_keyboard(w, ev))) {
					if (ev->modifiers & MUI_MODIFIER_EVENT_TRACE)
						printf("    window:%s handled it\n",
										w->title);
					break;
				}
			}
			if (ev->modifiers & MUI_MODIFIER_EVENT_TRACE)
				if (!res)
					printf("    no window handled it\n");
		}	break;
		case MUI_EVENT_BUTTONUP:
		case MUI_EVENT_BUTTONDOWN:
		case MUI_EVENT_WHEEL:
		case MUI_EVENT_DRAG: {
			if (ev->type == MUI_EVENT_BUTTONDOWN && ev->mouse.button > 1) {
				printf("%s: button %d not handled\n", __func__,
								ev->mouse.button);
				ui->draw_debug++;
				c2_rect_t whole = C2_RECT(0, 0, ui->screen_size.x, ui->screen_size.y);
				pixman_region32_reset(&ui->inval, (pixman_box32_t*)&whole);
			}
			if (ev->modifiers & MUI_MODIFIER_EVENT_TRACE)
				printf("%s %d mouse %d %3dx%3d capture:%s\n", __func__,
								ev->type, ev->mouse.button,
								ev->mouse.where.x, ev->mouse.where.y,
								ui->event_capture ?
										ui->event_capture->title : "(none)");
			if (ui->event_capture) {
				res = mui_window_handle_mouse(ui->event_capture, ev);
				break;
			} else {
				mui_window_t *w, *safe;
				TAILQ_FOREACH_REVERSE_SAFE(w, &ui->windows, windows, self, safe) {
					if ((res = mui_window_handle_mouse(w, ev))) {
						if (ev->modifiers & MUI_MODIFIER_EVENT_TRACE)
							printf("    window:%s handled it\n",
											w->title);
						break;
					}
				}
			}
			if (ev->modifiers & MUI_MODIFIER_EVENT_TRACE)
				if (!res)
					printf("    no window handled it\n");
		}	break;
	}
	ui->action_active--;
	return res;
}

static uint16_t
_mui_simplify_mods(
		uint16_t mods)
{
	uint16_t res = 0;
	if (mods & MUI_MODIFIER_SHIFT)
		res |= MUI_MODIFIER_RSHIFT;
	if (mods & MUI_MODIFIER_CTRL)
		res |= MUI_MODIFIER_RCTRL;
	if (mods & MUI_MODIFIER_ALT)
		res |= MUI_MODIFIER_RALT;
	if (mods & MUI_MODIFIER_SUPER)
		res |= MUI_MODIFIER_RSUPER;
	return res;
}

bool
mui_event_match_key(
		mui_event_t *ev,
		mui_key_equ_t key_equ)
{
	if (ev->type != MUI_EVENT_KEYUP && ev->type != MUI_EVENT_KEYDOWN)
		return false;
	if (toupper(ev->key.key) != toupper(key_equ.key))
		return false;
	if (_mui_simplify_mods(ev->modifiers) != _mui_simplify_mods(key_equ.mod))
		return false;
	return true;
}

uint8_t
mui_timer_register(
		mui_t *ui,
		mui_timer_p cb,
		void *param,
		uint32_t delay)
{
	if (ui->timer.map == (uint64_t)-1) {
		fprintf(stderr, "%s ran out of timers\n", __func__);
		return -1;
	}
	int ti = ffsl(~ui->timer.map) - 1;
	ui->timer.map |= 1 << ti;
	ui->timer.timers[ti].cb = cb;
	ui->timer.timers[ti].param = param;
	ui->timer.timers[ti].when = mui_get_time() + delay;
	return 0;
}

void
mui_timers_run(
		mui_t *ui )
{
	uint64_t now = mui_get_time();
	uint64_t map = ui->timer.map;
	while (map) {
		int ti = ffsl(map) - 1;
		map &= ~(1 << ti);
		if (ui->timer.timers[ti].when > now)
			continue;
		mui_time_t r = ui->timer.timers[ti].cb(
							ui, now,
							ui->timer.timers[ti].param);
		if (r == 0)
			ui->timer.map &= ~(1 << ti);
		else
			ui->timer.timers[ti].when += r;
	}
}

void
_mui_window_free(
		mui_window_t *win);

void
mui_garbage_collect(
		mui_t * ui)
{
	mui_window_t *win, *safe;
	TAILQ_FOREACH_SAFE(win, &ui->zombies, self, safe) {
		TAILQ_REMOVE(&ui->zombies, win, self);
		_mui_window_free(win);
	}
}

void
mui_run(
		mui_t *ui)
{
	mui_timers_run(ui);
	mui_garbage_collect(ui);
}

bool
mui_has_active_windows(
		mui_t *ui)
{
	mui_window_t *win;
	TAILQ_FOREACH(win, &ui->windows, self) {
		if (mui_menubar_window(win) || win->flags.hidden)
			continue;
		return true;
	}
	return false;
}
