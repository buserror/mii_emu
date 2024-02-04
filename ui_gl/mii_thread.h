/*
 * mii_thread.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include "fifo_declare.h"

enum mii_th_state_e {
	SIGNAL_RESET,
	SIGNAL_STOP,
	SIGNAL_STEP,
	SIGNAL_RUN,
};

typedef struct mii_th_signal_t {
	uint8_t 	cmd;
	uint8_t 	data;
} mii_th_signal_t;

DECLARE_FIFO(mii_th_signal_t, mii_th_fifo, 16);
DEFINE_FIFO(mii_th_signal_t, mii_th_fifo);

struct mii_t;

pthread_t
mii_threads_start(
		struct mii_t *mii);
struct mii_th_fifo_t*
mii_thread_get_fifo(
		struct mii_t *mii);
int
mii_thread_set_fps(
		int timerfd,
		float fps);
