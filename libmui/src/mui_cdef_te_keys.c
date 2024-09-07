/*
 * mui_cdef_te_keys.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include "mui.h"
#include "mui_cdef_te_priv.h"


/*
	██   ██ ███████ ██    ██ ██████   ██████   █████  ██████  ██████
	██  ██  ██       ██  ██  ██   ██ ██    ██ ██   ██ ██   ██ ██   ██
	█████   █████     ████   ██████  ██    ██ ███████ ██████  ██   ██
	██  ██  ██         ██    ██   ██ ██    ██ ██   ██ ██   ██ ██   ██
	██   ██ ███████    ██    ██████   ██████  ██   ██ ██   ██ ██████
*/
bool
_mui_textedit_key(
		struct mui_control_t * 	c,
		mui_event_t * 			ev)
{
	mui_textedit_control_t *te = (mui_textedit_control_t *)c;

	mui_glyph_line_array_t * me = &te->measure;
	if (ev->modifiers & MUI_MODIFIER_CTRL) {
		switch (ev->key.key) {
			case 'T': {
				te->trace = !te->trace;
				printf("TRACE %s\n", te->trace ? "ON" : "OFF");
			}	break;
			case 'D': {// dump text status and measures lines
				printf("Text:\n'%s'\n", te->text.e);
				printf("Text count: %d\n", te->text.count);
				printf("Text measure: %d\n", me->count);
				for (uint i = 0; i < me->count; i++) {
					mui_glyph_array_t * line = &me->e[i];
					printf("  line %d: %d\n", i, line->count);
					for (uint j = 0; j < line->count; j++) {
						mui_glyph_t * g = &line->e[j];
						printf("    %3d: %04x:%c x:%3f w:%3d\n",
								j, te->text.e[g->pos],
								te->text.e[g->pos] < ' ' ?
										'.' : te->text.e[g->pos],
								g->x, g->w);
					}
				}
				te->flags |= MUI_TEXT_DEBUG;
			}	break;
			case 'a': {
				printf("Select all: %d\n", te->text.count-1);
				_mui_textedit_select_signed(te, 0, te->text.count-1);
			}	break;
			case 'c': {
				if (te->sel.start != te->sel.end) {
					uint32_t start = _mui_glyph_to_byte_offset(me, te->sel.start);
					uint32_t end = _mui_glyph_to_byte_offset(me, te->sel.end);
					mui_clipboard_set(c->win->ui,
								te->text.e + start, end - start);
				}
			}	break;
			case 'x': {
				if (te->sel.start != te->sel.end) {
					uint32_t start = _mui_glyph_to_byte_offset(me, te->sel.start);
					uint32_t end = _mui_glyph_to_byte_offset(me, te->sel.end);
					mui_clipboard_set(c->win->ui,
								te->text.e + start, end - start);
					_mui_textedit_sel_delete(te, true, true);
				}
			}	break;
			case 'v': {
				uint32_t len;
				const uint8_t * clip = mui_clipboard_get(c->win->ui, &len);
				if (clip) {
					if (te->sel.start != te->sel.end)
						_mui_textedit_sel_delete(te, true, true);
					mui_utf8_insert(&te->text,
							_mui_glyph_to_byte_offset(me, te->sel.start),
							clip, len);
					_mui_textedit_refresh_measure(te);
					_mui_textedit_select_signed(te,
							te->sel.start + len, te->sel.start + len);
				}
			}	break;
		}
		return true;
	}
	switch (ev->key.key) {
		case MUI_KEY_UP: {
			uint line, index;
			_mui_glyph_to_line_index(me, te->sel.start, &line, &index);
			if (line > 0) {
				uint pos = _mui_line_index_to_glyph(me, line-1, index);
				if (ev->modifiers & MUI_MODIFIER_SHIFT) {
					_mui_textedit_select_signed(te, te->sel.start, pos);
				} else {
					_mui_textedit_select_signed(te, pos, pos);
				}
			}
		}	break;
		case MUI_KEY_DOWN: {
			uint line, index;
			_mui_glyph_to_line_index(me, te->sel.start, &line, &index);
			if (line < me->count-1) {
				uint pos = _mui_line_index_to_glyph(me, line+1, index);
				if (ev->modifiers & MUI_MODIFIER_SHIFT) {
					_mui_textedit_select_signed(te, te->sel.start, pos);
				} else {
					_mui_textedit_select_signed(te, pos, pos);
				}
			}
		}	break;
		case MUI_KEY_LEFT: {
			if (ev->modifiers & MUI_MODIFIER_SHIFT) {
				_mui_textedit_select_signed(te, te->sel.start - 1, te->sel.end);
			} else {
				if (te->sel.start == te->sel.end)
					_mui_textedit_select_signed(te, te->sel.start - 1, te->sel.start - 1);
				else
					_mui_textedit_select_signed(te, te->sel.start, te->sel.start);
			}
		}	break;
		case MUI_KEY_RIGHT: {
			if (ev->modifiers & MUI_MODIFIER_SHIFT) {
				_mui_textedit_select_signed(te, te->sel.start, te->sel.end + 1);
			} else {
				if (te->sel.start == te->sel.end)
					_mui_textedit_select_signed(te, te->sel.start + 1, te->sel.start + 1);
				else
					_mui_textedit_select_signed(te, te->sel.end, te->sel.end);
			}
		}	break;
		case MUI_KEY_BACKSPACE: {
			if (te->sel.start == te->sel.end) {
				if (te->sel.start > 0) {
					mui_utf8_delete(&te->text,
								_mui_glyph_to_byte_offset(me, te->sel.start - 1),
								1);
					_mui_textedit_refresh_measure(te);
					_mui_textedit_select_signed(te, te->sel.start - 1, te->sel.start - 1);
				}
			} else {
				_mui_textedit_sel_delete(te, true, true);
			}
		}	break;
		case MUI_KEY_DELETE: {
			if (te->sel.start == te->sel.end) {
				if (te->sel.start < te->text.count-1) {
					mui_utf8_delete(&te->text,
							_mui_glyph_to_byte_offset(me, te->sel.start), 1);
					_mui_textedit_refresh_measure(te);
					_mui_textedit_select_signed(te, te->sel.start, te->sel.start);
				}
			} else {
				_mui_textedit_sel_delete(te, true, true);
			}
		}	break;
		case MUI_KEY_TAB: {
			mui_control_switch_focus(c->win,
					ev->modifiers & MUI_MODIFIER_SHIFT ? -1 : 0);
		}	break;
		default:
			printf("%s key 0x%x\n", __func__, ev->key.key);
			if (ev->key.key == 13 && !(te->flags & MUI_CONTROL_TEXTEDIT_VERTICAL))
				return false;
			if (ev->key.key == 13 ||
						(ev->key.key >= 32 && ev->key.key < 127)) {
				if (te->sel.start != te->sel.end) {
					_mui_textedit_sel_delete(te, false, false);
					_mui_textedit_select_signed(te, te->sel.start, te->sel.start);
				}
				uint8_t k = ev->key.key;
				mui_utf8_insert(&te->text,
							_mui_glyph_to_byte_offset(me, te->sel.start), &k, 1);
				_mui_textedit_refresh_measure(te);
				_mui_textedit_select_signed(te,
							te->sel.start + 1, te->sel.start + 1);
			}
			break;
	}
	_mui_textedit_show_carret(te);
	return true;
}
