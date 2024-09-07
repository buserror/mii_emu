/*
 * mui_menu.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_event.h>
#include <mui/mui_window.h>

struct mui_menu_items_t;

/*
 * This is a menu item descriptor (also used for the titles, bar a few bits).
 * This is not a *control* in the window, instead this is used to describe
 * the menus and menu item controls that are created when the menu becomes
 * visible.
 */
typedef struct mui_menu_item_t {
	uint32_t 					disabled : 1,
								hilited : 1,
								is_menutitle : 1;
	uint32_t 					index: 9;
	uint32_t 					uid;
	char * 						title;
	// currently only supported for menu titles
	const uint32_t *			color_icon;		// optional, ARGB colors
	char  						mark[8];		// UTF8 -- Charcoal
	char						icon[8];		// UTF8 -- Wider, icon font
	char 						kcombo[16];		// UTF8 -- display only
	mui_key_equ_t 				key_equ;		// keystroke to select this item
	struct mui_menu_item_t *	submenu;
	c2_coord_t					location;		// calculated by menu creation code
	c2_coord_t					height;
} mui_menu_item_t;

/*
 * The menu item array is atypical as the items ('e' field) are not allocated
 * by the array, but by the menu creation code. This is because the menu
 * reuses the pointer to the items that is passed when the menu is added to
 * the menubar.
 * the 'read only' field is used to prevent the array from trying to free the
 * items when being disposed.
 */
DECLARE_C_ARRAY(mui_menu_item_t, mui_menu_items, 2,
				bool read_only; );
IMPLEMENT_C_ARRAY(mui_menu_items);

enum mui_menubar_action_e {
	// parameter is a mui_menu_item_t* for the first item of the menu,
	// this is exactly the parameter passed to add_simple()
	// you can use this to disable/enable menu items etc
	MUI_MENUBAR_ACTION_PREPARE 		= FCC('m','b','p','r'),
	// parameter 'target' is a mui_menuitem_t*
	MUI_MENUBAR_ACTION_SELECT 		= FCC('m','b','a','r'),
};
/*
 * Menu related.
 * Menubar, and menus/popups are windows as well, in a layer above the
 * normal ones.
 */
mui_window_t *
mui_menubar_new(
		struct mui_t *	ui );
// return the previously created menubar (or NULL)
mui_window_t *
mui_menubar_get(
		struct mui_t *	ui );

/*
 * Add a menu to the menubar. 'items' is an array of mui_menu_item_t
 * terminated by an element with a NULL title.
 *
 * Note: The array is NOT const, it will be tweaked for storing items
 * position, it can also be tweaked to set/reset the disabled state,
 * check marks etc
 *
 * Once created, you can do a mui_popupmenu_get_items() to get the array,
 * modify it (still make sure there is a NULL item at the end) then
 * call mui_popupmenu_prepare() to update the menu.
 */
struct mui_control_t *
mui_menubar_add_simple(
		mui_window_t *		win,
		const char * 		title,
		uint32_t 			menu_uid,
		mui_menu_item_t * 	items );
struct mui_control_t *
mui_menubar_add_menu(
		mui_window_t *		win,
		uint32_t 			menu_uid,
		mui_menu_item_t * 	items,
		uint 				count );

/* Turn off any highlighted menu titles */
mui_window_t *
mui_menubar_highlight(
		mui_window_t *	win,
		bool 			ignored );