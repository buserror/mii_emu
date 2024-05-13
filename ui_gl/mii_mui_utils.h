
#pragma once

#include "mui.h"


typedef struct mii_mui_file_select_t {
	mui_control_t *			box;
	mui_control_t *			icon;
	mui_control_t *			fname;
	mui_control_t *			button;
	mui_control_t *			checkbox;
	mui_control_t *			warning;
} mii_mui_file_select_t;

int
mii_mui_fileselect_widget( //
		mii_mui_file_select_t *out,
		mui_window_t		  *w,
		c2_rect_t			  *where,
		const char			  *enclosing_box_title,
		const char			  *button_title,
		const char			  *checkbox_title);
