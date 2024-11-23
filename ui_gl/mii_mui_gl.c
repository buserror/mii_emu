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

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include "mii_mui_gl.h"
#include "mii_floppy.h"

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
	mii_mui_gl_prepare_textures(ui);
}

/*
 * Grayscale textures are the floppy 'bits' and the heapmaps. They actually
 * look a lot better as nearest neighbour, as they ought to look like pixels.
 */
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
mui_mui_gl_regenerate_ui_texture(
		mii_mui_t *ui)
{
	mui_drawable_t * dr = &ui->pixels.mui;
	if (dr->texture.id != 0) {
		glDeleteTextures(1, &dr->texture.id);
		glGenTextures(1, &dr->texture.id);
	}
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// here we use GL_BGRA, as the pixman/libmui uses that.
	dr->texture.kind = GL_BGRA;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			dr->pix.row_bytes / 4,
			dr->texture.size.y, 0, dr->texture.kind,
			GL_UNSIGNED_BYTE, //GL_UNSIGNED_INT_8_8_8_8_REV,
			dr->pix.pixels);
}

void
mii_mui_gl_prepare_textures(
		mii_mui_t *ui)
{
	mii_t * mii = &ui->mii;

	glEnable(GL_TEXTURE_2D);
	mui_drawable_t * dr = &ui->pixels.mii;
	unsigned int tex = dr->texture.id;
	mui_drawable_init(dr, C2_PT(MII_VIDEO_WIDTH, MII_VIDEO_HEIGHT),
			32, mii->video.pixels, 0);
	dr->texture.id = tex;
	dr->texture.size = dr->pix.size;
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// disable the repeat of textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	dr->texture.kind = GL_RGBA;// note RGBA here, it's quicker!!
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
			dr->pix.row_bytes / 4,
			dr->texture.size.y, 0, dr->texture.kind,
			GL_UNSIGNED_BYTE, //GL_UNSIGNED_INT_8_8_8_8_REV,
			dr->pix.pixels);
#if 0
	{
		printf("Creating video mesh: %d vertices %d indices\n",
				ui->video_mesh.count, ui->video_indices.count);
		glGenVertexArrays(1, &ui->video_mesh.vao);
		glBindVertexArray(ui->video_mesh.vao);

		glGenBuffers(1, &ui->video_mesh.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, ui->video_mesh.vbo);
		glBufferData(GL_ARRAY_BUFFER,
					sizeof(ui->video_mesh.e[0]) * ui->video_mesh.count,
					ui->video_mesh.e, GL_STATIC_DRAW);

		glGenBuffers(1, &ui->video_indices.ebo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->video_indices.ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
					sizeof(ui->video_indices.e[0]) * ui->video_indices.count,
					ui->video_indices.e, GL_STATIC_DRAW);

		// describe what's in that buffer
		// Vertices
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
						4 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);
		// Texture coordinates
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
						4 * sizeof(float), (void*)(2 * sizeof(float)));
		glEnableVertexAttribArray(1);

		glBindVertexArray(0);
	}
#endif
	mui_mui_gl_regenerate_ui_texture(ui);

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
			mii_generate_floppy_mesh(&ui->floppy[fi].vtx, 1.0);
		}
	} else {
		printf("%s No floppy found\n", __func__);
		for (int fi = 0; fi < 2; fi++) {
			ui->floppy[fi].floppy = NULL;
			mui_drawable_clear(&ui->pixels.floppy[fi].bits);
			mui_drawable_clear(&ui->pixels.floppy[fi].hm_read);
			mui_drawable_clear(&ui->pixels.floppy[fi].hm_write);
			mii_generate_floppy_mesh(&ui->floppy[fi].vtx, 1.0);
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
						GL_UNSIGNED_BYTE, //GL_UNSIGNED_INT_8_8_8_8_REV,
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
	//	glPixelStorei(GL_UNPACK_ROW_LENGTH, dr->pix.row_bytes / 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
				dr->pix.size.x, dr->pix.size.y,
				dr->texture.kind,
				GL_UNSIGNED_BYTE, //GL_UNSIGNED_INT_8_8_8_8_REV,
				dr->pix.pixels);
	//	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
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
			mii_generate_floppy_mesh(&ui->floppy[fi].vtx,
							ui->floppy[fi].max_width);
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

/*
 * small replacement for glBegin/glEnd with a static array buffer. Use default
 * texture coordinates
 */
static void
glRect(
		c2_rect_p r)
{
	const float vtx[] = {
		r->l, r->t, 0, 0,
		r->r, r->t, 1, 0,
		r->r, r->b, 1, 1,
		r->l, r->b, 0, 1,
	};
	const uint32_t idx[] = {0, 1, 2, 0, 2, 3};
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), &vtx[0]);
	glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), &vtx[2]);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
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
	glLoadIdentity();
	glOrtho(0.0f, ui->window_size.x, ui->window_size.y,
				0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	/* draw mii texture */
	glColor3f(1.0f, 1.0f, 1.0f);
	mui_drawable_t * dr = &ui->pixels.mii;
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, dr->texture.id);
	if (ui->video_mesh.count == 0) {
		c2_rect_t r = ui->video_frame;
		glRect(&r);
	} else {
		c2_rect_t r = ui->video_frame;
		glDisable(GL_TEXTURE_2D);
		glColor4f(0.0f, 0.0f, 0.0f, 1.0);
		/* replacement for glBegin/end with a small static vertex buffer,
		   and a draw */
		glRect(&r);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0);
		glEnable(GL_TEXTURE_2D);
#if 0
		glPushMatrix();
		glTranslatef(r.l, r.t, 0);
		glScalef((float)c2_rect_width(&r) / (float)MII_VIDEO_WIDTH,
				(float)c2_rect_height(&r) / (float)MII_VIDEO_HEIGHT, 1);
		glBindVertexArray(ui->video_mesh.vao);
		glDrawElements(GL_TRIANGLES, ui->video_indices.count,
						GL_UNSIGNED_INT, 0);
		glBindVertexArray(0);
		glPopMatrix();
#else
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), &ui->video_mesh.e[0].x);
		glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), &ui->video_mesh.e[0].u);
		glPushMatrix();
		glTranslatef(r.l, r.t, 0);
		glScalef((float)c2_rect_width(&r) / (float)MII_VIDEO_WIDTH,
				(float)c2_rect_height(&r) / (float)MII_VIDEO_HEIGHT, 1);
		glDrawElements(GL_TRIANGLES, ui->video_indices.count,
						GL_UNSIGNED_INT, ui->video_indices.e);
		glPopMatrix();
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
	}
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
		c2_rect_t r = ui->video_frame;
		c2_rect_offet(&r, -10, 0);
		r.r = r.l + 10;
		glRect(&r);
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
				glVertexPointer(2, GL_FLOAT,
							4 * sizeof(float), &ui->floppy[i].vtx.e[0].x);
				int element_count = ui->floppy[i].vtx.count ;
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
			glVertexPointer(2, GL_FLOAT,
							4 * sizeof(float), &ui->floppy[i].vtx.e[0].x);
			glTexCoordPointer(2, GL_FLOAT,
							4 * sizeof(float), &ui->floppy[i].vtx.e[0].u);
			int element_count = ui->floppy[i].vtx.count;
			glDrawArrays(GL_TRIANGLES, 0, element_count);
			// draw heatmap and head with full opacity
			// otherwise we get wierd artifacts
			if (f->heat) {
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
				dr = &ui->pixels.floppy[i].hm_read;
				glColor4f(0.0f, 1.0f, 0.0f, 1.0);
				glBindTexture(GL_TEXTURE_2D, dr->texture.id);
				glDrawArrays(GL_TRIANGLES, 0, element_count);
				dr = &ui->pixels.floppy[i].hm_write;
				glColor4f(1.0f, 0.0f, 0.0f, 1.0);
				glBindTexture(GL_TEXTURE_2D, dr->texture.id);
				glDrawArrays(GL_TRIANGLES, 0, element_count);
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
	glPopAttrib();
}
