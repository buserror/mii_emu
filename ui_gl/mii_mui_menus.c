/*
 * mii_mui_menus.c
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

#include "mii_mui.h"
#include "mii_thread.h"
#define MII_MUI_MENUS_C
#include "mii_mui_menus.h"
#include "mii_mui_settings.h"


struct mii_x11_t;
void
mii_x11_reload_config(
	struct mii_x11_t *ui );
void
mii_ui_reconfigure_slot(
		mii_t * mii,
		mii_machine_config_t * config,
		int slot );


static int
mii_quit_confirm_cb(
		mui_window_t *win,
		void * cb_param,
		uint32_t what,
		void * param)
{
	mii_mui_t * ui = cb_param;
//	printf("%s %4.4s\n", __func__, (char*)&what);
	if (what == MUI_CONTROL_ACTION_SELECT) {
		mui_control_t * c = param;
//		printf("%s %4.4s\n", __func__, (char*)&c->uid);
		if (c->uid == MUI_ALERT_BUTTON_OK) {
//			printf("%s Quit\n", __func__);
			mii_t * mii = &ui->mii;
			mii->state = MII_TERMINATE;
		}
	}
	return 0;
}

static int
mii_config_save_cb(
		mui_window_t *win,
		void * cb_param,
		uint32_t what,
		void * param)
{
	mii_mui_t * ui = cb_param;
	printf("%s %4.4s\n", __func__, (char*)&what);
	switch (what) {
		case MII_MUI_SLOTS_SAVE:
			printf("%s *** Rebooting\n", __func__);
			mii_x11_reload_config((void*)ui);
			mii_emu_save(&ui->cf, &ui->config);
			break;
		case MII_MUI_DISK2_SAVE:
		case MII_MUI_SMARTPORT_SAVE: {
			mii_2dsk_conf_t * conf = param;
			mii_t * mii = &ui->mii;
			mii_machine_config_t * config = &ui->config;
			mii_ui_reconfigure_slot(mii, config, conf->slot_id + 1);
			mii_emu_save(&ui->cf, &ui->config);
		}	break;
		case MII_MUI_1MB_SAVE: {
			mii_1mb_conf_t * conf = param;
			mii_t * mii = &ui->mii;
			mii_machine_config_t * config = &ui->config;
			mii_ui_reconfigure_slot(mii, config, conf->slot_id + 1);
			mii_emu_save(&ui->cf, &ui->config);
		}	break;
		case MII_MUI_SSC_SAVE: {
			mii_ssc_conf_t * conf = param;
			mii_t * mii = &ui->mii;
			mii_machine_config_t * config = &ui->config;
			mii_ui_reconfigure_slot(mii, config, conf->slot_id + 1);
			mii_emu_save(&ui->cf, &ui->config);
		}	break;
		case MII_MUI_LOADBIN_SAVE: {
			mii_loadbin_conf_t * conf = param;
		//	mii_t * mii = &ui->mii;
		//	mii_machine_config_t * config = &ui->config;
			mii_th_signal_t sig = {
				.cmd = SIGNAL_LOADBIN,
				.ptr = conf,
			};
			mii_th_fifo_write(mii_thread_get_fifo(&ui->mii), sig);
			mii_emu_save(&ui->cf, &ui->config);
		}	break;
	}
	return 0;
}

void
mii_config_open_slots_dialog(
		mii_mui_t * ui)
{
	mui_window_set_action(
		mii_mui_configure_slots(&ui->mui, &ui->config),
		mii_config_save_cb, ui);
}

// this probably needs to be declared differently
void
mii_mui_toggle_fullscreen(
	mii_mui_t * _ui );

static int
mii_menubar_action(
		mui_window_t *win,	// window (menubar)
		void * cb_param,
		uint32_t what,
		void * param)
{
	mii_mui_t * ui = cb_param;
	mui_t * mui = &ui->mui;
	mii_t * mii = &ui->mii;

//	printf("%s %4.4s\n", __func__, (char*)&what);

	switch (what) {
		case MUI_MENUBAR_ACTION_PREPARE: {
			mui_menu_item_t * items = param;
		//	printf("%s prepare (%s)\n", __func__, items[0].title);
			for (int i = 0; items[i].title; i++) {
				switch (items[i].uid) {
					case FCC('v','d','c','0'):
					case FCC('v','d','c','1'):
					case FCC('v','d','c','2'):
					case FCC('v','d','c','3'):
					case FCC('v','d','c','4'):
					case FCC('v','d','c','5'):
					case FCC('v','d','c','6'): {
						int idx = FCC_INDEX(items[i].uid);
						if (mii->video.color_mode == idx)
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						else
							items[i].mark[0] = 0;
					}	break;
					case FCC('a','u','d','0'):
						if (mii->audio.muted)
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						else
							items[i].mark[0] = 0;
						break;
					case FCC('a','u','d','+'):
						items[i].disabled = mii->speaker.source.volume >= 10;
						break;
					case FCC('a','u','d','-'):
						items[i].disabled = mii->speaker.source.volume <= 0.1;
						break;
					case FCC('s','a','u','d'):
						// are we in silent mode ?
						items[i].disabled = mii->audio.drv == NULL;
						break;
					case FCC('m','h','z','1'):
						if (mii->speed <= 1.1 && mii->speed >= 0.9)
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						else
							items[i].mark[0] = 0;
						break;
					case FCC('m','h','z','3'):
						if (mii->speed >= 2)
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						else
							items[i].mark[0] = 0;
						break;
					case FCC('s','t','o','p'): {
						if (mii->state == MII_STOPPED) {
							items[i].title = "Stopped";
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						} else {
							items[i].title = "Stop";
							items[i].mark[0] = 0;
						}
					}	break;
					case FCC('r','u','n',' '): {
						if (mii->state == MII_RUNNING) {
							items[i].title = "Running";
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						} else {
							items[i].title = "Continue";
							items[i].mark[0] = 0;
						}
					}	break;
					case FCC('n','e','x','t'):
					case FCC('s','t','e','p'): {
						items[i].disabled = mii->state == MII_RUNNING;
					}	break;
					/*
					 * These menu items are disabled when a window/dialog
					 * is open.
					 */
				//	case FCC('j','o','y','s'):
				//	case FCC('l','r','u','n'):
					case FCC('a','b','o','t'):
					case FCC('s','l','o','t'): {
						items[i].disabled = mui_window_front(mui) != NULL;
					}	break;
					case FCC('s','h','m','b'): {
						items[i].disabled =
								(mui_window_front(mui) != NULL) ||
								(ui->transition.state != MII_MUI_TRANSITION_NONE);
					}	break;
					case FCC('a','l','t','f'): {
						items[i].disabled = mii->video.rom->len < (8*1024);
						// bank zero is the international one
						if (mii->video.rom_bank == 0 && !items[i].disabled)
							strcpy(items[i].mark, MUI_GLYPH_TICK);
						else
							items[i].mark[0] = 0;
					}	break;
				}
			}
		}	break;
		case MUI_MENUBAR_ACTION_SELECT: {
			mui_menu_item_t * item = param;
		//	printf("%s Selected %4.4s '%s'\n", __func__,
		//			(char*)&item->uid, item->title);
			switch (item->uid) {
				case FCC('a','b','o','t'): {
					mii_mui_about(&ui->mui);
				}	break;
				case FCC('q','u','i','t'): {
					if (!ui->mui_visible &&
								ui->transition.state == MII_MUI_TRANSITION_NONE)
						ui->transition.state = MII_MUI_TRANSITION_SHOW_UI;
					mui_window_t * really = mui_window_get_by_id(
											&ui->mui, FCC('q','u','i','t'));
					if (really)
						mui_window_select(really);
					else {
						really = mui_alert(mui, C2_PT(0,0),
										"Quitting",
										"Do you really want to quit the emulator?",
										MUI_ALERT_WARN);
						really->uid = FCC('q','u','i','t');
						mui_window_set_action(really, mii_quit_confirm_cb, ui);
					}
				}	break;
				case FCC('s','h','m','b'): {
					if (ui->transition.state != MII_MUI_TRANSITION_NONE)
						break;
					if (ui->mui_visible) {
						ui->transition.state = MII_MUI_TRANSITION_HIDE_UI;
					} else {
						ui->transition.state = MII_MUI_TRANSITION_SHOW_UI;
					}
				}	break;
				case FCC('s','l','t','0'):
				case FCC('s','l','t','1'):
				case FCC('s','l','t','2'):
				case FCC('s','l','t','3'):
				case FCC('s','l','t','4'):
				case FCC('s','l','t','5'):
				case FCC('s','l','t','6'): {
					int slot = FCC_INDEX(item->uid);
					printf("%s configure slot %d\n", __func__, slot);
					mui_window_set_action(
						mii_mui_configure_slot(&ui->mui, &ui->config, slot),
						mii_config_save_cb, ui);
				}	break;
				case FCC('s','l','o','t'): {
					mii_config_open_slots_dialog(ui);
				}	break;
				case FCC('l','r','u','n'): {
					mui_window_set_action(
						mii_mui_load_binary(&ui->mui, &ui->loadbin_conf),
						mii_config_save_cb, ui);
				}	break;
				case FCC('c','r','e','s'):
				case FCC('r','e','s','t'): {
					mii_th_signal_t sig = {
						.cmd = SIGNAL_RESET,
						.data = item->uid == FCC('c','r','e','s'),
					};
					mii_th_fifo_write(mii_thread_get_fifo(&ui->mii), sig);
				}	break;
				case FCC('a','u','d','0'):
					mii->audio.muted = !mii->audio.muted;
					ui->config.audio_muted = mii->audio.muted;
					break;
				case FCC('a','u','d','+'):
					mii_audio_volume(&mii->speaker.source,
							mii->speaker.source.volume + 1);
					ui->config.audio_volume = mii->speaker.source.volume;
					break;
				case FCC('a','u','d','-'):
					mii_audio_volume(&mii->speaker.source,
							mii->speaker.source.volume - 1);
					ui->config.audio_volume = mii->speaker.source.volume;
					break;
				case FCC('v','d','C','l'): {
//					printf("%s Cycle video\n", __func__);
					// this is auto clamped
					mii_video_set_mode(mii, mii->video.color_mode + 1);
					ui->config.video_mode = mii->video.color_mode;
				}	break;
				case FCC('v','d','c','0'):
				case FCC('v','d','c','1'):
				case FCC('v','d','c','2'):
				case FCC('v','d','c','3'):
				case FCC('v','d','c','4'):
					mii_video_set_mode(mii, FCC_INDEX(item->uid));
					ui->config.video_mode = mii->video.color_mode;
					break;
				case FCC('a','l','t','f'): {
					mii->video.rom_bank = !mii->video.rom_bank;
					// this forces a refresh
					mii_video_set_mode(mii, mii->video.color_mode);
				}	break;
				case FCC('t','g','l','F'):
					printf("Toggle Fullscreen\n");
					mii_mui_toggle_fullscreen(ui);
					break;
				case FCC('m','h','z','1'):
					mii->speed = MII_SPEED_NTSC;
					break;
				case FCC('m','h','z','3'):
					mii->speed = MII_SPEED_TITAN;
					break;
				case FCC('s','t','o','p'): {
					mii_th_signal_t sig = {
						.cmd = SIGNAL_STOP,
					};
					mii_th_fifo_write(mii_thread_get_fifo(&ui->mii), sig);
				}	break;
				case FCC('s','t','e','p'): {
					mii_th_signal_t sig = {
						.cmd = SIGNAL_STEP,
					};
					mii_th_fifo_write(mii_thread_get_fifo(&ui->mii), sig);
				}	break;
				case FCC('n','e','x','t'): {
					mii_th_signal_t sig = {
						.cmd = SIGNAL_NEXT,
					};
					mii_th_fifo_write(mii_thread_get_fifo(&ui->mii), sig);
				}	break;
				case FCC('r','u','n',' '): {
					mii_th_signal_t sig = {
						.cmd = SIGNAL_RUN,
					};
					mii_th_fifo_write(mii_thread_get_fifo(&ui->mii), sig);
				}	break;
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

void
mii_mui_menu_slot_menu_update(
	mii_mui_t * ui)
{
	mui_window_t * mbar = mui_menubar_get(&ui->mui);
	mui_control_t * m = mui_control_get_by_id(mbar, FCC('f','i','l','e'));
	mui_menu_items_t * items = mui_popupmenu_get_items(m);

//	printf("%s items %p count %d RO:%d\n",
//		__func__, items, items? items->count : 0, items? items->read_only : 0);

	mui_menu_items_clear(items);
	if (!items->read_only)
		mui_menu_items_free(items);
	static const mui_menu_items_t zero = {0};
	*items = zero;

	for (int i = 0; i < 7; i++) {
		static char static_label[7][64];
		char * label = static_label[i];
		label[0] = 0;
		int slot = i + 1;

		switch (ui->config.slot[i].driver) {
			case MII_SLOT_DRIVER_SSC:
				sprintf(label, "%d: Super Serial…", slot);
				break;
			case MII_SLOT_DRIVER_SMARTPORT:
				sprintf(label, "%d: SmartPort…", slot);
				break;
			case MII_SLOT_DRIVER_DISK2:
				sprintf(label, "%d: Disk ][…", slot);
				break;
			case MII_SLOT_DRIVER_ROM1MB:
				sprintf(label, "%d: ROM 1MB…", slot);
				break;
			default:
				break;
		}
		if (label[0]) {
			mui_menu_items_push(items, (mui_menu_item_t){
				.title = label,
				.uid = FCC('s','l','t','0' + i),
			});
		}
	}
	int start = items->count ? 1 : 0;
	for (int i = start; m_file_menu[i].title; i++) {
		mui_menu_items_push(items, m_file_menu[i]);
	}
	// array is NULL terminated
	mui_menu_items_push(items, (mui_menu_item_t){});
}

void
mii_mui_menus_init(
	mii_mui_t * ui)
{
//	printf("%s\n", __func__);
	mui_window_t * mbar = mui_menubar_new(&ui->mui);
	mui_window_set_action(mbar, mii_menubar_action, ui);

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
}
