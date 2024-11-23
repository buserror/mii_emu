/*
 * mii_mui.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
/*
	This tries to contains a structure that is the MUI interface over the MII
	video, but without any attachment to x11 or opengl. Basically hopefully
	segregating the relevant logic without tying it to a specific windowing
	system.
	Hopefully with a bit more work this OUGHT to allow Windows/macOS port
	with a native frontend.
 */

#include <stdbool.h>
#include <stdint.h>
#include "mii.h"
#include "mui.h"
#include "mii_mui_settings.h"
#include "mii_floppy.h"

enum mii_mui_transition_e {
	MII_MUI_TRANSITION_NONE,
	MII_MUI_TRANSITION_HIDE_UI,
	MII_MUI_TRANSITION_SHOW_UI,
};

#define MII_PIXEL_LAYERS	9

typedef struct mii_mui_v_t {
	float x, y, u, v;
}	mii_mui_v_t;

DECLARE_C_ARRAY(mii_mui_v_t, mii_mui_v_array, 16,
				unsigned int 		vao, vbo, kind;);
IMPLEMENT_C_ARRAY(mii_mui_v_array);

DECLARE_C_ARRAY(int, int_array, 16, unsigned int ebo;);
IMPLEMENT_C_ARRAY(int_array);


typedef struct mii_mui_t {
	mui_t 					mui;		// mui interface
	mii_t 					mii;		// apple II emulator
	c2_pt_t 				window_size;
	long					last_button_click;
	struct {
		int 					ungrab, grab, grabbed, down;
		c2_pt_t 				pos;
	} 						mouse;
	mui_event_t 			key;
	// This is the list of vertices and coordinates for the screen, including
	// the pincushion distortion. This is inset from video_frame
	mii_mui_v_array_t		video_mesh;
	int_array_t 			video_indices;
	c2_rect_t				video_frame; // current video frame
	uint32_t 				video_drawn_seed;
	float					mui_alpha;
	bool	 				mui_visible;
	void *					transision_state;
	struct {
		uint8_t 				state;
		mui_time_t				start, end;
		c2_rect_t 				from, to;
	} 						transition;
	unsigned int			tex_id[MII_PIXEL_LAYERS];
	union {
		struct {
			mui_drawable_t		mii;
			mui_drawable_t		mui;
			struct {
				mui_drawable_t		bits;
				mui_drawable_t		hm_read;
				mui_drawable_t		hm_write;
			}					floppy[2];
			// this is debug only!
			mui_drawable_t 		video_heapmap;
		};
		mui_drawable_t			v[MII_PIXEL_LAYERS];
	}						pixels;
	struct {
		mii_floppy_t * 				floppy;
		uint32_t 					seed_load;
		float 						max_width;
		/* Despite having the same size, they can (will) have different
		 * texture coordinates, so each floppy have their own */
		mii_mui_v_array_t			vtx;
	}							floppy[2];

	mii_machine_config_t	config;
	mii_loadbin_conf_t		loadbin_conf;

	mii_config_file_t		cf;
} mii_mui_t;

void
mii_mui_init(
	mii_mui_t * ui,
	c2_pt_t window_size);

void
mii_mui_menus_init(
	mii_mui_t * ui);
void
mii_mui_menu_slot_menu_update(
	mii_mui_t * ui);

void
mii_mui_update_mouse_card(
		mii_mui_t * ui);
void
mii_mui_showhide_ui_machine(
		mii_mui_t * ui );
c2_rect_t
mii_mui_get_video_position(
		mii_mui_t * ui);

#define MII_GL_FLOPPY_SEGMENT_COUNT 	48
#define MII_GL_FLOPPY_DISC_RADIUS_IN 	1.8
#define MII_GL_FLOPPY_DISC_RADIUS_OUT 	10
#define MII_GL_FLOPPY_FLUX_RADIUS_IN 	2.0
#define MII_GL_FLOPPY_FLUX_RADIUS_OUT 	9.8

void
mii_generate_floppy_mesh(
		mii_mui_v_array_t	* 	vtx,
		float 			tex_width );

// slot can be <= 0 to open the machine dialog instead
void
mii_config_open_slots_dialog(
		mii_mui_t * ui);


