/*
 * mish_vt_test.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include <stdlib.h>
#include <stdio.h>
#include "../src/mish_vt.c"

/* small test unit for the VT decoder, mostly for UTF8 */
int main()
{
	const char * input = "Hello 😈 \033[1mThere\033[0m";

	mish_vt_sequence_t sq = {};

	printf("%s\n", input);
	const char *s = input;
	int cg = 0;
	while (*s) {
		if (_mish_vt_sequence_char(&sq, *s)) {
			printf("glyph s:%08x g:%08x\n", sq.seq, sq.glyph);
			if (sq.glyph)
				cg++;
		}
		s++;
	}
	printf("%d glyphs in string\n", cg);
}
