/*
 * mui.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This is the main include file for the libmui UI library, it should be
 * the only one you need to include.
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_event.h>
#include <mui/mui_ref.h>
#include <mui/mui_action.h>
#include <mui/mui_control.h>
#include <mui/mui_window.h>
#include <mui/mui_drawable.h>
#include <mui/mui_text.h>
#include <mui/mui_menu.h>
#include <mui/mui_timer.h>
#include <mui/mui_control_group.h>
#include <mui/mui_alert.h>
#include <mui/mui_stdfile.h>

#include <control/drawable.h>
#include <control/button.h>
#include <control/box.h>
#include <control/textedit.h>
#include <control/scrollbar.h>
#include <control/listbox.h>
#include <control/popup.h>


/*
 * This is the head of the mui library, it contains the screen size,
 * the color scheme, the list of windows, the list of fonts, and the
 * clipboard.
 * Basically this is the primary parameter that you keep around.
 */
typedef struct mui_t {
	c2_pt_t 					screen_size;
	struct {
		mui_color_t					clear;
		mui_color_t 				highlight;
	}							color;
	uint16_t 					modifier_keys;
	mui_time_t 					last_click_stamp[MUI_EVENT_BUTTON_MAX];
	int 						draw_debug;
	int							quit_request;
	// this is the sum of all the window's dirty regions, inc moved windows etc
	mui_region_t 				inval;
	// once the pixels have been refreshed, 'inval' is copied to 'redraw'
	// to push the pixels to the screen.
	mui_region_t 				redraw;

	TAILQ_HEAD(, mui_font_t) 	fonts;
	TAILQ_HEAD(windows, mui_window_t) 	windows;
	mui_window_ref_t 			menubar;
	mui_window_ref_t 			event_capture;
	mui_utf8_t 					clipboard;
	mui_timer_group_t			timer;
	// only used by the text editor, as we can only have one carret
	mui_timer_id_t				carret_timer;
	char * 						pref_directory; /* optional */
} mui_t;

void
mui_init(
		mui_t *			ui);
void
mui_dispose(
		mui_t *			ui);
void
mui_draw(
		mui_t *			ui,
		mui_drawable_t *dr,
		uint16_t 		all);
void
mui_resize(
		mui_t *			ui,
		mui_drawable_t *dr,
		c2_pt_t 		size);
void
mui_run(
		mui_t *			ui);

/* If you want this notification, attach an action function to the
 * menubar */
enum {
	// note this will also be send if the application sets the
	// clipboard with the system's clipboard, so watch out for
	// recursion problems!
	MUI_CLIPBOARD_CHANGED = FCC('c','l','p','b'),
	// this is sent when the user type 'control-v', this gives
	// a chance to the application to do a mui_clipboard_set()
	// with the system's clipboard before it gets pasted.
	// the default is of course to use our internal clipboard
	MUI_CLIPBOARD_REQUEST = FCC('c','l','p','r'),
};
// This will send a notification that the clipboard was set,
// the notification is sent to the menubar, and the menubar will
// send it to the application.
void
mui_clipboard_set(
		mui_t * 		ui,
		const uint8_t * utf8,
		uint		 	len);
const uint8_t *
mui_clipboard_get(
		mui_t * 		ui,
		uint		 *	len);

// Pass an event to libmui. Return true if the event was handled by the ui
bool
mui_handle_event(
		mui_t *			ui,
		mui_event_t *	ev);
// Return true if event 'ev' is a key combo matching key_equ
bool
mui_event_match_key(
		mui_event_t *	ev,
		mui_key_equ_t 	key_equ);
/* Return true if the ui has any active windows, ie, not hidden, zombie;
 * This does not include the menubar, but it does include any menus or popups
 *
 * This is used to decide wether to hide the mouse cursor or not
 */
bool
mui_has_active_windows(
		mui_t *			ui);

/* Return a hash value for string inString */
uint32_t
mui_hash(
		const char * 		inString );
