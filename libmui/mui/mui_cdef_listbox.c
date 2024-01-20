/*
 * mui_cdef_listbox.c
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

#include "mui.h"
#include "cg.h"

typedef struct mui_listbox_control_t {
	mui_control_t 		control;
	struct mui_control_t * scrollbar;
	int32_t 			scroll;
	uint8_t				elem_height;
	mui_listbox_elems_t	elems;
	mui_ldef_p			ldef;
	// to handle double-click
	mui_time_t			last_click;
} mui_listbox_control_t;

extern const mui_control_color_t mui_control_color[MUI_CONTROL_STATE_COUNT];

static void
mui_listbox_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	c2_rect_t f = c->frame;
	c2_rect_offset(&f, win->content.l, win->content.t);

	struct cg_ctx_t * cg = mui_drawable_get_cg(dr);
	cg_set_line_width(cg, 1);
	cg_set_source_color(cg, &CG_COLOR(mui_control_color[c->state].frame));
	cg_rectangle(cg, f.l, f.t,
					c2_rect_width(&f), c2_rect_height(&f));
	cg_stroke(cg);
	{
		c2_rect_t clip = f;
		c2_rect_inset(&clip, 1, 1);
		mui_drawable_clip_push(dr, &clip);
	}
	mui_listbox_control_t *lb = (mui_listbox_control_t *)c;
	uint32_t top_element = lb->scroll / lb->elem_height;
	uint32_t bottom_element = top_element + 1 +
							(c2_rect_height(&f) / lb->elem_height);
//	printf("%s draw from %d to %d\n", __func__, top_element, bottom_element);

	mui_font_t * icons = mui_font_find(win->ui, "icon_small");
	mui_font_t * main = mui_font_find(win->ui, "main");
	mui_color_t highlight = MUI_COLOR(0xd6fcc0ff);

	for (unsigned int ii = top_element;
					ii < lb->elems.count && ii < bottom_element; ii++) {
		c2_rect_t ef = f;
		ef.b = ef.t + lb->elem_height;
		c2_rect_offset(&ef, 0, ii * lb->elem_height - lb->scroll);
		if (ii == c->value) {
			struct cg_ctx_t * cg = mui_drawable_get_cg(dr);
			cg_set_line_width(cg, 1);
			cg_set_source_color(cg, &CG_COLOR(highlight));
			cg_rectangle(cg, ef.l, ef.t,
							c2_rect_width(&ef), c2_rect_height(&ef));
			cg_fill(cg);
		}
		ef.l += 8;
		mui_listbox_elem_t *e = &lb->elems.e[ii];
		if (lb->elems.e[ii].icon[0])
			mui_font_text_draw(icons, dr, ef.tl, lb->elems.e[ii].icon, 0,
					mui_control_color[e->disabled ?
								MUI_CONTROL_STATE_DISABLED : 0].text);
		ef.l += 26;
		mui_font_text_draw(main, dr, ef.tl, e->elem, 0,
					mui_control_color[e->disabled ?
								MUI_CONTROL_STATE_DISABLED : 0].text);
	}
	mui_drawable_clip_pop(dr);
}

static bool
mui_listbox_key(
		mui_control_t * c,
		mui_event_t * ev)
{
	mui_listbox_control_t *lb = (mui_listbox_control_t *)c;
	printf("%s key: %d\n", __func__, ev->key.key);
	c2_rect_t f = c->frame;
	c2_rect_offset(&f, -f.l, -f.t);
	uint32_t page_size = (c2_rect_height(&f) / lb->elem_height)-1;

	int delta = 0;
	if (ev->modifiers & (MUI_MODIFIER_SUPER | MUI_MODIFIER_CTRL))
		return false;
	switch (ev->key.key) {
		case MUI_KEY_UP:	delta = -1; break;
		case MUI_KEY_DOWN: 	delta = 1;	break;
		case MUI_KEY_PAGEUP:	delta = -page_size; break;
		case MUI_KEY_PAGEDOWN:	delta = page_size; break;
	}
	if (!delta)
		return false;
	int nsel = c->value + delta;
	if (nsel < 0)
		nsel = 0;
	if (nsel >= (int)lb->elems.count)
		nsel = lb->elems.count - 1;
	if (nsel != (int)c->value) {
		c->value = nsel;
		c2_rect_t e = c->frame;
		e.b = e.t + lb->elem_height;

		c2_rect_offset(&e, -e.l,
				-e.t + (c->value * lb->elem_height));
		c2_rect_t w = f;
		c2_rect_offset(&w, 0, lb->scroll);
		printf("  e:%s f:%s\n", c2_rect_as_str(&e), c2_rect_as_str(&w));
		if (e.b > w.b) {
			lb->scroll = (e.b - c2_rect_height(&c->frame));
			printf("   over %d\n", lb->scroll);
		}
		if (e.t < w.t)
			lb->scroll = e.t;
		printf("   scroll:%d\n", lb->scroll);
		mui_control_set_value(lb->scrollbar, lb->scroll);
		mui_control_inval(c);
//		mui_control_inval(lb->scrollbar);
		mui_control_action(c, MUI_CONTROL_ACTION_VALUE_CHANGED,
						&lb->elems.e[nsel]);
		return true;
	}
	return false;
}

static bool
mui_cdef_event(
		struct mui_control_t * 	c,
		mui_event_t *ev)
{
	mui_listbox_control_t *lb = (mui_listbox_control_t *)c;
	switch (ev->type) {
		case MUI_EVENT_BUTTONDOWN: {
			c2_rect_t f = c->frame;
			c2_rect_offset(&f, c->win->content.l, c->win->content.t);
		//	uint32_t page_size = (c2_rect_height(&f) / lb->elem_height)-1;
			int nsel = lb->scroll + (ev->mouse.where.y - f.t);
			nsel /= lb->elem_height;
			if (nsel < 0)
				nsel = 0;
			if (nsel >= (int)lb->elems.count)
				nsel = lb->elems.count - 1;
			if (nsel != (int)c->value) {
				mui_control_set_value(c, nsel);
				mui_control_action(c,
							MUI_CONTROL_ACTION_VALUE_CHANGED,
							&lb->elems.e[nsel]);
			}
			{
				mui_time_t now = mui_get_time();
				if ((now - lb->last_click) <
							(MUI_TIME_MS * 300)) {
					lb->last_click = 0;
					if (lb->elems.e[nsel].disabled)
						return true;
					mui_control_action(c,
							MUI_CONTROL_ACTION_SELECT,
							&lb->elems.e[nsel]);
				} else
					lb->last_click = now;
			}
			return true;
		}	break;
		case MUI_EVENT_KEYUP: {	// ignore keydowns
			if (ev->key.key == 13) {
				if (!lb->elems.e[c->value].disabled) {
					mui_control_action(c,
								MUI_CONTROL_ACTION_SELECT,
								&lb->elems.e[c->value]);
				}
				return true;
			}
			if (mui_listbox_key(c, ev))
				return true;
		}	break;
		case MUI_EVENT_WHEEL: {
		//	printf("%s wheel delta %d\n", __func__, ev->wheel.delta);
			lb->scroll += ev->wheel.delta * 20;
			if (lb->scroll < 0)
				lb->scroll = 0;
			if (lb->scroll >
					(int32_t)((lb->elems.count * lb->elem_height) -
							c2_rect_height(&c->frame)))
				lb->scroll = (lb->elems.count * lb->elem_height) -
									c2_rect_height(&c->frame);
			mui_control_set_value(lb->scrollbar, lb->scroll);
			mui_control_inval(c);
			return true;
		}	break;
	}
	return false;
}

bool
mui_cdef_listbox(
		struct mui_control_t * 	c,
		uint8_t 				what,
		void * 					param)
{
	switch (what) {
		case MUI_CDEF_INIT:
			break;
		case MUI_CDEF_DISPOSE:
			break;
		case MUI_CDEF_DRAW: {
			mui_drawable_t * dr = param;
			mui_listbox_draw(c->win, c, dr);
		}	break;
		case MUI_CDEF_EVENT: {
			mui_event_t *ev = param;
			return mui_cdef_event(c, ev);
		}	break;
	}
	return false;
}

static int
mui_listbox_sbar_action(
		mui_control_t * c,
		void * 			cb_param,
		uint32_t 		what,
		void * 			param)
{
	mui_listbox_control_t *lb = (mui_listbox_control_t *)cb_param;
	lb->scroll = mui_control_get_value(lb->scrollbar);
//	printf("%s scroll %d\n", __func__, lb->scroll);
	mui_control_inval(&lb->control);
	return 0;
}

mui_listbox_elems_t *
mui_listbox_get_elems(
		mui_control_t * c)
{
	mui_listbox_control_t *lb = (mui_listbox_control_t *)c;
	return &lb->elems;
}


mui_control_t *
mui_listbox_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		uint32_t 		uid )
{
	c2_rect_t lbf = frame;
	c2_rect_t sb = frame;
	mui_font_t * main = mui_font_find(win->ui, "main");
	lbf.r -= main->size;
	sb.l = sb.r - main->size;
	mui_control_t *c = mui_control_new(
				win, MUI_CONTROL_LISTBOX, mui_cdef_listbox,
				lbf, NULL, uid, sizeof(mui_listbox_control_t));
	mui_listbox_control_t *lb = (mui_listbox_control_t *)c;
	lb->scrollbar = mui_scrollbar_new(win, sb, 0);
	mui_control_set_action(lb->scrollbar, mui_listbox_sbar_action, c);
	lb->elem_height = main->size + 2;

	return c;
}

void
mui_listbox_prepare(
		mui_control_t * c)
{
	mui_listbox_control_t *lb = (mui_listbox_control_t *)c;
	c2_rect_t content = C2_RECT_WH(0, 0,
			c2_rect_width(&c->frame), c2_rect_height(&c->frame));
	content.b = lb->elems.count * lb->elem_height;

	c2_rect_offset(&content, 0, lb->scroll);
	if (content.b < c2_rect_height(&c->frame)) {
		c2_rect_offset(&content, 0, c2_rect_height(&c->frame) - content.b);
	}
	if (content.t > 0) {
		c2_rect_offset(&content, 0, -content.t);
	}
	lb->scroll = content.t;
	if (c2_rect_height(&content) > c2_rect_height(&c->frame)) {
		mui_scrollbar_set_max(lb->scrollbar,
					c2_rect_height(&content));
		mui_control_set_value(lb->scrollbar, -lb->scroll);
		mui_scrollbar_set_page(lb->scrollbar, c2_rect_height(&c->frame));
	} else {
		mui_scrollbar_set_max(lb->scrollbar, 0);
		mui_control_set_value(lb->scrollbar, 0);
		mui_scrollbar_set_page(lb->scrollbar, 0);
	}
	mui_control_inval(lb->scrollbar);
	mui_control_inval(c);
}
