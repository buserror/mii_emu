/*
 * mii_thread.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "mii.h"
#include "mii_thread.h"

typedef uint64_t mii_time_t;
enum {
	MII_TIME_RES		= 1,
	MII_TIME_SECOND		= 1000000,
	MII_TIME_MS			= (MII_TIME_SECOND/1000),
};
mii_time_t
mii_get_time()
{
	struct timespec tim;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tim);
	uint64_t time = ((uint64_t)tim.tv_sec) * (1000000 / MII_TIME_RES) +
						tim.tv_nsec / (1000 * MII_TIME_RES);
	return time;
}


static pthread_t mii_thread;
static bool mii_thread_running = false;
static float default_fps = 60;
mii_th_fifo_t signal_fifo;

static void *
mii_thread_func(
		void *arg)
{
	mii_t *mii = (mii_t *) arg;
	mii_thread_running = true;
	__uint128_t last_cycles = mii->cycles;
	uint32_t running 	= 1;
	unsigned long target_fps_us = 1000000 / default_fps;
	long sleep_time 	= target_fps_us;

	//mii_time_t base = mii_get_time(NULL);
	uint32_t last_frame = mii->video.frame_count;
	mii_time_t last_frame_stamp = mii_get_time();
	while (mii_thread_running) {
		mii_th_signal_t sig;
		while (!mii_th_fifo_isempty(&signal_fifo)) {
			sig = mii_th_fifo_read(&signal_fifo);
			switch (sig.cmd) {
				case SIGNAL_RESET:
					mii_reset(mii, sig.data);
					break;
				case SIGNAL_STOP:
					mii_dump_run_trace(mii);
					mii_dump_trace_state(mii);
					mii->state = MII_STOPPED;
					break;
				case SIGNAL_STEP:
					mii->state = MII_STEP;
					running = 1;
					break;
				case SIGNAL_RUN:
					mii->state = MII_RUNNING;
					last_frame_stamp = mii_get_time();
					running = 1;
					break;
			}
		}
		if (mii->state != MII_STOPPED)
			mii_run(mii);

		switch (mii->state) {
			case MII_STOPPED:
				usleep(1000);
				break;
			case MII_STEP:
				if (running) {
					running--;
					mii_dump_trace_state(mii);
					usleep(1000);
					running = 1;
					if (mii->trace.step_inst)
						mii->trace.step_inst--;
					if (mii->trace.step_inst == 0)
						mii->state = MII_STOPPED;
				}
				break;
			case MII_RUNNING:
				break;
			case MII_TERMINATE:
				mii_thread_running = false;
				break;
		}

		if (mii->video.frame_count != last_frame) {
			sleep_time = target_fps_us;
			mii_time_t now = mii_get_time();
			if (mii->state == MII_RUNNING) {
				mii_time_t delta = now - last_frame_stamp;
		//		printf("frame time %d/%d sleep time %d\n",
		//					(int)delta, (int)target_fps_us,
		//					(int)target_fps_us - delta);
				sleep_time = target_fps_us - delta;
				if (sleep_time < 0)
					sleep_time = 0;
				last_frame = mii->video.frame_count;
				while (last_frame_stamp <= now)
					last_frame_stamp += target_fps_us;

				// calculate the MHz
				__uint128_t cycles = mii->cycles;
				__uint128_t delta_cycles = cycles - last_cycles;
				last_cycles = cycles;
				mii->speed_current = delta_cycles / (float)target_fps_us;
			}
			usleep(sleep_time);
		}
	}
	mii_dispose(mii);
	return NULL;
}

void
mii_thread_start(
		mii_t *mii)
{
	const mii_th_fifo_t zero = {};
	signal_fifo = zero;
	pthread_create(&mii_thread, NULL, mii_thread_func, mii);
}

struct mii_th_fifo_t*
mii_thread_get_fifo(
		struct mii_t *mii)
{
	return &signal_fifo;
}
