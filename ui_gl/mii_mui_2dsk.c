/*
 * mui_mui_2disks.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <libgen.h>
#include "mui.h"

#include "mii_mui_utils.h"
#include "mii_mui_settings.h"

enum {
	MII_2DSK_WINDOW_ID 		= FCC('2','d','s','k'),
	MII_2DSK_SAVE 			= FCC('s','a','v','e'),
	MII_2DSK_CANCEL 		= FCC('c','a','n','c'),
	MII_2DSK_SELECT1 		= FCC('s','e','l','1'),
	MII_2DSK_SELECT2 		= FCC('s','e','l','2'),
	MII_2DSK_WP1 			= FCC('w','p','1',' '),
	MII_2DSK_WP2 			= FCC('w','p','2',' '),
};

typedef struct mii_mui_2dsk_t {
	mui_window_t			win;
	uint8_t 				drive_kind;
	mui_control_t * 		load;
	uint32_t 				selecting;
	mii_mui_file_select_t	drive[2];
	mii_2dsk_conf_t * 		dst;
	mii_2dsk_conf_t			config;
} mii_mui_2dsk_t;

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

typedef struct mii_imagefile_check_t {
	char * 	error;
	char * 	warning;
	int 	file_ro;
	int		file_ro_format;
} mii_imagefile_check_t;

#define NIB_SIZE  	232960;
#define DSK_SIZE	143360;

// TODO move that to some common place
void
_size_string(
		size_t s,
		char *out,
		int out_size,
		uint16_t flags);

static int
_mii_floppy_check_file(
		const char * path,
		mii_imagefile_check_t * out)
{
	char *filename = basename((char*)path);

	out->file_ro = 0;
	out->file_ro_format = 0;
	out->error = NULL;
	out->warning = NULL;

	struct stat st;
	if (stat(path, &st) < 0) {
		asprintf(&out->error, "'%s': %s", filename, strerror(errno));
		return -1;
	}
	// has to have one
	char * suffix = strrchr(path, '.');
	if (!suffix) {
		asprintf(&out->error, "'%s' has no extension.", filename);
		return -1;
	}
	int want_size = 0;
	int iswoz = 0;
	if (!strcasecmp(suffix, ".nib")) {
		want_size = NIB_SIZE;
		out->file_ro_format = 0;
	} else if (!strcasecmp(suffix, ".dsk")) {
		want_size = DSK_SIZE;
		out->file_ro_format = 0;
	} else if (!strcasecmp(suffix, ".po") ||
				!strcasecmp(suffix, ".do")) {
		want_size = DSK_SIZE;
		out->file_ro_format = 0;
	} else if (!strcasecmp(suffix, ".woz") ||
				!strcasecmp(suffix, ".woz1") ||
				!strcasecmp(suffix, ".woz2")) {
		out->file_ro = 0;
		out->file_ro_format = 0;
		iswoz = 1;
	} else {
		asprintf(&out->error, "'%s' has an unknown extension.", filename);
		return -1;
	}
	if (out->error)
		return -1;
	if (want_size && st.st_size != want_size) {
		char stt[64];
		long delta = want_size - st.st_size;
		_size_string(delta < 0 ? -delta : delta, stt, sizeof(stt)-2, 1);
		strcat(stt + strlen(stt), "B");
		asprintf(&out->error,
				"File '%s' is the wrong size, %s too %s.",
				filename,
				stt, delta < 0 ? "big" : "small");
		return -1;
	}
	if (out->error)
		return -1;
	int fd = open(path, O_RDWR, 0);
	if (fd < 0) {
		fd = open(path, O_RDONLY, 0);
		if (fd < 0) {
			asprintf(&out->error, "'%s': %s", filename, strerror(errno));
			return -1;
		} else
			out->file_ro = 1;
	}
	if (iswoz) {
		// check the woz header
		uint8_t header[4];
		if (read(fd, header, sizeof(header)) != sizeof(header)) {
			asprintf(&out->error,
					"'%s': could not check WOZ header. Invalid file?",
					filename);
			close(fd);
			return -1;
		}
		if (memcmp(header, "WOZ1", 4) != 0 && memcmp(header, "WOZ2", 4) != 0) {
			asprintf(&out->error,
					"'%s' is not detected as a valid WOZ file.",
					filename);
			close(fd);
			return -1;
		}
	}
	close(fd);
	if (out->file_ro_format && !out->warning) {
		asprintf(&out->warning, "%s format is Read Only.", suffix);
	}
	if (out->file_ro && !out->warning) {
		asprintf(&out->warning, "File lacks write permissions.");
	}
	return 0;
}


static void
mii_mui_2dsk_load_conf(
		mii_mui_2dsk_t * m,
		mii_2dsk_conf_t * config)
{
	int ok = 1;
	for (int i = 0; i < 2; i++) {
		if (config->drive[i].disk[0]) {
			ok = 1;
			mii_imagefile_check_t check = {};
			if (m->drive_kind == MII_2DSK_DISKII) {
				if (_mii_floppy_check_file(config->drive[i].disk, &check) < 0) {
					mui_alert(m->win.ui, C2_PT(0,0),
								"Invalid Disk Image",
								check.error, MUI_ALERT_FLAG_OK);
					free(check.error);
					ok = 0;
				}
			}
			config->drive[i].ro_file = check.file_ro;
			config->drive[i].ro_format = check.file_ro_format;
			mui_control_set_state(m->drive[i].fname, MUI_CONTROL_STATE_NORMAL);
			char *dup = strdup(config->drive[i].disk);
			mui_control_set_title(m->drive[i].fname, basename(dup));
			free(dup);
			mui_control_set_state(m->drive[i].icon, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_title(m->drive[i].button, "Eject");
			if (check.warning) {
				mui_control_set_title(m->drive[i].warning, check.warning);
				mui_control_set_state(m->drive[i].checkbox, MUI_CONTROL_STATE_DISABLED);
				free(check.warning);
			} else {
				mui_control_set_title(m->drive[i].warning, "");
				mui_control_set_state(m->drive[i].checkbox, MUI_CONTROL_STATE_NORMAL);
			}
		} else {
			config->drive[i].ro_file = config->drive[i].ro_format	= 0;
			mui_control_set_state(m->drive[i].fname, MUI_CONTROL_STATE_DISABLED);
			mui_control_set_title(m->drive[i].fname, "Click \"Select\" to pick a file");
			mui_control_set_state(m->drive[i].icon, MUI_CONTROL_STATE_DISABLED);
			mui_control_set_title(m->drive[i].button, "Select…");
			mui_control_set_state(m->drive[i].checkbox, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_title(m->drive[i].warning, "");
		}
		mui_control_set_value(m->drive[i].checkbox,
			(config->drive[i].wp || config->drive[i].ro_file ||
				config->drive[i].ro_format) ? 1 : 0);
	}
	if (ok)
		mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
	else
		mui_control_set_state(m->load, MUI_CONTROL_STATE_DISABLED);
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
		//	printf("%s select %s\n", __func__, path);
			strncpy(m->config.drive[idx].disk, path,
						sizeof(m->config.drive[idx].disk)-1);
			free(path);
			mui_window_dispose(w);
			mii_mui_2dsk_load_conf(m, &m->config);
		}	break;
		case MUI_STDF_ACTION_CANCEL:
		//	printf("%s cancel\n", __func__);
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
//	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_mui_2dsk_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
			printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_2DSK_SAVE: {
					// save the config
				//	printf("%s save\n", __func__);
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
				//	printf("%s cancel\n", __func__);
					mui_window_dispose(&m->win);
				}	break;
				case MII_2DSK_SELECT1:
				case MII_2DSK_SELECT2: {
					mii_2dsk_conf_t * config = &m->config;
					// select a file
					m->selecting = uid; // remember which drive we're selecting
					int idx = uid == MII_2DSK_SELECT1 ? 0 : 1;
					if (config->drive[idx].disk[0]) {
					//	printf("%s eject %d\n", __func__, idx);
						config->drive[idx].disk[0] = 0;
						mii_mui_2dsk_load_conf(m, config);
					} else {
					//	printf("%s select %d\n", __func__, idx);
						mui_window_t * w = mui_stdfile_get(m->win.ui,
							C2_PT(0, 0),
							m->drive_kind == MII_2DSK_SMARTPORT ?
								"Select PO/HDV/2MG file to load" :
								"Select WOZ/DSK/NIB/PO/DO file to load",
							m->drive_kind == MII_2DSK_SMARTPORT ?
									"\\.(po|hdv|2mg)$" :
									"\\.(woz|nib|dsk|po|do)$",
							getenv("HOME"),
							MUI_STDF_FLAG_REGEXP);
						mui_window_set_action(w, _mii_2dsk_stdfile_cb, m);
					}
				}	break;
				case MII_2DSK_WP1:
				case MII_2DSK_WP2: {
					int idx = uid == MII_2DSK_WP1 ? 0 : 1;
					m->config.drive[idx].wp = mui_control_get_value(c);
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
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 640, 370);
	c2_rect_offset(&wpos,
		(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
		(ui->screen_size.y * 0.45) - (c2_rect_height(&wpos) / 2));

	char *label;
	asprintf(&label, "Select Files for %s 1&2 (Slot %d)",
				drive_kind == MII_2DSK_SMARTPORT ? "SmartPort" : "Disk II",
				config->slot_id + 1);
	w = mui_window_create(mui, wpos, NULL, MUI_WINDOW_LAYER_MODAL,
					label, sizeof(mii_mui_2dsk_t));
	free(label);
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
					c2_rect_width(&w->frame) - margin,
					(margin / 2) + base_size);
	c2_rect_t cp = cf;
	cp.l -= margin * 0.2;
	cp.b += base_size * 1.3;
	for (int i = 0; i < 2; i++) {
		mii_mui_file_select_t * fs = &m->drive[i];
		mii_mui_fileselect_widget(fs, w, &cp,
			i == 0 ? "Drive 1:" : "Drive 2:",
			"Select…",
			"Write Protect");
		m->drive[i].button->uid = i == 0 ? MII_2DSK_SELECT1 : MII_2DSK_SELECT2;
		m->drive[i].button->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '1' + i);
		if (drive_kind == MII_2DSK_SMARTPORT)
			m->drive[i].checkbox->state = MUI_CONTROL_STATE_DISABLED;
		else {
			mui_control_set_title(m->drive[i].icon, MUI_ICON_FLOPPY5);
		}
		m->drive[i].checkbox->uid = i == 0 ? MII_2DSK_WP1 : MII_2DSK_WP2;

		c2_rect_bottom_of(&cp, cp.b, margin);
	}
	cp = m->drive[1].box->frame;
	cp.b = cp.t + 2;
	c2_rect_top_of(&cp, m->drive[1].box->frame.t, margin * 0.4);
	cp.l = margin * 4;
	cp.r = c2_rect_width(&w->frame) - margin * 4;
	c = mui_separator_new(w, cp);

	c = mui_controls_first(&w->controls, MUI_CONTROLS_ALL);
	for (; c; c = mui_controls_next(c, MUI_CONTROLS_ALL)) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mii_2dsk_action_cb, m);
	}
	m->dst = config;
	m->config = *config;
	mii_mui_2dsk_load_conf(m, config);

	return w;
}

