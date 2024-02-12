/*
 * miigl_counter.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <time.h>
#include <stdint.h>
#include "fifo_declare.h"

/*
 * Cheapish way of counting how many time 'stuff' happends in a second,
 * Can be used to count FPS or other things as long as the frequency is less
 * than 1024hz.
 */
DECLARE_FIFO(uint64_t, miigl_counter, 1024);
DEFINE_FIFO(uint64_t, miigl_counter);
static uint64_t
miigl_get_time()
{
	struct timespec tim;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tim);
	uint64_t time = ((uint64_t)tim.tv_sec) * (1000000 / 1) +
						tim.tv_nsec / (1000 * 1);
	return time;
}
static int
miigl_counter_tick(
		miigl_counter_t *c,
		uint64_t time)
{
	// = miigl_get_time();
	// delete stamps that are older than a second
	while (!miigl_counter_isempty(c) &&
			(time - miigl_counter_read_at(c, 0)) > 1000000) {
		miigl_counter_read(c);
	}
	long freq = miigl_counter_get_read_size(c);
	if (!miigl_counter_isfull(c))
		miigl_counter_write(c, time);
	return freq;
}
