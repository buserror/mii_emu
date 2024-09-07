/*
 * mui_cdef_te_priv.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "mui.h"


/*
 * This describes a text edit action, either we insert some text at some position,
 * or we delete some text at some position.
 * These actions are queued in a TAILQ, so we can undo/redo them.
 * The text is UTF8, and the position is a BYTE index in the text (not a glyph).
 *
 * We preallocate a fixed number of actions, and when we reach the limit, we
 * start reusing the oldest ones. This limits the number of undo/redo actions
 * to something sensible.
 */
typedef struct mui_te_action_t {
	TAILQ_ENTRY(mui_te_action_t) self;
	uint  		insert : 1;			// if not insert, its a delete
	uint32_t  	position, length;
	mui_utf8_t 	text;
} mui_te_action_t;

// action queue
typedef TAILQ_HEAD(mui_te_action_queue_t, mui_te_action_t) mui_te_action_queue_t;

/*
 * This describes the selection in the text-edit, it can either be a carret,
 * or a selection of text. The selection is kept as a start and end glyph index,
 * and the drawing code calculates the rectangles for the selection.
 */
typedef struct mui_sel_t {
	uint carret: 1;		// carret is visible (if sel.start == end)
	uint start, end;	// glyph index in text
	// rectangles for the first partial line, the body,
	// and the last partial line. All of them can be empty
	union {
		struct {
			c2_rect_t first, body, last;
		};
		c2_rect_t e[3];
	};
} mui_sel_t;

typedef struct mui_textedit_control_t {
	mui_control_t 		control;
	uint				trace : 1;	// debug trace
	uint32_t			flags;		// display flags
	mui_sel_t			sel;
	mui_font_t *		font;
	mui_utf8_t 			text;
	mui_glyph_line_array_t  measure;
	c2_pt_t				margin;
	c2_rect_t 			text_content;
	struct {
		uint 				start, end;
	}					click;
	uint 				selecting_mode;
} mui_textedit_control_t;


bool
_mui_textedit_key(
		struct mui_control_t * 	c,
		mui_event_t * 			ev);

/* this 'forces' the carret to be visible, used when typing */
void
_mui_textedit_show_carret(
		mui_textedit_control_t *	te);
/* this one allows passing -1 etc, which is handy of cursor movement */
void
_mui_textedit_select_signed(
		mui_textedit_control_t *	te,
		int 						glyph_start,
		int 						glyph_end);
/* Refresh the whole selection (or around the carret selection) */
void
_mui_textedit_refresh_sel(
		mui_textedit_control_t *	te,
		mui_sel_t * 				sel);
uint
_mui_glyph_to_byte_offset(
		mui_glyph_line_array_t * 	measure,
		uint 						glyph_pos);
/* Return the glyph position in the text for line number and index in line */
uint
_mui_line_index_to_glyph(
		mui_glyph_line_array_t * 	measure,
		uint 						line,
		uint 						index);

/* Return the line number, and glyph position in line a glyph index */
int
_mui_glyph_to_line_index(
		mui_glyph_line_array_t * 	measure,
		uint  						glyph_pos,
		uint * 						out_line,
		uint * 						out_line_index);

void
_mui_textedit_sel_delete(
		mui_textedit_control_t *	te,
		bool 						re_measure,
		bool 						reset_sel);
int
_mui_make_sel_rects(
		mui_glyph_line_array_t * 	measure,
		mui_font_t *				font,
		mui_sel_t *					sel,
		c2_rect_t 					frame);
/* This scrolls the view following the carret, used when typing.
 * This doesn't check for out of bounds, but the clamping should
 * have made sure the text is always visible. */
void
_mui_textedit_ensure_carret_visible(
		mui_textedit_control_t *	te);
/* this makes sure the text is always visible in the frame */
void
_mui_textedit_clamp_text_frame(
		mui_textedit_control_t *	te);

void
_mui_textedit_refresh_measure(
		mui_textedit_control_t *	te);
/*
 * Rectangles passed here are in TEXT coordinates.
 * which means they are already offset by margin.x, margin.y
 * and the text_content.tl.x, text_content.tl.y
 */
void
_mui_textedit_inval(
		mui_textedit_control_t *	te,
		c2_rect_t 					r);
