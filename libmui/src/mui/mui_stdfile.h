/*
 * mui_stdfile.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_window.h>


/*
 * Standard file dialog
 */
enum mui_std_action_e {
	MUI_STDF_ACTION_NONE 		= 0,
	// parameter 'target' is a char * with full pathname of selected file
	MUI_STDF_ACTION_SELECT 		= FCC('s','t','d','s'),
	MUI_STDF_ACTION_CANCEL 		= FCC('s','t','d','c'),
};
enum mui_std_flags_e {
	// 'pattern' is a GNU extended regexp applied to filenames.
	MUI_STDF_FLAG_REGEXP 	= (1 << 0),
	// don't use the 'pref_directory', load, or same preference files
	MUI_STDF_FLAG_NOPREF 	= (1 << 1),
};

/*
 * Standard file dialog related
 *
 * Presents a standard 'get' file dialog, with optional prompt, regexp and
 * start path. The return value is a pointer to a window, you can add your own
 * 'action' function to get MUI_STDF_ACTION_* events.
 * Once in the action function, you can call mui_stdfile_get_selected_path()
 * to get the selected path, and free it when done.
 * NOTE: The dialog does not auto-close, your own action function should close
 * the dialog using mui_window_dispose().
 *
 * The dialog will attempt to remember the last directory used *for this
 * particular pattern* and will use it as the default start path when called
 * again. This is optional, it requires a mui->pref_directory to be set.
 * You can also disable this feature by setting the MUI_STDF_FLAG_NOPREF flag.
 *
 * + 'pattern' is a regular expression to filter the files, or NULL for no
 *    filter.
 * + if 'start_path' is NULL, the $HOME directory is used.
 * + 'where' is the location of the dialog, (0,0) will center it.
 */
mui_window_t *
mui_stdfile_get(
		struct mui_t * 	ui,
		c2_pt_t 		where,
		const char * 	prompt,
		const char * 	pattern,
		const char * 	start_path,
		uint16_t 		flags );
// return the curently selected pathname -- caller must free() it
char *
mui_stdfile_get_selected_path(
		mui_window_t * w );

mui_window_t *
mui_stdfile_put(
		struct mui_t * 	ui,
		c2_pt_t 		where,			// pass 0,0 to center
		const char * 	prompt,			// Window title
		const char * 	pattern,		// Enforce any of these suffixes
		const char * 	start_path,		// start in this path (optional)
		const char * 	save_filename,	// start with this filename
		uint16_t 		flags );
