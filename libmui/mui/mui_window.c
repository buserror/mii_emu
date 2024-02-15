/*
 * mui_window.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>

#include "mui_priv.h"
#include "cg.h"

enum mui_window_part_e {
	MUI_WINDOW_PART_NONE = 0,
	MUI_WINDOW_PART_CONTENT,
	MUI_WINDOW_PART_TITLE,
	MUI_WINDOW_PART_FRAME,
	MUI_WINDOW_PART_COUNT,
};

static void
mui_window_update_rects(
		mui_window_t *win,
		mui_font_t * main )
{
	int title_height = main->size;
	c2_rect_t content = win->frame;
	c2_rect_inset(&content, 4, 4);
	content.t += title_height - 1;
	win->content = content;
}

void
mui_titled_window_draw(
		struct mui_t *ui,
		mui_window_t *win,
		mui_drawable_t *dr)
{
	mui_font_t * main = mui_font_find(ui, "main");
	if (!main)
		return;
	mui_window_update_rects(win, main);
	int title_height = main->size;

	struct cg_ctx_t * cg 	= mui_drawable_get_cg(dr);
	mui_color_t frameFill 	= MUI_COLOR(0xbbbbbbff);
	mui_color_t contentFill = MUI_COLOR(0xf0f0f0ff);
	mui_color_t frameColor 	= MUI_COLOR(0x000000ff);
	mui_color_t decoColor 	= MUI_COLOR(0x999999ff);
	mui_color_t titleColor 	= MUI_COLOR(0x000000aa);
	mui_color_t dimTitleColor 	= MUI_COLOR(0x00000055);

	cg_set_line_width(cg, 1);
	cg_rectangle(cg, win->frame.l + 0.5f, win->frame.t + 0.5f,
					c2_rect_width(&win->frame) - 1, c2_rect_height(&win->frame) - 1);
	cg_rectangle(cg, win->content.l + 0.5f, win->content.t + 0.5f,
					c2_rect_width(&win->content) - 1, c2_rect_height(&win->content) - 1);
	cg_set_source_color(cg, &CG_COLOR(frameFill));
	cg_fill_preserve(cg);
	cg_set_source_color(cg, &CG_COLOR(frameColor));
	cg_stroke(cg);

	bool isFront = mui_window_front(ui) == win;
	if (isFront) {
		const int lrMargin = 6;
		const int steps = 6;
		cg_set_line_width(cg, 2);
		for (int i = 1; i < (title_height + 4) / steps; i++) {
			cg_move_to(cg, win->frame.l + lrMargin, win->frame.t + i * steps);
			cg_line_to(cg, win->frame.r - lrMargin, win->frame.t + i * steps);
		}
		cg_set_source_color(cg, &CG_COLOR(decoColor));
		cg_stroke(cg);
	}
	if (win->title) {
		stb_ttc_measure m = {};
		mui_font_text_measure(main, win->title, &m);

		int title_width = m.x1 - m.x0;
		c2_rect_t title = win->frame;
		c2_rect_offset(&title, 0, 1);
		title.b = title.t + title_height;
		title.l += (c2_rect_width(&win->frame) / 2) - (title_width / 2);
		title.r = title.l + title_width;
		if (isFront) {
			c2_rect_t titleBack = title;
			c2_rect_inset(&titleBack, -6, 0);
			cg_round_rectangle(cg, titleBack.l, titleBack.t,
					c2_rect_width(&titleBack), c2_rect_height(&titleBack), 12, 12);
			cg_set_source_color(cg, &CG_COLOR(frameFill));
			cg_fill(cg);
		}
		mui_font_text_draw(main, dr,
				C2_PT(-m.x0 + 1 + title.l, title.t + 0),
				win->title, strlen(win->title),
				isFront ? titleColor : dimTitleColor);
	}
	cg_set_source_color(cg, &CG_COLOR(contentFill));
	cg_rectangle(cg, win->content.l + 0.5f, win->content.t + 0.5f,
					c2_rect_width(&win->content) - 1, c2_rect_height(&win->content) - 1);
	cg_fill(cg);
}

mui_window_t *
mui_window_create(
		struct mui_t *ui,
		c2_rect_t frame,
		mui_wdef_p wdef,
		uint8_t layer,
		const char *title,
		uint32_t instance_size)
{
	mui_window_t * w = calloc(1,
						instance_size >= sizeof(*w) ?
								instance_size : sizeof(*w));
	w->ui = ui;
	w->frame = frame;
	w->title = title ? strdup(title) : NULL;
	w->wdef = wdef;
	w->flags.layer = layer;
	TAILQ_INIT(&w->controls);
	TAILQ_INIT(&w->zombies);
	STAILQ_INIT(&w->actions);
	pixman_region32_init(&w->inval);
	TAILQ_INSERT_HEAD(&ui->windows, w, self);
	mui_window_select(w); // place it in it's own layer
	mui_font_t * main = mui_font_find(ui, "main");
	mui_window_update_rects(w, main);
	mui_window_inval(w, NULL); // just to mark the UI dirty

	return w;
}

void
_mui_control_free(
		mui_control_t * c );
void
_mui_window_free(
		mui_window_t *win)
{
	if (!win)
		return;
	pixman_region32_fini(&win->inval);
	mui_control_t * c;
	while ((c = TAILQ_FIRST(&win->controls))) {
		mui_control_dispose(c);
	}
	while ((c = TAILQ_FIRST(&win->zombies))) {
		TAILQ_REMOVE(&win->zombies, c, self);
		_mui_control_free(c);
	}
	if (win->title)
		free(win->title);
	free(win);
}

void
mui_window_dispose(
		mui_window_t *win)
{
	if (!win)
		return;
	if (win->flags.zombie) {
		printf("%s: DOUBLE delete %s\n", __func__, win->title);
		return;
	}
	bool was_front = mui_window_isfront(win);
	mui_window_action(win, MUI_WINDOW_ACTION_CLOSE, NULL);
	mui_window_inval(win, NULL); // just to mark the UI dirty
	if (win->wdef)
		win->wdef(win, MUI_WDEF_DISPOSE, NULL);
	struct mui_t *ui = win->ui;
	TAILQ_REMOVE(&ui->windows, win, self);
	if (ui->event_capture == win)
		ui->event_capture = NULL;
	if (ui->action_active) {
	//	printf("%s %s is now zombie\n", __func__, win->title);
		win->flags.zombie = true;
		TAILQ_INSERT_TAIL(&ui->zombies, win, self);
	} else
		_mui_window_free(win);
	if (was_front) {
		mui_window_t * front = mui_window_front(ui);
		if (front)
			mui_window_inval(front, NULL);
	}
}

void
mui_window_draw(
		mui_window_t *win,
		mui_drawable_t *dr)
{
	mui_drawable_clip_push(dr, &win->frame);
	if (win->wdef)
		win->wdef(win, MUI_WDEF_DRAW, dr);
	else
		mui_titled_window_draw(win->ui, win, dr);
	struct cg_ctx_t * cg 	= mui_drawable_get_cg(dr);
	cg_save(cg);
//	cg_translate(cg, content.l, content.t);
	mui_control_t * c, *safe;
	TAILQ_FOREACH_SAFE(c, &win->controls, self, safe) {
		mui_control_draw(win, c, dr);
	}
	cg_restore(cg);

	mui_drawable_clip_pop(dr);
}

bool
mui_window_handle_keyboard(
		mui_window_t *win,
		mui_event_t *event)
{
	if (!mui_window_isfront(win))
		return false;
	if (win->wdef && win->wdef(win, MUI_WDEF_EVENT, event)) {
//		printf("%s  %s handled it\n", __func__, win->title);
		return true;
	}
//	printf("%s %s checkint controls\n", __func__, win->title);
	mui_control_t * c, *safe;
	TAILQ_FOREACH_SAFE(c, &win->controls, self, safe) {
		if (mui_control_event(c, event)) {
//			printf("%s control %s handled it\n", __func__, c->title);
			return true;
		}
	}
	return false;
}

bool
mui_window_handle_mouse(
		mui_window_t *win,
		mui_event_t *event)
{
	if (win->wdef && win->wdef(win, MUI_WDEF_EVENT, event))
		return true;
	switch (event->type) {
		case MUI_EVENT_WHEEL: {
			if (!c2_rect_contains_pt(&win->frame, &event->wheel.where))
				return false;
			mui_control_t * c = mui_control_locate(win, event->wheel.where);
			if (!c)
				return false;
			if (c->cdef && c->cdef(c, MUI_CDEF_EVENT, event)) {
				return true;
			}
		}	break;
		case MUI_EVENT_BUTTONDOWN: {
			if (!c2_rect_contains_pt(&win->frame, &event->mouse.where))
				return false;
			mui_control_t * c = mui_control_locate(win, event->mouse.where);
			/* if modifiers like control is down, don't select */
			if (!(event->modifiers & MUI_MODIFIER_CTRL))
				mui_window_select(win);
			if (mui_window_front(win->ui) != win)
				c = NULL;
			if (!c) {
				/* find where we clicked in the window */
				win->ui->event_capture = win;
				win->click_loc = event->mouse.where;
				c2_pt_offset(&win->click_loc, -win->frame.l, -win->frame.t);
				win->flags.hit_part = MUI_WINDOW_PART_FRAME;
				if (event->mouse.where.y < win->content.t)
					win->flags.hit_part = MUI_WINDOW_PART_TITLE;
				else if (c2_rect_contains_pt(&win->content, &event->mouse.where))
					win->flags.hit_part = MUI_WINDOW_PART_CONTENT;
			} else
				win->flags.hit_part = MUI_WINDOW_PART_CONTENT;
			if (c) {
				if (c->cdef && c->cdef(c, MUI_CDEF_EVENT, event)) {
	//			c->state = MUI_CONTROL_STATE_CLICKED;
					win->control_clicked = c;
				}
			}
			return true;
		}	break;
		case MUI_EVENT_DRAG:
			if (win->flags.hit_part == MUI_WINDOW_PART_TITLE) {
				c2_rect_t frame = win->frame;
				c2_rect_offset(&frame,
						-win->frame.l + event->mouse.where.x - win->click_loc.x,
						-win->frame.t + event->mouse.where.y - win->click_loc.y);
				// todo, get that visibel rectangle from somewhere else
				c2_rect_t screen = { .br = win->ui->screen_size };
				screen.t += 35;
				c2_rect_t title_bar = frame;
				title_bar.b = title_bar.t + 35; // TODO fix that
				if (c2_rect_intersect_rect(&title_bar, &screen)) {
					c2_rect_t o;
					c2_rect_clip_rect(&title_bar, &screen, &o);
					if (c2_rect_width(&o) > 10 && c2_rect_height(&o) > 10) {
						mui_window_inval(win, NULL);	// old frame
						win->frame = frame;
						mui_window_inval(win, NULL);	// new frame
					}
				}
			//	mui_window_inval(win, NULL);
				return true;
			}
			if (win->control_clicked) {
				mui_control_t * c = win->control_clicked;
				if (c->cdef && c->cdef(c, MUI_CDEF_EVENT, event)) {
					return true;
				} else
					win->control_clicked = NULL;
			}
			return win->flags.hit_part != MUI_WINDOW_PART_NONE;
			break;
		case MUI_EVENT_BUTTONUP: {
			int part = win->flags.hit_part;
			win->flags.hit_part = MUI_WINDOW_PART_NONE;
			win->ui->event_capture = NULL;
			if (win->control_clicked) {
				mui_control_t * c = win->control_clicked;
				win->control_clicked = NULL;
				if (c->cdef && c->cdef(c, MUI_CDEF_EVENT, event))
					return true;
			}
			return part != MUI_WINDOW_PART_NONE;
		}	break;
		case MUI_EVENT_MOUSEENTER:
		case MUI_EVENT_MOUSELEAVE:
			break;
	}
//	printf("MOUSE %s button %d\n", __func__, event->mouse.button);
//	printf("MOUSE %s %s\n", __func__, c->title);
	return false;
}

void
mui_window_inval(
		mui_window_t *win,
		c2_rect_t * r)
{
	if (!win)
		return;
	c2_rect_t frame = win->frame;
	c2_rect_t forward = {};

	if (!r) {
	//	printf("%s %s inval %s (whole)\n", __func__, win->title, c2_rect_as_str(&frame));
		pixman_region32_reset(&win->inval, (pixman_box32_t*)&frame);
		forward = frame;

		mui_window_t * w, *save;
		TAILQ_FOREACH_SAFE(w, &win->ui->windows, self, save) {
			if (w == win || !c2_rect_intersect_rect(&w->frame, &forward))
				continue;
			pixman_region32_union_rect(&w->inval, &w->inval,
				forward.l, forward.t,
				c2_rect_width(&forward), c2_rect_height(&forward));
		}
	} else {
		c2_rect_t local = *r;
		c2_rect_offset(&local, win->content.l, win->content.t);
		forward = local;

		pixman_region32_union_rect(&win->inval, &win->inval,
			forward.l, forward.t,
			c2_rect_width(&forward), c2_rect_height(&forward));
	}
	if (c2_rect_isempty(&forward))
		return;
	pixman_region32_union_rect(&win->ui->inval, &win->ui->inval,
			forward.l, forward.t,
			c2_rect_width(&forward), c2_rect_height(&forward));
}

mui_window_t *
mui_window_front(
		struct mui_t *ui)
{
	if (!ui)
		return NULL;
	mui_window_t * w, *save;
	TAILQ_FOREACH_REVERSE_SAFE(w, &ui->windows, windows, self, save) {
		if (w->flags.hidden)
			continue;
		if (w->flags.layer < MUI_WINDOW_MENUBAR_LAYER)
			return w;
	}
	return NULL;
}

bool
mui_window_isfront(
		mui_window_t *win)
{
	if (!win)
		return NULL;
	mui_window_t * next = TAILQ_NEXT(win, self);
	while (next && next->flags.hidden)
		next = TAILQ_NEXT(next, self);
	if (!next)
		return true;
	if (next->flags.layer > win->flags.layer)
		return true;
	return false;
}

bool
mui_window_select(
		mui_window_t *win)
{
	bool res = false;
	if (!win)
		return false;
	mui_window_t *last = NULL;
	if (mui_window_isfront(win))
		goto done;
	res = true;
	mui_window_inval(win, NULL);
	TAILQ_REMOVE(&win->ui->windows, win, self);
	mui_window_t *w, *save;
	TAILQ_FOREACH_SAFE(w, &win->ui->windows, self, save) {
		if (w->flags.layer > win->flags.layer) {
			TAILQ_INSERT_BEFORE(w, win, self);
			goto done;
		}
		last = w;
	}
	TAILQ_INSERT_TAIL(&win->ui->windows, win, self);
done:
	if (last) // we are deselecting this one, so redraw it
		mui_window_inval(last, NULL);
#if 0
	printf("%s %s res:%d stack is now:\n", __func__, win->title, res);
	TAILQ_FOREACH(w, &win->ui->windows, self) {
		printf("  L:%2d T:%s\n", w->flags.layer, w->title);
	}
#endif
	return res;
}

void
mui_window_action(
		mui_window_t * 	win,
		uint32_t 		what,
		void * 			param )
{
	if (!win)
		return;
	win->ui->action_active++;
	mui_action_t *a;
	STAILQ_FOREACH(a, &win->actions, self) {
		if (!a->window_cb)
			continue;
		a->window_cb(win, a->cb_param, what, param);
	}
	win->ui->action_active--;
}

void
mui_window_set_action(
		mui_window_t * 	win,
		mui_window_action_p 	cb,
		void * 			param )
{
	if (!win || !cb)
		return;

	mui_action_t *a;
	STAILQ_FOREACH(a, &win->actions, self) {
		if (a->window_cb == cb && a->cb_param == param)
			return;
	}
	a = calloc(1, sizeof(*a));
	a->window_cb = cb;
	a->cb_param = param;
	STAILQ_INSERT_TAIL(&win->actions, a, self);
}

mui_window_t *
mui_window_get_by_id(
		struct mui_t *ui,
		uint32_t uid )
{
	mui_window_t *w;
	TAILQ_FOREACH(w, &ui->windows, self) {
		if (w->uid == uid)
			return w;
	}
	return NULL;
}

void
mui_window_set_id(
		mui_window_t *win,
		uint32_t uid)
{
	if (!win)
		return;
	win->uid = uid;
}
