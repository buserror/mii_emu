/*
 * mui_cdef_te_metrics.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "mui.h"
#include "mui_cdef_te_priv.h"


#define D(_w)  ; // _w

/*
	███    ███ ███████  █████  ███████ ██    ██ ██████  ███████
	████  ████ ██      ██   ██ ██      ██    ██ ██   ██ ██
	██ ████ ██ █████   ███████ ███████ ██    ██ ██████  █████
	██  ██  ██ ██      ██   ██      ██ ██    ██ ██   ██ ██
	██      ██ ███████ ██   ██ ███████  ██████  ██   ██ ███████
*/
/*
 * Calculate the 3 rectangles that represent the graphical selection.
 * The 'start' is the first line of the selection, or the position of the
 * carret if the selection is empty.
 * The other two are 'optional' (they can be empty), and represent the last
 * line of the selection, and the body of the selection that is the rectangle
 * between the first and last line.
 */
int
_mui_make_sel_rects(
		mui_glyph_line_array_t * 	measure,
		mui_font_t *				font,
		mui_sel_t *					sel,
		c2_rect_t 					frame)
{
	if (!measure->count)
		return -1;
	sel->last = sel->first = sel->body = (c2_rect_t) {};
	uint start_line, start_index;
	uint end_line, end_index;
	_mui_glyph_to_line_index(measure, sel->start, &start_line, &start_index);
	_mui_glyph_to_line_index(measure, sel->end, &end_line, &end_index);
	mui_glyph_array_t * line = &measure->e[start_line];

	if (start_line == end_line) {
		// single line selection
		sel->first = (c2_rect_t) {
			.l = frame.l + line->e[start_index].x,
			.t = frame.t + line->t,
			.r = frame.l + line->e[end_index].x,
			.b = frame.t + line->b,
		};
		return 0;
	}
	// first line
	sel->first = (c2_rect_t) {
		.l = frame.l + line->e[start_index].x, .t = frame.t + line->t,
		.r = frame.r, .b = frame.t + line->b,
	};
	// last line
	line = &measure->e[end_line];
	sel->last = (c2_rect_t) {
		.l = frame.l, .t = frame.t + line->t,
		.r = frame.l + line->e[end_index].x, .b = frame.t + line->b,
	};
	// body
	sel->body = (c2_rect_t) {
		.l = frame.l, .t = sel->first.b,
		.r = frame.r, .b = sel->last.t,
	};
	return 0;
}

/* Refresh the whole selection (or around the carret selection) */
void
_mui_textedit_refresh_sel(
		mui_textedit_control_t *	te,
		mui_sel_t * 				sel)
{
	if (!sel)
		sel = &te->sel;
	for (int i = 0; i < 3; i++) {
		c2_rect_t r = te->sel.e[i];
		if (i == 0 && te->sel.start == te->sel.end) {
			c2_rect_inset(&r, -1, -1);
		//	printf("refresh_sel: carret %s\n", c2_rect_as_str(&r));
		}
		if (!c2_rect_isempty(&r))
			_mui_textedit_inval(te, r);
	}
}

/* this makes sure the text is always visible in the frame */
void
_mui_textedit_clamp_text_frame(
		mui_textedit_control_t *	te)
{
	c2_rect_t f = te->control.frame;
	c2_rect_offset(&f, -f.l, -f.t);
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME)
		c2_rect_inset(&f, te->margin.x, te->margin.y);
	c2_rect_t old = te->text_content;
	te->text_content.r = te->text_content.l + te->measure.margin_right;
	te->text_content.b = te->text_content.t + te->measure.height;
	D(printf("  %s %s / %3dx%3d\n", __func__,
			c2_rect_as_str(&te->text_content),
			c2_rect_width(&f), c2_rect_height(&f));)
	if (te->text_content.b < c2_rect_height(&f))
		c2_rect_offset(&te->text_content, 0,
				c2_rect_height(&f) - te->text_content.b);
	if (te->text_content.t > f.t)
		c2_rect_offset(&te->text_content, 0, f.t - te->text_content.t);
	if (te->text_content.r < c2_rect_width(&f))
		c2_rect_offset(&te->text_content,
				c2_rect_width(&f) - te->text_content.r, 0);
	if (te->text_content.l > f.l)
		c2_rect_offset(&te->text_content, f.l - te->text_content.l, 0);
	if (c2_rect_equal(&te->text_content, &old))
		return;
	D(printf("   clamped TE from %s to %s\n", c2_rect_as_str(&old),
			c2_rect_as_str(&te->text_content));)
	mui_control_inval(&te->control);
}

/* This scrolls the view following the carret, used when typing.
 * This doesn't check for out of bounds, but the clamping should
 * have made sure the text is always visible. */
void
_mui_textedit_ensure_carret_visible(
		mui_textedit_control_t *	te)
{
	c2_rect_t f = te->control.frame;
//	c2_rect_offset(&f, -f.l, -f.t);
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME)
		c2_rect_inset(&f, te->margin.x, te->margin.y);
	if (te->sel.start != te->sel.end)
		return;
	c2_rect_t old = te->text_content;
	c2_rect_t r = te->sel.first;
	D(printf("%s carret %s frame %s\n", __func__,
			c2_rect_as_str(&r), c2_rect_as_str(&f));)
	c2_rect_offset(&r, -te->text_content.l, -te->text_content.t);
	if (r.r < f.l) {
		D(printf("   moved TE LEFT %d\n", -(f.l - r.r));)
		c2_rect_offset(&te->text_content, -(f.l - r.l), 0);
	}
	if (r.l > f.r) {
		D(printf("   moved TE RIGHT %d\n", -(r.l - f.r));)
		c2_rect_offset(&te->text_content, -(r.l - f.r), 0);
	}
	if (r.t < f.t)
		c2_rect_offset(&te->text_content, 0, r.t - f.t);
	if (r.b > f.b)
		c2_rect_offset(&te->text_content, 0, r.b - f.b);
	if (c2_rect_equal(&te->text_content, &old))
		return;
	D(printf("   moved TE from %s to %s\n", c2_rect_as_str(&old),
			c2_rect_as_str(&te->text_content));)
	_mui_textedit_clamp_text_frame(te);
}

/*
 * This is to be called when the text changes, or the frame (width) changes
 */
void
_mui_textedit_refresh_measure(
		mui_textedit_control_t *	te)
{
	c2_rect_t f = te->control.frame;
	c2_rect_offset(&f, -f.l, -f.t);
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME)
		c2_rect_inset(&f, te->margin.x, te->margin.y);
	if (!(te->flags & MUI_CONTROL_TEXTEDIT_VERTICAL))
		f.r = 0x7fff; // make it very large, we don't want wrapping.

	mui_glyph_line_array_t new_measure = {};

	mui_font_measure(te->font, f,
					(const char*)te->text.e, te->text.count-1,
					&new_measure, te->flags);

	f = te->control.frame;
	if (te->flags & MUI_CONTROL_TEXTBOX_FRAME)
		c2_rect_inset(&f, te->margin.x, te->margin.y);
	// Refresh the lines that have changed. Perhaps all of them did,
	// doesn't matter, but it's nice to avoid redrawing the whole text
	// when someone is typing.
	for (uint i = 0; i < new_measure.count && i < te->measure.count; i++) {
		if (i >= te->measure.count) {
			c2_rect_t r = f;
			r.t += new_measure.e[i].t;
			r.b = r.t + new_measure.e[i].b;
			r.r = new_measure.e[i].x + new_measure.e[i].w;
			_mui_textedit_inval(te, r);
		} else if (i >= new_measure.count) {
			c2_rect_t r = f;
			r.t += te->measure.e[i].t;
			r.b = r.t + te->measure.e[i].b;
			r.r = te->measure.e[i].x + te->measure.e[i].w;
			_mui_textedit_inval(te, r);
		} else {
			int dirty = 0;
			// unsure if this could happen, but let's be safe --
			// technically we should refresh BOTH rectangles (old, new)
			if (new_measure.e[i].t != te->measure.e[i].t ||
					new_measure.e[i].b != te->measure.e[i].b) {
				dirty = 1;
			} else if (new_measure.e[i].x != te->measure.e[i].x ||
					new_measure.e[i].count != te->measure.e[i].count ||
					new_measure.e[i].w != te->measure.e[i].w)
				dirty = 1;
			else {
				for (uint x = 0; x < new_measure.e[i].count; x++) {
					if (new_measure.e[i].e[x].glyph != te->measure.e[i].e[x].glyph ||
							new_measure.e[i].e[x].x != te->measure.e[i].e[x].x ||
							new_measure.e[i].e[x].w != te->measure.e[i].e[x].w) {
						dirty = 1;
						break;
					}
				}
			}
			if (dirty) {
				c2_rect_t r = f;
				r.t += new_measure.e[i].t;
				r.b = r.t + new_measure.e[i].b;
				r.r = new_measure.e[i].x + new_measure.e[i].w;
				_mui_textedit_inval(te, r);
			}
		}
	}
	mui_font_measure_clear(&te->measure);
	te->measure = new_measure;
	_mui_textedit_clamp_text_frame(te);
}
