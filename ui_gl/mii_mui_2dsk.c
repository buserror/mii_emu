/*
 * mui_mui_2disks.c
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
#include <libgen.h>
#include "mui.h"

#include "mii_mui_settings.h"

enum {
	MII_2DSK_WINDOW_ID 		= FCC('2','d','s','k'),
	MII_2DSK_SAVE 			= FCC('s','a','v','e'),
	MII_2DSK_CANCEL 		= FCC('c','a','n','c'),
	MII_2DSK_SELECT1 		= FCC('s','e','l','1'),
	MII_2DSK_SELECT2 		= FCC('s','e','l','2'),
};

typedef struct mii_mui_2dsk_t {
	mui_window_t			win;
	uint8_t 				drive_kind;
	mui_control_t * 		load;
	uint32_t 				selecting;
	struct {
		mui_control_t *icon, *fname, *button;
	} 						drive[2];

	mii_2dsk_conf_t * 		dst;
	mii_2dsk_conf_t			config;
} mii_mui_2dsk_t;

static void
mii_mui_2dsk_load_conf(
		mii_mui_2dsk_t * m,
		mii_2dsk_conf_t * config)
{
	int ok = 0;
	for (int i = 0; i < 2; i++) {
		if (config->drive[i].disk[0]) {
			ok = 1;
			mui_control_set_state(m->drive[i].fname, MUI_CONTROL_STATE_NORMAL);
			char *dup = strdup(config->drive[i].disk);
			mui_control_set_title(m->drive[i].fname, basename(dup));
			free(dup);
			mui_control_set_state(m->drive[i].icon, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_title(m->drive[i].button, "Eject");
		} else {
			mui_control_set_state(m->drive[i].fname, MUI_CONTROL_STATE_DISABLED);
			mui_control_set_title(m->drive[i].fname, "Click \"Select\" to pick a file");
			mui_control_set_state(m->drive[i].icon, MUI_CONTROL_STATE_DISABLED);
			mui_control_set_title(m->drive[i].button, "Select…");
		}
	}
	if (ok)
		mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
//	else
//		mui_control_set_state(m->load, MUI_CONTROL_STATE_DISABLED);
}

static int
_mii_2dsk_stdfile_cb(
		mui_window_t *	w,
		void * 			cb_param, // mii_mui_2dsk_t
		uint32_t 		what,
		void * 			param)	// not used
{
	mii_mui_2dsk_t * m = cb_param;
	switch (what) {
		case MUI_STDF_ACTION_SELECT: {
			int idx = m->selecting == MII_2DSK_SELECT1 ? 0 : 1;
			char * path = mui_stdfile_get_selected_path(w);
			printf("%s select %s\n", __func__, path);
			strncpy(m->config.drive[idx].disk, path,
						sizeof(m->config.drive[idx].disk)-1);
			mii_mui_2dsk_load_conf(m, &m->config);
			mui_window_dispose(w);
		}	break;
		case MUI_STDF_ACTION_CANCEL:
			printf("%s cancel\n", __func__);
			mui_window_dispose(w);
			break;
	}
	return 0;
}

static int
_mii_2dsk_action_cb(
		mui_control_t * c,
		void * 			cb_param, // mii_mui_2dsk_t
		uint32_t 		what,
		void * 			param)	// not used
{
	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_mui_2dsk_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
			printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_2DSK_SAVE: {
					// save the config
					printf("%s save\n", __func__);
					if (m->dst)
						*m->dst = m->config;
					mui_window_action(&m->win,
							m->drive_kind == MII_2DSK_SMARTPORT ?
								MII_MUI_SMARTPORT_SAVE :
								MII_MUI_DISK2_SAVE,
							m->dst);
					mui_window_dispose(&m->win);
				}	break;
				case MII_2DSK_CANCEL: {
					// cancel the config
					printf("%s cancel\n", __func__);
					mui_window_dispose(&m->win);
				}	break;
				case MII_2DSK_SELECT1:
				case MII_2DSK_SELECT2: {
					mii_2dsk_conf_t * config = &m->config;
					// select a file
					m->selecting = uid; // remember which drive we're selecting
					int idx = uid == MII_2DSK_SELECT1 ? 0 : 1;
					if (config->drive[idx].disk[0]) {
						printf("%s eject %d\n", __func__, idx);
						config->drive[idx].disk[0] = 0;
						mii_mui_2dsk_load_conf(m, config);
					} else {
						printf("%s select %d\n", __func__, idx);
						mui_window_t * w = mui_stdfile_get(m->win.ui,
							C2_PT(0, 0),
							m->drive_kind == MII_2DSK_SMARTPORT ?
								"Select PO/HDV/2MG file to load" :
								"Select DSK file to load",
							m->drive_kind == MII_2DSK_SMARTPORT ?
									"\\.(po|hdv|2mg)$" :
									"\\.(woz|nib|dsk)$",
							getenv("HOME"),
							MUI_STDF_FLAG_REGEXP);
						mui_window_set_action(w, _mii_2dsk_stdfile_cb, m);
					}
				}	break;
			}
			break;
	}
	return 0;
}

struct mui_window_t *
mii_mui_load_2dsk(
		struct mui_t *mui,
		mii_2dsk_conf_t *config,
		uint8_t drive_kind)
{
	mui_t *ui = mui;
	float base_size = mui_font_find(ui, "main")->size;
	float margin = base_size * 0.7;
	mui_window_t *w = mui_window_get_by_id(mui, MII_2DSK_WINDOW_ID);
	if (w) {
		mui_window_select(w);
		return w;
	}
	c2_pt_t where = {};
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 640, 275);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	char label[128];
	sprintf(label, "Select Files for %s 1&2 (Slot %d)",
				drive_kind == MII_2DSK_SMARTPORT ? "SmartPort" : "Disk II",
				config->slot_id + 1);
	w = mui_window_create(mui,
					wpos, NULL, 0, label,
					sizeof(mii_mui_2dsk_t));
	mui_window_set_id(w, MII_2DSK_WINDOW_ID);
	mii_mui_2dsk_t * m = (mii_mui_2dsk_t*)w;
	m->drive_kind = drive_kind;

	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, base_size * 4, base_size * 1.4);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), margin);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), margin);
	m->load = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"OK", MII_2DSK_SAVE);
	c->key_equ = MUI_KEY_EQU(0, 13);
	c2_rect_left_of(&cf, cf.l, margin);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Cancel", MII_2DSK_CANCEL);
	c->key_equ = MUI_KEY_EQU(0, 27);

	c2_rect_set(&cf, margin, (margin / 2),
					c2_rect_width(&w->frame) - margin - 120,
					(margin/2) + base_size);
	c2_rect_t cp = cf;
	cp.l -= margin * 0.2;
	cp.b += base_size * 1.3;
	for (int i = 0; i < 2; i++) {
		char buf[32];
		snprintf(buf, sizeof(buf), "Drive %d:", i+1);
		c = mui_groupbox_new(w, cp, buf, MUI_CONTROL_TEXTBOX_FRAME);

		float icons_size = mui_font_find(ui, "icon_small")->size;
		c2_rect_bottom_of(&cf, cp.t, base_size);
		c2_rect_right_of(&cf, cp.l, margin * 0.5);
		cf.b = cf.t + icons_size;
		cf.r = cf.l + icons_size;
		m->drive[i].icon = c = mui_textbox_new(w, cf,
					MUI_ICON_FILE, "icon_small",
					MUI_TEXT_ALIGN_MIDDLE | MUI_TEXT_ALIGN_CENTER | 0);
		c->state = MUI_CONTROL_STATE_DISABLED;
		cf.l = cf.r;
		cf.r = cp.r - margin * 0.5;
		m->drive[i].fname = c = mui_textbox_new(w, cf,
							"Click \"Select\" to pick a file", NULL, 0);
		c->state = MUI_CONTROL_STATE_DISABLED;

		c2_rect_right_of(&cf, cp.r, margin * 0.8);
		cf.r = c2_rect_width(&w->frame) - margin * 1.2;
		c2_rect_inset(&cf, -4,-4);
		m->drive[i].button = c = mui_button_new(w,
						cf, MUI_BUTTON_STYLE_NORMAL,
						"Select…" , i == 0 ?
							MII_2DSK_SELECT1 : MII_2DSK_SELECT2);
		c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '1' + i);

		c2_rect_bottom_of(&cp, cp.b, margin * 0.2);
	}
	c2_rect_bottom_of(&cp, cp.t, margin * 0.8);
	cp.l = margin * 4;
	cp.r = c2_rect_width(&w->frame) - margin * 4;
	c = mui_separator_new(w, cp);

	c = NULL;
	TAILQ_FOREACH(c, &w->controls, self) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mii_2dsk_action_cb, m);
	}
	m->dst = config;
	m->config = *config;
	mii_mui_2dsk_load_conf(m, config);
	return w;
}

