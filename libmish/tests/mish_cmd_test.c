/*
 * mish_cmd_test.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define MISH_DEBUG 1
#include "mish_cmd.c"

int main()
{
	int argc;
	char ** argv = mish_argv_make(
				"command with some \" quoted\\\"words \" should work\n", &argc);

	for (int i = 0; i < argc; i++)
		printf("%2d: '%s'\n", i, argv[i]);
	mish_argv_free(argv);
}
