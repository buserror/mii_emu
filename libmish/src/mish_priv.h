/*
 * mish_priv.h
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBMISH_SRC_MISH_PRIV_H_
#define LIBMISH_SRC_MISH_PRIV_H_

#include <sys/select.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <termios.h>
#include "bsd_queue.h"
#include "mish_priv_vt.h"
#include "mish_priv_line.h"

struct mish_t;

/*
 * The process_char callback of mish_input_t returns one of these.
 */
enum {
	MISH_IN_SKIP = 0, 	// skip current character (don't store)
	MISH_IN_STORE, 		// store current character in input buffer
	MISH_IN_SPLIT, 		// store current line, add it to backlog
};

#define MISH_CMD_KIND 			('m' << 24 | 'i' << 16 | 's' << 8 | 'h')
#define MISH_CLIENT_CMD_KIND  	('c' << 24 | 'l' << 16 | 'i' << 8 | 'e')

/*
 * This receives stuff from a file descriptor, 'processes' it, and split it
 * into lines to eventually store new lines into the 'backlog'.
 */
typedef struct mish_input_t {
	// lines read from fd are queued in here when \n has been received
	mish_line_queue_t	backlog;
	uint32_t		is_telnet : 1, flush_on_nl : 1;

	// return 1 for the character to be stored, 0 for it to be skipped
	int (*process_char)(
			struct mish_t *m,
			struct mish_input_t *i,
			uint8_t ch);
	void * 			refcon;	// reference constant, for the callbacks
	int 			fd;
	mish_line_p		line;
} mish_input_t, *mish_input_p;

/* various internal states for the client */
enum {
	MISH_CLIENT_INIT_SENT 		= (1 << 0),
	MISH_CLIENT_HAS_WINDOW_SIZE = (1 << 1),
	MISH_CLIENT_HAS_CURSOR_POS 	= (1 << 2),
	MISH_CLIENT_UPDATE_PROMPT 	= (1 << 3),
	MISH_CLIENT_UPDATE_WINDOW 	= (1 << 4),
	MISH_CLIENT_SCROLLING 		= (1 << 5),
	MISH_CLIENT_HAS_CMD			= (1 << 6),
	MISH_CLIENT_DELETE 			= (1 << 7),
};

typedef struct mish_client_t {
	TAILQ_ENTRY(mish_client_t) self;
	struct mish_t *	mish;
	uint32_t		flags;

	// "coroutine" processing received lines from the stdout/err
	struct {
		void * 			state;	// state for the prompt coroutine
		// _mish_client_interractive_cr() or _mish_client_dumb_cr()
		void			(*process)(
								struct mish_t * m,
								struct mish_client_t* c);
	}				cr;
	int				footer_height;	// # static lines at bottom of screen
	int				current_vpos;
	mish_line_p		bottom;
	// Line we are currently sending (or NULL)
	mish_line_p		sending;

	/*
	 * Output sent to the client is made of bits we want to send to move
	 * the cursor and so on, and, the actual lines we captured from the program.
	 * So a vector list is built as the output is built, and it's made of bits
	 * we've copied in "sqb" as well as lines from the mish backlog.
	 * Once we deem a good time to output stuff, we send that vector and wait
	 * until it's all gone before starting again.
	 */
	struct {
		int				fd; 	// index of fd we writev to
		struct iovec	*v;		// starts at NULL
		int				count;// how many are filled in
		int				size;	// how many are allocated
		// temporary sequence buffer, for composite output.
		mish_line_p		sqb;
		size_t			total;	// total bytes we sent
	}				output;

	char			prompt[64];
	/* Since the prompt will contains more bytes than displayed glyphs,
	 * we keep track of the number of glyphs so we can reposition the
	 * cursor accurately. */
	int				prompt_gc;	// prompt real glyph count

	/*
	 * Unfiltered console input from the client. This get stored in there
	 * until the input code notices any output has been sent. Then it is
	 * passed down to 'vts'
	 */
	mish_input_t	input;
	/*
	 * this handles the vt sequences received at the command prompt,
	 * like direction keys etc -also handles UTF8 sequences- (but doesn't do
	 * anything with them). It is also subverted by the telnet decoder to
	 * do it's own things, separately.
	 * Sequences coming out are processed in mish_client_input.
	 */
	mish_vt_sequence_t vts;
	/*
	 * Stuff received from 'input' side gets filtered via 'vts' and
	 * stored in this.
	 * Once a command has been validated, it is added back to the input
	 * backlog to make this client 'history'
	 */
	mish_line_p		cmd;

	struct {
		int w, h;	} window_size; // valid if MISH_CLIENT_HAS_WINDOW_SIZE
	struct {
		int x, y;	} cursor_pos; // valid if MISH_CLIENT_HAS_CURSOR_POS
} mish_client_t, *mish_client_p;

// supplement the public ones from mish.h
enum {
	MISH_QUIT			= (1 << 31),
	// the process console we started was a tty, so has terminal settings
	MISH_CONSOLE_TTY	= (1 << 30),
	// request to clear the backlog
	MISH_CLEAR_BACKLOG	= (1 << 29),
};

typedef struct mish_t {
	uint32_t		flags;
	struct termios	orig_termios;	// original terminal settings
	int				originals[2];	// backup the original 1,2 fds
	/* These are the ones we use to read the output from the main program. */
	mish_input_t	origin[2];
	uint64_t		stamp_start;

	TAILQ_HEAD(, mish_client_t) clients;
	mish_client_p	console;		// client that is also the original terminal.

	pthread_t 		capture;		// libmish main thread
	pthread_t 		cmd_runner;		// command runner thread
	sem_t 			runner_block;	// semaphore to block the runner thread
	pthread_t		main;			// todo: allow pause/stop/resume?

	struct {
		unsigned int 		max_lines;	// max lines in backlog (0 = unlimited)
		mish_line_queue_t	log;
		unsigned int		size;	// number of lines in backlog
		size_t				alloc;	// number of bytes in the backlog
	}				backlog;
	struct {
		int				listen;		// listen socket
		int				port;		// port we're listening on
	}				telnet;
	// Used by the select thread in mish_capture_select.c
	struct {
		fd_set			read, write;
		int				max;
	}				select;
} mish_t, *mish_p;

mish_client_p
mish_client_new(
		mish_p m,
		int in,
		int out,
		int is_tty);
void
mish_client_delete(
		mish_p m,
		mish_client_p c);

/*
 * Sequence buffer handling
 */
int
_mish_send_flush(
		mish_p m,
		mish_client_p c);
void
_mish_send_queue(
		mish_client_p c,
		const char * b);
void
_mish_send_queue_fmt(
		mish_client_p c,
		const char *fmt, ...);
void
_mish_send_queue_line(
		mish_client_p c,
		mish_line_p line );

//! Parse current input buffer fro VT sequences, like keys.
int
_mish_client_vt_parse_input(
		struct mish_t *m,
		struct mish_input_t *in,
		uint8_t ich);

/*
 * This is the main interactive client coroutine. This one is interesting.
 */
void
_mish_client_interractive_cr(
		mish_p m,
		mish_client_p c);
void
_mish_client_dumb_cr(
		mish_p m,
		mish_client_p c);

uint64_t
_mish_stamp_ms();

/*
 * telnet handling
 */
int
mish_telnet_prepare(
		mish_p m,
		uint16_t port);
int
mish_telnet_in_check(
		mish_p m,
		fd_set * r);
void
mish_telnet_send_init(
		mish_client_p c);
int
_mish_telnet_parse(
		mish_client_p c,
		uint8_t ch);

/*
 * input handling
 */
void
_mish_input_init(
		mish_p m,
		mish_input_p in,
		int fd);
void
_mish_input_clear(
		mish_p m,
		mish_input_p in);
int
_mish_input_read(
		mish_p m,
		fd_set * fds,
		mish_input_p in);
// flush the command FIFO (queue = 0 for non-safe commands)
int
_mish_cmd_flush(
		unsigned int queue);

/*
 * Thread functions
 */
void *
_mish_capture_select(
		void *param);
void *
_mish_cmd_runner_thread(
		void *param);

/*
 * https://en.wikipedia.org/wiki/ANSI_escape_code#Terminal_output_sequences
 */
#define MISH_COLOR_RED 		"\033[38;5;125m"
#define MISH_COLOR_GREEN 	"\033[38;5;28m"
#define MISH_COLOR_RESET 	"\033[0m"

#endif /* LIBMISH_SRC_MISH_PRIV_H_ */
