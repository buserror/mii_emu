/*
 * mii_test.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * This is just a small example on how to use mii as a library
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "mii.h"
#include "mish.h"

// so mii_mish_cmd can access the global mii_t
mii_t g_mii;

void
_mii_mish_bp(
		void * param,
		int argc,
		const char * argv[]);

int main(
		int argc,
		const char * argv[])
{
	mii_t *mii = &g_mii;
	mii_init(mii);
    int idx = 1;
    uint32_t flags = MII_INIT_DEFAULT;
    int r = mii_argv_parse(&g_mii, argc, argv, &idx, &flags);
    if (r == 0) {
        printf("mii: Invalid argument %s, skipped\n", argv[idx]);
    } else if (r == -1)
		exit(1);
	mii_prepare(mii, MII_INIT_DEFAULT);
	int count = 500000;

	mish_prepare(0);
	mish_set_command_parameter(MII_MISH_KIND, &g_mii);
	// add a breakpoint
	const char *av[] = {"bp", "+d5fdr", NULL};
	_mii_mish_bp(mii, 2, av);
	do {
		if (mii->state != MII_STOPPED)
			mii_run(mii);
		if (mii->state == MII_STEP) {
//			if (mii->trace_cpu)
			mii_dump_trace_state(mii);
			if (mii->trace.step_inst)
				mii->trace.step_inst--;
			if (mii->trace.step_inst == 0)
				mii->state = MII_STOPPED;
		}
		if (mii->state != MII_RUNNING) {
		//	if (!mii->trace_cpu) {
		//		printf("mii: stopped\n");
		//		mii->trace_cpu = 1;
		//	}
			usleep(1000);
		}
	} while (mii->state != MII_TERMINATE && count--);
	mii_dispose(mii);
	return 0;
}
