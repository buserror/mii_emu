/*
 * mish_capture_select.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "mish_priv.h"
#include "mish.h"

/*
 * This is a select() based capture thread, it's not /ideal/ in terms of
 * performances, but it's portable and should work on OSX/BSD Linux etc.
 */
void *
_mish_capture_select(
		void *param)
{
	mish_p m = param;

	while (!(m->flags & MISH_QUIT)) {
		mish_client_p c;
		/*
		 * Call each client state machine, handle new output,
		 * new input, draw prompts etc etc.
		 * Also allow clients to tweak their 'output request' flag here
		 */
		TAILQ_FOREACH(c, &m->clients, self)
			c->cr.process(m, c);

		fd_set r = m->select.read;
		fd_set w = m->select.write;
		struct timeval tv = { .tv_sec = 1 };
		int max = select(m->select.max, &r, &w, NULL, &tv);
		if (max == 0)
			continue;
		if (max == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			// real error here?
			continue;
		}
		/* check the telnet listen socket */
		mish_telnet_in_check(m, &r);
		/*
		 * Get any input from the original terminals, it has been split
		 * into lines already and queue all into the main backlog.
		 */
		for (int i = 0; i < 1 + !(m->flags & MISH_CAP_NO_STDERR); i++) {
			_mish_input_read(m, &r, &m->origin[i]);

			mish_line_p l;
			while ((l = TAILQ_FIRST(&m->origin[i].backlog)) != NULL) {
				l->err = i == 1;	// mark stderr as such
				TAILQ_REMOVE(&m->origin[i].backlog, l, self);
				TAILQ_INSERT_TAIL(&m->backlog.log, l, self);
				m->backlog.size++;
				m->backlog.alloc += sizeof(*l) + l->size;
			}
		}
		mish_client_p safe;
		// check if any client has input, or was closed down, and remove them
		TAILQ_FOREACH_SAFE(c, &m->clients, self, safe) {
			_mish_input_read(m, &r, &c->input);
			if (c->input.fd == -1 || (c->flags & MISH_CLIENT_DELETE))
				mish_client_delete(m, c);
		}
	}
	if ((m->flags & MISH_CONSOLE_TTY) &&
			tcsetattr(0, TCSAFLUSH, &m->orig_termios))
		perror("thread tcsetattr");
	/*
	 * Try to be nice and tell all clients to cleanup
	 */
	mish_client_p c;
	while ((c = TAILQ_FIRST(&m->clients)) != NULL)
		mish_client_delete(m, c);
	m->flags &= ~MISH_QUIT;
	m->capture = 0;	// mark the thread done
	exit(0);	// this calls mish_terminate, on main thread
//	return NULL;
}
