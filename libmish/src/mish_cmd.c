/*
 * mish_cmd.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mish_priv_cmd.h"
#include "mish_priv.h"
#include "mish.h"

#include "fifo_declare.h"


#ifndef offsetof
#define offsetof(type, member)  __builtin_offsetof (type, member)
#endif
#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const char *__mptr = (const char *)(ptr);    \
        (type *)(__mptr - offsetof(type, member) );})
#endif

typedef struct mish_cmd_t {
	TAILQ_ENTRY(mish_cmd_t)	self;
	mish_cmd_handler_p cmd_cb;
	void *			param_cb;
	uint32_t 		kind; 	// optional, for your own use
	mish_cmd_flags_t flags;

	const char **	names;	// list of aliases for the command
	const char **	help;
} mish_cmd_t, *mish_cmd_p;

typedef struct mish_cmd_call_t {
	mish_cmd_p 		cmd;
	char ** 		argv;
	int				argc;
} mish_cmd_call_t;

DECLARE_FIFO(mish_cmd_call_t, mish_call_queue, 4);
DEFINE_FIFO(mish_cmd_call_t, mish_call_queue);

static TAILQ_HEAD(,mish_cmd_t) _cmd_list = TAILQ_HEAD_INITIALIZER(_cmd_list);
static mish_call_queue_t 	_cmd_fifo[2] = {0};

void __attribute__((weak))
mish_register_cmd_kind(
		const char ** cmd_names,
		const char ** cmd_help,
		mish_cmd_handler_p cmd_handler,
		void * handler_param,
		mish_cmd_flags_t flags,
		uint32_t kind)
{
	if (!cmd_names || !cmd_help || !cmd_handler) {
		fprintf(stderr, "%s invalid parameters\n", __func__);
		return;
	}
	mish_cmd_p cmd = calloc(1, sizeof(*cmd));

	cmd->names = cmd_names;
	cmd->help = cmd_help;
	cmd->cmd_cb = cmd_handler;
	cmd->param_cb = handler_param;
	cmd->kind = kind;
	cmd->flags = flags;

	// keep the list roughtly sorted
	mish_cmd_p c, s;
	TAILQ_FOREACH_SAFE(c, &_cmd_list, self, s) {
		if (strcmp(cmd_names[0], c->names[0]) < 0) {
			TAILQ_INSERT_BEFORE(c, cmd, self);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&_cmd_list, cmd, self);
}

void
mish_set_command_parameter(
		unsigned int kind,
		void * param)
{
	mish_cmd_p cmd;
	TAILQ_FOREACH(cmd, &_cmd_list, self) {
		if (!kind || cmd->kind == kind)
			cmd->param_cb = param;
	}
}

/*
 * Returns the length of the first word of cmd_line
 */
static int
first_word_length(
		const char * cmd_line)
{
	const char *d = cmd_line;
	while (*d && *d != ' ')
		d++;
	return d - cmd_line;
}

mish_cmd_p
mish_cmd_lookup(
		const char * cmd_line)
{
	if (!cmd_line)
		return NULL;
	int l = first_word_length(cmd_line);
	mish_cmd_p cmd;
	TAILQ_FOREACH(cmd, &_cmd_list, self) {
		for (int i = 0; cmd->names && cmd->names[i]; i++)
			if (!strncmp(cmd->names[i], cmd_line, l))
				return cmd;
	}
	return NULL;
}

typedef struct _mish_argv_t {
	char * line;
	int ac;
	char * av[0];
} _mish_argv_t;

/*
 * Duplicate 'line', split it into words, store word pointers in an array,
 * NULL terminate it. Also return the number of words in the array in argc.
 *
 * The returned value is made of two malloc()ed blocks. use mish_argv_free
 * to free the memory.
 * It's OK to change any of the pointers. But no not try to realloc() the
 * vector as it hides a structure
 */
static char **
mish_argv_make(
		const char * line,
		int * argc )
{
	const char separator = ' ';
	_mish_argv_t * r = calloc(1, sizeof(*r));
	r->line = strdup(line);
	char *dup = r->line;
	char quote = 0;
	enum { s_newarg = 0, s_startarg, s_copyquote, s_skip, s_copy };
	int state = s_newarg;
	do {
		switch (state) {
			case s_newarg:
				r = realloc(r, sizeof(*r) + ((r->ac + 2) * sizeof(char*)));
				while (*dup == ' ' || *dup == separator)
					dup++;
				r->av[r->ac++] = dup;
				state = s_startarg;
				break;
			case s_startarg:
				if (*dup == '"' || *dup == '\'') {
					quote = *dup++;
					state = s_copyquote;
				} else
					state = s_copy;
				break;
			case s_copyquote:
				if (*dup == '\\') {
					state = s_skip;
					dup++;
				} else if (*dup == quote) {
					state = s_newarg;
					dup++;
					if (*dup) *dup++ = 0;
				} else if (*dup)
					dup++;
				break;
			case s_skip:
				dup++;
				state = s_copyquote;
				break;
			case s_copy:
				if (*dup == 0)
					break;
				if (*dup != separator)
					dup++;
				else {
					state = s_newarg;
					if (*dup) *dup++ = 0;
				}
				break;
		}
	} while (*dup);
	r->av[r->ac] = NULL;
	if (argc)
		*argc = r->ac;
	return r->av;
}

/*
 * Free memory allocated by a mish_argv_make
 */
void
mish_argv_free(
		char **_av)
{
	if (!_av)
		return;
	_mish_argv_t * r = container_of(_av, _mish_argv_t, av);
	free((void*)r->line);
	free((void*)r);
}

int
mish_cmd_call(
		const char * cmd_line,
		void * c)
{
	if (!cmd_line || !*cmd_line)
		return -1;

	mish_cmd_p cmd = mish_cmd_lookup(cmd_line);
	if (!cmd) {
		int l = first_word_length(cmd_line);
		printf(MISH_COLOR_RED
				"mish: '%.*s' not found. type 'help'."
				MISH_COLOR_RESET "\n",
				l, cmd_line);
		return -1;
	}
	int ac = 0;
	char ** av = mish_argv_make(cmd_line, &ac);

	mish_call_queue_t 	*fifo = &_cmd_fifo[cmd->flags.safe];

	// these are special commands, their parameter is the client
	if (cmd->kind == MISH_CLIENT_CMD_KIND) {
		cmd->cmd_cb(c, ac, (const char**)av);
		mish_argv_free(av);
		return 0;
	}
	// all other commands are queued
	if (!mish_call_queue_isfull(fifo)) {
		mish_cmd_call_t fe = {
				.cmd = cmd,
				.argv = av,
				.argc = ac,
		};
		mish_call_queue_write(fifo, fe);
	} else {
		fprintf(stderr,
			"mish: cmd FIFO%d full, make sure to call mish_cmd_poll()!\n",
			cmd->flags.safe);
	}
	return cmd->flags.safe == 0;	// we got a command to run?
}

int
_mish_cmd_flush(
		unsigned int queue)
{
	int res = 0;
	mish_call_queue_t 	*fifo = &_cmd_fifo[!!queue];
	while (!mish_call_queue_isempty(fifo)) {
		mish_cmd_call_t c = mish_call_queue_read(fifo);
		c.cmd->cmd_cb(
				c.cmd->param_cb,
				c.argc, (const char**)c.argv);
		mish_argv_free(c.argv);
		res++;
	}
	return res;

}

int
mish_cmd_poll()
{
	return _mish_cmd_flush(1);
}

static const char *_help[] = {
	"A few of the typical EMACS keys work for editing commands.",
	"like, ^A-^E, ^W, ^K - ^P,^N to navigate history and ^L to",
	"redraw.",
	"BEG/PGUP/DOWN/END to change the view of the backlog buffer.",
	0,
};
static void
_mish_cmd_help(
		void * param,
		int argc,
		const char * argv[])
{
	mish_cmd_p cmd;

	if (argc < 2) {
		printf(MISH_COLOR_GREEN "mish: Key binding\n");
		for (int i = 0; _help[i]; i++)
			printf("  %s\n", _help[i]);
		printf(MISH_COLOR_GREEN "List of commands\n");

		TAILQ_FOREACH(cmd, &_cmd_list, self) {
			printf("  ");
			for (int i = 0; cmd->names && cmd->names[i]; i++)
				printf("%s%s", i > 0 ? "," : "", cmd->names[i]);
			printf(" - %s\n", cmd->help[0]);
		}
		printf(MISH_COLOR_RESET);
	} else {
		for (int i = 1; i < argc; i++) {
			mish_cmd_p cmd = mish_cmd_lookup(argv[i]);

			if (!cmd) {
				printf(MISH_COLOR_RED
						"mish: Unknown command '%s'"
						MISH_COLOR_RESET "\n", argv[i]);
				continue;
			}
			printf(MISH_COLOR_GREEN);
			for (int i = 0; cmd->names && cmd->names[i]; i++)
				printf("%s%s", i > 0 ? "," : "", cmd->names[i]);
			printf("\n");
			for (int i = 0; cmd->help && cmd->help[i]; i++)
				printf(" %s\n", cmd->help[i]);
			printf(MISH_COLOR_RESET);
		}
	}
}

MISH_CMD_NAMES(help, "help");
MISH_CMD_HELP(help,
		"[cmd...] - Display command list, or help for commands",
		"(optional) [cmd...] will display all the help for [cmd]");
MISH_CMD_REGISTER(help, _mish_cmd_help);
