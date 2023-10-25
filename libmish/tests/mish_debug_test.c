/*
 * mish_debug_test.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * NOTE this program is made for debug purpose only, it basically give you
 * telnet command access remotely
 * IT IS JUST MADE TO DEBUG LIBMISH, DO NOT USE IN PROD!
 */
#define _GNU_SOURCE // for asprintf
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "mish.h"

static void
sigchld_handler(int s)
{
	int status = 0;
	pid_t pid = waitpid(-1, &status, WNOHANG);
	if (pid > 0) {
		if (WIFEXITED(status) && WEXITSTATUS(status))
			fprintf(stderr, "pid %d returned %d\n", (int)pid, WEXITSTATUS(status));
		if (WIFSIGNALED(status))
			fprintf(stderr, "pid %d terminated with %s\n",
					(int)pid, strsignal(WTERMSIG(status)));
	}
}

int main()
{
	signal(SIGCHLD, sigchld_handler);
	mish_prepare(0);

	while (1) {
		sleep(1);
	}
}

static const char *
_shell_which(
	const char * cmd)
{
	if (cmd[0] == '.' || cmd[0] == '/')
		return strdup(cmd);
	char * path = strdup(getenv("PATH"));
	char * cur = path, *e;
	while ((e = strsep(&cur, ":")) != NULL) {
		char *pe;
		asprintf(&pe, "%s/%s", e, cmd);
		struct stat st = {};
		if (stat(pe, &st) == 0) {
			return pe;
		}
		free(pe);
	}
	fprintf(stderr, "%s: command not found\n", cmd);
	return NULL;
}

static void
_test_shell_cnt(
		void * param,
		int argc,
		const char * argv[])
{
	const char * av[argc + 1];
	av[0] = _shell_which(argv[1]);
	if (av[0] == NULL)
		return;
	for (int ai = 1; ai <= argc; ai++)
		av[ai] = argv[ai+1];
	pid_t pid = fork();
	if (pid == 0) {
		execv(av[0], (void*)av);
		perror(av[0]);
		close(0);close(1);close(2);
		exit(1);
	}
}

MISH_CMD_NAMES(shell, "shell");
MISH_CMD_HELP(shell,
		"run a shell command",
		"test command for libmish!");
MISH_CMD_REGISTER(shell, _test_shell_cnt);
