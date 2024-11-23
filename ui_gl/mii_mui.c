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
#include <math.h>
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

/*
 * This returns the position where the II video should be drawn, depending on
 * the window size and the visibility of the MUI interface.
 */
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


/*
 * If the mesh is empty, create one. Then on future calls, just update the
 * texture coordinates as it's width can change depending on which floppy is
 * being drawn.
 */
void
mii_generate_floppy_mesh(
		mii_mui_v_array_t	* 	vtx,
		float 			tex_width )
{
	if (vtx->count == 0) {
		const double astep = 2 * M_PI / MII_GL_FLOPPY_SEGMENT_COUNT; // Angle step for each blade
		const double radius_out = MII_GL_FLOPPY_FLUX_RADIUS_OUT;
		const double radius_in = MII_GL_FLOPPY_FLUX_RADIUS_IN;

		for (int i = 0; i < MII_GL_FLOPPY_SEGMENT_COUNT; ++i) {
			double a = i * astep, b = (i + 1) * astep;
			// Outer vertex
			double x_out = radius_out * cos(a);
			double y_out = radius_out * sin(a);
			double x_out2 = radius_out * cos(b);
			double y_out2 = radius_out * sin(b);
			// Inner vertex
			double x_in = radius_in * cos(a);
			double y_in = radius_in * sin(a);
			double x_in2 = radius_in * cos(b);
			double y_in2 = radius_in * sin(b);
			// add two triangles winded in the right direction
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x_out, .y = y_out,
			});
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x_in, .y = y_in,
			});
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x_out2, .y = y_out2,
			});
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x_in2, .y = y_in2,
			});
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x_out2, .y = y_out2,
			});
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x_in, .y = y_in,
			});
		}
	}
	const double tex_y_in = 1;
	const double tex_y_out = 0;
	mii_mui_v_t * tex = vtx->e;
	for (int i = 0; i < MII_GL_FLOPPY_SEGMENT_COUNT; ++i) {
		tex->u = (i * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT;
		tex->v = tex_y_out;
		tex++;
		tex->u = (i * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT;
		tex->v = tex_y_in;
		tex++;
		tex->u = ((i + 1) * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT;
		tex->v = tex_y_out;
		tex++;
		tex->u = ((i + 1) * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT;
		tex->v = tex_y_in;
		tex++;
		tex->u = ((i + 1) * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT;
		tex->v = tex_y_out;
		tex++;
		tex->u = (i * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT;
		tex->v = tex_y_in;
		tex++;
	}
}

/*
 * This function takes the video frame size and calculate a vertice list and
 * texture coordinates that will be used to draw the video frame.
 *
 * The base rectangle is made from video_frame, split into a grid , that are
 * then distorted to simulate the barell effect of the CRT screen.
 */
void
mii_generate_screen_mesh(
		mii_mui_t * ui )
{
	mii_mui_v_array_t * vtx = &ui->video_mesh;

	mii_mui_v_array_clear(vtx);

	c2_rect_t r = C2_RECT(0, 0, MII_VIDEO_WIDTH, MII_VIDEO_HEIGHT);
	const int x_steps = 32;
	const int y_steps = 32;
	const float width = c2_rect_width(&r), height = c2_rect_height(&r);
	const float barell = 0.00000025;
//	printf("Frame is %s w:%d h:%d\n", c2_rect_as_str(&r), (int)width, (int)height);

	for (int j = 0; j <= y_steps; j++) {
		float y1 = -(height / 2) + ((height / y_steps) * j);
		float v = (float)j / y_steps;
		for (int i = 0; i <= x_steps; i++) {
			float x1 = -(width / 2) + ((width / x_steps) * i);
			float u = (float)i / x_steps;
			float r1 =  sqrt(x1 * x1 + y1 * y1);
			float r2 = 1.0 - barell * (r1 * r1);
			float x2 = x1 * r2;
			float y2 = y1 * r2;
			float x3 = r.l + x2 + (width / 2);
			float y3 = r.t + y2 + (height / 2);
#if 0
			printf("%2dx%2d : x:%4.2f y:%4.2f x1:%4.2f y1:%4.2f\n"
					"\tr1:%.2f r2:%.2f\n"
					"\tx2:%4.2f y2:%4.2f\n"
					"\tx3:%4.2f y3:%4.2f\n",
				 i, j, x, y, x1, y1, r1, r2, x2, y2, x3, y3);
#endif
			mii_mui_v_array_push(vtx, (mii_mui_v_t){
				.x = x3, .y = y3,
				.u = u, .v = v
			});
		}
	}
	int_array_t * idx = &ui->video_indices;
	int_array_clear(idx);
	/* generate triangles vertice indexes */
	for (int j = 0; j < y_steps; j++) {
		for (int i = 0; i < x_steps ; i++) {
			int tl = i + j * (x_steps + 1);
			int tr = i + 1 + j * (x_steps + 1);
			int bl = i + (j + 1) * (x_steps + 1);
			int br = i + 1 + (j + 1) * (x_steps + 1);
			int_array_push(idx, tl);
			int_array_push(idx, bl);
			int_array_push(idx, tr);
			int_array_push(idx, tr);
			int_array_push(idx, bl);
			int_array_push(idx, br);
		}
	}
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
	// non-power-of-2 texture extension is not available everywhere
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
	ui->mui.screen_size = dr->pix.size;

	ui->window_size = window_size;
	ui->mui_alpha = 1.0f;
	ui->mui_visible = true;
	ui->video_frame = mii_mui_get_video_position(ui);
	mii_generate_screen_mesh(ui);

	mui_t * mui = &ui->mui;
	mui_init(mui);
	mii_mui_menus_init(ui);
	mii_mui_menu_slot_menu_update(ui);
	// Tell libmui to clear the background with transparency.
	mui->color.clear.value = 0;
}

