/*
 * mish_priv_vt.h
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBMISH_SRC_MISH_PRIV_VT_H_
#define LIBMISH_SRC_MISH_PRIV_VT_H_

#include <stdint.h>

/*
 * Input sequence decoder, deals with all the ESC bits and
 * any UTF8 bits that gets encountered
 */
#define MISH_VT_RAW		0
#define MISH_VT_TELNET 	(0xff)
#define MISH_VT_ESC 	(0x1b)
#define MISH_VT_CSI 	((MISH_VT_ESC << 8) | ('['))
#define MISH_VT_CSIQ 	((MISH_VT_CSI << 8) | ('?'))
#define MISH_VT_UTF8	0x80

// allow writing MISH_VT_SEQ(CSI, 'H')
#define MISH_VT_SEQ(_kind, _ch) (((MISH_VT_ ## _kind) << 8) | (_ch))

typedef struct mish_vt_sequence_t {
	uint32_t 	seq;		// ESC, CSI, CSI?
	int			p[8];
	union {
		struct {
			uint32_t	pc : 4,			// CSI; parameter count
						seq_want : 4,	// how many more bytes we want (UTF8)
						done : 1,		// sequence is done
						error : 1;		// sequence was not valid
		};
		uint32_t	flags;
	};
	uint32_t	glyph;		// for RAW and UTF8
} mish_vt_sequence_t, *mish_vt_sequence_p;

int
_mish_vt_sequence_char(
		mish_vt_sequence_p s,
		uint8_t ch);

#endif /* LIBMISH_SRC_MISH_PRIV_VT_H_ */
