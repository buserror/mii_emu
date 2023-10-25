/*
 * mish_priv_line.h
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBMISH_SRC_MISH_PRIV_LINE_H_
#define LIBMISH_SRC_MISH_PRIV_LINE_H_

#include <stdint.h>
#include "bsd_queue.h"

#define MISH_MAX_LINE_SIZE	0xffff

struct mish_line_t {
	TAILQ_ENTRY(mish_line_t) 	self;
	/* The stamp is made with _mish_get_stamp, which returns epoch
	 * milliseconds, so we don't need the 64 bits for the stamp here,
	 * lets say 34 bits seconds + 10 bits milliseconds
	 */
	uint64_t		stamp : 44;
	uint64_t		err: 1, use: 4, draw_stamp: 1,
					size : 16, len: 16, // len <= size
					done: 16;	// done <= len
	char			line[0];
};

typedef struct mish_line_t mish_line_t, *mish_line_p;

typedef TAILQ_HEAD(mish_line_queue_t, mish_line_t)	mish_line_queue_t;

/*
 * mish_line tools
 */
int
_mish_line_reserve(
		mish_line_p *line,
		uint32_t count);
mish_line_p
_mish_line_add(
		mish_line_queue_t * q,
		char * buffer,
		size_t length );

#endif /* LIBMISH_SRC_MISH_PRIV_LINE_H_ */
