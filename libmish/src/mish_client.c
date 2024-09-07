/*
 * mish_client.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "mish_priv.h"
#include "mish.h"
#include "minipt.h"


mish_client_p
mish_client_new(
		mish_p m,
		int in,
		int out,
		int is_tty)
{
	mish_client_p c = (mish_client_p)calloc(1, sizeof(*c));

	_mish_input_init(m, &c->input, in);
	if (is_tty) {
		c->input.process_char = _mish_client_vt_parse_input;
		c->cr.process = _mish_client_interractive_cr;
	} else {
		c->cr.process = _mish_client_dumb_cr;
	}
	c->input.refcon = c;
	c->output.fd = out;
	c->mish = m;	// pointer to parent
	c->footer_height = 2;
	/*
	 * Mark the in & out descriptors (could be the same, if a socket)
	 * as non blocking.
	 */
	int fds[2] = { in, out};
	for (int i = 0; i < 2; i++) {
		int fd = fds[i];
		if (fd >= m->select.max - 1)
			m->select.max = fd +1;
		int flags = fcntl(fd, F_GETFL, NULL);
		if (flags == 1) {
			perror("mish: input F_GETFL");
			flags = 0;
		}
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
			perror("mish: input F_SETFL");
	}

	TAILQ_INSERT_TAIL(&m->clients, c, self);
	return c;
}

void
mish_client_delete(
		mish_p m,
		mish_client_p c)
{
	/*
	 * Turn back synchronous IO here, I don't want to have to deal with EAGAIN
	 * when I'm actually trying to be nice and close as quickly as I can
	 */
	int fds[2] = { c->input.fd, c->output.fd };
	for (int i = 0; i < 2; i++) {
		if (fds[i] == -1)
			continue;
		FD_CLR(fds[i], &m->select.read);
		FD_CLR(fds[i], &m->select.write);
		int flags;
		if (fcntl(fds[i], F_GETFL, &flags) == 0) {
			flags &= ~O_NONBLOCK;
			if (fcntl(fds[i], F_SETFL, &flags))
				/* don't care */;
		}
	}
	const char * restore = "\033[4l\033[;r\033[999;1H";
	if (write(c->output.fd, restore, strlen(restore)))
		;
	close(c->output.fd);

	TAILQ_REMOVE(&m->clients, c, self);
	if (c == m->console)
		m->console = NULL;
	_mish_input_clear(m, &c->input);
	free(c);
}

/*
 * this calculates the number of glyphs in the prompt, it's needed
 * so we know where to position the cursor when displaying the input line.
 * Since prompt can have escape sequences like colors, or UTF8 glyphs
 */
static void
_mish_client_set_prompt(
		mish_client_p c,
		const char *p)
{
	if (p != c->prompt)
		strncpy(c->prompt, p, sizeof(c->prompt)-1);

	mish_vt_sequence_t sq = {};
	char *s = c->prompt;
	c->prompt_gc = 0;
	while (*s) {
		if (_mish_vt_sequence_char(&sq, *s))
			if (sq.glyph)
				c->prompt_gc++;
		s++;
	}
}

/*
 * Remember, NO LOCALS in here -- this is a coroutine with no stack!
 *
 * Interactive 'client' prompt handling
 */
void
_mish_client_interractive_cr(
		mish_p m,
		mish_client_p c)
{
	pt_start(c->cr.state);

	if (c->input.is_telnet)
		mish_telnet_send_init(c);
	/*
	 * move cursor to 999;999 then asks the cursor position. That tells
	 * us the window size! (handy)
	 */
	_mish_send_queue(c,
			"\033[999;999H\033[6n");
	while (_mish_send_flush(m, c))
		pt_yield(c->cr.state);
	/* remember when we started */
	c->output.sqb->stamp = _mish_stamp_ms();
	/* Wait for the input handler to get the right sequence */
	/* can be either the terminal response OR the telnet sequence */
	while (!(c->flags &
			(MISH_CLIENT_HAS_CURSOR_POS | MISH_CLIENT_HAS_WINDOW_SIZE)) &&
				(_mish_stamp_ms() - c->output.sqb->stamp) < (2 * 1000))
		pt_yield(c->cr.state);

	if (c->flags & MISH_CLIENT_HAS_CURSOR_POS) {
		c->window_size.w = c->cursor_pos.x;
		c->window_size.h = c->cursor_pos.y;
		c->flags |= MISH_CLIENT_HAS_WINDOW_SIZE;
	}
	if (!(c->flags & MISH_CLIENT_HAS_WINDOW_SIZE)) {
		printf("mish: no window size, falling back to dumb scrollback\n");
		c->cr.process = _mish_client_dumb_cr;
		goto finish;
	}
	/* We are live scrolling, and we are at the last line of scrollback */
	c->flags |= MISH_CLIENT_INIT_SENT | MISH_CLIENT_SCROLLING;
	c->bottom = TAILQ_LAST(&m->backlog.log, mish_line_queue_t);
	/*
	 * This is where we arrive to draw the entire screen; to start up,
	 * and each time you do a control-l (TODO: or if the window is resized)
	 * Or use the scrollback (page Up/down etc)
	 */
redraw:
	c->flags |= MISH_CLIENT_UPDATE_PROMPT;
	c->sending = c->bottom;
	c->current_vpos = c->window_size.h - c->footer_height;
	/*
	 * walk back from the 'bottom' line we want until we reach the top of the
	 * screen, or ran out of lines.
	 */
	while (c->sending && c->current_vpos >= 1) {
		mish_line_p p = TAILQ_PREV(c->sending, mish_line_queue_t, self);
		if (p) {
			c->sending = p;
			c->current_vpos--;
		} else
			break;
	}
	/*
	 * Now, we've just connected here, and there might be backlog, and we want
	 * just enough of that to fill our screen
	 */
	/* Set the scrolling region to all minus the 2 bottom lines */
	_mish_send_queue_fmt(c, "\033D\033[1;%dr",
			c->window_size.h - c->footer_height);
	_mish_send_queue_fmt(c,
			"\033[%d;1H"
			"\033[J",	// clear to bottom of scrolling area
			c->current_vpos);
	do {
		if (c->flags & MISH_CLIENT_UPDATE_WINDOW) {
			c->flags &= ~MISH_CLIENT_UPDATE_WINDOW;
			goto redraw;
		}
		/*
		 * If there isn't anything to send from the stdout/stderr, do
		 * prompt editing input handling.
		 */
		if (c->flags & MISH_CLIENT_UPDATE_PROMPT) {
			c->flags &= ~MISH_CLIENT_UPDATE_PROMPT;
			sprintf(c->prompt, ">>: ");
			_mish_client_set_prompt(c, c->prompt);

			_mish_send_queue_fmt(c,
				//	"_" // draw a fake cursor and end of scrolling text
					"\033[%d;1H" 	/* reposition the cursor to prompt area */
					"%s" 			// print prompt
					"\033[J"		// Clear to end of screen
					"\033[4h",		// Set insert mode
					c->window_size.h - c->footer_height + 1, c->prompt);
			if (c->cmd && c->cmd->len) {
				_mish_send_queue_fmt(c, "%s", c->cmd->line);
				/* if cursor is inside the line, move it back */
				if (c->cmd->len > c->cmd->done)
					_mish_send_queue_fmt(c, "\033[%dD",
							c->cmd->len - c->cmd->done);
			}
		}
		/*
		 * Now, flush everything we had from the last iteration (inc prompt)
		 * from the scatter gather vector.
		 */
		if (c->output.count) {
			while (_mish_send_flush(m, c))
				pt_yield(c->cr.state);
		} else
			pt_yield(c->cr.state);

		if (!c->sending) {
			/* we're starting up, pool the backlog for a line to display */
			if (!c->bottom) {
				c->bottom = TAILQ_LAST(&m->backlog.log, mish_line_queue_t);
				c->sending = c->bottom;
			} else {
				// we WERE at the bottom, so find a possible next line
				mish_line_p next = TAILQ_NEXT(c->bottom, self);
				if (c->flags & MISH_CLIENT_SCROLLING) {
					if (next) {
						c->bottom = c->sending = next;
					}
				}
			}
		}
		if (!c->sending)	// bah, no new output, loop on
			continue;
		/* Remember you can't yield() until we're out of the while() loop */
		size_t start = c->output.total;
		/*
		 * Now there is something to send, reposition the cursor in the
		 * scrolling area, and start sending backlog lines.
		 */
		_mish_send_queue_fmt(c,
				"\033[s" /* Save cursor position (from the prompt area) */
				"\x8\033[%d;1H" /* pos cursor at bottom of scroll area */
				"\033[8l" /* replace mode */,
				c->current_vpos);
		/*
		 * Send until there's nothing else OR we sent a screen worth
		 * this allow a chance to service the input prompt
		 */
		size_t screen_worth = (c->window_size.h * c->window_size.w) / 1;
		do {
			if (c->sending->err)
				_mish_send_queue(c, MISH_COLOR_RED);
			_mish_send_queue_line(c, c->sending);
			if (c->sending->err)
				_mish_send_queue(c, "\033[m");
			// if we reach the bottom mark, stop
			c->sending = c->sending == c->bottom ?
					NULL : TAILQ_NEXT(c->sending, self);
		} while (c->sending &&
				(c->output.total - start) <= screen_worth);
		// update cursor position here -- SHOULD update it with each lines,
		// to handle word wrapping, but for now it's OK
		// TODO: Update cursor V pos handling line wrap
		if (!c->sending)
			c->current_vpos = c->window_size.h - c->footer_height;
		/* Restore the cursor to the prompt area */
		_mish_send_queue(c, "\033[u");
	} while(1);
finish:
	pt_end(c->cr.state);
}

/*
 * this is the dump pooler; just sends the lines, as is, as they appear.
 * Still try to max up the buffer if possible, but that's about it.
 */
void
_mish_client_dumb_cr(
		mish_p m,
		mish_client_p c)
{
	pt_start(c->cr.state);
	printf(MISH_COLOR_RED "mish: Started dumb console\n" MISH_COLOR_RESET);
	do {
		pt_yield(c->cr.state);

		if (!c->sending) {
			/* we're starting up, pool the backlog for a line to display */
			if (!c->bottom) {
				c->bottom = TAILQ_LAST(&m->backlog.log, mish_line_queue_t);
				c->sending = c->bottom;
			} else {
				// we WERE at the bottom, so find a possible next line
				mish_line_p next = TAILQ_NEXT(c->bottom, self);
				if (next) {
					c->sending = next;
					c->bottom = TAILQ_LAST(&m->backlog.log, mish_line_queue_t);
				}
			}
		}
		if (!c->sending)	// bah, no new output, loop on
			continue;

		do {
			_mish_send_queue_line(c, c->sending);
			// if we reach the bottom mark, stop
			c->sending = c->sending == c->bottom ?
					NULL : TAILQ_NEXT(c->sending, self);
		} while (c->sending);

		while (_mish_send_flush(m, c))
			pt_yield(c->cr.state);
	} while(1);

	pt_end(c->cr.state);
}

static void
_mish_cmd_history(
		void * param,
		int argc,
		const char * argv[])
{
	mish_client_p c = param;
	mish_input_p in = &c->input;
	mish_line_p l;
	int i = 0;
	TAILQ_FOREACH(l, &in->backlog, self) {
		printf("%3d %s\n", i+1, l->line);
		i++;
	}
	printf(MISH_COLOR_GREEN
			"mish: %d history"
			MISH_COLOR_RESET "\n", i);
}

MISH_CMD_NAMES(history, "history");
MISH_CMD_HELP(history,
		"Display the history of commands.");
MISH_CMD_REGISTER_KIND(history, _mish_cmd_history, 0, MISH_CLIENT_CMD_KIND);


static void
_mish_cmd_disconnect(
		void * param,
		int argc,
		const char * argv[])
{
	mish_client_p c = param;

	if (c == c->mish->console) {
		printf(MISH_COLOR_RED
				"mish: can't disconnect console"
				MISH_COLOR_RESET "\n");
		return;
	}
	printf(MISH_COLOR_GREEN
			"mish: telnet: logout"
			MISH_COLOR_RESET "\n");
	c->flags |= MISH_CLIENT_DELETE;
}

MISH_CMD_NAMES(disconnect, "dis", "disconnect", "logout");
MISH_CMD_HELP(disconnect,
		"Disconnect this telnet session. If appropriate");
MISH_CMD_REGISTER_KIND(disconnect, _mish_cmd_disconnect, 0, MISH_CLIENT_CMD_KIND);

