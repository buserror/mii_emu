/*
 * mish_cmd_env.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mish.h"

extern char ** environ;

static void
_mish_cmd_env(
		void * param,
		int argc,
		const char * argv[])
{
	if (argc < 2) {
		for (int i = 0; environ[i]; i++)
			if (strncmp(environ[i], "LS_COLORS=", 10))
				printf("%s\n", environ[i]);
		return;
	}
	for (int i = 0; environ[i]; i++)
		for (int ei = 1; ei < argc; ei++) {
			int l = strlen(argv[ei]);
			if (!strncmp(environ[i], argv[ei], l))
				printf("%s\n", environ[i]);
		}
}

MISH_CMD_NAMES(env, "env");
MISH_CMD_HELP(env,
		"[names ...] display all environment, or variables",
		"Apart from LS_COLORS: that is just spam.",
		"If you specify names it'll just show the ones whose name",
		"start with that prefix");
MISH_CMD_REGISTER(env, _mish_cmd_env);

static void
_mish_cmd_setenv(
		void * param,
		int argc,
		const char * argv[])
{
	for (int ei = 1; ei < argc; ei++) {
		char *equal = strchr(argv[ei], '=');
		if (!equal) {
			printf("mish: setenv: '%s' requires an '='\n", argv[ei]);
			return;
		}
		*equal++ = 0;
		printf("mish: %s%s%s%s\n", *equal ? "" : "unset ",
				argv[ei], *equal ? " = ": "",
				*equal ? equal : "");
		if (!*equal)
			unsetenv(argv[ei]);
		else
			setenv(argv[ei], equal, 1);
	}
}

MISH_CMD_NAMES(setenv, "setenv");
MISH_CMD_HELP(setenv,
		"[<name>=<value>...] set/clear environment variable(s)",
		"Set <name> to <value> .. if <value> is omitted, clears it.",
		"The '=' is required, even when clearing.");
MISH_CMD_REGISTER(setenv, _mish_cmd_setenv);
