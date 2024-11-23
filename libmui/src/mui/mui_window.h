/*
 * mui_window.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_control.h>
#include <mui/mui_drawable.h>
#include <mui/mui_control_group.h>
#include <mui/mui_action.h>
#include <mui/mui_ref.h>


/*
 * Window DEFinition -- Handle all related to a window, from drawing to
 * event handling.
 */
enum {
	MUI_WDEF_INIT = 0,	// param is NULL
	MUI_WDEF_DISPOSE,	// param is NULL
	// used before controls are drawn
	MUI_WDEF_DRAW,		// param is mui_drawable_t*
	// used after controls are drawn
	MUI_WDEF_DRAW_POST,	// param is mui_drawable_t*
	MUI_WDEF_EVENT,		// param is mui_event_t*
	MUI_WDEF_SELECT,	// param is NULL
	MUI_WDEF_DESELECT,	// param is NULL
	// called when the window is resized, param is a c2_rect_t with the new rectangle
	// it CAN be edited by the callback to change the new size, if appopriate
	MUI_WDEF_RESIZE,	// param is a c2_rect_t with the new rectangle
};
typedef bool (*mui_wdef_p)(
			struct mui_window_t * win,
			uint8_t 		what,
			void * 			param);

enum mui_window_layer_e {
	MUI_WINDOW_LAYER_NORMAL 	= 0,
	MUI_WINDOW_LAYER_MODAL 		= 3,
	MUI_WINDOW_LAYER_ALERT 		= 5,
	MUI_WINDOW_LAYER_TOP 		= 15,
	MUI_WINDOW_LAYER_MASK 		= 0x0F,	// mask layer bits
	// Menubar and Menus (popups) are also windows
	MUI_WINDOW_MENUBAR_LAYER 	= MUI_WINDOW_LAYER_TOP - 1,
	MUI_WINDOW_MENU_LAYER,

	MUI_WINDOW_FLAGS_CLOSEBOX 	= 0x100,
};

enum mui_window_action_e {
	MUI_WINDOW_ACTION_NONE 		= 0,
	// window is closing. param is NULL
	MUI_WINDOW_ACTION_CLOSE		= FCC('w','c','l','s'),
	// closebox has been clicked. param is a bool* with 'true' (default) if
	// the window should be closed. 'false' if it shouldn't be close now
	MUI_WINDOW_ACTION_CLOSEBOX	= FCC('w','c','b','x'),
};

/*
 * A window is basically 2 rectangles in 'screen' coordinates. The
 *   'frame' rectangle that encompasses the whole of the window, and the
 *   'content' rectangle that is the area where the controls are drawn.
 * * The 'content' rectangle is always fully included in the 'frame'
 *   rectangle.
 * * The 'frame' rectangle is the one that is used to move the window
 *   around.
 * * All controls coordinates are related to the 'content' rectangle.
 *
 * The window 'layer' is used to determine the order of the windows on the
 * screen, the higher the layer, the more in front the window is.
 * Windows can be 'selected' to bring them to the front -- that brings
 * them to the front of their layer, not necessarily the topmost window.
 *
 * Windows contain an 'action' list that are called when the window
 * wants to signal the application; for example when the window is closed,
 * but it can be used for other things as application requires.
 *
 * Mouse clicks are handled by the window, and the window by first
 * checking if the click is in a control, and if so, passing the event
 * to the control.
 * Any control that receives the 'mouse' down will ALSO receive the
 * mouse DRAG and UP events, even if the mouse has moved outside the
 * control. This is the meaning of the 'control_clicked' field.
 *
 * The 'control_focus' field is used to keep track of the control that
 * has the keyboard focus. This is used to send key events to the
 * control that has the focus. That control can still 'refuse' the event,
 * in which case is it passed in turn to the others, and to the window.
 */
typedef struct mui_window_t {
	TAILQ_ENTRY(mui_window_t)	self;
	struct mui_t *				ui;
	mui_wdef_p 					wdef;
	uint32_t					uid;		// optional, pseudo unique id
	struct {
		uint						hidden: 1,
									disposed : 1,
									layer : 4,
									closebox : 1,
									style: 4,	// specific to the WDEF
									hit_part : 8, in_part : 8;
	}							flags;
	c2_pt_t 					click_loc;
	// both these rectangles are in screen coordinates, even tho
	// 'contents' is fully included in 'frame'
	c2_rect_t					frame;
	c2_rect_t					content;
	char *						title;
	mui_action_queue_t			actions;
	mui_control_group_t			main_group;	// do not use directly
	mui_controls_t				controls;
	mui_refqueue_t				refs;
	mui_window_ref_t 			lock;
	mui_control_ref_t 			control_clicked;
	mui_control_ref_t	 		control_focus;
	mui_region_t				inval;
} mui_window_t;

/*
 * Window related
 */
/*
 * This is the main function to create a window. The
 * * 'wdef' is the window definition (or NULL for a default window).
 *   see mui_wdef_p for the callback definition.
 * * 'layer' layer to put it in (default to zero for normal windows)
 * * 'instance_size' zero (for default) or the size of the window instance
 *   object that is returned, you can therefore have your own custom field
 *   attached to a window.
 */
mui_window_t *
mui_window_create(
		struct mui_t *	ui,
		c2_rect_t 		frame,
		mui_wdef_p 		wdef,
		uint32_t 		layer_flags,
		const char *	title,
		uint32_t 		instance_size);
// Dispose of a window and it's content (controls).
/*
 * Note: if the window is 'locked' the window is not freed immediately.
 * This is to prevent re-entrance problems. This allows window actions to
 * delete their own window without crashing.
 */
void
mui_window_dispose(
		mui_window_t *	win);
// Invalidate 'r' in window coordinates, or the whole window if 'r' is NULL
void
mui_window_inval(
		mui_window_t *	win,
		c2_rect_t * 	r);
// return true if the window is the frontmost window (in that window's layer)
bool
mui_window_isfront(
		mui_window_t *	win);
// return the top (non menubar/menu) window
mui_window_t *
mui_window_front(
		struct mui_t *ui);
void
mui_window_resize(
		mui_window_t *	win,
		c2_pt_t 		size);
// move win to the front (of its layer), return true if it was moved
bool
mui_window_select(
		mui_window_t *	win);
// call the window action callback(s), if any
void
mui_window_action(
		mui_window_t * 	c,
		uint32_t 		what,
		void * 			param );
// Add an action callback for this window
void
mui_window_set_action(
		mui_window_t * 	c,
		mui_window_action_p	cb,
		void * 			param );
// return the window whose UID is 'uid', or NULL if not found
mui_window_t *
mui_window_get_by_id(
		struct mui_t *	ui,
		uint32_t 		uid );
// set the window UID
void
mui_window_set_id(
		mui_window_t *	win,
		uint32_t 		uid);
