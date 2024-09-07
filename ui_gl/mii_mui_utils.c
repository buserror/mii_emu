

#include "mii_mui_utils.h"

int
mii_mui_fileselect_widget( //
		mii_mui_file_select_t *out,
		mui_window_t		  *w,
		c2_rect_t			  *where,
		const char			  *enclosing_box_title,
		const char			  *button_title,
		const char			  *checkbox_title)
{
	mui_t *ui = w->ui;
	float base_size = mui_font_find(ui, "main")->size;
	float icons_size = mui_font_find(ui, "icon_small")->size;
	float margin = base_size * 0.7;

	mui_control_t * c = NULL;
	c2_rect_t cf;
	c2_rect_t cp = *where;
	cp.b = cp.t + base_size * 3;
	where->b = cp.b;
	const int but_width = 100;
	cp.r = where->r - margin * 2 - but_width;

	const int label_size = base_size * 1.85;
	c2_rect_set(&cf, margin, (margin / 2),
					c2_rect_width(&w->frame) - margin - but_width,
					(margin/2) + label_size);
	out->box = c = mui_groupbox_new(w, cp,
						enclosing_box_title, MUI_CONTROL_TEXTBOX_FRAME);
	c2_rect_bottom_of(&cf, cp.t,
			 (c2_rect_height(&cf) / 2));
	c2_rect_right_of(&cf, cp.l, margin * 0.5);
	//cf.b = cf.t + icons_size;
	cf.r = cf.l + icons_size;
	out->icon = c = mui_textbox_new(w, cf,
				MUI_ICON_FILE, "icon_small",
				MUI_TEXT_ALIGN_MIDDLE | MUI_TEXT_ALIGN_CENTER | 0);
	c->state = MUI_CONTROL_STATE_DISABLED;
	cf.l = cf.r + 5;
	cf.r = cp.r - margin * 0.5;
	cf.b = cf.t + label_size;
	out->fname = c = mui_textbox_new(w, cf,
						"Click \"Select\" to pick a file", NULL,
						MUI_TEXT_ALIGN_MIDDLE|0);
	c->state = MUI_CONTROL_STATE_DISABLED;

	c2_rect_right_of(&cf, cp.r, margin);
	cf.r = where->r - margin * 0.5;
	cf.b = cf.t + base_size * 1.1;
	c2_rect_offset(&cf, 0, (c2_rect_height(&cf) / 2.5));
	c2_rect_inset(&cf, -4,-4);
	out->button = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					button_title , 0);
	if (checkbox_title) {
		c2_rect_bottom_of(&cf, cp.b, margin * 0.4);
		cf.l = cp.l + (margin * 0.7);
		cf.r = cf.l + 200;
		cf.b = cf.t + base_size;
		where->b = cf.b;
		out->checkbox = c = mui_button_new(w,
						cf, MUI_BUTTON_STYLE_CHECKBOX,
						checkbox_title, 0);
		c2_rect_right_of(&cf, cf.r, margin * 0.5);
		cf.r = c2_rect_width(&w->frame) - margin * 1.2;
		out->warning = c = mui_textbox_new(w, cf,
						"", NULL,
						MUI_TEXT_ALIGN_MIDDLE|MUI_TEXT_ALIGN_RIGHT);
	}
	return 0;
}
