/*
 * mish_line.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include "mish_priv.h"
#include "mish_priv_line.h"

static const mish_line_t zero = {};

mish_line_p
_mish_line_add(
		mish_line_queue_t * q,
		char * buffer,
		size_t length )
{
	size_t l = length + 1;
	mish_line_p nl = (mish_line_p)malloc(sizeof(*nl) + l);
	*nl = zero;
	nl->size = l;
	nl->len = length;
	memcpy(nl->line, buffer, length);
	nl->line[l] = 0;
	nl->stamp = _mish_stamp_ms();
	TAILQ_INSERT_TAIL(q, nl, self);
	return nl;
}

int
_mish_line_reserve(
		mish_line_p *line,
		uint32_t count)
{
	if (!line)
		return -1;
	mish_line_p l = *line;
	if (count < 40)
		count = 40;
	if (!l) {
		l = (mish_line_p)calloc(1, sizeof(mish_line_t) + count);
		*l = zero;
		l->size = count;
		*line = l;
	}
	if (l->size + count >= MISH_MAX_LINE_SIZE)
		return 1;
	if (l->size - l->len < count) {
		l = (mish_line_p)realloc(l,
							sizeof(mish_line_t) + l->size + count);
		l->size += count;
		*line = l;
	}
	return 0;
}
