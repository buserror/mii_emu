/*
 * mui_cdef_textedit.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * This is a simple textedit control, it's not meant to be a full fledged
 * text editor, but more a simple text input field.
 *
 * One obvious low hanging fruit would be to split the drawing code
 * to be able to draw line-by-line, allowing skipping lines that are
 * not visible. Currently the whole text is drawn every time, and relies
 * on clipping to avoid drawing outside the control.
 *
 * System is based on mui_font_measure() returning a mui_glyph_line_array_t
 * that contains the position of each glyph in the text, and the width of
 * each line.
 * The text itself is a UTF8 array, so we need to be aware of multi-byte
 * glyphs. The 'selection' is kept as a start and end glyph index, and
 * the drawing code calculates the rectangles for the selection.
 *
 * There is a text_content rectangle that deals with the scrolling, and
 * the text (and selection) is drawn offset by the top left of this rectangle.
 *
 * There is a carret timer that makes the carret blink, and a carret is
 * drawn when the selection is empty.
 *
 * There can only one 'carret' blinking at a time, the one in the control
 * that has the focus, so the carret timer is a global timer that is reset
 * every time a control gets the focus.
 *
 * The control has a margin, and a frame, and the text is drawn inside the
 * frame, and the margin is used to inset the text_content rectangle.
 * Margin is optional, and frame is optional too.
 *
 * The control deals with switching focus as well, so clicking in a textedit
 * will deactivate the previously focused control, and activate the new one.
 * TAB will do the same, for the current window.
 */
#include <stdio.h>
#include <stdlib.h>

#include "mui.h"
#include "cg.h"
#include "mui_cdef_te_priv.h"

enum {
	MUI_CONTROL_TEXTEDIT 		= FCC('T','e','a','c'),
};

#define D(_w)  ; // _w

enum {
	MUI_TE_SELECTING_GLYPHS		= 0,
	MUI_TE_SELECTING_WORDS,
//	MUI_TE_SELECTING_LINES,		// TODO?
};

extern const mui_control_color_t mui_control_color[MUI_CONTROL_STATE_COUNT];


static bool
mui_cdef_textedit(
		struct mui_control_t * 	c,
		uint8_t 				what,
		void * 					param);

/*
 * Rectangles passed here are in TEXT coordinates.
 * which means they are already offset by margin.x, margin.y
 * and the text_content.tl.x, text_content.tl.y
 */
void
_mui_textedit_inval(
		mui_textedit_control_t *	te,
		c2_rect_t 					r)
{
	c2_rect_offset(&r, te->text_content.tl.x, te->text_content.tl.y);
	if (!c2_rect_isempty(&r))
		mui_window_inval(te->control.win, &r);
}

/* this is the timer used to make the carret blink *for all windows* */
static mui_time_t
_mui_textedit_carret_timer(
		struct mui_t *				mui,
		mui_time_t 					now,
		void * 						param)
{
	mui_window_t *win = mui_window_front(mui);

//	printf("carret timer win %p focus %p\n", win, win->control_focus);
	if (win && win->control_focus.control &&
			win->control_focus.control->type == MUI_CONTROL_TEXTEDIT) {
		mui_textedit_control_t *te =
				(mui_textedit_control_t *)win->control_focus.control;
		te->sel.carret = !te->sel.carret;
		if (te->sel.start == te->sel.end)
			_mui_textedit_refresh_sel(te, NULL);
	}
	return 500 * MUI_TIME_MS;
}

/* this 'forces' the carret to be visible, used when typing */
void
_mui_textedit_show_carret(
		mui_textedit_control_t *	te)
{
	mui_t * mui = te->control.win->ui;
	mui_window_t *win = mui_window_front(mui);
	if (win && win->control_focus.control == &te->control) {
		mui_timer_reset(mui,
					mui->carret_timer,
					_mui_textedit_carret_timer,
					500 * MUI_TIME_MS);
	}
	te->sel.carret = 1;
	_mui_textedit_refresh_sel(te, NULL);
}

/* Return the line number, and glyph position in line a glyph index */
int
_mui_glyph_to_line_index(
		mui_glyph_line_array_t * 	measure,
		uint  						glyph_pos,
		uint * 						out_line,
		uint * 						out_line_index)
{
	*out_line = 0;
	*out_line_index = 0;
	if (!measure->count)
		return -1;
	for (uint i = 0; i < measure->count; i++) {
		mui_glyph_array_t * line = &measure->e[i];
		if (glyph_pos > line->count) {
			glyph_pos -= line->count;
			continue;
		}
		*out_line = i;
		*out_line_index = glyph_pos;
		return i;
	}
	// return last glyph last line
	*out_line = measure->count - 1;
	*out_line_index = measure->e[*out_line].count - 1;
	return measure->count - 1;
}

/* Return the line number and glyph index in that line for a point x,y */
static int
_mui_point_to_line_index(
		mui_textedit_control_t *	te,
		mui_font_t *				font,
		c2_rect_t 					frame,
		c2_pt_t 					where,
		uint * 						out_line,
		uint * 						out_line_index)
{
	mui_glyph_line_array_t * 	measure = &te->measure;
	if (!measure->count)
		return -1;
	*out_line = 0;
	*out_line_index = 0;
	for (uint i = 0; i < measure->count; i++) {
		mui_glyph_array_t * line = &measure->e[i];
		c2_rect_t line_r = {
			.l = frame.l + te->text_content.l,
			.t = frame.t + line->t + te->text_content.t,
			.r = frame.r + te->text_content.l,
			.b = frame.t + line->b + te->text_content.t,
		};
		if (!((where.y >= line_r.t) && (where.y < line_r.b)))
			continue;
		*out_line = i;
		*out_line_index = line->count;
	//	printf("  last x: %d where.x: %d\n",
	//			frame.l + (int)line->e[line->count-1].x, where.x);
		if (where.x > (line_r.l + (int)line->e[line->count].x)) {
			*out_line_index = line->count;
			return 0;
		} else if (where.x < (line_r.l + (int)line->e[0].x)) {
			*out_line_index = 0;
			return 0;
		}
		for (uint j = 0; j < line->count; j++) {
			if (where.x < (line_r.l + (int)line->e[j].x))
				return 0;
			*out_line_index = j;
		}
	//	printf("point_to_line_index: line %d:%d / %d\n",
	//		*out_line, *out_line_index, line->count);
		return 0;
	}
	return -1;
}

/* Return the glyph position in the text for line number and index in line */
uint
_mui_line_index_to_glyph(
		mui_glyph_line_array_t * 	measure,
		uint 						line,
		uint 						index)
{
	uint pos = 0;
	for (uint i = 0; i < line; i++)
		pos += measure->e[i].count;
	pos += index;
	return pos;
}

/* Return the beginning and end glyphs for the line/index in line */
static void
_mui_line_index_to_glyph_word(
		mui_glyph_line_array_t * 	measure,
		uint 						line,
		uint 						index,
		uint 						*word_start,
		uint 						*word_end)
{
	*word_start = 0;
	*word_end = 0;
	uint start = index;
	uint end = index;
	mui_glyph_array_t * l = &measure->e[line];
	while (start > 0 && l->e[start-1].glyph > 32)
		start--;
	while (end < l->count && l->e[end].glyph > 32)
		end++;
	*word_start = _mui_line_index_to_glyph(measure, line, start);
	*word_end = _mui_line_index_to_glyph(measure, line, end);
}

/* Convert a glyph index to a byte index (used to manipulate text array) */
uint
_mui_glyph_to_byte_offset(
		mui_glyph_line_array_t * 	measure,
		uint 						glyph_pos)
{
	uint pos = 0;
	for (uint i = 0; i < measure->count; i++) {
		mui_glyph_array_t * line = &measure->e[i];
		if (glyph_pos > pos + line->count) {
			pos += line->count;
			continue;
		}
		uint idx = glyph_pos - pos;
	//	printf("glyph_to_byte_offset: glyph_pos %d line %d:%2d\n",
	//			glyph_pos, i, idx);
		return line->e[idx].pos;
	}
//	printf("glyph_to_byte_offset: glyph_pos %d out of range\n", glyph_pos);
	return 0;
}

void
_mui_textedit_sel_delete(
		mui_textedit_control_t *	te,
		bool 						re_measure,
		bool 						reset_sel)
{
	if (te->sel.start == te->sel.end)
		return;
	mui_utf8_delete(&te->text,
			_mui_glyph_to_byte_offset(&te->measure, te->sel.start),
			_mui_glyph_to_byte_offset(&te->measure, te->sel.end) -
				_mui_glyph_to_byte_offset(&te->measure, te->sel.start));
	if (re_measure)
		_mui_textedit_refresh_measure(te);
	if (reset_sel)
		_mui_textedit_select_signed(te,
				te->sel.start, te->sel.start);
}

/* this one allows passing -1 etc, which is handy of cursor movement */
void
_mui_textedit_select_signed(
		mui_textedit_control_t *	te,
		int 						glyph_start,
		int 						glyph_end)
{
	if (glyph_start < 0)
		glyph_start = 0;
	if (glyph_end < 0)
		glyph_end = 0;
	if (glyph_end > (int)te->text.count)
		glyph_end = te->text.count+1;
	if (glyph_start > (int)te->text.count)
		glyph_start = te->text.count;
	if (glyph_start > glyph_end) {
		uint t = glyph_start;
		glyph_start = glyph_end;
		glyph_end = t;
	}

	printf("%s %d:%d\n", __func__, glyph_start, glyph_end);
	c2_rect_t f = te->control.frame;
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME)
		c2_rect_inset(&f, te->margin.x, te->margin.y);

	mui_glyph_line_array_t * 	measure = &te->measure;
	_mui_textedit_refresh_sel(te, NULL);
	mui_sel_t newone = { .start = glyph_start, .end = glyph_end };
	_mui_make_sel_rects(measure, te->font, &newone, f);
	te->sel = newone;
	_mui_textedit_ensure_carret_visible(te);
	_mui_textedit_refresh_sel(te, NULL);
}

/*
	██████  ██████   █████  ██     ██ ██ ███    ██  ██████
	██   ██ ██   ██ ██   ██ ██     ██ ██ ████   ██ ██
	██   ██ ██████  ███████ ██  █  ██ ██ ██ ██  ██ ██   ███
	██   ██ ██   ██ ██   ██ ██ ███ ██ ██ ██  ██ ██ ██    ██
	██████  ██   ██ ██   ██  ███ ███  ██ ██   ████  ██████
*/
static void
mui_textedit_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	c2_rect_t f = c->frame;
	c2_rect_offset(&f, win->content.l, win->content.t);

	mui_textedit_control_t *te = (mui_textedit_control_t *)c;

	mui_drawable_clip_push(dr, &f);
	struct cg_ctx_t * cg = mui_drawable_get_cg(dr);
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME) {
		cg_set_line_width(cg, mui_control_has_focus(c) ? 2 : 1);
		cg_set_source_color(cg, &CG_COLOR(mui_control_color[c->state].frame));
		cg_rectangle(cg, f.l + 0.5, f.t + 0.5,
						c2_rect_width(&f)-1, c2_rect_height(&f)-1);
		cg_stroke(cg);
	}
//	cg = mui_drawable_get_cg(dr);	// this updates the cg clip too
	if (te->text.count <= 1)
		goto done;
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME)
		c2_rect_inset(&f, te->margin.x, te->margin.y);
	mui_drawable_clip_push(dr, &f);
	cg = mui_drawable_get_cg(dr);	// this updates the cg clip too
	bool is_active = c == c->win->control_focus.control;
	if (te->sel.start == te->sel.end) {
		if (te->sel.carret && is_active) {
			c2_rect_t carret = te->sel.first;
			c2_rect_offset(&carret,
					c->win->content.l + te->text_content.tl.x,
					c->win->content.t + te->text_content.tl.y);
			// rect is empty, but it's a carret!
			// draw a line at the current position
			cg_set_line_width(cg, 1);
			cg_set_source_color(cg, &CG_COLOR(mui_control_color[c->state].text));
			cg_move_to(cg, carret.l, carret.t);
			cg_line_to(cg, carret.l, carret.b);
			cg_stroke(cg);
		}
	} else {
		if (is_active) {
			for (int i = 0; i < 3; i++) {
				if (!c2_rect_isempty(&te->sel.e[i])) {
					c2_rect_t sr = te->sel.e[i];
				//	c2_rect_clip_rect(&sr, &f, &sr);
					cg_set_source_color(cg, &CG_COLOR(c->win->ui->color.highlight));
					c2_rect_offset(&sr,
							c->win->content.l + te->text_content.tl.x,
							c->win->content.t + te->text_content.tl.y);
					cg_rectangle(cg,
							sr.l, sr.t, c2_rect_width(&sr), c2_rect_height(&sr));
					cg_fill(cg);
				}
			}
		} else {	// draw a path around the selection
			cg_set_line_width(cg, 2);
			cg_set_source_color(cg, &CG_COLOR(c->win->ui->color.highlight));
			mui_sel_t  o = te->sel;
			for (int i = 0; i < 3; i++)
				c2_rect_offset(&o.e[i],
						c->win->content.l + te->text_content.tl.x,
						c->win->content.t + te->text_content.tl.y);
			cg_move_to(cg, o.first.l, o.first.t);
			cg_line_to(cg, o.first.r, o.first.t);
			cg_line_to(cg, o.first.r, o.first.b);
			if (c2_rect_isempty(&o.last))
				cg_line_to(cg, o.first.l, o.first.b);
			else {
				cg_line_to(cg, o.first.r, o.first.b);
				cg_line_to(cg, o.first.r, o.last.t);
				cg_line_to(cg, o.last.r, o.last.t);
				cg_line_to(cg, o.last.r, o.last.b);
				cg_line_to(cg, o.last.l, o.last.b);
				cg_line_to(cg, o.last.l, o.first.b);
			}
			cg_line_to(cg, o.first.l, o.first.b);
			cg_line_to(cg, o.first.l, o.first.t);
			cg_stroke(cg);
		}
	}
	c2_rect_t tf = f;
	c2_rect_offset(&tf, te->text_content.tl.x, te->text_content.tl.y);
	mui_font_measure_draw(te->font, dr, tf,
			&te->measure, mui_control_color[c->state].text, te->flags);
	mui_drawable_clip_pop(dr);
	cg = mui_drawable_get_cg(dr);	// this updates the cg clip too
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME) {
		if (c2_rect_width(&f) < c2_rect_width(&te->text_content)) {
			// draw a line-like mini scroll bar to show scroll position
			int fsize = c2_rect_width(&f);
			int tsize = c2_rect_width(&te->text_content);
			float ratio = fsize / (float)tsize;
			float dsize = fsize * ratio;
			c2_rect_t r = C2_RECT_WH(f.l, f.b + 1, dsize, 1);
			float pos = -te->text_content.tl.x / (float)(tsize - fsize);
			c2_rect_offset(&r, (fsize - dsize) * pos, 0);
			cg_set_source_color(cg,
					&CG_COLOR(mui_control_color[c->state].frame));
			cg_move_to(cg, r.l, r.t);
			cg_line_to(cg, r.r, r.t);
			cg_stroke(cg);
		}
		// same for vertical
		if (c2_rect_height(&f) < c2_rect_height(&te->text_content)) {
			int fsize = c2_rect_height(&f);
			int tsize = c2_rect_height(&te->text_content);
			float ratio = fsize / (float)tsize;
			float dsize = fsize * ratio;
			c2_rect_t r = C2_RECT_WH(f.r +1, f.t, 1, dsize);
			float pos = -te->text_content.tl.y / (float)(tsize - fsize);
			c2_rect_offset(&r, 0, (fsize - dsize) * pos);
			cg_set_source_color(cg,
					&CG_COLOR(mui_control_color[c->state].frame));
			cg_move_to(cg, r.l, r.t);
			cg_line_to(cg, r.l, r.b);
			cg_stroke(cg);
		}
	}
done:
	mui_drawable_clip_pop(dr);
}

/*
	███    ███  ██████  ██    ██ ███████ ███████
	████  ████ ██    ██ ██    ██ ██      ██
	██ ████ ██ ██    ██ ██    ██ ███████ █████
	██  ██  ██ ██    ██ ██    ██      ██ ██
	██      ██  ██████   ██████  ███████ ███████
*/
static bool
_mui_textedit_mouse(
		struct mui_control_t * 	c,
		mui_event_t * 			ev)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;

	c2_rect_t f = c->frame;
	c2_rect_offset(&f, c->win->content.l, c->win->content.t);
	uint line = 0, index = 0;
	bool res = false;
	switch (ev->type) {
		case MUI_EVENT_BUTTONDOWN: {
			if (!c2_rect_contains_pt(&f, &ev->mouse.where))
				break;
			if (!mui_control_has_focus(c))
				mui_control_set_focus(c);

			if (_mui_point_to_line_index(te, te->font, f,
					ev->mouse.where, &line, &index) == 0) {
				uint pos = _mui_line_index_to_glyph(
											&te->measure, line, index);
				te->selecting_mode = MUI_TE_SELECTING_GLYPHS;
				if (ev->mouse.count == 2) {
					// double click, select word
					uint32_t start,end;
					_mui_line_index_to_glyph_word(&te->measure, line, index,
							&start, &end);
					_mui_textedit_select_signed(te, start, end);
					te->selecting_mode = MUI_TE_SELECTING_WORDS;
				} else if (ev->modifiers & MUI_MODIFIER_SHIFT) {
					// shift click, extend selection
					if (pos < te->sel.start) {
						_mui_textedit_select_signed(te, pos, te->sel.end);
					} else {
						_mui_textedit_select_signed(te, te->sel.start, pos);
					}
				} else {
					// single click, set carret (and start selection
					_mui_textedit_select_signed(te, pos, pos);
				}
				te->click.start = te->sel.start;
				te->click.end = te->sel.end;
				D(printf("DOWN line %2d index %3d pos:%3d\n",
						line, index, pos);)
				res = true;
			};
			te->sel.carret = 0;
		}	break;
		case MUI_EVENT_BUTTONUP: {
			res = true;
			if (_mui_point_to_line_index(te, te->font, f,
					ev->mouse.where, &line, &index) == 0) {
				D(printf("UP line %d index %d\n", line, index);)
			}
			te->sel.carret = 1;
			_mui_textedit_refresh_sel(te, NULL);
		}	break;
		case MUI_EVENT_DRAG: {
			res = true;
			if (!c2_rect_contains_pt(&f, &ev->mouse.where)) {
				if (te->flags & MUI_CONTROL_TEXTEDIT_VERTICAL) {
					if (ev->mouse.where.y > f.b) {
						te->text_content.tl.y -= ev->mouse.where.y - f.b;
						D(printf("scroll down %3d\n", te->text_content.tl.y);)
						_mui_textedit_clamp_text_frame(te);
						mui_control_inval(c);
					} else if (ev->mouse.where.y < f.t) {
						te->text_content.tl.y += f.t - ev->mouse.where.y;
						D(printf("scroll up   %3d\n", te->text_content.tl.y);)
						_mui_textedit_clamp_text_frame(te);
						mui_control_inval(c);
					}
				} else {
					if (ev->mouse.where.x > f.r) {
						te->text_content.tl.x -= ev->mouse.where.x - f.r;
						D(printf("scroll right %3d\n", te->text_content.tl.x);)
						_mui_textedit_clamp_text_frame(te);
						mui_control_inval(c);
					} else if (ev->mouse.where.x < f.l) {
						te->text_content.tl.x += f.l - ev->mouse.where.x;
						D(printf("scroll left  %3d\n", te->text_content.tl.x);)
						_mui_textedit_clamp_text_frame(te);
						mui_control_inval(c);
					}
				}
			}
			if (_mui_point_to_line_index(te, te->font, f,
					ev->mouse.where, &line, &index) == 0) {
			//	printf("    line %d index %d\n", line, index);
				uint pos = _mui_line_index_to_glyph(
											&te->measure, line, index);
				if (te->selecting_mode == MUI_TE_SELECTING_WORDS) {
					uint32_t start,end;
					_mui_line_index_to_glyph_word(&te->measure, line, index,
							&start, &end);
					_mui_line_index_to_glyph_word(&te->measure,
							line, index, &start, &end);
					if (pos < te->click.start)
						_mui_textedit_select_signed(te, start, te->click.end);
					else
						_mui_textedit_select_signed(te, te->click.start, end);
				} else {
					if (pos < te->click.start)
						_mui_textedit_select_signed(te, pos, te->click.start);
					else
						_mui_textedit_select_signed(te, te->click.start, pos);
				}
			}
		}	break;
		case MUI_EVENT_WHEEL: {
			if (te->flags & MUI_CONTROL_TEXTEDIT_VERTICAL) {
				te->text_content.tl.y -= ev->wheel.delta * 10;
				_mui_textedit_clamp_text_frame(te);
				mui_control_inval(c);
			} else {
				te->text_content.tl.x -= ev->wheel.delta * 10;
				_mui_textedit_clamp_text_frame(te);
				mui_control_inval(c);
			}
			res = true;
		}	break;
		default:
			break;
	}
	return res;
}

static bool
mui_cdef_textedit(
		struct mui_control_t * 	c,
		uint8_t 				what,
		void * 					param)
{
	if (!c)
		return false;
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;
	switch (what) {
		case MUI_CDEF_INIT: {
			/* If we are the first text-edit created, register the timer */
			if (c->win->ui->carret_timer == MUI_TIMER_NONE)
				c->win->ui->carret_timer = mui_timer_register(c->win->ui,
							_mui_textedit_carret_timer, NULL,
							500 * MUI_TIME_MS);
			if (mui_window_isfront(c->win) &&
						c->win->control_focus.control == NULL)
				mui_control_set_focus(c);
		}	break;
		case MUI_CDEF_DRAW: {
			mui_drawable_t * dr = param;
			mui_textedit_draw(c->win, c, dr);
		}	break;
		case MUI_CDEF_DISPOSE: {
			mui_font_measure_clear(&te->measure);
			mui_utf8_clear(&te->text);
			/*
			 * If we are the focus, and we are being disposed, we need to
			 * find another control to focus on, if there is one.
			 * This is a bit tricky, as the control isn't attached to the
			 * window anymore, so we might have to devise another plan.
			 */
			if (c->win->control_focus.control == c) {
				mui_control_deref(&c->win->control_focus);
			}
		}	break;
		case MUI_CDEF_EVENT: {
		//	printf("%s event\n", __func__);
			mui_event_t *ev = param;
			switch (ev->type) {
				case MUI_EVENT_WHEEL:
				case MUI_EVENT_BUTTONUP:
				case MUI_EVENT_DRAG:
				case MUI_EVENT_BUTTONDOWN: {
					return _mui_textedit_mouse(c, ev);
				}	break;
				case MUI_EVENT_KEYDOWN: {
					return _mui_textedit_key(c, ev);
				}	break;
				default:
					break;
			}
		}	break;
		case MUI_CDEF_CAN_FOCUS: {
			return true;
		}	break;
		case MUI_CDEF_FOCUS: {
		//	int activate = *(int*)param;
		//	printf("%s activate %d\n", __func__, activate);
		//	mui_textedit_control_t *te = (mui_textedit_control_t *)c;
		}	break;
	}
	return false;
}

mui_control_t *
mui_textedit_control_new(
		mui_window_t * 	win,
		c2_rect_t 		frame,
		uint32_t 		flags)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)mui_control_new(
				win, MUI_CONTROL_TEXTEDIT, mui_cdef_textedit,
				frame,  NULL, 0, sizeof(mui_textedit_control_t));
	te->flags = flags;
	te->margin = (c2_pt_t){ .x = 4, .y = 2 };
	return &te->control;
}


/*
 * Mark old selection as invalid, and set the new one,
 * and make sure it's visible
 */
void
mui_textedit_set_selection(
		mui_control_t * 			c,
		uint 						glyph_start,
		uint 						glyph_end)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;
	printf("%s %d:%d\n", __func__, glyph_start, glyph_end);
	_mui_textedit_select_signed(te, glyph_start, glyph_end);
}

/*
 * Get current selection
 */
void
mui_textedit_get_selection(
		mui_control_t * 			c,
		uint * 						glyph_start,
		uint * 						glyph_end)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;
	*glyph_start = te->sel.start;
	*glyph_end = te->sel.end;
}

void
mui_textedit_set_text(
		mui_control_t * 			c,
		const char * 				text)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;
	mui_utf8_clear(&te->text);
	int tl = strlen(text);
	mui_utf8_realloc(&te->text, tl + 1);
	memcpy(te->text.e, text, tl + 1);
	/*
	 * Note, the text.count *counts the terminating zero*
	 */
	te->text.count = tl + 1;
	if (!te->font)
		te->font = mui_font_find(c->win->ui, "main");
	_mui_textedit_refresh_measure(te);
}

uint
mui_textedit_get_text(
		mui_control_t * 			c,
		char * 						text,
		uint 						max)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;
	uint tl = te->text.count - 1;
	if (tl > max)
		tl = max;
	memcpy(text, te->text.e, tl);
	text[tl] = 0;
	return tl;
}
