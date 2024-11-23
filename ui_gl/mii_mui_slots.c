/*
 * mui_mui_slots.c
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

#include "mui.h"
#include "mii_mui_settings.h"


typedef struct mii_ui_machine_config_t {
	mui_window_t			win;

	mui_control_t * 		slot[8];

	// for sub-windows
	unsigned 				slot_id;
	mii_machine_config_t *	dst;
	mii_machine_config_t	config;
} mii_ui_machine_config_t;

const mii_machine_config_t mii_default_config = {
	.no_slot_clock = 1,
	.titan_accelerator = 0,
	.audio_volume = 5.0,
	.slot = {
		[0] = {
//			.driver = MII_SLOT_DRIVER_SSC,
			.conf.ssc = {
				.kind = 0,
				.socket_port = 1969,
				.device = "/dev/ttyS0",
				.baud = 9600,
				.bits = 8,
				.parity = 0,
				.stop = 0,
			}
		},
		[1] = { .driver = 0, },
		[2] = { .driver = 0, },
		[3] = {
			.driver = MII_SLOT_DRIVER_MOUSE,
		},
		[4] = { .driver = 0, },
		[5] = {
			.driver = MII_SLOT_DRIVER_DISK2,
		},
		[6] = {
			.driver = MII_SLOT_DRIVER_SMARTPORT,
		},
	},
};

const mii_machine_config_t mii_default_config_2c
			__attribute__((unused)) = {
	.no_slot_clock = 1,
	.titan_accelerator = 0,
	.audio_volume = 5.0,
	.slot = {
		[0] = {
			.driver = MII_SLOT_DRIVER_SSC,
			.conf.ssc = {
				.kind = 0,
				.socket_port = 1969,
				.device = "/dev/ttyS0",
				.baud = 9600,
				.bits = 8,
				.parity = 0,
				.stop = 0,
			}
		},
		[1] = {
			.driver = MII_SLOT_DRIVER_SSC,
			.conf.ssc = {
				.kind = 0,
				.socket_port = 1970,
				.device = "/dev/ttyS1",
				.baud = 9600,
				.bits = 8,
				.parity = 0,
				.stop = 0,
			}
		},
		[2] = { .driver = 0, },
		[3] = {
			.driver = MII_SLOT_DRIVER_MOUSE,
		},
		[4] = {
			.driver = MII_SLOT_DRIVER_SMARTPORT,
		},
		[5] = {
			.driver = MII_SLOT_DRIVER_DISK2,
		},
		[6] = {
		},
	},
};

enum {
	MII_SLOT_WINDOW_ID 		= FCC('s','l','o','t'),
	MII_SLOT_SAVE 			= FCC('s','a','v','e'),
	MII_SLOT_CANCEL 		= FCC('c','a','n','c'),
	MII_SLOT_DEFAULT 		= FCC('d','e','f','a'),
	MII_SLOT_NSC 			= FCC('n','s','c','l'),
	MII_SLOT_TITAN 			= FCC('t','i','t','a'),
	MII_SLOT_DRIVER_POP 	= FCC('d','r','v','p'),
	MII_SLOT_DRIVER_CONF 	= FCC('d','r','v','c'),
};

static const struct {
	char * 	label;
	int		has_config;
} _mii_slot_drivers[] = {
	[MII_SLOT_DRIVER_NONE] 		= { "None", 0 },
	[MII_SLOT_DRIVER_SMARTPORT]	= { "SmartPort", 1 },
	[MII_SLOT_DRIVER_DISK2] 	= { "Disk II", 1 },
	[MII_SLOT_DRIVER_MOUSE] 	= { "Mouse", 0 },
	[MII_SLOT_DRIVER_SSC] 		= { "Super Serial", 1 },
	[MII_SLOT_DRIVER_ROM1MB]	= { "ROM 1MB", 1 },
//	[MII_SLOT_DRIVER_MOCKINGBOARD] = { "Mockingboard", },
#ifdef MII_DANII
//	[MII_SLOT_DRIVER_DANII]		= { "DAN ][", 0 },
#endif
	{ NULL, 0 },
};

static void
_mii_config_win_to_conf(
		mii_ui_machine_config_t * m)
{
	mui_window_t * w = &m->win;
	mii_machine_config_t	* cf = &m->config;

	cf->titan_accelerator = !!mui_control_get_value(
					mui_control_get_by_id(w, MII_SLOT_TITAN));
	cf->no_slot_clock = !!mui_control_get_value(
					mui_control_get_by_id(w, MII_SLOT_NSC));

	for (int slot_id = 1; slot_id < 8; slot_id++) {
		mui_control_t * c = mui_control_get_by_id(
								w, MII_SLOT_DRIVER_POP + slot_id);
		mui_menu_items_t * items = mui_popupmenu_get_items(c);
		int pop_val = mui_control_get_value(c);
		mui_menu_item_t * item = &items->e[pop_val];
	//	printf("%s popup slot %d %4.4s '%s'\n", __func__,
	//			slot_id,
	//			(char*)&item->uid, item->title);
		mui_control_t * c2 = mui_control_get_by_id(w,
				MII_SLOT_DRIVER_CONF + slot_id);
		if (c2) {
			mui_control_set_state(c2,
						_mii_slot_drivers[pop_val].has_config ?
							MUI_CONTROL_STATE_NORMAL :
							MUI_CONTROL_STATE_DISABLED);
		}
		if (cf->slot[slot_id-1].driver != item->uid) {
			cf->slot[slot_id-1].driver = item->uid;
			memset(&cf->slot[slot_id-1].conf, 0,
					sizeof(cf->slot[slot_id-1].conf));
		}
	}
}

static void
_mii_config_conf_to_win(
		mii_ui_machine_config_t * m)
{
	mui_window_t * w = &m->win;
	mii_machine_config_t	* cf = &m->config;

//	mui_control_set_value(mui_control_get_by_id(w, MII_SLOT_REBOOT),
//					cf->reboot_on_load);
	mui_control_set_value(mui_control_get_by_id(w, MII_SLOT_TITAN),
					cf->titan_accelerator);
	mui_control_set_value(mui_control_get_by_id(w, MII_SLOT_NSC),
					cf->no_slot_clock);

	for (int slot_id = 1; slot_id < 8; slot_id++) {
		mui_control_t * c = mui_control_get_by_id(
								w, MII_SLOT_DRIVER_POP + slot_id);
		mui_control_set_value(c, cf->slot[slot_id-1].driver);

		mui_control_t * c2 = mui_control_get_by_id(w,
				MII_SLOT_DRIVER_CONF + slot_id);
		if (c2) {
			mui_control_set_state(c2,
						_mii_slot_drivers[cf->slot[slot_id-1].driver].has_config ?
							MUI_CONTROL_STATE_NORMAL :
							MUI_CONTROL_STATE_DISABLED);
		}
	}
}

static int
_mii_config_sub_save_cb(
		mui_window_t *win, // the sub window/dialog
		void * cb_param,
		uint32_t what,
		void * param)
{
//	mii_ui_machine_config_t * m = cb_param;
	printf("%s %4.4s\n", __func__, (char*)&what);

	return 0;
}

mui_window_t *
mii_mui_configure_slot(
		struct mui_t *mui,
		mii_machine_config_t *config,
		int slot_id)
{
	printf("%s config slot %d\n", __func__, slot_id);
	mui_window_t * res = NULL;
	switch (config->slot[slot_id].driver) {
		case MII_SLOT_DRIVER_SMARTPORT: {
			config->slot[slot_id].conf.smartport.slot_id = slot_id;
			res = mii_mui_load_2dsk(mui,
						&config->slot[slot_id].conf.smartport,
						MII_2DSK_SMARTPORT);
		}	break;
		case MII_SLOT_DRIVER_DISK2: {
			config->slot[slot_id].conf.disk2.slot_id = slot_id;
			res = mii_mui_load_2dsk(mui,
						&config->slot[slot_id].conf.disk2,
						MII_2DSK_DISKII);
		}	break;
		case MII_SLOT_DRIVER_ROM1MB: {
			config->slot[slot_id].conf.rom1mb.slot_id = slot_id;
			res = mii_mui_load_1mbrom(mui,
						&config->slot[slot_id].conf.rom1mb);
		}	break;
		case MII_SLOT_DRIVER_SSC: {
			config->slot[slot_id].conf.ssc.slot_id = slot_id;
			res = mii_mui_configure_ssc(mui,
						&config->slot[slot_id].conf.ssc);
		}	break;
	}
	return res;
}

static int
_mii_config_slot_action_cb(
		mui_control_t * c,	// control here
		void * 			cb_param, // mii_ui_machine_config_t
		uint32_t 		what,
		void * 			param)	// not used
{
	printf("%s %4.4s\n", __func__, (char*)&what);
	mii_ui_machine_config_t * m = cb_param;
	uint32_t uid = mui_control_get_uid(c);

	switch (what) {
		case MUI_CONTROL_ACTION_SELECT:
			printf("%s control %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_SLOT_SAVE: {
					// save the config
					printf("%s save\n", __func__);
					_mii_config_win_to_conf(m);
					if (m->dst)
						*m->dst = m->config;
					mui_window_action(&m->win, MII_MUI_SLOTS_SAVE, m->dst);
					mui_window_dispose(&m->win);
				}	break;
				case MII_SLOT_CANCEL: {
					// cancel the config
					printf("%s cancel\n", __func__);
					mui_window_dispose(&m->win);
				}	break;
				case MII_SLOT_DEFAULT: {
					// set the default config
					printf("%s default\n", __func__);
					m->config = mii_default_config;
					_mii_config_conf_to_win(m);
				}	break;
#if 0
				case MII_SLOT_REBOOT: {
					// reboot the machine
					printf("%s reboot\n", __func__);
					m->config.reboot_on_load = !m->config.reboot_on_load;
				}	break;
#endif
				case MII_SLOT_NSC: {
					// no slot clock
					printf("%s nsc\n", __func__);
					m->config.no_slot_clock = !m->config.no_slot_clock;
				}	break;
				case MII_SLOT_TITAN: {
					// titan accelerator
					printf("%s titan\n", __func__);
					m->config.titan_accelerator = !m->config.titan_accelerator;
				}	break;
				case MII_SLOT_DRIVER_CONF + 1 ... MII_SLOT_DRIVER_CONF + 7: {
					// configure a slot
					int slot_id = uid - MII_SLOT_DRIVER_CONF - 1;
					printf("%s config slot %d\n", __func__, slot_id);
					switch (m->config.slot[slot_id].driver) {
						case MII_SLOT_DRIVER_SMARTPORT: {
							m->config.slot[slot_id].conf.smartport.slot_id = slot_id;
							mui_window_set_action(
								mii_mui_load_2dsk(m->win.ui,
										&m->config.slot[slot_id].conf.smartport,
										MII_2DSK_SMARTPORT),
								_mii_config_sub_save_cb, m);
						}	break;
						case MII_SLOT_DRIVER_DISK2: {
							m->config.slot[slot_id].conf.disk2.slot_id = slot_id;
							mui_window_set_action(
								mii_mui_load_2dsk(m->win.ui,
										&m->config.slot[slot_id].conf.disk2,
										MII_2DSK_DISKII),
								_mii_config_sub_save_cb, m);
						}	break;
						case MII_SLOT_DRIVER_ROM1MB: {
							m->config.slot[slot_id].conf.rom1mb.slot_id = slot_id;
							mui_window_set_action(
								mii_mui_load_1mbrom(m->win.ui,
										&m->config.slot[slot_id].conf.rom1mb),
								_mii_config_sub_save_cb, m);
						}	break;
						case MII_SLOT_DRIVER_SSC: {
							m->config.slot[slot_id].conf.ssc.slot_id = slot_id;
							mui_window_set_action(
								mii_mui_configure_ssc(m->win.ui,
										&m->config.slot[slot_id].conf.ssc),
								_mii_config_sub_save_cb, m);
						}	break;
					}
				}	break;
			}
			break;
		case MUI_CONTROL_ACTION_VALUE_CHANGED:
			// a popup menu changed
			printf("%s popup %4.4s\n", __func__, (char*)&uid);
			switch (uid) {
				case MII_SLOT_DRIVER_POP + 1 ... MII_SLOT_DRIVER_POP + 7: {
					_mii_config_win_to_conf(m);
				}	break;
			}
			break;
	}
	return 0;
}

struct mui_window_t *
mii_mui_configure_slots(
		struct mui_t *mui,
		mii_machine_config_t *config)
{
	mui_t *ui = mui;
	float base_size = mui_font_find(ui, "main")->size;
	float margin = base_size * 0.7;

	mui_window_t *w = mui_window_get_by_id(mui, MII_SLOT_WINDOW_ID);
	if (w) {
		mui_window_select(w);
		return w;
	}
	c2_pt_t where = {};
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 520, 480);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	w = mui_window_create(mui,
					wpos,
					NULL, 0,
					"Configure " MUI_GLYPH_IIE " Slots",
					sizeof(mii_ui_machine_config_t));
	mui_window_set_id(w, MII_SLOT_WINDOW_ID);
	mii_ui_machine_config_t *m = (mii_ui_machine_config_t*)w;

	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, 120, 40);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), 20);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), 20);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"Reboot", MII_SLOT_SAVE);
	c->key_equ = MUI_KEY_EQU(0, 13);
	c2_rect_left_of(&cf, cf.l, 20);
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Cancel", MII_SLOT_CANCEL);
	c->key_equ = MUI_KEY_EQU(0, 27);

	c2_rect_right_of(&cf, 0, 20);
	cf.r = cf.l + 200;
	#if 0
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_CHECKBOX,
					"Reboot Now", MII_SLOT_REBOOT);
	#endif
	c2_rect_top_of(&cf, cf.t, 36);
	cf.r = cf.l + 120;
	c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Default", MII_SLOT_DEFAULT);
	c2_rect_right_of(&cf, cf.r, 30);
	c2_rect_t rad = cf;
	rad.b += 5;
	c = mui_button_new(w,
					rad, MUI_BUTTON_STYLE_CHECKBOX,
					"No Slot Clock", MII_SLOT_NSC);
	c2_rect_right_of(&rad, rad.r, margin);
	rad.r = rad.l + 180;
	c = mui_button_new(w,
					rad, MUI_BUTTON_STYLE_CHECKBOX,
					"Titan Accelerator", MII_SLOT_TITAN);

	c2_rect_bottom_of(&cf, cf.b, 16);
	cf.l = 60;
	cf.r = c2_rect_width(&w->content) - 60;
	cf.b = cf.t + 1;
	c = mui_separator_new(w, cf);

	c2_rect_t slot_line_rect = C2_RECT_WH(0, 0, 500, 34);
	cf = slot_line_rect;
	cf.r = cf.l + 56;
	c = mui_textbox_new(w, cf, "Slot", NULL,
				MUI_TEXT_ALIGN_RIGHT | MUI_TEXT_ALIGN_MIDDLE |
				MUI_TEXT_STYLE_ULINE);
	c2_rect_right_of(&cf, cf.r, 0);
	cf.r = cf.l + 240;
	c = mui_textbox_new(w, cf, "Driver", NULL,
				MUI_TEXT_ALIGN_CENTER | MUI_TEXT_ALIGN_MIDDLE |
				MUI_TEXT_STYLE_ULINE);
	c2_rect_right_of(&cf, cf.r, 30);
	cf.r = cf.l + 150;
	c = mui_textbox_new(w, cf, "Config", NULL,
				MUI_TEXT_ALIGN_CENTER | MUI_TEXT_ALIGN_MIDDLE |
				MUI_TEXT_STYLE_ULINE);

	c2_rect_offset(&slot_line_rect, 0, 36);
	for (int i = 1; i < 8; i++) {
		cf = slot_line_rect;
		cf.r = cf.l + 50;
		char idx[16];
		sprintf(idx, "%d:", i);
		c = mui_textbox_new(w, cf, idx, NULL,
				MUI_TEXT_ALIGN_RIGHT | MUI_TEXT_ALIGN_MIDDLE |
				MUI_TEXT_STYLE_ULINE);
		c2_rect_right_of(&cf, cf.r, 6);
		cf.r = cf.l + 240;
		c = mui_popupmenu_new(w, cf,
						"Popup", MII_SLOT_DRIVER_POP + i,
						MUI_TEXT_ALIGN_CENTER);
		mui_menu_items_t * items = mui_popupmenu_get_items(c);
		mui_menu_items_clear(items);
		for (int j = 0; _mii_slot_drivers[j].label; j++) {
			mui_menu_item_t i = {
//				.title = strdup(_mii_slot_drivers[j].label),
				.title = _mii_slot_drivers[j].label,
				.uid = j,
			};
			mui_menu_items_push(items, i);
		}
		mui_menu_item_t z = {};
		mui_menu_items_push(items, z);
		mui_popupmenu_prepare(c);

		c2_rect_right_of(&cf, cf.r, 30);
		cf.r = cf.l + 150;
		c = mui_button_new(w,
						cf, MUI_BUTTON_STYLE_NORMAL,
						"Config…", MII_SLOT_DRIVER_CONF + i);
		c2_rect_offset(&slot_line_rect, 0, 38);
	}
	c = mui_controls_first(&w->controls, MUI_CONTROLS_ALL);
	for (; c; c = mui_controls_next(c, MUI_CONTROLS_ALL)) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mii_config_slot_action_cb, m);
	}
	m->dst = config;
	if (config) {
		if (config->load_defaults)
			m->config = mii_default_config;
		else
			m->config = *config;
	}
	_mii_config_conf_to_win(m);
	return w;
}
