/*
 * mui_menus_draw.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>

#include "mui.h"
#include "mui_priv.h"
#include "cg.h"


/*
 * Menubar/menus frames is easy as pie -- just a framed rectangle
 */
void
mui_wdef_menubar_draw(
		struct mui_window_t * 	win,
		mui_drawable_t * 		dr)
{
	c2_rect_t content = win->frame;
	win->content = content;

	struct cg_ctx_t * cg 	= mui_drawable_get_cg(dr);

	mui_color_t frameColor 	= MUI_COLOR(0x000000ff);
	mui_color_t contentFill = MUI_COLOR(0xf0f0f0ff);
	cg_set_line_width(cg, 1);
	cg_rectangle(cg, win->frame.l + 0.5f, win->frame.t + 0.5f,
					c2_rect_width(&win->frame) - 1, c2_rect_height(&win->frame) - 1);
	cg_set_source_color(cg, &CG_COLOR(contentFill));
	cg_fill_preserve(cg);
	cg_set_source_color(cg, &CG_COLOR(frameColor));
	cg_stroke(cg);
}


extern const mui_control_color_t mui_control_color[MUI_CONTROL_STATE_COUNT];

void
mui_menutitle_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	c2_rect_t f = c->frame;
	c2_rect_offset(&f, win->content.l, win->content.t);

	struct cg_ctx_t * cg = mui_drawable_get_cg(dr);

	mui_font_t * main = mui_font_find(win->ui, "main");
	stb_ttc_measure m = {};
	mui_font_text_measure(main, c->title, &m);

	int title_width = m.x1 - m.x0;
	c2_rect_t title = f;
	title.r = title.l + title_width + 1;
	title.b = title.t + m.ascent - m.descent;
	c2_rect_offset(&title, //-m.x0 +
			(int)((c2_rect_width(&f) / 2) - (c2_rect_width(&title)) / 2),
			(c2_rect_height(&f) / 2) - (c2_rect_height(&title) / 2));
	mui_drawable_clip_push(dr, &f);
	uint32_t state = mui_control_get_state(c);
	if (state) {
		cg_set_source_color(cg, &CG_COLOR(mui_control_color[state].fill));
		cg_rectangle(cg, f.l, f.t, c2_rect_width(&f), c2_rect_height(&f));
		cg_fill(cg);
	}
	mui_font_text_draw(main, dr,
			C2_PT(title.l, title.t), c->title, strlen(c->title),
			mui_control_color[state].text);
	mui_drawable_clip_pop(dr);
}


static void
mui_menuitem_get_locations(
		mui_t *				ui,
		c2_rect_t * 		frame,
		mui_menu_item_t *	item,
		c2_pt_t  			out[3] )
{
	mui_font_t * main = mui_font_find(ui, "main");

	stb_ttc_measure m = {};
	mui_font_text_measure(main, item->title, &m);

	c2_rect_t title = *frame;
	title.b = title.t + m.ascent - m.descent;
	c2_rect_offset(&title, 0,
			(c2_rect_height(frame) / 2) - (c2_rect_height(&title) / 2));

	if (item->icon[0]) {
		title.l += 6;
		mui_font_t * icons = mui_font_find(ui, "icon_small");

		mui_font_text_measure(icons, item->icon, &m);
		c2_pt_t loc = title.tl;
		loc.x = loc.x + (icons->size / 2) - ((m.x1 - m.x0) / 2);
		out[0] = loc;
		title.l += 6;
	} else if (item->mark[0]) {
		mui_font_text_measure(main, item->mark, &m);
		c2_pt_t loc = title.tl;
		loc.x = loc.x + (main->size / 2) - ((m.x1 - m.x0) / 2);
		out[0] = loc;
 	}
	title.l += main->size;
	out[1] = title.tl;

	if (item->kcombo[0]) {
		mui_font_text_measure(main, item->kcombo, &m);

		c2_pt_t loc = C2_PT(title.r - m.x1 - m.x0 - (main->size/3), title.t);
		out[2] = loc;
	}
}


extern const mui_control_color_t mui_control_color[MUI_CONTROL_STATE_COUNT];

void
mui_menuitem_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	c2_rect_t f = c->frame;
	c2_rect_offset(&f, win->content.l, win->content.t);

	struct cg_ctx_t * cg = mui_drawable_get_cg(dr);
	mui_drawable_clip_push(dr, &f);

	mui_font_t * main = TAILQ_FIRST(&win->ui->fonts);
	if (c->title && c->title[0] != '-') {
		c2_pt_t loc[3];
		mui_menuitem_control_t *mic = (mui_menuitem_control_t*)c;
		mui_menuitem_get_locations(win->ui, &f, &mic->item, loc);

		uint32_t state = mui_control_get_state(c);
		if (state && state != MUI_CONTROL_STATE_DISABLED) {
			c2_rect_t b = f;
			c2_rect_inset(&b, 1, 0);
			cg_set_source_color(cg, &CG_COLOR(mui_control_color[state].fill));
			cg_rectangle(cg, b.l, b.t, c2_rect_width(&b), c2_rect_height(&b));
			cg_fill(cg);
		}
		if (mic->item.icon[0]) {
			mui_font_t * icons = mui_font_find(win->ui, "icon_small");
			mui_font_text_draw(icons, dr,
					loc[0], mic->item.icon, 0,
					mui_control_color[state].text);
		} else if (mic->item.mark[0]) {
			mui_font_text_draw(main, dr,
					loc[0], mic->item.mark, 0,
					mui_control_color[state].text);
		}
		mui_font_text_draw(main, dr,
				loc[1], mic->item.title, 0,
				mui_control_color[state].text);

		if (mic->item.kcombo[0]) {
			mui_font_text_draw(main, dr,
					loc[2], mic->item.kcombo, 0,
					mui_control_color[state].text);
		}
	} else {
		cg_move_to(cg, f.l, f.t + c2_rect_height(&f) / 2);
		cg_line_to(cg, f.r, f.t + c2_rect_height(&f) / 2);
			mui_color_t decoColor 	= MUI_COLOR(0x666666ff);

		cg_set_source_color(cg, &CG_COLOR(decoColor));
		cg_stroke(cg);
	}
	mui_drawable_clip_pop(dr);
}


void
mui_popuptitle_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	mui_menu_control_t *pop = (mui_menu_control_t*)c;
	c2_rect_t f = c->frame;
	if (c2_rect_width(&pop->menu_frame) &&
				c2_rect_width(&pop->menu_frame) < c2_rect_width(&f)) {
		f = pop->menu_frame;
		f.b = c->frame.b;
	}
	c2_rect_offset(&f, win->content.l, win->content.t);

	mui_font_t * main = TAILQ_FIRST(&win->ui->fonts);
	mui_font_t * icons = mui_font_find(win->ui, "icon_small");
	uint32_t state = mui_control_get_state(c);

	mui_drawable_clip_push(dr, &f);
	struct cg_ctx_t * cg = mui_drawable_get_cg(dr);
	c2_rect_t inner = f;
	c2_rect_inset(&inner, 1, 1);
	cg_set_line_width(cg, 2);
	cg_round_rectangle(cg, inner.l, inner.t,
					c2_rect_width(&inner), c2_rect_height(&inner), 3, 3);
	cg_set_source_color(cg, &CG_COLOR(mui_control_color[state].fill));
	cg_fill_preserve(cg);
	cg_set_source_color(cg, &CG_COLOR(mui_control_color[state].frame));
	cg_stroke(cg);
	cg_move_to(cg, inner.r - 32, inner.t + 2);
	cg_line_to(cg, inner.r - 32, inner.b - 2);
	mui_color_t decoColor 	= MUI_COLOR(0x666666ff);
	cg_set_source_color(cg, &CG_COLOR(decoColor));
	cg_stroke(cg);

	if (pop->menu.count) {
		mui_menu_item_t item = pop->menu.e[c->value];
		c2_pt_t loc[3];
		c2_rect_offset(&f, 0, -1);
		mui_menuitem_get_locations(win->ui, &f, &item, loc);

		if (item.icon[0])
			mui_font_text_draw(icons, dr,
					loc[0], item.icon, 0,
					mui_control_color[state].text);
		mui_font_text_draw(main, dr,
				loc[1], item.title, 0,
				mui_control_color[state].text);
	}
	mui_font_text_draw(icons, dr,
			C2_PT(inner.r - 32 + 8, inner.t + 2), "", 0,
			mui_control_color[state].text);
	mui_drawable_clip_pop(dr);
}
