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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// probably should wrap these into a HAVE_JOYSTICK define for non-linux
#ifndef HAVE_JOYSTICK
#define HAVE_JOYSTICK 1
#endif

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

static bool mii_thread_running = false;
static float default_fps = 60;
mii_th_fifo_t signal_fifo;

static void *
mii_thread_cpu_regulator(
		void *arg)
{
	mii_t *mii = (mii_t *) arg;
	mii_thread_running = true;
	mii_cycles_t last_cycles = mii->cycles;
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
				mii_cycles_t cycles = mii->cycles;
				mii_cycles_t delta_cycles = cycles - last_cycles;
				last_cycles = cycles;
				mii->speed_current = delta_cycles / (float)target_fps_us;
			}
			usleep(sleep_time);
		}
	}
	mii_dispose(mii);
	return NULL;
}

#if HAVE_JOYSTICK
#include <linux/joystick.h>

static void *
mii_thread_joystick(
		void *arg)
{
	int fd = open("/dev/input/js0", O_RDONLY);
	if (fd < 0) {
		printf("No joystick found\n");
		return NULL;
	}
    uint8_t axes, buttons;
    if (ioctl(fd, JSIOCGAXES, &axes) == -1 ||
				ioctl(fd, JSIOCGBUTTONS, &buttons) == -1) {
		perror(__func__);
		return NULL;
	}

	struct js_event event;
	mii_t *mii = (mii_t *)arg;
	mii->analog.v[0].value = 127;
	mii->analog.v[1].value = 127;
	do {
		ssize_t rd = read(fd, &event, sizeof(event));
		if (rd != sizeof(event)) {
			perror(__func__);
			break;
		}
		switch (event.type) {
			case JS_EVENT_BUTTON:
			//	printf("button %u %s\n", event.number, event.value ? "pressed" : "released");
				switch (event.number) {
					case 2 ... 3:
						mii_bank_poke(&mii->bank[MII_BANK_MAIN],
								0xc061 + (event.number - 2),
								event.value ? 0x80 : 0);
					break;
					case 4 ... 5:
						mii_bank_poke(&mii->bank[MII_BANK_MAIN],
								0xc061 + (event.number - 4),
								event.value ? 0x80 : 0);
						break;
				}
				break;
			case JS_EVENT_AXIS:
				switch (event.number) {
					case 0 ... 1: {// X
						uint32_t v = (event.value + 0x8000) / 256;
						if (v > 255)
							v = 255;
						mii->analog.v[event.number ? 1 : 0].value = v;
//						printf("axis %u %6d %3dx%3d\n"
//								event.number, event.value,
//								mii->analog.v[0].value, mii->analog.v[1].value);
					}	break;
				}
				break;
			default:
				/* Ignore init events. */
				break;
		}
	} while (1);
	close(fd);
	printf("Joystick thread terminated\n");
	return NULL;
}
#endif

void
mii_thread_start(
		mii_t *mii)
{
	const mii_th_fifo_t zero = {};
	signal_fifo = zero;

	pthread_t thread;
	pthread_create(&thread, NULL, mii_thread_cpu_regulator, mii);
#if HAVE_JOYSTICK
	pthread_create(&thread, NULL, mii_thread_joystick, mii);
#endif
}

struct mii_th_fifo_t*
mii_thread_get_fifo(
		struct mii_t *mii)
{
	return &signal_fifo;
}
