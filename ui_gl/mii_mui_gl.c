/*
 * mii_mui_gl.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This contains OpenGL code, no x11 or GLx allowed in here, this is to be
 * used by a native windowing system, or a portable one like SDL2 or GLFW
 */

#include <stdio.h>
#include <stdlib.h>
#include <GL/gl.h>

#ifdef __SSE2__
#include <emmintrin.h> // SSE2 intrinsics
#endif

#include "mii_mui_gl.h"
#include "mii_floppy.h"


typedef struct c2_rect_f {
	float l,t,r,b;
} c2_rect_f;

void
mii_mui_gl_init(
		mii_mui_t *ui)
{
	GLuint tex[MII_PIXEL_LAYERS];
	glGenTextures(MII_PIXEL_LAYERS, tex);
	for (int i = 0; i < MII_PIXEL_LAYERS; i++) {
		printf("Texture %d created %d\n", i, tex[i]);
		ui->pixels.v[i].texture.id = tex[i];
		ui->tex_id[i] = tex[i];
	}

	mii_mui_gl_prepare_textures(ui);
}

static void
_prep_grayscale_texture(
	mui_drawable_t * dr)
{
	dr->texture.size = dr->pix.size;
	printf("Creating texture %4d %4dx%3d row_byte %4d\n",
			dr->texture.id,
			dr->pix.size.x, dr->pix.size.y,
			dr->pix.row_bytes);
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, 1,
			dr->pix.row_bytes, dr->texture.size.y, 0, GL_LUMINANCE,
			GL_UNSIGNED_BYTE,
			dr->pix.pixels);
}

void
mii_mui_gl_prepare_textures(
		mii_mui_t *ui)
{
	mii_t * mii = &ui->mii;

	glEnable(GL_TEXTURE_2D);
	mui_drawable_t * dr = &ui->pixels.mii;
	// bind the mii texture using the GL_ARB_texture_rectangle extension
	printf("Creating texture %4d %4dx%3d row_byte %4d (MII)\n",
			dr->texture.id,
			dr->pix.size.x, dr->pix.size.y,
			dr->pix.row_bytes);
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// disable the repeat of textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			MII_VRAM_WIDTH,
			MII_VRAM_HEIGHT, 0, GL_BGRA,	// note BGRA here, not RGBA
	        GL_UNSIGNED_BYTE,
	        mii->video.pixels);

	// bind the mui texture using the GL_ARB_texture_rectangle as well
	dr = &ui->pixels.mui;
	printf("Creating texture %4d %4dx%3d row_byte %4d (MUI)\n",
			dr->texture.id,
			dr->pix.size.x, dr->pix.size.y,
			dr->pix.row_bytes);
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			dr->pix.row_bytes / 4,	// already power of two.
			dr->texture.size.y, 0, GL_BGRA,
			GL_UNSIGNED_BYTE,
			dr->pix.pixels);

	mii_floppy_t * floppy[2] = {};
	for (int i = 0; i < 7; i++) {
		if (mii_slot_command(mii, i, MII_SLOT_D2_GET_FLOPPY, floppy) == 0)
			break;
	}
	if (floppy[0]) {
		for (int fi = 0; fi < 2; fi++) {
			mii_floppy_t * f = floppy[fi];
			ui->floppy[fi].floppy = f;

			dr = &ui->pixels.floppy[fi].bits;
			// the init() call clears the structure, keep our id around
			unsigned int tex = dr->texture.id;
			mui_drawable_init(dr,
					C2_PT(MII_FLOPPY_DEFAULT_TRACK_SIZE, MII_FLOPPY_TRACK_COUNT),
					8, floppy[fi]->track_data, MII_FLOPPY_DEFAULT_TRACK_SIZE);
			dr->texture.id = tex;
			_prep_grayscale_texture(dr);
			if (!f->heat)
				f->heat = calloc(1, sizeof(*f->heat));
			dr = &ui->pixels.floppy[fi].hm_read;
			tex = dr->texture.id;
			mui_drawable_init(dr,
					C2_PT(MII_FLOPPY_HM_TRACK_SIZE, MII_FLOPPY_TRACK_COUNT),
					8, f->heat->read.map, MII_FLOPPY_HM_TRACK_SIZE);
			dr->texture.id = tex;
			_prep_grayscale_texture(dr);
			dr = &ui->pixels.floppy[fi].hm_write;
			tex = dr->texture.id;
			mui_drawable_init(dr,
					C2_PT(MII_FLOPPY_HM_TRACK_SIZE, MII_FLOPPY_TRACK_COUNT),
					8, f->heat->write.map, MII_FLOPPY_HM_TRACK_SIZE);
			dr->texture.id = tex;
			_prep_grayscale_texture(dr);
		}
	} else {
		printf("No floppy found\n");
		for (int fi = 0; fi < 2; fi++) {
			ui->floppy[fi].floppy = NULL;
			mui_drawable_clear(&ui->pixels.floppy[fi].bits);
			mui_drawable_clear(&ui->pixels.floppy[fi].hm_read);
			mui_drawable_clear(&ui->pixels.floppy[fi].hm_write);
		}
	}
//	printf("%s texture created %d\n", __func__, mii_apple_screen_tex);
// display opengl error
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("Error creating texture: %d\n", err);
	}
}


static void
_mii_decay_heatmap_one(
		mii_track_heatmap_t *hm)
{
	uint32_t count = 0;
#ifdef __SSE2__
	const int size = (MII_FLOPPY_TRACK_COUNT * MII_FLOPPY_HM_TRACK_SIZE) / 16;
	__m128i * hmw = (__m128i*)&hm->map[0];
	const __m128i s = _mm_set1_epi8(2);
	for (int i = 0; i < size; i++) {
		__m128i b = _mm_load_si128(hmw + i);
		__m128i c = _mm_subs_epu8(b, s);
		hmw[i] = c;
		count += _mm_movemask_epi8(_mm_cmpgt_epi8(c, _mm_setzero_si128()));
	}
#else
	const int size = MII_FLOPPY_TRACK_COUNT * MII_FLOPPY_HM_TRACK_SIZE;
	uint8_t * hmb = (uint8_t*)&hm->map[0];
	for (int i = 0; i < size; i++) {
		uint8_t b = hmb[i];
		b = b > 2 ? b - 2 : 0;
		hmb[i] = b;
		count += !!b;
	}
#endif
	hm->cleared = count == 0;
}

static void
_mii_decay_heatmap(
		mii_floppy_heatmap_t *h)
{
	if (h->read.seed != h->read.tex || !h->read.cleared) {
		h->read.tex = h->read.tex;
		_mii_decay_heatmap_one(&h->read);
	}
	if (h->write.seed != h->write.tex || !h->write.cleared) {
		h->write.tex = h->write.tex;
		_mii_decay_heatmap_one(&h->write);
	}
}

bool
mii_mui_gl_run(
		mii_mui_t *ui)
{
	mii_t * mii = &ui->mii;
	mui_t * mui = &ui->mui;

	mui_run(mui);
	bool draw = false;
	if (pixman_region32_not_empty(&mui->inval)) {
		draw = true;
		mui_drawable_t * dr = &ui->pixels.mui;
		mui_draw(mui, dr, 0);
		glBindTexture(GL_TEXTURE_2D, dr->texture.id);

		pixman_region32_intersect_rect(&mui->redraw, &mui->redraw,
				0, 0, dr->pix.size.x, dr->pix.size.y);
		int rc = 0;
		c2_rect_t *ra = (c2_rect_t*)pixman_region32_rectangles(&mui->redraw, &rc);
	//	rc = 1; ra = &C2_RECT(0, 0, mui->screen_size.x, mui->screen_size.y);
		if (rc) {
	//		printf("GL: %d rects to redraw\n", rc);
			for (int i = 0; i < rc; i++) {
				c2_rect_t r = ra[i];
	//			printf("GL: %d,%d %dx%d\n", r.l, r.t, c2_rect_width(&r), c2_rect_height(&r));
				glPixelStorei(GL_UNPACK_ROW_LENGTH, dr->pix.row_bytes / 4);
				glTexSubImage2D(GL_TEXTURE_2D, 0, r.l, r.t,
						c2_rect_width(&r), c2_rect_height(&r),
						GL_BGRA, GL_UNSIGNED_BYTE,
						dr->pix.pixels + (r.t * dr->pix.row_bytes) + (r.l * 4));
			}
		}
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		pixman_region32_clear(&mui->redraw);
	}
	uint32_t current_frame = mii->video.frame_count;
	if (current_frame != mii->video.frame_drawn) {
	//	miigl_counter_tick(&ui->videoc, miigl_get_time());
		draw = true;
		mii->video.frame_drawn = current_frame;
		// update the whole texture
		mui_drawable_t * dr = &ui->pixels.mii;
		glBindTexture(GL_TEXTURE_2D, dr->texture.id);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
				MII_VRAM_WIDTH,
				MII_VIDEO_HEIGHT, GL_BGRA,
				GL_UNSIGNED_BYTE,
				mii->video.pixels);
	}
	for (int fi = 0; fi < 2; fi++) {
		if (!ui->floppy[fi].floppy)
			continue;
		mui_drawable_t * dr = NULL;
		mii_floppy_t * f = ui->floppy[fi].floppy;
		if (ui->floppy[fi].seed_load != f->seed_dirty) {
			draw = true;
			ui->floppy[fi].seed_load = f->seed_dirty;
		//	printf("Floppy %d: Reloading texture\n", fi);
			dr = &ui->pixels.floppy[fi].bits;
			int bc = (f->tracks[0].bit_count + 7) / 8;
			int max = MII_FLOPPY_DEFAULT_TRACK_SIZE;
			ui->floppy[fi].max_width = (double)bc / (double)max;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					f->track_data);
		}
//		int rm = f->heat->read.tex != f->heat->read.seed;
//		int wm = f->heat->write.tex != f->heat->write.seed;
		_mii_decay_heatmap(f->heat);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, MII_FLOPPY_HM_TRACK_SIZE);
//		if (rm) {
			dr = &ui->pixels.floppy[fi].hm_read;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					f->heat->read.map);
//		}
//		if (wm) {
			dr = &ui->pixels.floppy[fi].hm_write;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					f->heat->write.map);
//		}
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
	return draw;
}

void
mii_mui_gl_render(
		mii_mui_t *ui)
{
	glClearColor(
		.6f * ui->mui_alpha,
		.6f * ui->mui_alpha,
		.6f * ui->mui_alpha,
		1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_TRANSFORM_BIT);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* setup viewport/project */
	glViewport(0, 0,
				(GLsizei)ui->window_size.x,
				(GLsizei)ui->window_size.y);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0f, ui->window_size.x, ui->window_size.y,
				0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	// This (was) the recommended way to handle pixel alignment in glOrtho
	// mode, but this seems to have changed -- now it looks like Linear filtering
//	glTranslatef(0.375f, 0.375f, 0.0f);
	{
		/* draw mii texture */
		glColor3f(1.0f, 1.0f, 1.0f);
		mui_drawable_t * dr = &ui->pixels.mii;
		glBindTexture(GL_TEXTURE_2D, dr->texture.id);
		glBegin(GL_QUADS);
		c2_rect_t r = ui->video_frame;
		glTexCoord2f(0, 0);
				glVertex2f(r.l, r.t);
		glTexCoord2f(MII_VIDEO_WIDTH / (double)MII_VRAM_WIDTH, 0);
				glVertex2f(r.r, r.t);
		glTexCoord2f(MII_VIDEO_WIDTH / (double)MII_VRAM_WIDTH,
					MII_VIDEO_HEIGHT / (double)MII_VRAM_HEIGHT);
				glVertex2f(r.r, r.b);
		glTexCoord2f(0,
					MII_VIDEO_HEIGHT / (double)MII_VRAM_HEIGHT);
				glVertex2f(r.l, r.b);
		glEnd();

		/* draw floppy textures, floppy 0 is left of the screen,
		   floppy 1 is right */
		for (int i = 0; i < 2; i++) {
			dr = &ui->pixels.floppy[i].bits;
			mii_floppy_t *f = ui->floppy[i].floppy;
			if (!f || !dr->pix.pixels)
				continue;
			if (f->motor) {
				dr->texture.opacity = 1.0f;
			} else {
				if (dr->texture.opacity > 0.0f)
					dr->texture.opacity -= 0.01f;
				if (dr->texture.opacity < 0.0f)
					dr->texture.opacity = 0.0f;
			}
			if (dr->texture.opacity <= 0.0f)
				continue;
			c2_rect_t r = C2_RECT_WH( 0, 0,
								ui->video_frame.l - 20,
								c2_rect_height(&ui->video_frame) - 22);
			c2_rect_f tr = { 0, 0, ui->floppy[i].max_width, 1 };
			if (i == 0)
				c2_rect_offset(&r,
								ui->video_frame.l - c2_rect_width(&r) - 10,
								ui->video_frame.t + 10);
			else
				c2_rect_offset(&r, ui->video_frame.r + 10,
								ui->video_frame.t + 10);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1.0f, 1.0f, 1.0f, dr->texture.opacity);
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glBegin(GL_QUADS);
			// rotate texture 90 clockwise, and mirror left-right
			glTexCoord2f(tr.l, tr.t); glVertex2f(r.l, r.t);
			glTexCoord2f(tr.l, tr.b); glVertex2f(r.r, r.t);
			glTexCoord2f(tr.r, tr.b); glVertex2f(r.r, r.b);
			glTexCoord2f(tr.r, tr.t); glVertex2f(r.l, r.b);
			glEnd();

			if (f->heat) {
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
				dr = &ui->pixels.floppy[i].hm_read;
				glColor4f(0.0f, 1.0f, 0.0f, 1.0);
				glBindTexture(GL_TEXTURE_2D, dr->texture.id);
				glBegin(GL_QUADS);
				// rotate texture 90 clockwise, and mirror left-right
				glTexCoord2f(tr.l, tr.t); glVertex2f(r.l, r.t);
				glTexCoord2f(tr.l, tr.b); glVertex2f(r.r, r.t);
				glTexCoord2f(tr.r, tr.b); glVertex2f(r.r, r.b);
				glTexCoord2f(tr.r, tr.t); glVertex2f(r.l, r.b);
				glEnd();
				dr = &ui->pixels.floppy[i].hm_write;
				glColor4f(1.0f, 0.0f, 0.0f, 1.0);
				glBindTexture(GL_TEXTURE_2D, dr->texture.id);
				glBegin(GL_QUADS);
				// rotate texture 90 clockwise, and mirror left-right
				glTexCoord2f(tr.l, tr.t); glVertex2f(r.l, r.t);
				glTexCoord2f(tr.l, tr.b); glVertex2f(r.r, r.t);
				glTexCoord2f(tr.r, tr.b); glVertex2f(r.r, r.b);
				glTexCoord2f(tr.r, tr.t); glVertex2f(r.l, r.b);
				glEnd();
			}
		}
		/* draw mui texture */
		if (ui->mui_alpha > 0.0f) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1.0f, 1.0f, 1.0f, ui->mui_alpha);
			dr = &ui->pixels.mui;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glBegin(GL_QUADS);
			glTexCoord2f(0, 0); glVertex2f(0, 0);
			glTexCoord2f(
					ui->window_size.x / (double)(dr->pix.row_bytes / 4), 0);
					glVertex2f(ui->window_size.x, 0);
			glTexCoord2f(ui->window_size.x / (double)(dr->pix.row_bytes / 4),
						ui->window_size.y / (double)(dr->texture.size.y));
					glVertex2f(ui->window_size.x, ui->window_size.y);
			glTexCoord2f(0,
						ui->window_size.y / (double)(dr->texture.size.y));
					glVertex2f(0, ui->window_size.y);
			glEnd();
		}
	}
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0);
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopAttrib();
}
