/*
 * mui_mui_1mb.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>

#include "mui.h"
#include "mii_mui_settings.h"

enum {
	MII_1MB_WINDOW_ID 		= FCC('1','m','b','c'),
	MII_1MB_SAVE 			= FCC('s','a','v','e'),
	MII_1MB_CANCEL 			= FCC('c','a','n','c'),
	MII_1MB_SELECT 			= FCC('s','e','l','e'),
	MII_1MB_USE_FILE 		= FCC('r','o','m','0'),
	MII_1MB_USE_BIN 		= FCC('r','o','m','1'),
};

typedef struct mii_mui_1mb_t {
	mui_window_t			win;
	mui_control_t * 		load, *icon, *fname;
	mui_control_t * 		r0, *r1;

	mii_1mb_conf_t * 		dst;
	mii_1mb_conf_t 			config;
} mii_mui_1mb_t;

// TODO: Dedup that with mii_mui_2dsk.c
void
_size_string(
		size_t s,
		char *out,
		int out_size,
		uint16_t flags)
{
	if (s < 1024) {
		snprintf(out, out_size, "%ld", s);
		return;
	}
	s /= 1024;
	if (s < 1024)
		snprintf(out, out_size, "%ldK", s);
	else if (s < 1024*1024) {
		long r = ((s * 10) / (1024) % 10);
		if (r == 0 && (flags & 1))
			snprintf(out, out_size, "%ldM",
					s / (1024));
		else
			snprintf(out, out_size, "%ld.%ldM",
					s / (1024), r);
	} else {
		long r = ((s * 10) / (1024 * 1024) % 10);
		if (r == 0 && (flags & 1))
			snprintf(out, out_size, "%ldG",
					s / (1024 * 1024));
		else
			snprintf(out, out_size, "%ld.%ldG",
					s / (1024 * 1024), r);
	}
}

static int
_mii_check_1mb_file(
		struct mui_t *mui,
		const char * path)
{
	struct stat st;
	if (stat(path, &st) < 0) {
		char *msg;
		asprintf(&msg, "'%s': %s", path, strerror(errno));
		mui_alert(mui, C2_PT(0,0),
					"Could not find file",
					msg, MUI_ALERT_FLAG_OK);
		free(msg);
		return -1;
	}
	if (st.st_size != 1024*1024) {
		char stt[64];
		long delta = 1024*1024 - st.st_size;
		_size_string(delta < 0 ? -delta : delta, stt, sizeof(stt)-2, 1);
		strcat(stt + strlen(stt), "B");
		char *msg;
		asprintf(&msg, "File '%s' is the wrong size, %s too %s.",
					basename((char*)path),
					stt, delta < 0 ? "big" : "small");
		mui_alert(mui, C2_PT(0,0),
					"Invalid file",
					msg, MUI_ALERT_FLAG_OK);
		free(msg);
		return -1;
	}
	FILE * f = fopen(path, "rb");
	if (!f) {
		char *msg;
		asprintf(&msg, "Could not open '%s' for reading: %s",
					path, strerror(errno));
		mui_alert(mui, C2_PT(0,0),
					"Failed to open!",
					msg, MUI_ALERT_FLAG_OK);
		free(msg);
		return -1;
	}
	uint8_t buf[4];
	fseek(f, 512, SEEK_SET);
	fread(buf, 1, 4, f);
	fclose(f);
//	printf("%s %02x %02x %02x %02x\n", __func__, buf[0], buf[1], buf[2], buf[3]);
	if (buf[0] != 0x20 || buf[1] != 0x58 || buf[2] != 0xfc || buf[3] != 0xa0) {
		char *msg;
		asprintf(&msg, "WARNING: '%s' Lacks the 'bootloader' in block #1, "
					"this will likely not boot properly.", path);
		mui_alert(mui, C2_PT(0,0),
					"No bootloader found",
					msg, MUI_ALERT_FLAG_OK);
		free(msg);
		return 0;
	}
	return 1;
}

static int
_mii_1mb_stdfile_cb(
		mui_window_t *	w,
		void * 			cb_param, // mii_mui_1mb_t
		uint32_t 		what,
		void * 			param)	// not used
{
	mii_mui_1mb_t * m = cb_param;
	switch (what) {
		case MUI_STDF_ACTION_SELECT: {
			char * path = mui_stdfile_get_selected_path(w);
		//	printf("%s select %s\n", __func__, path);
			if (_mii_check_1mb_file(w->ui, path) < 0) {
				mui_control_set_title(m->fname, "Click \"Select\" to pick a file");
				mui_control_set_state(m->fname, MUI_CONTROL_STATE_DISABLED);
				mui_control_set_state(m->icon, MUI_CONTROL_STATE_DISABLED);
				mui_control_set_state(m->load, MUI_CONTROL_STATE_DISABLED);
				break;
			}
			strncpy(m->config.drive.disk, path, sizeof(m->config.drive.disk)-1);
			mui_control_set_state(m->fname, MUI_CONTROL_STATE_NORMAL);
			char *dup = strdup(path);
			mui_control_set_title(m->fname, basename(dup));
			free(dup);
			mui_control_set_state(m->icon, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_value(m->r0, 1);
			mui_control_set_value(m->r1, 0);
			m->config.use_default = 0;
			mui_window_dispose(w);
		}	break;
		case MUI_STDF_ACTION_CANCEL:
		//	printf("%s cancel\n", __func__);
			mui_window_dispose(w);
			break;
	}
	return 0;
}

static int
_mii_1mb_action_cb(
		mui_control_t * c,
		void * 			cb_param, // mii_mui_1mb_t
		uint32_t 		what,
		void * 			param)	// not used
{
//	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_mui_1mb_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
		//	printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_1MB_SAVE: {
					// save the config
			//		printf("%s save\n", __func__);
					if (m->dst)
						*m->dst = m->config;
					mui_window_action(&m->win, MII_MUI_1MB_SAVE, m->dst);
					mui_window_dispose(&m->win);
				}	break;
				case MII_1MB_CANCEL: {
					// cancel the config
			//		printf("%s cancel\n", __func__);
					mui_window_dispose(&m->win);
				}	break;
				case MII_1MB_SELECT: {
					// select a file
			//		printf("%s select\n", __func__);
					mui_window_t * w = mui_stdfile_get(m->win.ui,
						C2_PT(0, 0),
						"Select a file (Exactly 1MB in size)",
						"\\.(po|hdv|bin|rom)$",
						getenv("HOME"),
						MUI_STDF_FLAG_REGEXP);
					mui_window_set_action(w, _mii_1mb_stdfile_cb, m);
				}	break;
				case MII_1MB_USE_BIN:
					mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
					m->config.use_default = 1;
					break;
				case MII_1MB_USE_FILE: {
					m->config.use_default = 1;
					if (m->config.drive.disk[0] == 0) {
						mui_control_set_state(m->load, MUI_CONTROL_STATE_DISABLED);
					} else {
						m->config.use_default = 0;
						mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
					}
				}	break;
			}
			break;
	}
	return 0;
}

struct mui_window_t *
mii_mui_load_1mbrom(
		struct mui_t *mui,
		mii_1mb_conf_t *config)
{
	mui_t *ui = mui;
	float base_size = mui_font_find(ui, "main")->size;
	float margin = base_size * 0.7;
	mui_window_t *w = mui_window_get_by_id(mui, MII_1MB_WINDOW_ID);
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
					"1MB ROM Card File",
					sizeof(mii_mui_1mb_t));
	mui_window_set_id(w, MII_1MB_WINDOW_ID);
	mii_mui_1mb_t * m = (mii_mui_1mb_t*)w;
	m->dst = config;
	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, base_size * 4, base_size * 1.4);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), margin);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), margin);
	m->load = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"OK", MII_1MB_SAVE);
	c->key_equ = MUI_KEY_EQU(0, 13);
	c->state = MUI_CONTROL_STATE_DISABLED;
	c2_rect_left_of(&cf, cf.l, margin);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Cancel", MII_1MB_CANCEL);
	c->key_equ = MUI_KEY_EQU(0, 27);

	c2_rect_right_of(&cf, 0, margin);
	cf.r += 15;
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Select…" , MII_1MB_SELECT);
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 's');

	c2_rect_set(&cf, margin * 3, (margin/2),
					c2_rect_width(&w->frame) - margin, (margin/2) + base_size);
	c2_rect_t cp = cf;
	cp.l -= margin * 0.2;
	cp.b += base_size * 1.3;
	c = mui_groupbox_new(w, cp, "File to load:", MUI_CONTROL_TEXTBOX_FRAME);

	float icons_size = mui_font_find(ui, "icon_small")->size;
	c2_rect_bottom_of(&cf, cf.b, 0);
	cf.b = cf.t + icons_size;
	cf.r = cf.l + icons_size;
	m->icon = c = mui_textbox_new(w, cf, MUI_ICON_FILE, "icon_small",
				MUI_TEXT_ALIGN_MIDDLE | MUI_TEXT_ALIGN_CENTER | 0);
	c->state = MUI_CONTROL_STATE_DISABLED;
	cf.l = cf.r;
	cf.r = c2_rect_width(&w->content) - margin;
	m->fname = c = mui_textbox_new(w, cf,
						"Click \"Select\" to pick a file", NULL, 0);
	c->state = MUI_CONTROL_STATE_DISABLED;

	uint32_t uid_mask = FCC(0xff,0xff,0xff,0);
	cf.l = margin;
	m->r0 = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"", FCC('r','o','m','0'));
	c->uid_mask = uid_mask;
	c->value = config->use_default == 0;
	c2_rect_bottom_of(&cf, cf.b, margin * 0.8);
	m->r1 = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"Use Built-in image with a few games",
					FCC('r','o','m','1'));
	c->uid_mask = uid_mask;
	c->value = config->use_default != 0;

	c = mui_controls_first(&w->controls, MUI_CONTROLS_ALL);
	for (; c; c = mui_controls_next(c, MUI_CONTROLS_ALL)) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mii_1mb_action_cb, m);
	}
	m->config = *config;
	if (m->config.drive.disk[0] &&
				_mii_check_1mb_file(mui, m->config.drive.disk) >= 0) {
		char * path = m->config.drive.disk;
		mui_control_set_state(m->fname, MUI_CONTROL_STATE_NORMAL);
		char *dup = strdup(path);
		mui_control_set_title(m->fname, basename(dup));
		free(dup);
		mui_control_set_state(m->icon, MUI_CONTROL_STATE_NORMAL);
		mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
	} else {
		m->config.drive.disk[0] = 0;
		mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
	}
	return w;
}

