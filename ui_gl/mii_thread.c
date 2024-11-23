/*
 * mii_thread.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE // for asprintf
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
// probably should wrap these into a HAVE_JOYSTICK define for non-linux
#ifndef HAVE_JOYSTICK
#define HAVE_JOYSTICK 1
#endif

#include "mii.h"
#include "mii_sw.h"
#include "mii_thread.h"
#include "miigl_counter.h"
#include "mii_mui_settings.h"

static float default_fps = 60;
mii_th_fifo_t signal_fifo;


int
mii_thread_set_fps(
		int timerfd,
		float fps)
{
	default_fps = fps;
	long target_fps_us = 1000000 / default_fps;
	struct itimerspec its = {
		.it_interval = { .tv_sec = 0, .tv_nsec = target_fps_us * 1000 },
		.it_value = { .tv_sec = 0, .tv_nsec = target_fps_us * 1000 },
	};
	if (timerfd_settime(timerfd, 0, &its, NULL) < 0) {
		perror(__func__);
		return -1;
	}
	return 0;
}

static void *
mii_thread_cpu_regulator(
		void *arg)
{
	// ignore the signal, we use it to wake up the thread
	sigaction(SIGUSR1, &(struct sigaction){
		.sa_handler = SIG_IGN,
//		.sa_flags = SA_RESTART,
	}, NULL);

	mii_t *mii = (mii_t *) arg;
	uint32_t running 	= 1;

	// use a timerfd as regulation
	int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (timerfd < 0) {
		perror(__func__);
		return NULL;
	}
	mii_thread_set_fps(timerfd, default_fps);
	mii->state = MII_RUNNING;
	uint32_t last_frame = mii->video.frame_count;

//	miigl_counter_t frame_counter = {};
	uint8_t * 	paste_buffer = NULL;
	uint32_t 	paste_buffer_index = 0;
	while (running) {
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
					mii_cpu_step(mii, 1);
					running = 1;
					break;
				case SIGNAL_NEXT:
					mii_cpu_next(mii);
					running = 1;
					break;
				case SIGNAL_RUN:
					mii->state = MII_RUNNING;
					running = 1;
					break;
				case SIGNAL_PASTE: {
					if (paste_buffer)
						free(paste_buffer);
					paste_buffer = sig.ptr;
					paste_buffer_index = 0;
				}	break;
				case SIGNAL_LOADBIN: {
					mii_loadbin_conf_t * conf = sig.ptr;
					printf("%s $%04x Loadbin %s\n",
							__func__, conf->addr, conf->path);
				//	mii_load_bin(mii, sig.ptr);
				}	break;
			}
		}
		if (mii->state != MII_STOPPED)
			mii_run(mii);
		bool sleep = false;
		switch (mii->state) {
			case MII_STOPPED:
				sleep = true;
				break;
			case MII_STEP:
				sleep = true;
				if (running) {
					running--;
					mii_dump_trace_state(mii);
					running = 1;
					if (mii->trace.step_inst)
						mii->trace.step_inst--;
					if (mii->trace.step_inst == 0)
						mii->state = MII_STOPPED;
				}
				break;
			case MII_RUNNING: {
				uint32_t fi = mii->video.frame_count;
				sleep = fi != last_frame;
				last_frame = fi;
			}	break;
			case MII_TERMINATE:
				running = 0;
				break;
		}
		if (sleep) {
			if (paste_buffer) {
				mii_bank_t * sw = &mii->bank[MII_BANK_SW];
				if (paste_buffer[paste_buffer_index] == 0) {
					free(paste_buffer);
					paste_buffer = NULL;
				} else if (!(mii_bank_peek(sw, SWAKD) & 0x80)) {
					mii_keypress(mii, paste_buffer[paste_buffer_index]);
					paste_buffer_index++;
				}
			}
			uint64_t timer_v;
			// this can be interrupted and return EINTR, but we don't care
			/*size_t r = */read(timerfd, &timer_v, sizeof(timer_v));
/*
			long current_fps = miigl_counter_tick(&frame_counter,
										miigl_get_time());
			if (!(last_frame % 60)) {
				printf("FPS: %4ld\n", current_fps);
			}
*/
		}
	}
	mii_dispose(mii);	// this sets mii->state to MII_INIT
	return NULL;
}

#if HAVE_JOYSTICK
#include <linux/joystick.h>

static void
_printf_once(
		char **last,
		const char *fmt,
		...)
{
	va_list ap;
	va_start(ap, fmt);
	char * error = NULL;
	vasprintf(&error, fmt, ap);
	va_end(ap);
	if (*last != NULL && strcmp(*last, error) != 0) {
		printf("%s", error);
		free(*last);
	} else if (*last == NULL) {
		printf("%s", error);
	}
	*last = error;
}

static void *
mii_thread_joystick(
		void *arg)
{
	// ignore the signal, we use it to wake up the thread
	sigaction(SIGUSR1, &(struct sigaction){
		.sa_handler = SIG_IGN,
//		.sa_flags = SA_RESTART,
	}, NULL);
	char * last_error = NULL;
	do {
		int fd = open("/dev/input/js0", O_RDONLY);
		if (fd < 0) {
			_printf_once(&last_error, "%s No Joystick found (%s)\n",
						__func__, strerror(errno));
			goto retry;
		}
		uint8_t axes, buttons;
		char name[128];
		if (ioctl(fd, JSIOCGAXES, &axes) == -1 ||
					ioctl(fd, JSIOCGBUTTONS, &buttons) == -1 ||
					ioctl(fd, JSIOCGNAME(sizeof(name)), name) == -1) {
			_printf_once(&last_error, "%s: %s\n",
						__func__, strerror(errno));
			goto retry;
		}
		_printf_once(&last_error, "%s Joystick found: '%s'\n",
					__func__, name);
	#if 0
		printf("   %d axes, %d buttons\n", axes, buttons);
		// get and print mappings
		struct js_corr corr[axes] = {};
		if (ioctl(fd, JSIOCGCORR, corr) == -1) {
			perror(__func__);
		//	return NULL;
		}
		printf("   %d axes, %d buttons\n", axes, buttons);
		for (int i = 0; i < axes; i++) {
			printf("coor %d: type %d, prec %d: %d %d %d %d\n", i,
					corr[i].type, corr[i].prec,
					corr[i].coef[0], corr[i].coef[1],
					corr[i].coef[2], corr[i].coef[3]);
		}
		printf("Joystick thread started: %d axis, %d buttons\n", axes, buttons);
	#endif
		struct js_event event;
		mii_t *mii = (mii_t *)arg;
		mii->analog.v[0].value = 127;
		mii->analog.v[1].value = 127;
		short axis[2] = { 0, 0 };
		float reprojected[2] = { 0, 0 };
		do {
			ssize_t rd = read(fd, &event, sizeof(event));
			if (rd == -1) {
				// this is fine, we can be interrupted
				if (errno == EINTR || errno == EAGAIN)
					continue;
			}
			if (rd != sizeof(event)) {
				_printf_once(&last_error, "%s read error: %s\n",
							__func__, strerror(errno));
				break;
			}
			switch (event.type) {
				case JS_EVENT_BUTTON:
				//	printf("button %u %s\n", event.number, event.value ? "pressed" : "released");
					switch (event.number) {
						case 2 ... 3:
							mii_bank_poke(&mii->bank[MII_BANK_SW],
									0xc061 + (event.number - 2),
									event.value ? 0x80 : 0);
						break;
						case 4 ... 5:
							mii_bank_poke(&mii->bank[MII_BANK_SW],
									0xc061 + (event.number - 4),
									event.value ? 0x80 : 0);
							break;
					}
					break;
				case JS_EVENT_AXIS:
					// TODO: Use some sort of settings on which axis to use
					switch (event.number) {
						case 0 ... 1: {// X
							axis[event.number] = event.value;
						}	break;
					}
					for (int i = 0; i < 2; i++)
						reprojected[i] = axis[i] / 256;
					/*
					* This remaps the circular coordinates of the joystick to
					* a square, the 'modern' joystick I use has a top left corner of
					* -94,-94, bottom 130,130, so we need to remap the values to
					* -127,127 - 127,127 to be able to use them as a joystick
					* otherwise some games aren't happy (Wings of Fury for example)
					*
					* The formula is something I thrown together, I'm sure there's
					* a better way to do this, but there isn't many of these events
					* so it's not a big deal.
					*/
					if (1) {
						float x = (float)reprojected[0] / 256.0f;
						float y = (float)reprojected[1] / 256.0f;
						reprojected[0] = reprojected[0] + (fabs(reprojected[1]) * x);
						reprojected[1] = reprojected[1] + (fabs(reprojected[0]) * y);
					}
					for (int i = 0; i < 2; i++) {
						int32_t v = reprojected[i] + 127;
						if (v > 255)
							v = 255;
						else if (v < 0)
							v = 0;
						mii->analog.v[i].value = v;
					}
					break;
				default:
					/* Ignore init events. */
					break;
			}
		} while (1);
retry:
		close(fd);
		// sleep a bit before retrying
		sleep(2);
	} while (1);

	printf("Joystick thread terminated\n");
	return NULL;
}
#endif

pthread_t
mii_threads_start(
		mii_t *mii)
{
	const mii_th_fifo_t zero = {};
	signal_fifo = zero;

	pthread_t cpu, joystick;
	pthread_create(&cpu, NULL, mii_thread_cpu_regulator, mii);
#if HAVE_JOYSTICK
	pthread_create(&joystick, NULL, mii_thread_joystick, mii);
#endif
	return cpu;
}

struct mii_th_fifo_t*
mii_thread_get_fifo(
		struct mii_t *mii)
{
	return &signal_fifo;
}
