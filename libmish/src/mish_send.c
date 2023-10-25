/*
 * mish_send.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/uio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "mish_priv.h"
#include "mish_priv_line.h"

/*
 * We arrive here with an array of iovec buffers already prepared by
 * _mish_send_queue_*();
 * This is called repeatedly, and we continually call writev, gather how
 * many bytes have been written, and mark each corresponding buffer as done.
 * Once all the iovec buffers have been filled, we're done!
 *
 * All of this is made to reduce to the max the number of 'write' system
 * calls.
 *
 * There are two kinds of buffers in the vectors:
 * 1) the "backlog" lines themselves. These have a pointer, all is well and good.
 * 2) sequences we are making up on the fly, like prompt, colors etc; these
 *    are allocated in a 'sqb' (sequence buffer) which can grow (via realloc)
 *    as more stuff is added.
 *    Therefore, we can't pre-populate the vector array with the iov_base
 *    pointers, we keep these to NULL, and in this function, the first thing
 *    we do is derivate the pointers from their 'iov_len' in the current 'sqb'.
 *
 *    Oh and we "lock" sqb to prevent any other things to be added.
 */
int
_mish_send_flush(
		mish_p m,
		mish_client_p c)
{
	/* If it's a new sequence, 'lock' it and prepare it's iovec pointers */
	if (c->output.sqb) {
		if (!c->output.sqb->done) {
			/*
			 * Here we construct the base address of out character buffers.
			 * They start with a NULL address as the sqb could grow when
			 * adding sequences.
			 * So once we are ready to send, we need to compute the
			 * base addresses of everyone that ought to point into the sqb,
			 * then we 'lock' it by marking it 'done'.
			 */
			char * base = c->output.sqb->line;
			for (int i = 0; i < c->output.count; i++) {
				if (!c->output.v[i].iov_base) {
					c->output.v[i].iov_base = base;
					base += c->output.v[i].iov_len;
				}
			}
			c->output.sqb->done = 1;	// "lock" the sqb until all is sent.
		}
	}
	if (!c->output.count)
		return 0;
	/* if we don't have 'permission' to write yet, ask for it */
	if (!FD_ISSET(c->output.fd, &m->select.write)) {
		FD_SET(c->output.fd, &m->select.write);
		return 1;
	}
	/* skip what we've already done */
	struct iovec * io = c->output.v;
	int ioc = c->output.count;
	while (io->iov_len == 0 && ioc) {
		io++; ioc--;
	}
	int res = 1;
	if (ioc) {
		ssize_t got = writev(c->output.fd, io, ioc);
		if (got == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				// we've been close down?
				FD_CLR(c->output.fd, &m->select.write);
				goto done;
			}
		}
		/* fill up vectors with what was written */
		while (got > 0) {
			ssize_t b = got > io->iov_len ? io->iov_len : got;
			io->iov_len -= b;
			io->iov_base += b;
			if (io->iov_len == 0) {
				io++;
				ioc--;
			}
			got -= b;
		}
	}
	if (ioc == 0) { // done!
done:
		res = 0;
		c->output.count = 0;
		// also flush out temporary buffers, and unlock it
		if (c->output.sqb) {
			c->output.sqb->done = 0;
			c->output.sqb->len = 0;
		}
		/* if nothing else is ready to send, clear us from the select loop */
		if (!c->sending)
			FD_CLR(c->output.fd, &m->select.write);
	}
	return res;
}

/*
 * Add up a bit of output to something we want to send as a sequence
 */
static char *
_mish_send_prep(
		mish_client_p c,
		size_t l)
{
	if (c->output.sqb && c->output.sqb->done) {
		fprintf(stderr, "%s problem\n", __func__);
		abort();
	}
	c->output.total += l;
	/* allocate enough in the character buffer */
	_mish_line_reserve(&c->output.sqb, l + 1);
	/* allocate enough in the vector buffer */
	if (c->output.count == c->output.size) {
		c->output.size += 8;
		c->output.v = realloc(c->output.v, c->output.size * sizeof(c->output.v[0]));
	}
	/* this is where the calling function will copy it's data */
	char * res = (char*)c->output.sqb->line + c->output.sqb->len;
	c->output.sqb->len += l;
	/* try to concatenate buffers together, if they are 'ours' */
	if (c->output.count && c->output.v[c->output.count-1].iov_base == NULL) {
		struct iovec *v = c->output.v + c->output.count - 1;
		v->iov_len += l;
	} else {
		struct iovec *v = c->output.v + c->output.count;
		v->iov_base = NULL; // important, see top of the file for details
		v->iov_len = l;
		v++;
		c->output.count++;
	}
	return res;
}

/*
 * Add up a bit of output to something we want to send as a sequence
 */
void
_mish_send_queue(
		mish_client_p c,
		const char * b)
{
	int l = strlen(b);
	char *d = _mish_send_prep(c, l);

	strcpy(d, b);
}

/*
 * Same as above, but with formatting.
 */
void
_mish_send_queue_fmt(
		mish_client_p c,
		const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int l = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	char *d = _mish_send_prep(c, l);
	va_start(ap, fmt);
	vsnprintf(d, l + 1, fmt, ap);
	va_end(ap);
}

void
_mish_send_queue_line(
		mish_client_p c,
		mish_line_p line )
{
	/* allocate enough in the vector buffer */
	if (c->output.count == c->output.size) {
		c->output.size += 8;
		c->output.v = realloc(c->output.v, c->output.size * sizeof(c->output.v[0]));
	}
	struct iovec *v = c->output.v + c->output.count;
	v->iov_base = line->line;
	v->iov_len = line->len;
	c->output.count++;
	c->output.total += line->len;
}
