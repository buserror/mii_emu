/*
 * mish_priv_cmd.h
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBMISH_SRC_MISH_PRIV_CMD_H_
#define LIBMISH_SRC_MISH_PRIV_CMD_H_

/*
 * I decided that the command list would't be attached to the mish_t,
 *
 * In any other API I would, for consistency sake, but here I'm more
 * interested in having a convenient way to add commands, regardless of the
 * state of mish_t, and have macro that register them before main() is called
 * and that sort of things.
 *
 * In the same vein, I also don't provide a way to remove a command, I don't
 * think it's terribly necessary at the minute
 */
#include "bsd_queue.h"

typedef void (*mish_cmd_handler_p)(
		void * param,
		int argc,
		const char *argv[]);
#define _LIBMISH_HAS_CMD_HANDLER_

int
mish_cmd_call(
		const char * cmd_line,
		void * c);


#endif /* LIBMISH_SRC_MISH_PRIV_CMD_H_ */
