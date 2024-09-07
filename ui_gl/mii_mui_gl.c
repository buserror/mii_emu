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
#include <math.h>
#include <stdlib.h>
#include <GL/gl.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include "mii_mui_gl.h"
#include "mii_floppy.h"

#define MII_GL_FLOPPY_SEGMENT_COUNT 	32
#define MII_GL_FLOPPY_DISC_RADIUS_IN 	1.8
#define MII_GL_FLOPPY_DISC_RADIUS_OUT 	10
#define MII_GL_FLOPPY_FLUX_RADIUS_IN 	2.0
#define MII_GL_FLOPPY_FLUX_RADIUS_OUT 	9.8

static void
mii_gl_make_disc(
		float_array_t * pos,
		const double radius_out,
		const double radius_in,
		const int count)
{
	float_array_clear(pos);

	const double astep = 2 * M_PI / count; // Angle step for each blade

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
		float_array_push(pos, x_out); float_array_push(pos, y_out);
		float_array_push(pos, x_in); float_array_push(pos, y_in);
		float_array_push(pos, x_out2); float_array_push(pos, y_out2);

		float_array_push(pos, x_in2); float_array_push(pos, y_in2);
		float_array_push(pos, x_out2); float_array_push(pos, y_out2);
		float_array_push(pos, x_in); float_array_push(pos, y_in);
	}
}
static void
mii_gl_make_floppy(
	mii_vtx_t	* 	vtx,
	float 			tex_width,
	bool 			do_pos,
	bool 			do_tex)
{
	vtx->kind = GL_TRIANGLES;
	if (do_pos) {
		mii_gl_make_disc(&vtx->pos,
				MII_GL_FLOPPY_FLUX_RADIUS_OUT,
				MII_GL_FLOPPY_FLUX_RADIUS_IN,
				MII_GL_FLOPPY_SEGMENT_COUNT);
	}
	if (!do_tex)
		return;
	const double tex_y_in = 1;
	const double tex_y_out = 0;
	float_array_t * tex = &vtx->tex;
	float_array_clear(tex);
	for (int i = 0; i < MII_GL_FLOPPY_SEGMENT_COUNT; ++i) {
		float_array_push(tex, (i * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT);
		float_array_push(tex, tex_y_out);
		float_array_push(tex, (i * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT);
		float_array_push(tex, tex_y_in);
		float_array_push(tex, ((i + 1) * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT);
		float_array_push(tex, tex_y_out);
		float_array_push(tex, ((i + 1) * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT);
		float_array_push(tex, tex_y_in);
		float_array_push(tex, ((i + 1) * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT);
		float_array_push(tex, tex_y_out);
		float_array_push(tex, (i * tex_width) / MII_GL_FLOPPY_SEGMENT_COUNT);
		float_array_push(tex, tex_y_in);
	}
}

void
mii_mui_gl_init(
		mii_mui_t *ui)
{
	GLuint tex[MII_PIXEL_LAYERS];
	glGenTextures(MII_PIXEL_LAYERS, tex);
	for (int i = 0; i < MII_PIXEL_LAYERS; i++) {
	//	printf("Texture %d created %d\n", i, tex[i]);
		ui->pixels.v[i].texture.id = tex[i];
		ui->tex_id[i] = tex[i];
	}
	mii_gl_make_disc(&ui->floppy_base,
			MII_GL_FLOPPY_DISC_RADIUS_OUT,
			MII_GL_FLOPPY_DISC_RADIUS_IN,
			MII_GL_FLOPPY_SEGMENT_COUNT);
	mii_mui_gl_prepare_textures(ui);
}

static void
_prep_grayscale_texture(
	mui_drawable_t * dr)
{
	dr->texture.size = dr->pix.size;
//	printf("Creating texture %4d %4dx%3d row_byte %4d\n",
//			dr->texture.id, dr->pix.size.x, dr->pix.size.y, dr->pix.row_bytes);
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	dr->texture.kind = GL_LUMINANCE;
	glTexImage2D(GL_TEXTURE_2D, 0, 1,
			dr->pix.row_bytes, dr->texture.size.y, 0, dr->texture.kind,
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
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// disable the repeat of textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	dr->texture.kind = GL_RGBA;// note RGBA here, it's quicker!!
	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			MII_VRAM_WIDTH,
			MII_VRAM_HEIGHT, 0, dr->texture.kind,
	        GL_UNSIGNED_BYTE,	// GL_UNSIGNED_INT_8_8_8_8_REV
	        mii->video.pixels);

	// bind the mui texture using the GL_ARB_texture_rectangle as well
	dr = &ui->pixels.mui;
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// here we use GL_BGRA, as the pixman/libmui uses that.
	dr->texture.kind = GL_BGRA;
	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			dr->pix.row_bytes / 4,	// already power of two.
			dr->texture.size.y, 0, dr->texture.kind,
			GL_UNSIGNED_INT_8_8_8_8_REV,
			dr->pix.pixels);

#if MII_VIDEO_DEBUG_HEAPMAP
	dr = &ui->pixels.video_heapmap;
	unsigned int tex = dr->texture.id;
	mui_drawable_init(dr, C2_PT(192, 1), 8, mii->video.video_hmap, 192);
	dr->texture.id = tex;
	_prep_grayscale_texture(dr);
#endif
	// ask MII for floppies -- we'll only use the first two (TODO?)
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
					C2_PT(MII_FLOPPY_MAX_TRACK_SIZE, MII_FLOPPY_TRACK_COUNT),
					8, floppy[fi]->track_data, MII_FLOPPY_MAX_TRACK_SIZE);
			dr->texture.id = tex;
			_prep_grayscale_texture(dr);
			if (!f->heat) {
#if defined(__AVX2__)
				posix_memalign((void**)&f->heat, 32, sizeof(*f->heat));
#elif defined(__SSE2__)
				posix_memalign((void**)&f->heat, 16, sizeof(*f->heat));
#else
				f->heat = malloc(sizeof(*f->heat));
#endif
				memset(f->heat, 0, sizeof(*f->heat));
			}
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
			mii_gl_make_floppy(&ui->floppy[fi].vtx, 1.0, true, true);
		}
	} else {
		printf("%s No floppy found\n", __func__);
		for (int fi = 0; fi < 2; fi++) {
			ui->floppy[fi].floppy = NULL;
			mui_drawable_clear(&ui->pixels.floppy[fi].bits);
			mui_drawable_clear(&ui->pixels.floppy[fi].hm_read);
			mui_drawable_clear(&ui->pixels.floppy[fi].hm_write);
			mii_gl_make_floppy(&ui->floppy[fi].vtx, 1.0, true, true);
		}
	}
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("%s Error creating texture: %d\n", __func__, err);
	}
}

typedef uint8_t u8_v __attribute__((vector_size(16)));

/* Heatmaps 'decay' by a gradient each frame until fully transparent */
static inline int
_mii_decay_buffer(
		uint8_t *buffer,
		int size)
{
	uint32_t count = 0;
	const uint8_t decay = 4;		// totally arbitrary
#if defined(__AVX2__)
	size /= 32;
	__m256i * hmw = (__m256i*)buffer;
	const __m256i s = _mm256_set1_epi8(decay);
	for (int i = 0; i < size; i++) {
		__m256i b = _mm256_load_si256(hmw + i);
		__m256i c = _mm256_subs_epu8(b, s);
		hmw[i] = c;
		// add 1 to count if c is non zero
		count += !!_mm256_movemask_epi8(
						_mm256_cmpeq_epi8(c, _mm256_setzero_si256()));
	}
#elif defined(__SSE2__)
	size /= 16;
	__m128i * hmw = (__m128i*)buffer;
	const __m128i s = _mm_set1_epi8(decay);
	for (int i = 0; i < size; i++) {
		__m128i b = _mm_load_si128(hmw + i);
		__m128i c = _mm_subs_epu8(b, s);
		hmw[i] = c;
		// add 1 to count if c is non zero
		count += !!_mm_movemask_epi8(
						_mm_cmpeq_epi8(c, _mm_setzero_si128()));
	}
#else
#if 1	// generic vector code, NEON or WASM?
	size /= sizeof(u8_v);
	u8_v * hmb = (u8_v*)buffer;
	for (int i = 0; i < size; i++) {
		u8_v b = hmb[i];
		u8_v c;
		for (uint j = 0; j < sizeof(c); j++)
			c[j] = b[j] > decay ? b[j] - decay : 0;
		hmb[i] = c;
		uint64_t * w = (uint64_t*)&c;
		count += w[0] || w[1];
	}
#else
	uint8_t * hmb = buffer;
	for (int i = 0; i < size; i++) {
		uint8_t b = hmb[i];
		b = b > decay ? b - decay : 0;
		hmb[i] = b;
		count += !!b;
	}
#endif
#endif
	return count;
}

static void
_mii_decay_heatmap_one(
		mii_track_heatmap_t *hm)
{
	const int size = MII_FLOPPY_TRACK_COUNT * MII_FLOPPY_HM_TRACK_SIZE;
	uint32_t count = _mii_decay_buffer((uint8_t*)&hm->map[0], size);
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
	if (ui->transition.state != MII_MUI_TRANSITION_NONE)
		draw = true;
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
						dr->texture.kind,
						GL_UNSIGNED_INT_8_8_8_8_REV,
						dr->pix.pixels + (r.t * dr->pix.row_bytes) + (r.l * 4));
			}
		}
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		pixman_region32_clear(&mui->redraw);
	}
	uint32_t current_seed = mii->video.frame_seed;
	if (current_seed != ui->video_drawn_seed) {
	//	miigl_counter_tick(&ui->videoc, miigl_get_time());
		draw = true;
		ui->video_drawn_seed = current_seed;
		// update the whole texture
		mui_drawable_t * dr = &ui->pixels.mii;
		glBindTexture(GL_TEXTURE_2D, dr->texture.id);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
				MII_VRAM_WIDTH,
				MII_VIDEO_HEIGHT, dr->texture.kind,
				GL_UNSIGNED_INT_8_8_8_8_REV,
				mii->video.pixels);
	}
#if MII_VIDEO_DEBUG_HEAPMAP
	if (ui->mii.state == MII_RUNNING) {
		int cnt = _mii_decay_buffer(ui->mii.video.video_hmap, 192);
		if (cnt) {
			draw = true;
			mui_drawable_t * dr = &ui->pixels.video_heapmap;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					dr->texture.kind, GL_UNSIGNED_BYTE,
					dr->pix.pixels);
		}
	}
#endif
	for (int fi = 0; fi < 2; fi++) {
		if (!ui->floppy[fi].floppy)
			continue;
		mui_drawable_t * dr = NULL;
		mii_floppy_t * f = ui->floppy[fi].floppy;
		dr = &ui->pixels.floppy[fi].bits;
		if (ui->floppy[fi].seed_load != f->seed_dirty) {
			draw = true;
			ui->floppy[fi].seed_load = f->seed_dirty;
		//	printf("Floppy %d: Reloading texture\n", fi);
			int bc = (f->tracks[0].bit_count + 7) / 8;
			int max = MII_FLOPPY_MAX_TRACK_SIZE;
			ui->floppy[fi].max_width = (double)bc / (double)max;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					dr->texture.kind, GL_UNSIGNED_BYTE,
					f->track_data);
			// dont recalculate the vertices, just the texture coordinates
			mii_gl_make_floppy(&ui->floppy[fi].vtx,
							ui->floppy[fi].max_width, false, true);
		} else if (dr->texture.opacity > 0.0f ||
						ui->floppy[fi].floppy->motor) {// still animating
			draw = true;
		}
		int rm = f->heat->read.tex != f->heat->read.seed ||
						!f->heat->read.cleared;
		int wm = f->heat->write.tex != f->heat->write.seed ||
						!f->heat->write.cleared;
		// don't decay if we're not running
		if (ui->mii.state == MII_RUNNING)
			_mii_decay_heatmap(f->heat);
		if (rm) {
			glPixelStorei(GL_UNPACK_ROW_LENGTH, MII_FLOPPY_HM_TRACK_SIZE);
			dr = &ui->pixels.floppy[fi].hm_read;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					dr->texture.kind, GL_UNSIGNED_BYTE,
					f->heat->read.map);
		}
		if (wm) {
			glPixelStorei(GL_UNPACK_ROW_LENGTH, MII_FLOPPY_HM_TRACK_SIZE);
			dr = &ui->pixels.floppy[fi].hm_write;
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					dr->pix.row_bytes, dr->pix.size.y,
					dr->texture.kind, GL_UNSIGNED_BYTE,
					f->heat->write.map);
		}
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

	/* draw mii texture */
	glColor3f(1.0f, 1.0f, 1.0f);
	mui_drawable_t * dr = &ui->pixels.mii;
	glEnable(GL_TEXTURE_2D);
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

#if MII_VIDEO_DEBUG_HEAPMAP
	/* draw video heatmap */
	dr = &ui->pixels.video_heapmap;
	if (dr->pix.pixels) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
		glPushMatrix();
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, dr->texture.id);
		glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
		// draw a vertical quad on the left side of the video to show
		// what's being updated
		glBegin(GL_QUADS);
		c2_rect_t r = ui->video_frame;
		glTexCoord2f(0, 1);
			glVertex2f(r.l - 10, r.t);
		glTexCoord2f(0, 0);
			glVertex2f(r.l + 0, r.t);
		glTexCoord2f(1, 0);
			glVertex2f(r.l + 0, r.b);
		glTexCoord2f(1, 1);
			glVertex2f(r.l - 10, r.b);
		glEnd();
		glPopMatrix();
	}
#endif
	/* draw floppies. First the disk, then the texture, then the heatmap */
	for (int i = 0; i < 2; i++) {
		dr = &ui->pixels.floppy[i].bits;
		mii_floppy_t *f = ui->floppy[i].floppy;
		if (!f || !dr->pix.pixels)
			continue;
		if (ui->mii.state == MII_RUNNING) {
			if (f->motor) {
				if (dr->texture.opacity < 1.0f)
					dr->texture.opacity += 0.10f;
				if (dr->texture.opacity > 1.0f)
					dr->texture.opacity = 1.0f;
			} else  {
				if (dr->texture.opacity > 0.0f)
					dr->texture.opacity -= 0.01f;
				if (dr->texture.opacity < 0.0f)
					dr->texture.opacity = 0.0f;
			}
		}
		float main_opacity = dr->texture.opacity;
		if (main_opacity <= 0.0f)
			continue;
		const float angle_offset = 60;	// head angle offset on display
		{
			glPushMatrix();
			// make floppy slide in/out with opacity
			glTranslatef(-10 - (100.0 * ( 1.0f - main_opacity) ),
					200 + (i * 350), 0);
			glScalef(15, 15, 1);
			{
				glColor4f(0.0f, 0.0f, 0.0f, main_opacity);
				glDisable(GL_TEXTURE_2D);
				glEnableClientState(GL_VERTEX_ARRAY);
				glVertexPointer(2, GL_FLOAT, 0, ui->floppy_base.e);
				int element_count = ui->floppy_base.count / 2;
				glDrawArrays(GL_TRIANGLES, 0, element_count);
			}
			int track_id = f->track_id[f->qtrack];
			double bc = (double)f->bit_position /
					(double)f->tracks[track_id].bit_count;
			bc = 360 - (bc * 360.0);
			bc += angle_offset;
			if (bc >= 360.0)
				bc -= 360.0;
			glRotatef(bc, 0, 0, 1);
			dr = &ui->pixels.floppy[i].bits;
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, dr->texture.id);
//				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1.0f, 1.0f, 1.0f, main_opacity);
			glEnableClientState(GL_VERTEX_ARRAY);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glVertexPointer(2, GL_FLOAT, 0, ui->floppy[i].vtx.pos.e);
			glTexCoordPointer(2, GL_FLOAT, 0, ui->floppy[i].vtx.tex.e);
			int element_count = ui->floppy[i].vtx.pos.count / 2;
			glDrawArrays(ui->floppy[i].vtx.kind, 0, element_count);
			// draw heatmap and head with full opacity
			// otherwise we get wierd artifacts
			if (f->heat) {
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
				dr = &ui->pixels.floppy[i].hm_read;
				glColor4f(0.0f, 1.0f, 0.0f, 1.0);
				glBindTexture(GL_TEXTURE_2D, dr->texture.id);
				glDrawArrays(ui->floppy[i].vtx.kind, 0, element_count);
				dr = &ui->pixels.floppy[i].hm_write;
				glColor4f(1.0f, 0.0f, 0.0f, 1.0);
				glBindTexture(GL_TEXTURE_2D, dr->texture.id);
				glDrawArrays(ui->floppy[i].vtx.kind, 0, element_count);
			}
			glDisableClientState(GL_VERTEX_ARRAY);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			if (main_opacity > 0.8f) {
				// Draw head small rectangle
				dr = &ui->pixels.floppy[i].bits;
				track_id = f->qtrack / 4;
				glDisable(GL_TEXTURE_2D);
				glRotatef(-bc + angle_offset, 0, 0, 1);
				glTranslatef(MII_GL_FLOPPY_FLUX_RADIUS_IN +
							(((35 - track_id) / 35.0) *
							(MII_GL_FLOPPY_FLUX_RADIUS_OUT-
							MII_GL_FLOPPY_FLUX_RADIUS_IN)), 0, 0);
				const float r = 0.3;
				glColor4f(1.0f, 0.0f, 0.0f, main_opacity);
				glBegin(GL_QUADS);
				glVertex2f(-r, -r); glVertex2f(-r, r);
				glVertex2f(r, r); glVertex2f(r, -r);
				glEnd();
			}
			glPopMatrix();
		}
	}
	/* draw mui texture */
	if (ui->mui_alpha > 0.0f) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(1.0f, 1.0f, 1.0f, ui->mui_alpha);
		dr = &ui->pixels.mui;
		glEnable(GL_TEXTURE_2D);
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
