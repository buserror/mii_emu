/*
 * mui_mui_loadbin.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include "mui.h"

#include "mii_mui_settings.h"
#include "mii_mui_utils.h"

enum {
	MII_LBIN_WINDOW_ID 		= FCC('l','b','i','n'),
	MII_LBIN_SAVE 			= FCC('s','a','v','e'),
	MII_LBIN_CANCEL 		= FCC('c','a','n','c'),
	MII_LBIN_SELECT 		= FCC('s','e','l','e'),
	MII_LBIN_AUTO_RELOAD 	= FCC('a','u','t','o'),

	MII_LBIN_ADDR_0300		= FCC('a','d','d','0'),
	MII_LBIN_ADDR_0800		= FCC('a','d','d','1'),
	MII_LBIN_ADDR_2000		= FCC('a','d','d','2'),
};

typedef struct mii_mui_loadbin_t {
	mui_window_t			win;
	mii_mui_file_select_t	file;
	mui_control_t *			load;
	mii_loadbin_conf_t *	dst, config;
} mii_mui_loadbin_t;

static int
_mii_loadbin_stdfile_cb(
		mui_window_t *	w,
		void * 			cb_param, // mii_mui_loadbin_t
		uint32_t 		what,
		void * 			param)	// not used
{
	mii_mui_loadbin_t * m = cb_param;
	switch (what) {
		case MUI_STDF_ACTION_SELECT: {
			char * path = mui_stdfile_get_selected_path(w);
			printf("%s select %s\n", __func__, path);
			mui_control_set_state(m->file.fname, MUI_CONTROL_STATE_NORMAL);
			strncpy(m->config.path, path, sizeof(m->config.path)-1);
			char *dup = strdup(path);
			mui_control_set_title(m->file.fname, basename(dup));
			free(dup);
			mui_control_set_state(m->file.icon, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
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
_mii_loadbin_action_cb(
		mui_control_t * c,
		void * 			cb_param, // mii_mui_loadbin_t
		uint32_t 		what,
		void * 			param)	// not used
{
	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_mui_loadbin_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
			printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_LBIN_SAVE: {
					// save the config
					printf("%s save\n", __func__);
					if (m->dst)
						*m->dst = m->config;
					mui_window_action(&m->win, MII_MUI_LOADBIN_SAVE, m->dst);
					mui_window_dispose(&m->win);
				}	break;
				case MII_LBIN_CANCEL: {
					// cancel the config
					printf("%s cancel\n", __func__);
					mui_window_dispose(&m->win);
				}	break;
				case MII_LBIN_SELECT: {
					// select a file
					printf("%s select\n", __func__);
					mui_window_t * w = mui_stdfile_get(m->win.ui,
						C2_PT(0, 0),
						"Select a .bin file to run",
						"\\.(bin|rom)$",
						getenv("HOME"),
						MUI_STDF_FLAG_REGEXP);
					mui_window_set_action(w, _mii_loadbin_stdfile_cb, m);
				}	break;
				case MII_LBIN_AUTO_RELOAD: {
					// toggle auto reload
					m->config.auto_reload = !!mui_control_get_value(c);
					printf("%s auto reload %d\n", __func__, m->config.auto_reload);
				}	break;
				case MII_LBIN_ADDR_0300:
				case MII_LBIN_ADDR_0800:
				case MII_LBIN_ADDR_2000: {
					int idx = FCC_INDEX(uid);
					static const uint16_t addrs[] = { 0x0300, 0x0800, 0x2000 };
					m->config.addr = addrs[idx];
					printf("%s load addr $%04x\n", __func__, m->config.addr);
				}	break;
			}
			break;
	}
	return 0;
}

struct mui_window_t *
mii_mui_load_binary(
		struct mui_t *mui,
		mii_loadbin_conf_t *config)
{
	mui_t *ui = mui;
	float base_size = mui_font_find(ui, "main")->size;
	float margin = base_size * 0.7;
	mui_window_t *w = mui_window_get_by_id(mui, MII_LBIN_WINDOW_ID);
	if (w) {
		mui_window_select(w);
		return w;
	}
	c2_pt_t where = {};
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 580, 294);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	w = mui_window_create(mui,
					wpos,
					NULL, 0,
					"Load&Run Binary File",
					sizeof(mii_mui_loadbin_t));
	mui_window_set_id(w, MII_LBIN_WINDOW_ID);
	mii_mui_loadbin_t * m = (mii_mui_loadbin_t*)w;
	m->dst = config;

	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, base_size * 4, base_size * 1.4);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), margin);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), margin);
	m->load = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"Load", MII_LBIN_SAVE);
	c->key_equ = MUI_KEY_EQU(0, 13);
	c->state = MUI_CONTROL_STATE_DISABLED;
	c2_rect_left_of(&cf, cf.l, margin);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Cancel", MII_LBIN_CANCEL);
	c->key_equ = MUI_KEY_EQU(0, 27);

	cf.b = cf.t + base_size;
	c2_rect_top_of(&cf, cf.t, margin);
	c2_rect_right_of(&cf, 0, margin);
	cf.r = cf.l + 200;
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_CHECKBOX,
					"Auto Reload", MII_LBIN_AUTO_RELOAD);
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 'r');
	// this tell libmui that it can clear the radio values of the 'sister'
	// radio buttons when one matching the uid&mask is selected
	uint32_t uid_mask = FCC(0xff,0xff,0xff,0);
	c2_rect_top_of(&cf, cf.t, 10);
//	cf.l += margin * 2;
	cf.r = cf.l + 110;
	c = mui_button_new(w, cf, MUI_BUTTON_STYLE_RADIO,
					"$0300", MII_LBIN_ADDR_0300);
	c->uid_mask = uid_mask;
	c->value = 1;
	int inter_button_margin = margin/3;
	c2_rect_right_of(&cf, cf.r, inter_button_margin);
	c = mui_button_new(w, cf, MUI_BUTTON_STYLE_RADIO,
					"$0800", MII_LBIN_ADDR_0800);
	c->uid_mask = uid_mask;
	c2_rect_right_of(&cf, cf.r, inter_button_margin);
	c = mui_button_new(w, cf, MUI_BUTTON_STYLE_RADIO,
					"$2000", MII_LBIN_ADDR_2000);
	c->uid_mask = uid_mask;

	c2_rect_right_of(&cf, cf.r, inter_button_margin);
	cf.r = cf.l + 90;
	c = mui_textbox_new(w, cf, "Other: $", NULL, 0);
	c->state = MUI_CONTROL_STATE_DISABLED;
	c2_rect_right_of(&cf, cf.r, 4);
	c2_rect_t tbf = cf;
	tbf.r = c2_rect_width(&w->frame) - margin*2;
	tbf.b = cf.t + base_size * 1.3;
	c2_rect_bottom_of(&tbf, cf.t, -margin/4);
	c = mui_textedit_control_new(w, tbf, MUI_CONTROL_TEXTBOX_FRAME);
	mui_textedit_set_text(c, "4000");
	c->state = MUI_CONTROL_STATE_DISABLED;

	c2_rect_right_of(&cf, 0, margin);
	c2_rect_top_of(&cf, cf.t, margin / 2);
	cf.r = cf.l + 200;
	c = mui_textbox_new(w, cf, "Load address:", NULL, 0);

	c2_rect_set(&cf, margin, (margin/2),
					c2_rect_width(&w->frame) - margin, (margin/2) + base_size);
	c2_rect_t cp = cf;
	cp.l -= margin * 0.2;
	cp.b += base_size * 1.3;

	mii_mui_fileselect_widget(&m->file, w, &cf, "Device:", "Select…", NULL);
	m->file.button->uid = MII_LBIN_SELECT;
	m->file.button->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 's');

	c = mui_controls_first(&w->controls, MUI_CONTROLS_ALL);
	for (; c; c = mui_controls_next(c, MUI_CONTROLS_ALL)) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mii_loadbin_action_cb, m);
	}
	m->config = *config;

	if (m->config.path[0]) {
		char * path = m->config.path;
		mui_control_set_state(m->file.fname, MUI_CONTROL_STATE_NORMAL);
		char *dup = strdup(path);
		mui_control_set_title(m->file.fname, basename(dup));
		free(dup);
		mui_control_set_state(m->file.icon, MUI_CONTROL_STATE_NORMAL);
		mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
	} else {
		m->config.path[0] = 0;
	//	mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
	}
	switch (m->config.addr) {
		case 0x0300:
			mui_control_set_value(
					mui_control_get_by_id(w,
								FCC_INDEXED(MII_LBIN_ADDR_0300, 0)), 1);
			break;
		case 0x0800:
			mui_control_set_value(
					mui_control_get_by_id(w,
								FCC_INDEXED(MII_LBIN_ADDR_0300, 1)), 1);
			break;
		case 0x2000:
			mui_control_set_value(
					mui_control_get_by_id(w,
								FCC_INDEXED(MII_LBIN_ADDR_0300, 2)), 1);
			break;
	}
	// now check the auto reload
	mui_control_set_value(
			mui_control_get_by_id(w, MII_LBIN_AUTO_RELOAD),
			m->config.auto_reload);
	return w;
}

