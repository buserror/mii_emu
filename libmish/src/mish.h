/*
 * mish.h
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBMISH_SRC_MISH_H_
#define LIBMISH_SRC_MISH_H_

#ifndef _LIBMISH_HAS_CMD_HANDLER_
/*
 * If you register with a non NULL parameter, you get that, otherwise
 * you get an (opaque) mish_client_p
 */
typedef void (*mish_cmd_handler_p)(
		void * param,
		int argc,
		const char *argv[]);
#endif

enum {
	MISH_CAP_NO_STDERR 	= (1 << 0),
	MISH_CAP_NO_TELNET	= (1 << 2),
	MISH_CAP_FORCE_PTY 	= (1 << 1),
};

/*!
 * Starts the mish thread, recover the stdout/err discriptor, and starts
 * displaying a prompt for your process.
 */
struct mish_t *
mish_prepare(
		unsigned long caps );
//! Returns the flags you passed to mish_prepare()
unsigned long
mish_get_flags(
		struct mish_t * m);
/*!
 * This is normally called automatically at exit() time
 */
void
mish_terminate(
		struct mish_t * m);

/*
 * Register a command; please use the macros, don't call this directly.
 *
 * Symbol is weak, so you can have mish commands in a dynamic library,
 * without having to drag libmish into programs that use it; the commands
 * won't register, but if you then link with a program that uses libmish,
 * they will.
 */
void
mish_register_cmd(
		const char ** cmd_names,
		const char ** cmd_help,
		mish_cmd_handler_p cmd_handler,
		void * handler_param,
		int safe) __attribute__((weak));
/*!
 * Poll the mish threads for pending commands, and call their handlers.
 * This is only necessary for 'safe' commands that needs to be run on the
 * main thread
 */
int
mish_cmd_poll();

/*
 * This is how to add a command to your program:
 *
 * #include <mish.h>
 *
 * static void _my_command(void * yours, int argc, const char * argv[] ) {
 * 	// do something here, like in main()
 * }
 *
 * MISH_CMD_NAMES(_my_cmd, "cmd", "cmd_alias");
 * MISH_CMD_HELP(_my_cmd, "Short help one liner", "Extra help for help <cmd>")
 * MISH_CMD_REGISTER(_my_cmd, _my_command);
 *
 * That's it!
 */

#define MISH_CMD_NAMES(_n, args...) \
	static const char * _cmd_##_n[] = { args, 0 }
#define MISH_CMD_HELP(_n, args...) \
	static const char * _help_##_n[] = { args, 0 }
/* This gymnastic is required becase GCC has decided that it'll ignore
 * ((weak)) symbols can be NULL, and throw me an error instead */
static inline int _gcc_warning_false_pos_workaround(void * func) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
	return func != NULL;
#pragma GCC diagnostic pop
}
//! These are called on libmish thread, immediately.
#define MISH_CMD_REGISTER(_d, _handler) \
	__attribute__((constructor,used)) \
	static void _mish_register_##_d() { \
		if (_gcc_warning_false_pos_workaround(mish_register_cmd)) \
			mish_register_cmd(_cmd_##_d,_help_##_d,_handler,0,0);\
	}
//! These are called when the main program calls mish_cmd_poll()
#define MISH_CMD_REGISTER_SAFE(_d, _handler) \
	__attribute__((constructor,used)) \
	static void _mish_register_##_d() { \
		if (_gcc_warning_false_pos_workaround(mish_register_cmd)) \
			mish_register_cmd(_cmd_##_d,_help_##_d,_handler,0,1);\
	}

#endif /* LIBMISH_SRC_MISH_H_ */
