/*
 * ui_tests.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE // for asprintf
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "mui.h"
#include "mui_plugin.h"
#include "c2_geometry.h"

typedef struct cg_ui_t {
	mui_t *ui;
} cg_ui_t;

#define MII_MUI_MENUS_C
#include "mii_mui_menus.h"
#include "mii_mui_settings.h"


#ifndef UI_VERSION
#define UI_VERSION "0.0.0"
#endif

static void
_test_show_about(
		cg_ui_t *g)
{
	mui_window_t *w = mui_window_get_by_id(g->ui, FCC('a','b','o','t'));
	if (w) {
		mui_window_select(w);
		return;
	}
	w = mui_alert(g->ui, C2_PT(0,0),
					"About MII",
					"Version " UI_VERSION "\n"
					"Build " __DATE__ " " __TIME__,
					MUI_ALERT_INFO);
	mui_window_set_id(w, FCC('a','b','o','t'));
}

static mii_machine_config_t g_machine_conf = {};
static mii_loadbin_conf_t g_loadbin_conf = {};

/* this is heavily endian dependend, as is the FCC macro */
#define FCC_INDEX(_fcc) (isdigit(_fcc>>24) ? ((_fcc >> 24) - '0') : 0)

int
_test_menubar_action(
		mui_window_t *win,
		void * cb_param,
		uint32_t what,
		void * param)
{
	cg_ui_t *g = cb_param;

	printf("%s %4.4s\n", __func__, (char*)&what);

	static int video_mode = 0;
	static int audio_mute = 0;
	switch (what) {
		case MUI_MENUBAR_ACTION_PREPARE: {
			mui_menu_item_t * items = param;
			if (items == m_video_menu) {
				printf("%s prepare video %d\n", __func__, video_mode);
				for (int i = 0; m_video_menu[i].title; i++) {
					switch (m_video_menu[i].uid) {
						case FCC('v','d','c','0'):
						case FCC('v','d','c','1'):
						case FCC('v','d','c','2'): {
							int idx = FCC_INDEX(m_video_menu[i].uid);
							if (video_mode == idx)
								strcpy(m_video_menu[i].mark, MUI_GLYPH_TICK);
							else
								m_video_menu[i].mark[0] = 0;
						}	break;
					}
				}
			} else if (items == m_audio_menu) {
				printf("%s prepare audio %d\n", __func__, audio_mute);
				for (int i = 0; m_audio_menu[i].title; i++) {
					switch (m_audio_menu[i].uid) {
						case FCC('a','u','d','0'):
							if (audio_mute)
								strcpy(m_audio_menu[i].mark, MUI_GLYPH_TICK);
							else
								m_audio_menu[i].mark[0] = 0;
							break;
					}
				}
			} else {
				printf("%s prepare (%s)\n", __func__, items[0].title);
			}
		}	break;
		case MUI_MENUBAR_ACTION_SELECT: {
			mui_menu_item_t * item = param;
			printf("%s Selected %4.4s '%s'\n", __func__,
					(char*)&item->uid, item->title);
			switch (item->uid) {
				case FCC('a','b','o','t'): {
					_test_show_about(g);
				}	break;
				case FCC('q','u','i','t'): {
					printf("%s Quit\n", __func__);
					g->ui->quit_request = 1;
				}	break;
				case FCC('s','l','o','t'): {
					mii_mui_configure_slots(g->ui, &g_machine_conf);
				}	break;
				case FCC('l', 'r', 'u', 'n'): {
					mii_mui_load_binary(g->ui, &g_loadbin_conf);
				}	break;
				case FCC('a','u','d','0'):
					audio_mute = !audio_mute;
					break;
				case FCC('v','d','C','l'): {
//					printf("%s Cycle video\n", __func__);
					video_mode = (video_mode + 1) % 3;
				}	break;
				case FCC('v','d','c','0'):
				case FCC('v','d','c','1'):
				case FCC('v','d','c','2'):
					video_mode = FCC_INDEX(item->uid);
					break;
				default:
					printf("%s menu item %4.4s %s IGNORED\n",
							__func__, (char*)&item->uid, item->title);
					break;
			}
		}	break;
		default:
			printf("%s %4.4s IGNORED?\n", __func__, (char*)&what);
			break;
	}

	return 0;
}

static void
plain_test_window(
		mui_t *mui)
{
	mui_window_t *w = mui_window_get_by_id(mui, FCC('t','e','s','t'));
	if (w) {
		mui_window_select(w);
		return;
	}
	c2_pt_t where = {};
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 510, 270);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(mui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(mui->screen_size.y * 0.45) - (c2_rect_height(&wpos) / 2));
	w = mui_window_create(mui, wpos, NULL, MUI_WINDOW_LAYER_NORMAL,
					"Test", 0);
	mui_window_set_id(w, FCC('t','e','s','t'));

	mui_control_t * c = NULL;
	c2_rect_t cf;

	cf = C2_RECT_WH(10, 10, 480, 170);
	c = mui_textedit_control_new(w, cf,
			MUI_CONTROL_TEXTBOX_FRAME | MUI_CONTROL_TEXTEDIT_VERTICAL);
	mui_textedit_set_text(c,
		"The quick brown fox Jumps over the Lazy dog.\n"
		"Lorem Ipsum is simply dummy text of the printing "
		"and typesetting industry. Lorem Ipsum has been the "
		"industry's standard dummy text ever since the 1500s.\n"
		#if 1
		"Now let's step back and look at what's happening. "
		"Writing to the disk is a load and shift process, a "
		"little like HIRES pattern outputs but much slower.\n"
		"Also, the MPU takes a very active role in the loading "
		"and shifting of disk write data. There are two 8-Htate "
		"loops in the WRITE sequence. After initializing the "
		"WRITE sequence, data is stored in the data register "
		"at a critical point in the A7' loop. As (quickly "
		"thereafter as the 6502 can do it, the sequencer is "
		"configured to shift left at the critical point "
		"instead of loading."
		#endif
		);
	c2_rect_bottom_of(&cf, cf.b, 10);
	cf.b = cf.t + 35;
	c = mui_textedit_control_new(w, cf, MUI_CONTROL_TEXTBOX_FRAME);
	mui_textedit_set_text(c,
		"Fulling Mill Online Return Center.pdf");

}

static void *
_init(
		struct mui_t * ui,
		struct mui_plug_t * plug,
		mui_drawable_t * pix)
{
	mui_init(ui);
	ui->screen_size = pix->pix.size;
	asprintf(&ui->pref_directory, "%s/.local/share/mii", getenv("HOME"));

	cg_ui_t *g = calloc(1, sizeof(*g));
	g->ui = ui;
	printf("%s\n", __func__);
	mui_window_t * mbar = mui_menubar_new(ui);
	mui_window_set_action(mbar, _test_menubar_action, g);

	mui_menubar_add_menu(mbar, FCC('a','p','p','l'), m_color_apple_menu, 2);
	mui_menubar_add_simple(mbar, "File",
								FCC('f','i','l','e'),
								m_file_menu);
	mui_menubar_add_simple(mbar, "Machine",
								FCC('m','a','c','h'),
								m_machine_menu);
	mui_menubar_add_simple(mbar, "CPU",
								FCC('c','p','u','m'),
								m_cpu_menu);
	plain_test_window(ui);
//	mii_mui_configure_slots(g->ui, &g_machine_conf);
//	mii_mui_load_binary(g->ui, &g_loadbin_conf);
//	mii_mui_load_1mbrom(g->ui, &g_machine_conf.slot[0].conf.rom1mb);
//	mii_mui_load_2dsk(g->ui, &g_machine_conf.slot[0].conf.disk2, MII_2DSK_DISKII);
//	mii_mui_about(g->ui);
//	mii_mui_configure_ssc(g->ui, &g_machine_conf.slot[0].conf.ssc);

#if 0
	mui_alert(ui, C2_PT(0,0),
					"Testing one Two",
					"Do you really want the printer to catch fire?\n"
					"This operation cannot be cancelled.",
					MUI_ALERT_WARN);
#endif
#if 0
	mui_stdfile_get(ui,
				C2_PT(0, 0),
				"Select image for SmartPort card",
				"hdv,po,2mg",
				getenv("HOME"), 0);
#endif

	return g;
}

static void
_dispose(
		void *_ui)
{
	cg_ui_t *ui = _ui;
	printf("%s\n", __func__);
	mui_dispose(ui->ui);
	free(ui);
}

static int
_draw(
		struct mui_t *ui,
		void *param,
		mui_drawable_t *dr,
		uint16_t all)
{
//	cg_ui_t *g = param;
	mui_draw(ui, dr, all);
	return 1;
}

static int
_event(
		struct mui_t *ui,
		void *param,
		mui_event_t *event)
{
//	cg_ui_t *g = param;
//	printf("%s %d\n", __func__, event->type);
	mui_handle_event(ui, event);
	return 0;
}


mui_plug_t mui_plug = {
	.init = _init,
	.dispose = _dispose,
	.draw = _draw,
	.event = _event,
};