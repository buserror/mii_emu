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



enum {
	MII_ABOUT_WINDOW_ID 		= FCC('a','b','o','t'),
	MII_ABOUT_OK 				= FCC('O','K','!',' '),
};

typedef struct mii_mui_about_t {
	mui_window_t			win;
} mii_mui_about_t;


static int
_mii_about_action_cb(
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
					// save the config
					mui_window_dispose(&m->win);
				}	break;
			}
			break;
	}
	return 0;
}

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
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 560, 240);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	w = mui_window_create(mui, wpos, NULL, MUI_WINDOW_LAYER_MODAL,
					"About the MII " MUI_GLYPH_IIE " Emulator",
					sizeof(mii_mui_about_t));
	mui_window_set_id(w, MII_ABOUT_WINDOW_ID);
	mii_mui_about_t * m = (mii_mui_about_t*)w;

	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, base_size * 4, base_size * 1.4);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), margin);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), margin);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"OK", MII_ABOUT_OK);
	c->key_equ = MUI_KEY_EQU(0, 13);

	return w;
}

