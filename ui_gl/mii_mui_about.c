/*
 * mui_mui_about.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mui.h"
#include "mii-icon-64.h"

extern const unsigned char mui_geneva_font_data[];
extern const unsigned int mui_geneva_font_size;

enum {
	MII_ABOUT_WINDOW_ID 		= FCC('a','b','o','t'),
	MII_ABOUT_OK 				= FCC('O','K','!',' '),
};

struct mui_drawable_control_t;
typedef struct mii_mui_about_t {
	mui_window_t			win;
	struct mui_drawable_control_t * text;
	uint8_t 				timer_id;
	bool					terminate;
} mii_mui_about_t;

//DECLARE_C_ARRAY(mui_drawable_t *, mui_drawable_array, 4);

typedef struct mui_drawable_control_t {
	mui_control_t 		control;
	uint16_t			flags;
	mui_drawable_t *	mask;
	mui_drawable_array_t drawables;
} mui_drawable_control_t;

IMPLEMENT_C_ARRAY(mui_drawable_array);

static void
mui_drawable_draw(
		mui_window_t * 	win,
		mui_control_t * c,
		mui_drawable_t *dr )
{
	c2_rect_t f = c->frame;
	c2_rect_offset(&f, win->content.l, win->content.t);

	mui_drawable_clip_push(dr, &f);
	mui_drawable_control_t *dc = (mui_drawable_control_t *)c;

	for (int i = 0; i < (int)dc->drawables.count; i++) {
		mui_drawable_t *d = dc->drawables.e[i];
		if (!d->pix.pixels)
			continue;
		c2_rect_t src = C2_RECT_WH(0, 0,
							d->pix.size.x,
							d->pix.size.y);
		c2_rect_offset(&src, d->origin.x, d->origin.y);

		pixman_image_composite32(PIXMAN_OP_OVER,
				mui_drawable_get_pixman(d),
				mui_drawable_get_pixman(dc->mask),
				mui_drawable_get_pixman(dr),
				src.l, src.t, 0, 0,
				f.l, f.t,
				c2_rect_width(&src), c2_rect_height(&src));
	}
	mui_drawable_clip_pop(dr);
}

static bool
mui_cdef_drawable(
		struct mui_control_t * 	c,
		uint8_t 				what,
		void * 					param)
{
//	mui_textbox_control_t *tb = (mui_textbox_control_t *)c;
	switch (what) {
		case MUI_CDEF_DRAW: {
			mui_drawable_t * dr = param;
			switch (c->type) {
				case 0:
					mui_drawable_draw(c->win, c, dr);
					break;
			}
		}	break;
		case MUI_CDEF_DISPOSE: {
			switch (c->type) {
				case 0: {
					mui_drawable_control_t *dc = (mui_drawable_control_t *)c;
					for (int i = 0; i < (int)dc->drawables.count; i++) {
						mui_drawable_t *d = dc->drawables.e[i];
						mui_drawable_dispose(d);
						free(d);
					}
					mui_drawable_dispose(dc->mask);
					if (dc->mask)
						free(dc->mask);
					mui_drawable_array_free(&dc->drawables);
				}	break;
			}
		}	break;
	}
	return false;
}


static mui_time_t
mui_about_timer_cb(
				struct mui_t * mui,
				mui_time_t 	now,
				void * 		param)
{
	mii_mui_about_t * m = (mii_mui_about_t*)param;

	if (m->terminate) {
		mui_window_dispose(&m->win);
		return 0;
	}
	mui_drawable_control_t *dc = m->text;
	mui_drawable_t * dr = dc->drawables.e[0];

	dr->origin.y++;
	int height = dr->pix.size.y + c2_rect_height(&dc->control.frame);
	if (dr->origin.y > dr->pix.size.y)
		dr->origin.y -= height;
//	printf("Y: %d/%d\n", dr->origin.y, height);
	mui_control_inval(&dc->control);
	return MUI_TIME_SECOND / 30;
}


static int
_mii_about_button_cb(
		mui_control_t * c,
		void * 			cb_param, // mii_mui_about_t
		uint32_t 		what,
		void * 			param)	// not used
{
	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_mui_about_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
			printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_ABOUT_OK: {
					m->terminate = true;
				}	break;
			}
			break;
	}
	return 0;
}


#ifndef MII_VERSION
#define MII_VERSION "0.0.0"
#endif
static const char * about =
	"\n"
	"The MII " MUI_GLYPH_IIE " Emulator\n"
	"Version " MII_VERSION "\n"
	"Built " __DATE__ " " __TIME__ "\n"
	"© Michel Pollet 2023-2024\n\n"
	"Thanks to:\n"
	;
static const char * thanksto =
	"Steve Wozniak\n"
	"Bill Atkinson\n"
	"Andy Hertzfeld\n"
	"Randy Wigginton\n"
	"Jef Raskin\n"
	"Susan Kare\n"
	"Thierry Magniez\n"
	"Charles \"regnips\" Springer\n"
	"Jeroen \"Sprite_tm\" Domburg\n"
	"Claude \"Claude\" Schwarz\n"
	"... and many others"
	;

struct mui_window_t *
mii_mui_about(
		struct mui_t *mui )
{
	mui_t *ui = mui;
	float base_size = mui_font_find(ui, "main")->size;
	float margin = base_size * 0.7;
	mui_window_t *w = mui_window_get_by_id(mui, MII_ABOUT_WINDOW_ID);
	if (w) {
		mui_window_select(w);
		return w;
	}
	c2_pt_t where = {};
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 500, 240);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	w = mui_window_create(mui, wpos, NULL, MUI_WINDOW_LAYER_MODAL,
					"About the MII " MUI_GLYPH_IIE " Emulator",
					sizeof(mii_mui_about_t));
	mui_window_set_id(w, MII_ABOUT_WINDOW_ID);

	mii_mui_about_t * m = (mii_mui_about_t*)w;

	mui_control_t * c = NULL, *ok = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, base_size * 4, base_size * 1.4);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), margin);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), margin);
	ok = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"OK", MII_ABOUT_OK);
	c->key_equ = MUI_KEY_EQU(0, 13);
	mui_control_set_action(c, _mii_about_button_cb, w);

	cf = C2_RECT_WH(margin, margin, mii_icon64[0], mii_icon64[1]);
	/*
	 	Icon
	 */
	c = mui_control_new(
				w, 0, mui_cdef_drawable,
				cf,  NULL, 0, sizeof(mui_drawable_control_t));
	mui_drawable_control_t *dc = (mui_drawable_control_t*)c;
	mui_drawable_t *dr = calloc(1, sizeof(mui_drawable_t));
	/* the Xorg icon is in fact using unsigned int (64 bits!) per pixels
	 * for some bizare reason. Here we convert it back to 32bpp */
	dr->pix.bpp = 32;
	dr->pix.size.x = mii_icon64[0];
	dr->pix.size.y = mii_icon64[1];
	dr->pix.row_bytes = dr->pix.size.x * 4;
	dr->pix.pixels = calloc(1, dr->pix.row_bytes * dr->pix.size.y);
	dr->dispose_pixels = true;

	/*
	 * if the clear color is 0, it means we are using OpenGL as a renderer
	 * this is not ideal as it means we need to reverse our internal format
	 * (ARGB) to the one used by OpenGL (ABGR)
	 */
	for (int y = 0; y < dr->pix.size.y; y++) {
		for (int x = 0; x < dr->pix.size.x; x++) {
			uint32_t *p = (uint32_t*)dr->pix.pixels;
			p += y * dr->pix.size.x + x;
			uint32_t pix = mii_icon64[2 + y * dr->pix.size.x + x];
			if (ui->color.clear.a == 0) {
				// ARGB -> ABGR
				*p = ((pix & 0xff00ff00) |
						((pix & 0x00ff0000) >> 16) |
						((pix & 0x000000ff) << 16));
			} else
				*p = pix;
		}
	}
	mui_drawable_array_add(&dc->drawables, dr);

	/*
	 * Text in two parts
	 */
	cf = C2_RECT_WH(cf.r + margin, 10,
				c2_rect_width(&w->frame) - cf.r - margin*2,
				ok->frame.t - margin);

	c2_rect_t tbox = cf;
	c2_rect_offset(&tbox, -tbox.l, -tbox.t);
	tbox.b = 1000;
	mui_font_t *font = mui_font_find(ui, "main");
	mui_font_t *geneva = mui_font_find(ui, "geneva");
	if (!geneva) {
		geneva = mui_font_from_mem(ui, "geneva", 24,
							mui_geneva_font_data, mui_geneva_font_size);
	}
	mui_glyph_line_array_t lines_about = {};
	mui_font_measure(font, tbox, about, 0, &lines_about, MUI_TEXT_ALIGN_CENTER);

	c2_rect_t about_frame = tbox;
	about_frame.b = 0;
	for (int li = 0; li < (int)lines_about.count; li++) {
		mui_glyph_array_t * line = &lines_about.e[li];
	//	printf("line %d x %d y %d w %d\n", li, line->x, line->y, line->w);
		about_frame.b = line->y;
	}
	mui_glyph_line_array_t lines_thanks = {};
	mui_font_measure(geneva, tbox, thanksto, 0, &lines_thanks, MUI_TEXT_ALIGN_CENTER);
	c2_rect_t frame_thanks = tbox;
	frame_thanks.b = 0;
	for (int li = 0; li < (int)lines_thanks.count; li++) {
		mui_glyph_array_t * line = &lines_thanks.e[li];
	//	printf("line %d x %d y %d w %d\n", li, line->x, line->y, line->w);
		frame_thanks.b = line->y;
	}
	c2_rect_offset(&frame_thanks, 0, about_frame.b + 0);

	c = mui_control_new(
				w, 0, mui_cdef_drawable,
				cf,  NULL, 0, sizeof(mui_drawable_control_t));
	tbox.b = frame_thanks.b + 2;

	m->text = dc = (mui_drawable_control_t*)c;

	dr = calloc(1, sizeof(mui_drawable_t));
	dr->pix.bpp = 32;
	dr->pix.size.x = c2_rect_width(&tbox);
	dr->pix.size.y = c2_rect_height(&tbox);
	dr->pix.row_bytes = dr->pix.size.x * 4;
	dr->pix.pixels = calloc(1, dr->pix.row_bytes * dr->pix.size.y);
	dr->dispose_pixels = true;
	mui_color_t text_color = MUI_COLOR(0x000000ff);
	mui_font_measure_draw(font, dr, tbox, &lines_about,
				text_color, MUI_TEXT_ALIGN_CENTER);
	mui_font_measure_draw(geneva, dr, frame_thanks, &lines_thanks,
				text_color, MUI_TEXT_ALIGN_CENTER);
	mui_drawable_array_add(&dc->drawables, dr);

	m->timer_id = mui_timer_register(ui,
						mui_about_timer_cb, m, MUI_TIME_SECOND);
	return w;
}

