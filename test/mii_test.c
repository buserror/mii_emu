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

int main()
{
	mii_t *mii = &g_mii;
	mii_init(mii);
	mii_prepare(mii, MII_INIT_DEFAULT);
//	int count = 100000;

	mish_prepare(0);

	// add a breakpoint
	const char *av[] = {"bp", "+d5fdr", NULL};
	_mii_mish_bp(NULL, 2, av);
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
	} while (1);
	return 0;
}
