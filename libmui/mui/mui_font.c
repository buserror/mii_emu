/*
 * mui_font.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_TTC_IMPLEMENTATION
#include "stb_ttc.h"


#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX mui_
#include "incbin.h"

INCBIN(main_font, "fonts/Charcoal_mui.ttf");
INCBIN(icon_font, "fonts/typicon.ttf");
INCBIN(dingbat_font, "fonts/Dingbat.ttf");

#include "mui.h"

mui_font_t *
mui_font_find(
	mui_t *ui,
	const char *name)
{
	mui_font_t *f;
	TAILQ_FOREACH(f, &ui->fonts, self) {
		if (!strcmp(f->name, name))
			return f;
	}
	return NULL;
}

static void
_mui_font_pixman_prep(
	mui_font_t *f)
{
	f->font.pix.bpp = 8;
	f->font.pix.size.x = f->ttc.p_stride;
	f->font.pix.size.y = f->ttc.p_height;
	f->font.pix.row_bytes = f->ttc.p_stride;
	f->font.pix.pixels = f->ttc.pixels;
}

mui_font_t *
mui_font_from_mem(
		mui_t *ui,
		const char *name,
		unsigned int size,
		const void *font_data,
		unsigned int font_size )
{
	mui_font_t *f = calloc(1, sizeof(*f));
	f->name = strdup(name);
	f->size = size;
	stb_ttc_LoadFont(&f->ttc, font_data, font_size);
	TAILQ_INSERT_TAIL(&ui->fonts, f, self);
	printf("%s: Loaded font %s:%d\n", __func__, name, size);

	return f;
}

void
mui_font_init(
		mui_t *ui)
{
	printf("%s: Loading fonts\n", __func__);
	mui_font_from_mem(ui, "main", 28,
			mui_main_font_data, mui_main_font_size);
	mui_font_from_mem(ui, "icon_large", 96,
			mui_icon_font_data, mui_icon_font_size);
	mui_font_from_mem(ui, "icon_small", 30,
			mui_icon_font_data, mui_icon_font_size);
}

void
mui_font_dispose(
		mui_t *ui)
{
	mui_font_t *f;
	while ((f = TAILQ_FIRST(&ui->fonts))) {
		TAILQ_REMOVE(&ui->fonts, f, self);
		stb_ttc_Free(&f->ttc);
		free(f->name);
		free(f);
	}
}

int
mui_font_text_measure(
		mui_font_t *font,
		const char *text,
		stb_ttc_measure *m )
{
	struct stb_ttc_info * ttc = &font->ttc;
	float scale = stbtt_ScaleForPixelHeight(&ttc->font, font->size);
	int w = stb_ttc_MeasureText(ttc, scale, text, m);
	return w;
}


void
mui_font_text_draw(
		mui_font_t *font,
		mui_drawable_t *dr,
		c2_pt_t where,
		const char *text,
		unsigned int text_len,
		mui_color_t color)
{
	struct stb_ttc_info * ttc = &font->ttc;
	unsigned int state = 0;
	float scale = stbtt_ScaleForPixelHeight(&ttc->font, font->size);
	double xpos = 0;
	unsigned int last = 0;
	unsigned int cp = 0;

	if (!text_len)
		text_len = strlen(text);
	mui_drawable_t * src = &font->font;
	mui_drawable_t * dst = dr;

	pixman_color_t pc = PIXMAN_COLOR(color);
	pixman_image_t * fill = pixman_image_create_solid_fill(&pc);

	where.y += font->ttc.ascent * scale;
	for (unsigned int ch = 0; text[ch] && ch < text_len; ch++) {
		if (stb_ttc__UTF8_Decode(&state, &cp, text[ch]) != UTF8_ACCEPT)
			continue;
		if (last) {
			int kern = scale * stb_ttc__CodepointsGetKerning(ttc, last, cp);
			xpos += kern;
		}
		last = cp;
		int gl = stb_ttc__CodepointGetGlyph(ttc, cp);
		if (gl == -1)
			continue;
		stb_ttc_g *gc = stb_ttc__ScaledGlyphGetCache(ttc, gl, scale);
		if (!gc)
			continue;
		if (gc->p_y == (unsigned short) -1)
			stb_ttc__ScaledGlyphRenderToCache(ttc, gc);
//		int pxpos = gc->x0 + ((xpos + gc->lsb) * scale);
		int pxpos = where.x + gc->x0 + ((xpos + 0) * scale);
	//	if (gc->lsb)
	//		printf("glyph %3d : %04x:%c lsb %d\n", ch, cp, cp < 32 ? '.' : cp, gc->lsb);

		int ph = gc->y1 - gc->y0;
		int pw = gc->x1 - gc->x0;
		_mui_font_pixman_prep(font);
		pixman_image_composite32(
				PIXMAN_OP_OVER,
				fill,
				mui_drawable_get_pixman(src),
				mui_drawable_get_pixman(dst),
				0, 0, gc->p_x, gc->p_y,
				pxpos, where.y + gc->y0, pw, ph);
		xpos += gc->advance;
	}
	pixman_image_unref(fill);
}

IMPLEMENT_C_ARRAY(mui_glyph_array);
IMPLEMENT_C_ARRAY(mui_glyph_line_array);

void
mui_font_measure(
		mui_font_t *font,
		c2_rect_t bbox,
		const char *text,
		unsigned int text_len,
		mui_glyph_line_array_t *lines,
		uint16_t flags)
{
	struct stb_ttc_info * ttc = &font->ttc;
	unsigned int state = 0;
	float scale = stbtt_ScaleForPixelHeight(&ttc->font, font->size);
	unsigned int last = 0;
	unsigned int cp = 0;

	if (!text_len)
		text_len = strlen(text);

	c2_pt_t where = {};
	unsigned int ch = 0;
	int wrap_chi = 0;
	int wrap_w = 0;
	int wrap_count = 0;
	do {
		where.y += font->ttc.ascent * scale;
		const mui_glyph_array_t zero = {};
		mui_glyph_line_array_push(lines, zero);
		mui_glyph_array_t * line = &lines->e[lines->count - 1];
		line->x = 0;
		line->y = where.y;
		line->w = 0;
		wrap_chi = ch;
		wrap_w = 0;
		wrap_count = 0;
		for (;text[ch]; ch++) {
			if (stb_ttc__UTF8_Decode(&state, &cp, text[ch]) != UTF8_ACCEPT)
				continue;
			if (last) {
				int kern = scale * stb_ttc__CodepointsGetKerning(ttc, last, cp);
				line->w += kern;
			}
			last = cp;
//			printf("glyph %3d : %04x:%c\n", ch, cp, cp < 32 ? '.' : cp);
			if (cp == '\n') {
				ch++;
				break;
			}
			if (isspace(cp) || ispunct(cp)) {
				wrap_chi 	= ch;
				wrap_w 		= line->w;
				wrap_count 	= line->count;
			}
			int gl = stb_ttc__CodepointGetGlyph(ttc, cp);
			if (gl == -1)
				continue;
			stb_ttc_g *gc = stb_ttc__ScaledGlyphGetCache(ttc, gl, scale);
			if (!gc)
				continue;
			if (gc->p_y == (unsigned short) -1)
				stb_ttc__ScaledGlyphRenderToCache(ttc, gc);
			if (((line->w + gc->advance) * scale) > c2_rect_width(&bbox)) {
				if (wrap_count) {
					ch = wrap_chi + 1;
					line->count = wrap_count;
					line->w = wrap_w;
				}
				break;
			}
			line->w += gc->advance;
			mui_glyph_array_push(line, gc);
		};
	} while (text[ch] && ch < text_len);
	int bh = 0;
	for (int i = 0; i < (int)lines->count; i++) {
		mui_glyph_array_t * line = &lines->e[i];
		bh = line->y - (font->ttc.descent * scale);
		line->w *= scale;
//		printf("  line %d y %3d size %d width %d\n", i,
//				line->y, line->count, line->w);
	}
//	printf("box height is %d/%d\n", bh, c2_rect_height(&bbox));
	int ydiff = 0;
	if (flags & MUI_TEXT_ALIGN_MIDDLE) {
		ydiff = (c2_rect_height(&bbox) - bh) / 2;
	} else if (flags & MUI_TEXT_ALIGN_BOTTOM) {
		ydiff = c2_rect_height(&bbox) - bh;
	}
	for (int i = 0; i < (int)lines->count; i++) {
		mui_glyph_array_t * line = &lines->e[i];
		line->y += ydiff;
		if (flags & MUI_TEXT_ALIGN_RIGHT) {
			line->x = c2_rect_width(&bbox) - line->w;
		} else if (flags & MUI_TEXT_ALIGN_CENTER) {
			line->x = (c2_rect_width(&bbox) - line->w) / 2;
		}
	}
}

void
mui_font_measure_clear(
		mui_glyph_line_array_t *lines)
{
	if (!lines)
		return;
	for (int i = 0; i < (int)lines->count; i++) {
		mui_glyph_array_t * line = &lines->e[i];
		mui_glyph_array_free(line);
	}
	mui_glyph_line_array_free(lines);
}


void
mui_font_measure_draw(
		mui_font_t *font,
		mui_drawable_t *dr,
		c2_rect_t bbox,
		mui_glyph_line_array_t *lines,
		mui_color_t color,
		uint16_t flags)
{
	pixman_color_t pc = PIXMAN_COLOR(color);
	pixman_image_t * fill = pixman_image_create_solid_fill(&pc);
	struct stb_ttc_info * ttc = &font->ttc;
	float scale = stbtt_ScaleForPixelHeight(&ttc->font, font->size);

	mui_drawable_t * src = &font->font;
	mui_drawable_t * dst = dr;

	// all glyphs we need were loaded, update the pixman texture
	_mui_font_pixman_prep(font);

	for (int li = 0; li < (int)lines->count; li++) {
		mui_glyph_array_t * line = &lines->e[li];
		int xpos = 0;//where.x / scale;
		for (int ci = 0; ci < (int)line->count; ci++) {
			stb_ttc_g *gc = line->e[ci];
//			int pxpos = gc->x0 + ((xpos + gc->lsb) * scale);
			int pxpos = gc->x0 + ((xpos + 0) * scale);

			int ph = gc->y1 - gc->y0;
			int pw = gc->x1 - gc->x0;
			pixman_image_composite32(
					PIXMAN_OP_OVER,
					fill,
					mui_drawable_get_pixman(src),
					mui_drawable_get_pixman(dst),
					0, 0, gc->p_x, gc->p_y,
					bbox.l + line->x + pxpos,
					bbox.t + line->y + gc->y0, pw, ph);
			xpos += gc->advance;
		}
	}
	pixman_image_unref(fill);
}

void
mui_font_textbox(
		mui_font_t *font,
		mui_drawable_t *dr,
		c2_rect_t bbox,
		const char *text,
		unsigned int text_len,
		mui_color_t color,
		uint16_t flags)
{
	mui_glyph_line_array_t lines = {};

	if (!text_len)
		text_len = strlen(text);

	mui_font_measure(font, bbox, text, text_len, &lines, flags);

	mui_font_measure_draw(font, dr, bbox, &lines, color, flags);

	mui_font_measure_clear(&lines);
}
