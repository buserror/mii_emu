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
#include <signal.h>
#include <unistd.h>
#include "mish_priv.h"
#include "mish.h"

/*
 * We now use a thread to run the commands; this solve the problem of
 * command generating a lot of output, deadlocking the select() thread,
 * as the command write() would fill up the pipe buffer and block.
 */
void *
_mish_cmd_runner_thread(
		void *param)
{
	mish_p m = param;

	printf("%s\n", __func__);
	while (!(m->flags & MISH_QUIT)) {
		sem_wait(&m->runner_block);
		_mish_cmd_flush(0);
	};
	printf("Exiting %s\n", __func__);
	m->cmd_runner = 0;
	sem_destroy(&m->runner_block);
	return NULL;
}

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
			if (c->flags & MISH_CLIENT_HAS_CMD) {
			//	printf("Waking up cmd_runner\n");
				c->flags &= ~MISH_CLIENT_HAS_CMD;
				// wake up the command runner
				sem_post(&m->runner_block);
			}
		}

		unsigned int max_lines = m->backlog.max_lines;
		if (m->flags & MISH_CLEAR_BACKLOG) {
			max_lines = 1;	// zero is unlimited, we don't want that
			m->flags &= ~MISH_CLEAR_BACKLOG;
			printf("Clearing backlog has %d lines\n", m->backlog.size);
		}
		/*
		* It is not enough just to remove the top lines from the backlog,
		* We also need to check all the current clients in case they have
		* a line we are going to remove in their display... We just have to
		* check the top line in this case, and 'scroll' their display to the
		* next line, if applicable
		*/
		if (max_lines && m->backlog.size > max_lines) {
			mish_line_p l;
			while ((l = TAILQ_FIRST(&m->backlog.log)) != NULL) {
				TAILQ_REMOVE(&m->backlog.log, l, self);
				m->backlog.size--;
				m->backlog.alloc -= sizeof(*l) + l->size;
				// now check the clients for this line
				TAILQ_FOREACH_SAFE(c, &m->clients, self, safe) {
					if (c->bottom == l)
						c->bottom = TAILQ_NEXT(l, self);
					if (c->sending == l)
						c->sending = TAILQ_NEXT(l, self);
				}
				free(l);
				if (m->backlog.size <= max_lines)
					break;
			}
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
//	m->flags &= ~MISH_QUIT;
	m->capture = 0;	// mark the thread done
//	printf("Exiting %s\n", __func__);
	exit(0);	// this calls mish_terminate, on main thread
//	return NULL;
}
