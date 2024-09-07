/*
 * mish_session.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for isatty etc
#include <ctype.h>	// for isdigit
#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#include <util.h>	// for openpty
#endif

#ifdef __linux__
#include <pty.h>
#endif

#include "mish_priv.h"
#include "mish.h"

/*
 * get a localtime equivalent, with milliseconds added
 */
uint64_t
_mish_stamp_ms()
{
	struct timespec tim;
#ifdef __MACH__
	clock_serv_t cclock;
	mach_timespec_t mts;
	host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
	tim.tv_sec = mts.tv_sec;
	tim.tv_nsec = mts.tv_nsec;
#else
	clock_gettime(CLOCK_REALTIME, &tim);
#endif

	uint64_t res = (tim.tv_sec * 1000) + (tim.tv_nsec / 1000000);
	return res;
}

/*
 * need to keep this around for atexit()
 */
static mish_p _mish = NULL;

static void
_mish_atexit()
{
	if (_mish)
		mish_terminate(_mish);
}

mish_p
mish_prepare(
		unsigned long caps )
{
	/* In production, of if you want to make sure mish is disabled without
	 * re-linking, set this env variable.
	 */
	if (getenv("MISH_OFF")) {
		if (atoi(getenv("MISH_OFF"))) {
			unsetenv("MISH_TELNET_PORT");
			printf("mish: Disabled my MISH_OFF\n");
			return NULL;
		}
	}

	mish_p m = (mish_p)calloc(1, sizeof(*m));

	FD_ZERO(&m->select.read);
	FD_ZERO(&m->select.write);
	TAILQ_INIT(&m->backlog.log);
	TAILQ_INIT(&m->clients);
	m->flags = caps;
	int tty = 0;
	if (getenv("MISH_TTY")) {
		tty = atoi(getenv("MISH_TTY"));
	} else
		tty = (isatty(0) && isatty(1) && isatty(2)) ||
				(caps & MISH_CAP_FORCE_PTY);
	int io[2];	// stdout pipe
	int ie[2];	// stderr pipe
#if !defined(__wasm__)
	{
		char pty[80];
		if (openpty(&io[0], &io[1], pty, NULL, NULL) == -1) {
			perror("openpty");
			goto error;
		}
		if (!(caps & MISH_CAP_NO_STDERR)) {
			if (openpty(&ie[0], &ie[1], pty, NULL, NULL) == -1) {
				perror("openpty");
				goto error;
			}
		}
		if (tty)
			m->flags |= MISH_CONSOLE_TTY;
		if (tcgetattr(0, &m->orig_termios))
			;//perror("tcgetattr");
		struct termios raw = m->orig_termios;
		raw.c_iflag &= ~(ICRNL | IXON);
		raw.c_lflag &= ~(ECHO | ICANON | IEXTEN); // ISIG
		if (tcsetattr(0, TCSAFLUSH, &raw))
			;//perror("tcsetattr");
	}
#endif
	if (!(caps & MISH_CAP_NO_TELNET)) {
		uint16_t port = 0; // suggested telnet port
		if (getenv("MISH_TELNET_PORT"))
			port = atoi(getenv("MISH_TELNET_PORT"));

		if (mish_telnet_prepare(m, port) == 0) {
			char port[8];
			snprintf(port, sizeof(port), "%d", m->telnet.port);
			setenv("MISH_TELNET_PORT", port, 1);
		} else
			unsetenv("MISH_TELNET_PORT");
	}

	// backup the existing descriptors, to make a 'client', we replace
	// the original 1,2 with out own pipe/pty
	m->originals[0] = dup(1);
	m->originals[1] = dup(2);
	// we do another dup(0,1) here, as that client will close() them
	m->console = mish_client_new(m, dup(0), dup(1), tty);
	m->stamp_start = _mish_stamp_ms();

	_mish_input_init(m, &m->origin[0], io[0]);
	if (dup2(io[1], 1) == -1) {
		perror("dup2");
		goto error;
	}
	if (!(caps & MISH_CAP_NO_STDERR)) {
		_mish_input_init(m, &m->origin[1], ie[0]);
		if (dup2(ie[1], 2) == -1) {
			perror("dup2");
			goto error;
		}
	}
	mish_set_command_parameter(MISH_CMD_KIND, m);
	atexit(_mish_atexit);
//	m->main = pthread_self();
	// TODO: Make an epoll polling thread for linux
	sem_init(&m->runner_block, 0, 0);
	pthread_create(&m->cmd_runner, NULL, _mish_cmd_runner_thread, m);
	pthread_create(&m->capture, NULL, _mish_capture_select, m);

	_mish = m;
	return m;
error:
	if (io[0]) {
		close(io[0]);
		close(io[1]);
	}
	if (ie[0]) {
		close(ie[0]);
		close(ie[1]);
	}
	free(m);
	return NULL;
}

unsigned long
mish_get_flags(
		struct mish_t *m)
{
	return m ? m->flags : 0;
}

/*
 * This can be called at any time; if the thread is running, tell it to
 * close everything and exit... then wait for a little bit for it to
 * terminate, then restore the terminal.
 */
void
mish_terminate(
		mish_p m)
{
	if (!_mish)
		return;
	// restore normal IOs
	dup2(m->originals[0], 1);
	dup2(m->originals[1], 2);
	// the thread does this, but we also do it in case this function is
	// called from another exit(x)
#if !defined(__wasm__)
	if ((m->flags & MISH_CONSOLE_TTY) &&
			tcsetattr(0, TCSAFLUSH, &m->orig_termios))
		perror("mish_terminate tcsetattr");
#endif
	close(m->originals[0]); close(m->originals[1]);
	pthread_t t1 = m->cmd_runner, t2 = m->capture;
	m->flags |= MISH_QUIT;
	if (t1)
		sem_post(&m->runner_block);
	if (t2) {
		// this will wake the select() call from sleep
		if (write(1, "\n", 1))
			;
		time_t start = time(NULL);
		time_t now;
		while (((now = time(NULL)) - start < 2) && m->capture)
			usleep(1000);
	}
	printf("\033[4l\033[;r\033[999;1H"); fflush(stdout);
	//printf("%s done\n", __func__);
	free(m);
	_mish = NULL;
}

static void
_mish_cmd_quit(
		void * param,
		int argc,
		const char * argv[])
{
	printf(MISH_COLOR_RED
			"mish: Quitting."
			MISH_COLOR_RESET "\n");

	mish_p m = param;
	m->flags |= MISH_QUIT;
}

MISH_CMD_NAMES(quit, "q", "quit");
MISH_CMD_HELP(quit,
		"exit running program",
		"Close all clients and exit(0)");
MISH_CMD_REGISTER_KIND(quit, _mish_cmd_quit, 0, MISH_CMD_KIND);

#define VT_COL(_c) "\033[" #_c "G"

static void
_mish_cmd_mish(
		void * param,
		int argc,
		const char * argv[])
{
	printf(MISH_COLOR_RED
			"mish: mish command."
			MISH_COLOR_RESET "\n");

	mish_p m = param;
	printf("Backlog: %6d lines (%5dKB)"  VT_COL(40) "Telnet Port: %5d\n",
			m->backlog.size,
			(int)m->backlog.alloc / 1024,
			m->telnet.port);
#if 0
	printf("  read: ");
	for (int i = 0; i < m->select.max; i++)
		if (FD_ISSET(i, &m->select.read)) printf("%d ", i);
	printf(VT_COL(40) "  write: ");
	for (int i = 0; i < m->select.max; i++)
		if (FD_ISSET(i, &m->select.write)) printf("%d ", i);
	printf("\n");
#endif
	mish_client_p c;
	TAILQ_FOREACH(c, &m->clients, self) {
		printf("  Client: r: %d w: %d %s %s\n",
				c->input.fd, c->output.fd,
				c->input.is_telnet ? "telnet session" :
						c == m->console ? "console" : "*unknown*",
						c == m->console ?
								m->flags & MISH_CONSOLE_TTY ? "(tty)": "(dumb)"
										: "");
		printf("          max sizes: vector: %d input: %d\n",
				c->output.size, c->input.line ? c->input.line->size : 0);
	}
	if (argv[1] && !strcmp(argv[1], "clear")) {
		printf("Clearing backlog\n");
		m->flags |= MISH_CLEAR_BACKLOG;
	}
	if (argv[1] && !strcmp(argv[1], "backlog")) {
		if (argv[2]) {
			if (!strcmp(argv[2], "clear")) {
				m->flags |= MISH_CLEAR_BACKLOG;
			} else if (!strcmp(argv[2], "max") && argv[3] && isdigit(argv[3][0])) {
				m->backlog.max_lines = atoi(argv[3]);
				printf("Backlog max lines set to %d\n", m->backlog.max_lines);
			} else if (isdigit(argv[2][0])) {
				m->backlog.max_lines = atoi(argv[2]);
				printf("Backlog max lines set to %d\n", m->backlog.max_lines);
			} else
				fprintf(stderr, "Unknown backlog command '%s'\n", argv[2]);
		} else {
			printf("Backlog: %6d/%6d lines (%5dKB)\n",
					m->backlog.size, m->backlog.max_lines,
					(int)m->backlog.alloc / 1024);
		}
	}
}

MISH_CMD_NAMES(mish, "mish");
MISH_CMD_HELP(mish,
		"[cmd...] Displays mish status.",
		"backlog [clear] [max <n>] - show backlog status\n"
		"   also set the maximum lines in the backlog\n"
		"   (0 = unlimited)\n"
		"Show status and a few bits of internals.");
MISH_CMD_REGISTER_KIND(mish, _mish_cmd_mish, 0, MISH_CMD_KIND);

