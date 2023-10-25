/*
 * mish_vt.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "mish_priv_vt.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof((_a)[0]))
#endif

/*
 * Handle sequences of VT100 and UTF8 chars.
 *
 * Uses the sequence 's' as temporary buffer, and returns 1 when a complete
 * sequence OR a glyph has been decoded.
 */
int
_mish_vt_sequence_char(
		mish_vt_sequence_p s,
		uint8_t ch)
{
	// if previous sequence was done, reset it.
	if (s->done)
		s->flags = s->seq = s->glyph = s->pc = s->p[0] = 0;
	switch (s->seq) {
		case MISH_VT_RAW: {
			switch (ch) {
				case 0x1b:
					s->seq = MISH_VT_ESC;
					break;
				default: {
					if (ch & 0x80) { // UTF8 !??!
						int mask = 0x40;
						s->seq_want = 0;
						while ((ch & mask) && s->seq_want < 5) {
							s->seq_want++;
							mask >>= 1;
						}
						// clear header bits (ought to be 2; 1 allows encoding of 0xffffff)
						s->glyph = ch & (0xff >> (s->seq_want + 1));
						s->seq = MISH_VT_UTF8;
					} else {
						s->seq = (s->seq << 8) | ch;
						s->glyph = ch;	// technically it's a glyph
						s->done = 1;
					}
				}
			}
		}	break;
		case MISH_VT_ESC: {
			if (ch == '[')
				s->seq = MISH_VT_CSI;
			else {
				s->seq = (s->seq << 8) | ch;
				s->done = 1;
			}
		}	break;
		case MISH_VT_CSIQ:
		case MISH_VT_CSI: {
			switch (ch) {
				case '?': {
					// note this would still accept somelike like CSI ? 00?; 1
					if ((s->seq == MISH_VT_CSIQ) || s->pc) {
						s->done = s->error = 1;
					} else
						s->seq = MISH_VT_CSIQ;
				}	break;
				case '0' ... '9':
					s->p[s->pc] = (s->p[s->pc] * 10) + (ch - '0');
					break;
				case ';': {
					if (s->pc < ARRAY_SIZE(s->p)) {
						s->pc++;
						s->p[s->pc] = 0;
					} else {
						s->done = s->error = 1;
					}
				}	break;
				default:
					if (s->p[s->pc])
						s->pc++;
					s->seq = (s->seq << 8) | ch;
					s->done = 1;
					break;
			}
		}	break;
		case MISH_VT_UTF8: {
			s->glyph = (s->glyph << 6) | (ch & 0x3f);
			if (s->seq_want) s->seq_want--;
			s->done = s->seq_want == 0;
		}	break;
	}
#ifdef MISH_VT_DEBUG
	if (s->done)
		fprintf(stderr, "in %x(%c) state %x done:%d error:%d pc:%d/%d\n",
				ch, ch < ' '?'.':ch,s->seq, s->done, s->error,
						s->pc, (int)ARRAY_SIZE(s->p));
#endif
	return s->done;
}
