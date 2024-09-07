/*
 * mui_mui_ssc.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include "mui.h"

#include "mii_mui_settings.h"
#include "mii_mui_utils.h"

enum {
	MII_SSC_WINDOW_ID 	= FCC('s','s','c','c'),
	MII_SSC_SAVE 		= FCC('s','a','v','e'),
	MII_SSC_CANCEL 		= FCC('c','a','n','c'),
	MII_SSC_SELECT 		= FCC('s','e','l','e'),
	MII_SSC_BAUD 		= FCC('b','a','u','d'),
	MII_SSC_BITS 		= FCC('b','i','t','0'),
	MII_SSC_PARITY 		= FCC('p','a','r','i'),
	MII_SSC_STOPS 		= FCC('s','t','o','p'),
	MII_SSC_HANDSHAKE 	= FCC('h','s','h','k'),
};


typedef struct mii_mui_ssc_t {
	mui_window_t			win;
	mii_mui_file_select_t	dev;
	mui_control_t *			load;
	mui_control_t *			baud, *handshake;
	mui_control_t *			parity[3];
	mui_control_t *			bits[2];
	mui_control_t *			stops[2];
	mii_ssc_conf_t *		dst, config;
} mii_mui_ssc_t;


#include <termios.h>
#include <pty.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

static int
_mii_ssc_check_device_file(
		mui_t * mui,
		char * path)
{
	char *error = NULL;
	// try tyo open device and do a control call on it
	// to see if it's a serial device
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		asprintf(&error, "Failed to open %s: %s", path, strerror(errno));
	}
	if (!error) {
		struct termios tio;
		if (tcgetattr(fd, &tio) < 0) {
			asprintf(&error, "Not a TTY device? %s: %s", path, strerror(errno));
		}
	}
	if (error) {
		mui_alert(mui, C2_PT(0,0), "Error", error, MUI_ALERT_FLAG_OK);
		free(error);
	}
	if (fd >= 0)
		close(fd);
	return error ? -1 : 0;
}

static void
mii_mui_ssc_load_conf(
		mii_mui_ssc_t * m,
		mii_ssc_conf_t * config)
{
	int ok = 1;

	mui_menu_items_t *items = mui_popupmenu_get_items(m->baud);
	printf("%s config %s %d bits %d parity %d stop %d hw %d\n",
			__func__, config->device, config->baud, config->bits,
			config->parity, config->stop, config->hw_handshake);
	for (int i = 0; items->e[i].title; i++) {
		if (items->e[i].uid == config->baud) {
			mui_control_set_value(m->baud, i);
			break;
		}
	}
	char *fname = config->device;
	if (fname[0] == 0) {
		mui_control_set_title(m->dev.fname, "Click \"Select\" to pick a device file");
		mui_control_set_state(m->dev.icon, MUI_CONTROL_STATE_DISABLED);
		mui_control_set_state(m->dev.fname, MUI_CONTROL_STATE_DISABLED);
		ok = 0;
	} else {
		char *dup = strdup(fname);
		mui_control_set_title(m->dev.fname, basename(dup));
		free(dup);
		if (_mii_ssc_check_device_file(m->win.ui, config->device) < 0) {
			ok = 0;
			mui_control_set_state(m->dev.icon, MUI_CONTROL_STATE_DISABLED);
			mui_control_set_state(m->dev.fname, MUI_CONTROL_STATE_DISABLED);
		} else {
			mui_control_set_state(m->dev.icon, MUI_CONTROL_STATE_NORMAL);
			mui_control_set_state(m->dev.fname, MUI_CONTROL_STATE_NORMAL);
		}
	}
	for (uint8_t i = 0; i < 2; i++)
		mui_control_set_value(m->bits[i], i == config->bits);
	for (uint8_t i = 0; i < 3; i++)
		mui_control_set_value(m->parity[i], i == config->parity);
	for (uint8_t i = 0; i < 2; i++)
		mui_control_set_value(m->stops[i], i == config->stop);
	mui_control_set_value(m->handshake, config->hw_handshake);

	if (ok)
		mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
	else
		mui_control_set_state(m->load, MUI_CONTROL_STATE_DISABLED);
}

static int
_mii_ssc_stdfile_cb(
		mui_window_t *	w,
		void * 			cb_param, // mii_mui_ssc_t
		uint32_t 		what,
		void * 			param)	// not used
{
	mii_mui_ssc_t * m = cb_param;
	switch (what) {
		case MUI_STDF_ACTION_SELECT: {
			char * path = mui_stdfile_get_selected_path(w);
			printf("%s select %s\n", __func__, path);
			if (_mii_ssc_check_device_file(m->win.ui, path) < 0) {
				mui_control_set_state(m->dev.icon, MUI_CONTROL_STATE_DISABLED);
				mui_control_set_state(m->dev.fname, MUI_CONTROL_STATE_DISABLED);
				mui_control_set_state(m->load, MUI_CONTROL_STATE_DISABLED);
			} else {
				strcpy(m->config.device, path);
				mui_control_set_state(m->dev.fname, MUI_CONTROL_STATE_NORMAL);
				char *dup = strdup(path);
				mui_control_set_title(m->dev.fname, basename(dup));
				free(dup);
				mui_control_set_state(m->dev.icon, MUI_CONTROL_STATE_NORMAL);
				mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
				mui_control_set_state(m->load, MUI_CONTROL_STATE_NORMAL);
			}
			mui_control_set_state(m->dev.icon, MUI_CONTROL_STATE_NORMAL);
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
_mii_ssc_action_cb(
		mui_control_t * c,
		void * 			cb_param, // mii_mui_ssc_t
		uint32_t 		what,
		void * 			param)	// not used
{
	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_mui_ssc_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
			printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_SSC_SAVE: {
					// save the config
					printf("%s save\n", __func__);
					if (m->dst)
						*m->dst = m->config;
					mui_window_action(&m->win, MII_MUI_SSC_SAVE, m->dst);
					mui_window_dispose(&m->win);
				}	break;
				case MII_SSC_CANCEL: {
					// cancel the config
					printf("%s cancel\n", __func__);
					mui_window_dispose(&m->win);
				}	break;
				case MII_SSC_SELECT: {
					// select a file
					printf("%s select\n", __func__);
					mui_window_t * w = mui_stdfile_get(m->win.ui,
						C2_PT(0, 0),
						"Select a Serial device in /dev",
						"(ttyS[0-9]+|tnt[0-9]+|ttyUSB[0-9]+|ttyACM[0-9]+)$",
						"/dev", MUI_STDF_FLAG_REGEXP|MUI_STDF_FLAG_NOPREF);
					mui_window_set_action(w, _mii_ssc_stdfile_cb, m);
				}	break;
				case MII_SSC_HANDSHAKE: {
					// toggle handshake
					printf("%s handshake\n", __func__);
					m->config.hw_handshake = mui_control_get_value(c);
				}	break;
				case FCC('b','i','t','0'):
				case FCC('b','i','t','1'):
					if (mui_control_get_value(c)) {
						m->config.bits = uid == FCC('b','i','t','1');
					}	break;
				case FCC('p','a','r','0'):
				case FCC('p','a','r','1'):
				case FCC('p','a','r','2'):
					if (mui_control_get_value(c)) {
						m->config.parity = uid == FCC('p','a','r','0') ? 0 :
										uid == FCC('p','a','r','1') ? 1 : 2;
					}	break;
				case FCC('s','t','o','0'):
				case FCC('s','t','o','1'):
					if (mui_control_get_value(c)) {
						m->config.stop = uid == FCC('s','t','o','1');
					}	break;
			}
			break;
		case MUI_CONTROL_ACTION_VALUE_CHANGED: {
			switch (uid) {
				case MII_SSC_BAUD: {
					// select a baud rate
					printf("%s baud\n", __func__);
					mui_menu_items_t *items = mui_popupmenu_get_items(c);
					// get value
					int32_t val = mui_control_get_value(c);
					printf("%s baud %d\n", __func__, items->e[val].uid);
					m->config.baud = items->e[val].uid;
				}	break;
			}
		}	break;
	}
	return 0;
}

struct mui_window_t *
mii_mui_configure_ssc(
		struct mui_t *mui,
		mii_ssc_conf_t *config)
{
	mui_t *ui = mui;
	float base_size = mui_font_find(ui, "main")->size;
	float margin = base_size * 0.7;
	float base_height = base_size * 1.4;
	mui_window_t *w = mui_window_get_by_id(mui, MII_SSC_WINDOW_ID);
	if (w) {
		mui_window_select(w);
		return w;
	}
	c2_pt_t where = {};
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 620, 320);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	char *label;
	asprintf(&label, "Super Serial Card (Slot %d)",
				config->slot_id + 1);
	w = mui_window_create(mui, wpos, NULL, MUI_WINDOW_LAYER_MODAL,
					label, sizeof(mii_mui_ssc_t));
	mui_window_set_id(w, MII_SSC_WINDOW_ID);
	mii_mui_ssc_t * m = (mii_mui_ssc_t*)w;
	m->dst = config;
	m->config = *config;

	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, base_size * 4, base_height);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), margin);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), margin);
	m->load = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"OK", MII_SSC_SAVE);
	c->key_equ = MUI_KEY_EQU(0, 13);
	c->state = MUI_CONTROL_STATE_DISABLED;
	c2_rect_left_of(&cf, cf.l, margin);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Cancel", MII_SSC_CANCEL);
	c->key_equ = MUI_KEY_EQU(0, 27);

	c2_rect_set(&cf, margin, (margin/2),
					c2_rect_width(&w->frame) - margin - 0,
					(margin/2) + base_size);

	c2_rect_t cp = cf;
	cp.l -= margin * 0.2;
	cp.b += base_size * 1.3;

	mii_mui_fileselect_widget(&m->dev, w, &cf, "Device:", "Select…", NULL);
	m->dev.button->uid = MII_SSC_SELECT;
	m->dev.button->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 's');

	c2_rect_right_of(&cf, 0, margin);
	c2_rect_bottom_of(&cf, cf.b, margin);
	cf.r = cf.l + 80;
	cf.b = cf.t + 34;
	c = mui_textbox_new(w, cf, "Baud:", NULL,
			MUI_TEXT_ALIGN_RIGHT|MUI_TEXT_ALIGN_MIDDLE);

	c2_rect_t popr = cf;
	c2_rect_right_of(&popr, cf.r, margin/2);
	popr.b = cf.t + 34;
	popr.r = popr.l + 160;
	m->baud = c = mui_popupmenu_new(w,
				popr, "Popup", MII_SSC_BAUD, MUI_TEXT_ALIGN_CENTER);
	mui_menu_items_t *items = mui_popupmenu_get_items(c);
	mui_menu_items_add(items, (mui_menu_item_t){.title="1200", .uid=1200 });
	mui_menu_items_add(items, (mui_menu_item_t){.title="2400", .uid=2400 });
	mui_menu_items_add(items, (mui_menu_item_t){.title="4800", .uid=4800 });
	mui_menu_items_add(items, (mui_menu_item_t){.title="9600", .uid=9600 });
	mui_menu_items_add(items, (mui_menu_item_t){.title="19200", .uid=19200 });
	// popup needs to be NULL terminated, AND prepared()
	mui_menu_items_add(items, (mui_menu_item_t){.title=NULL });
	mui_popupmenu_prepare(c);

	// this tell libmui that it can clear the radio values of the 'sister'
	// radio buttons when one matching the uid&mask is selected
	uint32_t uid_mask = FCC(0xff,0xff,0xff,0);
	c2_rect_right_of(&cf, popr.r, margin);
	cf.t += 1;
	cf.b = cf.t + base_size;
	cf.r = cf.l + 95;
	c2_rect_t base_radio = cf;
	base_radio.l = margin;
	m->bits[0] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"8 bits", FCC('b','i','t','0'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '8');
	c->uid_mask = uid_mask;
	c->value = 1;
	c2_rect_right_of(&cf, cf.r, margin);
	cf.r = cf.l + 160;
	m->bits[1] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"7 bits Data", FCC('b','i','t','1'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '7');
	c->uid_mask = uid_mask;

	cf = base_radio;
	c2_rect_bottom_of(&cf, cf.b, margin);
	cf.r = cf.l + 70;
	m->parity[0] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"No", FCC('p','a','r','0'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 'n');
	c->uid_mask = uid_mask;
	c->value = 1;
	c2_rect_right_of(&cf, cf.r, margin);
	cf.r = cf.l + 80;
	m->parity[1] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"Odd", FCC('p','a','r','1'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 'o');
	c->uid_mask = uid_mask;
	c2_rect_right_of(&cf, cf.r, margin);
	cf.r = cf.l + 170;
	m->parity[2] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"Even Parity", FCC('p','a','r','2'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 'e');
	c->uid_mask = uid_mask;

	c2_rect_right_of(&cf, cf.r, margin);
//	cf.l = base_radio.l;
	cf.r = cf.l + 50;
	m->stops[0] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"1", FCC('s','t','o','0'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '1');
	c->uid_mask = uid_mask;
	c->value = 1;
	c2_rect_right_of(&cf, cf.r, margin);
	cf.r = cf.l + 120;
	m->stops[1] = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_RADIO,
					"2 Stops", FCC('s','t','o','1'));
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '2');
	c->uid_mask = uid_mask;

	cf.b = cf.t + base_size;
	c2_rect_bottom_of(&cf, cf.b, margin);
	c2_rect_right_of(&cf, 0, margin);
	cf.r = cf.l + 280;
	m->handshake = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_CHECKBOX,
					"Hardware Handshake",
					MII_SSC_HANDSHAKE);
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 'h');
	c = mui_controls_first(&w->controls, MUI_CONTROLS_ALL);
	for (; c; c = mui_controls_next(c, MUI_CONTROLS_ALL)) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mii_ssc_action_cb, m);
	}
	mii_mui_ssc_load_conf(m, &m->config);
	return w;
}

