/*
 * mish_test.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "mish.h"

int cnt = 0;

int main()
{
	mish_prepare(0);

	while (1) {
		sleep(1);
		printf("Count %d\n", cnt++);
	}
}


/* And here is a command line action that can reset the 'cnt' variable */
static void _test_set_cnt(void * param, int argc, const char * argv[])
{
	if (argc > 1) {
		cnt = atoi(argv[1]);
	} else {
		fprintf(stderr,
				"%s: syntax 'set XXX' to set the variable\n", argv[0]);
	}
}

MISH_CMD_NAMES(set, "set");
MISH_CMD_HELP(set,
		"set 'cnt' variable",
		"test command for libmish!");
MISH_CMD_REGISTER(set, _test_set_cnt);
