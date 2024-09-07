/*
 * mish_client_input.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mish_priv.h"
#include "mish_priv_cmd.h"
#include "mish.h"

/*
 * Don't assume this handles every key combination in the world, it doesn't,
 * it handles mostly the one I use most from bash!
 * You like VI syntax, saaaaad story :-)
 */
//! Parse current input buffer
int
_mish_client_vt_parse_input(
		struct mish_t *m,
		struct mish_input_t *in,
		uint8_t ich)
{
	mish_client_p c = in->refcon;

	/*
	 * if the sequence buffer is currently being flushed, we need to store
	 * the input until it's done.
	 * So lets store it into the input buffer, and once that output is finished
	 * we can process it as before.
	 */
	if (c->output.sqb->done)
		return MISH_IN_STORE;
	// now flush what was stored, plus the one we just received
	for (int i = 0; i <= in->line->done; i++) {
		uint8_t ch = i == in->line->done ? ich : in->line->line[i];

		if (in->is_telnet) {
			if (_mish_telnet_parse(c, ch))
				continue;
		}
		if (!_mish_vt_sequence_char(&c->vts, ch))
			continue;
		// if no command is editing, create an empty one, add it to history
		if (!c->cmd) {
			_mish_line_reserve(&c->cmd, 4);
			TAILQ_INSERT_TAIL(&in->backlog, c->cmd, self);
		} else {
			/*
			 * we need to detach the element if we're going to resize it,
			 * otherwise the list is completely fubar as the pointer changes
			 */
			if (c->cmd->size - c->cmd->len <= 4) {
				mish_line_p next = TAILQ_NEXT(c->cmd, self);
				TAILQ_REMOVE(&in->backlog, c->cmd, self);
				_mish_line_reserve(&c->cmd, 4);
				if (next)
					TAILQ_INSERT_BEFORE(next, c->cmd, self);
				else
					TAILQ_INSERT_TAIL(&in->backlog, c->cmd, self);
			}
		}
		switch (c->vts.seq) {
			case MISH_VT_SEQ(CSI, '~'): {
				mish_line_p cursor = c->bottom;
				if (c->vts.p[0] == 1)	// GNU screen HOME seq
					goto kb_home;
				else if (c->vts.p[0] == 4)	// GNU screen END seq
					goto kb_end;
				if (c->vts.p[0] == 5) { // Page UP
					for (int i = 0; i < c->window_size.h - 3 && cursor; i++)
						cursor = TAILQ_PREV(cursor, mish_line_queue_t, self);
					if (cursor) {
						c->bottom = cursor;
						c->flags |= MISH_CLIENT_UPDATE_WINDOW;
						c->flags &= ~MISH_CLIENT_SCROLLING;
					}
				} else if (c->vts.p[0] == 6) {	// down
					for (int i = 0; i < c->window_size.h - 3 && cursor; i++)
						cursor = TAILQ_NEXT(cursor, self);
					c->bottom = cursor;
					c->flags |= MISH_CLIENT_UPDATE_WINDOW;
					if (!c->bottom)
						c->flags |= MISH_CLIENT_SCROLLING;
				}
			}	break;
			case MISH_VT_SEQ(CSI, 'H'): {	// Home
kb_home:
				// don't bother if there's not enough backlog
				if (m->backlog.size < c->window_size.h - 2)
					break;
				c->bottom = TAILQ_FIRST(&m->backlog.log);
				for (i = 0; i < c->window_size.h - 2 - 1 && c->bottom; i++)
					c->bottom = TAILQ_NEXT(c->bottom, self);
				c->flags |= MISH_CLIENT_UPDATE_WINDOW;
				if (c->bottom)
					c->flags &= ~MISH_CLIENT_SCROLLING;
			}	break;
			case MISH_VT_SEQ(CSI, 'F'): {	// END
kb_end:
				c->flags |= MISH_CLIENT_UPDATE_WINDOW | MISH_CLIENT_SCROLLING;
				c->bottom = NULL;
			}	break;
			case MISH_VT_SEQ(CSI, 'R'):
				c->flags |= MISH_CLIENT_HAS_CURSOR_POS;
				c->cursor_pos.y = c->vts.p[0];
				c->cursor_pos.x = c->vts.p[1];
				break;
			case MISH_VT_SEQ(RAW, 16): {		// CTRL-P	Prev history
				if (TAILQ_FIRST(&in->backlog) != c->cmd) {
					c->flags |= MISH_CLIENT_UPDATE_PROMPT;
					c->cmd = TAILQ_PREV(c->cmd, mish_line_queue_t, self);
				}
			}	break;
			case MISH_VT_SEQ(RAW, 14): 		// CTRL-N 	Next history
				if (TAILQ_LAST(&in->backlog, mish_line_queue_t) != c->cmd) {
					c->flags |= MISH_CLIENT_UPDATE_PROMPT;
					c->cmd = TAILQ_NEXT(c->cmd, self);
				}
				break;
			case MISH_VT_SEQ(RAW, 1): 		// CTRL-A	Start of line
				if (c->cmd->done) {
					_mish_send_queue_fmt(c, "\033[%dD", c->cmd->done);
					c->cmd->done = 0;
				}
				break;
			case MISH_VT_SEQ(RAW, 5): 		// CTRL-E	End of Line
				if (c->cmd->done < c->cmd->len) {
					_mish_send_queue_fmt(c, "\033[%dC",
							c->cmd->len - c->cmd->done);
					c->cmd->done = c->cmd->len;
				}
				break;
			case MISH_VT_SEQ(RAW, 2): 		// CTRL-B	Prev char
				if (c->cmd->done) {
					c->cmd->done--;
					_mish_send_queue_fmt(c, "\033[%dD", 1);
				}
				break;
			case MISH_VT_SEQ(RAW, 6): 		// CTRL-F	Next Char
				if (c->cmd->done < c->cmd->len) {
					c->cmd->done++;
					_mish_send_queue_fmt(c, "\033[%dC", 1);
				}
				break;
			case MISH_VT_SEQ(RAW, 23): {		// CTRL-W Delete prev word
				int old_pos = c->cmd->done;
				while (c->cmd->done && c->cmd->line[c->cmd->done-1] == ' ')
					c->cmd->done--;
				while (c->cmd->done && c->cmd->line[c->cmd->done-1] != ' ')
					c->cmd->done--;

				if (old_pos - c->cmd->done) {
					int del = old_pos - c->cmd->done;
					memmove(c->cmd->line + c->cmd->done,
							c->cmd->line + old_pos,
							c->cmd->len - old_pos + 1);
					c->cmd->len -= del;
					// move back del characters, and delete them
					_mish_send_queue_fmt(c, "\033[%dD\033[%dP", del, del);
				}
			}	break;
			case MISH_VT_SEQ(RAW, 0x7f): 	// DEL
			case MISH_VT_SEQ(RAW, 8): 		// CTRL-H
				if (c->cmd->done) {
					c->cmd->done--;
					memmove(c->cmd->line + c->cmd->done,
							c->cmd->line + c->cmd->done + 1,
							c->cmd->len - c->cmd->done + 1);
					c->cmd->len--;
					// backspace plus Delete (1) Character
					_mish_send_queue(c, "\x8\033[P");
				}
				break;
			case MISH_VT_SEQ(RAW, 11): 		// CTRL-K	Kill rest of line
				c->cmd->len = c->cmd->done;
				c->cmd->line[c->cmd->len] = 0;
				_mish_send_queue(c, "\033[K");
				break;
			case MISH_VT_SEQ(RAW, 12): 		// CTRL-L	Redraw
				c->flags |= MISH_CLIENT_UPDATE_WINDOW;
				break;
			case MISH_VT_SEQ(RAW, 13): 		// CTRL-M aka return
				c->cmd->line[c->cmd->len] = 0;
				if (0) {	// debug
					fprintf(stdout, "CMD: '%.*s", c->cmd->done, c->cmd->line);
					fprintf(stdout, "\033[7m%.*s\033[0m", 1,
							c->cmd->line + c->cmd->done);
					if (c->cmd->done < c->cmd->len)
						fprintf(stdout, "%s", c->cmd->line + c->cmd->done+1);
					fprintf(stdout, "'\n");
				}
				// if we have a non-safe command, we need to signal the
				// cmd execution thread, so mark the client as having a command
				if (mish_cmd_call(c->cmd->line, c) == 1)
					c->flags |= MISH_CLIENT_HAS_CMD;
				c->cmd = NULL;	// new one
				{	// reuse the last empty one
					mish_line_p last = TAILQ_LAST(&in->backlog, mish_line_queue_t);
					if (last && last->len == 0)
						c->cmd = last;
				}
				c->flags |= MISH_CLIENT_UPDATE_PROMPT;
				break;
			default:
				if (c->vts.seq & ~0xff) {
					printf(MISH_COLOR_RED
							"mish: Unknown sequence: %08x ", c->vts.seq);
					for (int i = 0; i < c->vts.pc; i++)
						printf(":%d", c->vts.p[i]);
					printf("'%c%c'", c->vts.seq >> 8, c->vts.seq & 0xff);
					printf(MISH_COLOR_RESET "\n");
				}
				break;
		}
		if (c->vts.glyph && c->vts.glyph >= ' ' && c->vts.glyph < 0x7f) {
			memmove(c->cmd->line + c->cmd->done + 1,
					c->cmd->line + c->cmd->done,
					c->cmd->len - c->cmd->done);
			c->cmd->line[c->cmd->done] = c->vts.glyph;
			c->cmd->done++;
			c->cmd->len++;
			c->cmd->line[c->cmd->len] = 0;
			// no need to explicitly insert, terminal should already be setup
			_mish_send_queue_fmt(c, "%lc", c->vts.glyph);
		}
	}
	// remove any buffered input.
	in->line->len = 0;
	return MISH_IN_SKIP;
}
