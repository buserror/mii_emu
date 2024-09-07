/*
 * mui_text.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <mui/mui_drawable.h>

typedef struct mui_font_t {
	// this MUST be first
	mui_drawable_t			 	font;	// points to ttc pixels!
	char * 						name;	// not filename, internal name, aka 'main'
	uint		 				size;	// in pixels
	TAILQ_ENTRY(mui_font_t) 	self;
	struct stb_ttc_info  		ttc;	// internal; glyph cache/hash etc
} mui_font_t;

/*
 * Font related
 */
void
mui_font_init(
		struct mui_t *	ui);
void
mui_font_dispose(
		struct mui_t *	ui);

mui_font_t *
mui_font_find(
		struct mui_t *	ui,
		const char *	name);
mui_font_t *
mui_font_from_mem(
		struct mui_t *	ui,
		const char *	name,
		uint		 	size,
		const void *	font_data,
		uint		 	font_size );
/*
 * Draw a text string at 'where' in the drawable 'dr' with the
 * given color. This doesn't handle line wrapping, or anything,
 * it just draws the text at the given position.
 * If you want more fancy text drawing, use mui_font_textbox()
 */
void
mui_font_text_draw(
		mui_font_t *	font,
		mui_drawable_t *dr,
		c2_pt_t 		where,
		const char *	text,
		uint		 	text_len,
		mui_color_t 	color);
/*
 * This is a low level function to measure a text string, it returns
 * the width of the string in pixels, and fills the 'm' structure with
 * the position of each glyph in the string. Note that the returned
 * values are all in FIXED POINT format.
 */
int
mui_font_text_measure(
		mui_font_t *	font,
		const char *	text,
		struct stb_ttc_measure *m );

typedef enum mui_text_e {
	// 2 bits for horizontal alignment, 2 bits for vertical alignment
	MUI_TEXT_ALIGN_LEFT 	= 0,
	MUI_TEXT_ALIGN_CENTER	= (1 << 0),
	MUI_TEXT_ALIGN_RIGHT	= (1 << 1),
	MUI_TEXT_ALIGN_TOP		= 0,
	MUI_TEXT_ALIGN_MIDDLE	= (MUI_TEXT_ALIGN_CENTER << 2),
	MUI_TEXT_ALIGN_BOTTOM	= (MUI_TEXT_ALIGN_RIGHT << 2),
	MUI_TEXT_ALIGN_FULL		= (1 << 5),
	MUI_TEXT_ALIGN_COMPACT	= (1 << 6),	// compact line spacing
	MUI_TEXT_DEBUG			= (1 << 7),
	MUI_TEXT_STYLE_BOLD		= (1 << 8),	// Synthetic (ugly) bold
	MUI_TEXT_STYLE_ULINE	= (1 << 9), // Underline
	MUI_TEXT_STYLE_NARROW	= (1 << 10),// Synthetic narrow
	MUI_TEXT_FLAGS_COUNT	= 11,
} mui_text_e;

/*
 * Draw a text string in a bounding box, with the given color. The
 * 'flags' parameter is a combination of MUI_TEXT_ALIGN_* flags.
 * This function will handle line wrapping, and will draw as much text
 * as it can in the given box.
 * The 'text' parameter can be a UTF8 string, and the 'text_len' is
 * the number of bytes in the string (or zero), not the number of
 * glyphs.
 * The 'text' string can contain '\n' to force a line break.
 */
void
mui_font_textbox(
		mui_font_t *	font,
		mui_drawable_t *dr,
		c2_rect_t 		bbox,
		const char *	text,
		uint		 	text_len,
		mui_color_t 	color,
		mui_text_e 		flags );

// this is what is returned by mui_font_measure()
typedef struct mui_glyph_t {
	uint32_t 	pos; 	// position in text, in *bytes*
	uint32_t 	w;		// width of the glyph, in *pixels*
	float		x;		// x position in *pixels*
	uint32_t	index;	// cache index, for internal use, do not change
	uint32_t 	glyph;  // Unicode codepoint
} mui_glyph_t;

DECLARE_C_ARRAY(mui_glyph_t, mui_glyph_array, 8,
		uint line_break : 1;
		int x, y, t, b; float w;);
DECLARE_C_ARRAY(mui_glyph_array_t, mui_glyph_line_array, 8,
		uint margin_left, margin_right,	// minimum x, and max width
		height; );

/*
 * Measure a text string, return the number of lines, and each glyphs
 * position already aligned to the MUI_TEXT_ALIGN_* flags.
 * Note that the 'compact', 'narrow' flags are used here,
 * the 'compact' flag is used to reduce the line spacing, and the
 * 'narrow' flag is used to reduce the advance between glyphs.
 */
void
mui_font_measure(
		mui_font_t *	font,
		c2_rect_t 		bbox,
		const char *	text,
		uint		 	text_len,
		mui_glyph_line_array_t *lines,
		mui_text_e 		flags);
/*
 * to be used exclusively with mui_font_measure.
 * Draw the lines and glyphs returned by mui_font_measure, with the
 * given color and flags.
 * The significant flags here are no longer the text aligment, but
 * how to render them:
 * + MUI_TEXT_STYLE_BOLD will draw each glyphs twice, offset by 1 pixel
 * + MUI_TEXT_STYLE_ULINE will draw a line under the text glyphs, unless
 *   they have a descent that is lower than the underline.
 */
void
mui_font_measure_draw(
		mui_font_t *	font,
		mui_drawable_t *dr,
		c2_rect_t 		bbox,
		mui_glyph_line_array_t *lines,
		mui_color_t 	color,
		mui_text_e 		flags);
// clear all the lines, and glyph lists. Use it after mui_font_measure
void
mui_font_measure_clear(
		mui_glyph_line_array_t *lines);
