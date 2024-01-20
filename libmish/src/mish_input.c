/*
 * mish_input.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "mish_priv.h"
#include "mish_priv_line.h"

#ifdef MISH_INPUT_TEST
#define D(_w) _w
#else
#define D(_w)
#endif

void
_mish_input_init(
		mish_p m,
		mish_input_p in,
		int fd)
{
	TAILQ_INIT(&in->backlog);
//	fprintf(stderr, "%s %d\n", __func__, fd);
	in->fd = fd;
	in->line = NULL;
	FD_SET(in->fd, &m->select.read);
	if (in->fd >= m->select.max - 1)
		m->select.max = in->fd +1;

	int flags = fcntl(fd, F_GETFL, NULL);
	if (flags == 1) {
	//	perror("mish: input F_GETFL");
		flags = 0;
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
		perror("mish: input F_SETFL");
}

void
_mish_input_clear(
		mish_p m,
		mish_input_p in)
{
	mish_line_p l;
	while ((l = TAILQ_FIRST(&in->backlog)) != NULL) {
		TAILQ_REMOVE(&in->backlog, l, self);
		free(l);
	}
	if (in->line)
		free(in->line);
	in->line = NULL;
	if (in->fd != -1) {
		FD_CLR(in->fd, &m->select.read);
		FD_CLR(in->fd, &m->select.write);
		close(in->fd);
		in->fd = -1;
	}
}

int
_mish_input_read(
		mish_p m,
		fd_set * fds,
		mish_input_p in)
{
	if (!FD_ISSET(in->fd, fds))
		return 0;
	do {
		if (_mish_line_reserve(&in->line, 80)) {
			D(printf("  reserve bailed us\n");)
			break;
		}
		ssize_t rd = read(in->fd,
						in->line->line + in->line->len,
						in->line->size - in->line->len - 1);
		if (rd == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
			break;
		if (rd <= 0) {
			close(in->fd);
			FD_CLR(in->fd, &m->select.read);
			FD_CLR(in->fd, &m->select.write);
			in->fd = -1;
			printf(MISH_COLOR_RED "mish: telnet: disconnected"
					MISH_COLOR_RESET "\n");
			return -1;
		}
		in->line->len += rd;
	} while (1);
	uint8_t * s = (uint8_t*) in->line->line + in->line->done;
	uint8_t * d = s;
	int added = in->line->len - in->line->done;
	D(printf(" buffer added %d done %d len %d size %d\n", added,
			(int)in->line->done, (int)in->line->len, (int)in->line->size);
	if (in->line->done)
		printf("    buffer: '%.*s'\n", in->line->done, in->line->line);)
	/*
	 * This loop re-parse the data, passes it to the optional handler,
	 * and then store processed lines into the backlog. This is not super
	 * optimal in the case there isn't a process_char() callback, so perhaps
	 * I should make another less 'generic' input parser that doesn't copy
	 * stuff around (in place, often).
	 */
	while (added) {
		int r = MISH_IN_STORE;
		if (in->process_char)
			r = in->process_char(m, in, *s);
		else
			r = *s == '\n' ? MISH_IN_SPLIT : MISH_IN_STORE;

		if (r == MISH_IN_STORE || r == MISH_IN_SPLIT) {
			*d++ = *s;
			in->line->done++;
		}
		if (r == MISH_IN_SPLIT) {
			D(printf("  split size %d remains %d : '%.*s'\n", in->line->done,
					(int)added, in->line->done-1, in->line->line);)
			_mish_line_add(&in->backlog, in->line->line, in->line->done);
			d = (uint8_t*)in->line->line;
			in->line->len = in->line->done = 0;
		}
		s++; added--;
	}
	*d = 0; // NUL terminate for debug purpose!
	D(printf(" exit added %d done %d len %d size %d\n", added,
			(int)in->line->done, (int)in->line->len, (int)in->line->size);
	in->line->len = in->line->done;
	if (in->line->done)
		printf("    buffer: '%s'\n", in->line->line);)

	return TAILQ_FIRST(&in->backlog) != NULL;
}
