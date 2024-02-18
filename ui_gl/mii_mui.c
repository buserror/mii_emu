/*
 * mii_mui.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This contains the integration between the MII video and the MUI interface
 * without any specific windowing system, it should be possible to use this
 * with a native windowing system, or a portable one like SDL2 or GLFW
 * This doesn't do anything to draw on screen, it just moves the video
 * rectangle around, and handles the mouse mapping to the video frame.
 */
#include <stdio.h>
#include <stdlib.h>

#include "mii_mui.h"
#include "minipt.h"

#define MII_MUI_GL_POW2 0

c2_rect_t
c2_rect_interpolate(
		c2_rect_t *a,
		c2_rect_t *b,
		float t)
{
	c2_rect_t r = {};
	r.l = 0.5 + a->l + (b->l - a->l) * t;
	r.r = 0.5 + a->r + (b->r - a->r) * t;
	r.t = 0.5 + a->t + (b->t - a->t) * t;
	r.b = 0.5 + a->b + (b->b - a->b) * t;
	return r;
}

c2_rect_t
mii_mui_get_video_position(
		mii_mui_t * ui)
{
	c2_rect_t r = C2_RECT(0, 0, MII_VIDEO_WIDTH, MII_VIDEO_HEIGHT);
	if (ui->mui_visible) {
		float fac = (ui->window_size.y - 38) / (float)MII_VIDEO_HEIGHT;
		c2_rect_scale(&r, fac);
		c2_rect_offset(&r,
				(ui->window_size.x / 2) - (c2_rect_width(&r) / 2), 36);
	} else {
		float fac = (ui->window_size.y) / (float)MII_VIDEO_HEIGHT;
		c2_rect_scale(&r, fac);
		c2_rect_offset(&r,
				(ui->window_size.x / 2) - (c2_rect_width(&r) / 2),
				(ui->window_size.y / 2) - (c2_rect_height(&r) / 2));
		c2_rect_inset(&r, 10, 10);
	}
	return r;
}

void
mii_mui_showhide_ui_machine(
		mii_mui_t * ui )
{
	pt_start(ui->transision_state);

	while (ui->transition.state == MII_MUI_TRANSITION_NONE)
		pt_yield(ui->transision_state);

	ui->transition.start = mui_get_time();
	ui->transition.end = ui->transition.start + (MUI_TIME_SECOND / 2);
	ui->transition.from = ui->video_frame;

	switch (ui->transition.state) {
		case MII_MUI_TRANSITION_HIDE_UI:
			ui->mui_visible = false;
			ui->transition.to = mii_mui_get_video_position(ui);
			ui->mui_visible = true;
			break;
		case MII_MUI_TRANSITION_SHOW_UI:
			ui->mui_visible = true;
			ui->transition.to = mii_mui_get_video_position(ui);
			break;
	}
	while (1) {
		mui_time_t now = mui_get_time();
		float t = (now - ui->transition.start) /
						(float)(ui->transition.end - ui->transition.start);
		if (t >= 1.0f)
			break;
		switch (ui->transition.state) {
			case MII_MUI_TRANSITION_HIDE_UI:
				ui->mui_alpha = 1.0f - t;
				break;
			case MII_MUI_TRANSITION_SHOW_UI:
				ui->mui_alpha = t;
				break;
		}
		ui->video_frame = c2_rect_interpolate(
							&ui->transition.from, &ui->transition.to, t);
		pt_yield(ui->transision_state);
	}
	switch (ui->transition.state) {
		case MII_MUI_TRANSITION_HIDE_UI:
			ui->mui_visible = false;
			ui->mui_alpha = 0.0f;
			break;
		case MII_MUI_TRANSITION_SHOW_UI:
			ui->mui_alpha = 1.0f;
			break;
	}
	ui->transition.state = MII_MUI_TRANSITION_NONE;

	pt_end(ui->transision_state);
}

void
mii_mui_update_mouse_card(
		mii_mui_t * ui)
{
	mii_t * mii = &ui->mii;
	mui_t * mui = &ui->mui;
	/*
	 * We can grab the mouse if it is enabled by the driver, it is in the
	 * video frame, and there is no active MUI windows (or menus).
	 */
	if (mii->mouse.enabled &&
			c2_rect_contains_pt(&ui->video_frame, &ui->mouse.pos) &&
			!(ui->mui_visible && mui_has_active_windows(mui))) {
		if (!ui->mouse.grabbed) {
			ui->mouse.grab = 1;
			ui->mouse.grabbed = 1;
		//	printf("Grab mouse\n");
		}
	} else {
		if (ui->mouse.grabbed) {
			ui->mouse.ungrab = 1;
			ui->mouse.grabbed = 0;
		//	printf("Ungrab mouse\n");
		}
	}
	if (!ui->mouse.grabbed)
		return;
	double x = ui->mouse.pos.x - ui->video_frame.l;
	double y = ui->mouse.pos.y - ui->video_frame.t;
	// get mouse button state
	int button = ui->mouse.down;
	// clamp coordinates inside bounds
	double vw = c2_rect_width(&ui->video_frame);
	double vh = c2_rect_height(&ui->video_frame);
	double mw = mii->mouse.max_x - mii->mouse.min_x;
	double mh = mii->mouse.max_y - mii->mouse.min_y;
	// normalize mouse coordinates
	mii->mouse.x = mii->mouse.min_x	+ (x * mw / vw) + 0.5;
	mii->mouse.y = mii->mouse.min_y	+ (y * mh / vh) + 0.5;
	mii->mouse.button = button;
}

void
mii_mui_init(
	mii_mui_t * ui,
	c2_pt_t window_size)
{
	mui_drawable_t * dr = &ui->pixels.mui;
	// annoyingly I have to make it a LOT bigger to handle that the
	// non-power-of-2 texture extension is not avialable everywhere
	// textures, which is a bit of a waste of memory, but oh well.
#if MII_MUI_GL_POW2
	int padded_x = 1;
	int padded_y = 1;
	while (padded_x < window_size.x)
		padded_x <<= 1;
	while (padded_y < window_size.y)
		padded_y <<= 1;
#else
	int padded_x = window_size.x;
	int padded_y = window_size.y;
#endif
	mui_drawable_init(dr, C2_PT(padded_x, padded_y), 32, NULL, 0);
	dr->texture.size = C2_PT(padded_x, padded_y);
	printf("MUI Padded UI size is %dx%d\n", padded_x, padded_y);
	ui->mui.screen_size = dr->pix.size;

	ui->window_size = window_size;
	ui->mui_alpha = 1.0f;
	ui->mui_visible = true;
	ui->video_frame = mii_mui_get_video_position(ui);

	mui_t * mui = &ui->mui;
	mui_init(mui);
	mii_mui_menus_init(ui);
	mii_mui_menu_slot_menu_update(ui);
	// Tell libmui to clear the background with transparency.
	mui->color.clear.value = 0;
}
