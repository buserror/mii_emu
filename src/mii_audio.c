/*
 * mii_audio.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mii.h"

void
mii_audio_init(
		struct mii_t *mii,
		mii_audio_sink_t *sink)
{
	sink->drv = NULL;
	sink->mii = mii;
	SLIST_INIT(&sink->source);
	mii_audio_run(sink);
}

void
mii_audio_dispose(
		mii_audio_sink_t *sink)
{
	if (sink->drv && sink->drv->stop)
		sink->drv->stop(sink);
	while (!SLIST_EMPTY(&sink->source)) {
		mii_audio_source_t *source = SLIST_FIRST(&sink->source);
		source->sink = NULL;
		SLIST_REMOVE_HEAD(&sink->source, self);
	}
}

void
mii_audio_set_driver(
		mii_audio_sink_t *sink,
		const mii_audio_driver_t *drv)
{
	printf("%s: %p\n", __func__, drv);
	sink->drv = (mii_audio_driver_t*)drv;
}

void
mii_audio_start(
		mii_audio_sink_t *sink )
{
	if (sink->drv && sink->drv->start)
		sink->drv->start(sink);
}

void
mii_audio_run(
		mii_audio_sink_t *s )
{
	if (!s || !s->mii) return;
	// if CPU speed has changed, recalculate the number of cycles per sample
	if (s->cpu_speed != s->mii->speed) {
		s->cpu_speed = s->mii->speed;
		s->clk_per_sample = ((1000000.0 * s->mii->speed) /
					(float)MII_AUDIO_FREQ) + 0.5f;
		printf("%s: %.2f cycles per sample\n", __func__, s->clk_per_sample);
	}
}

// this is here so we dont' have to drag in libm math library.
double fastPow(double a, double b) {
	union { double d; int32_t x[2]; } u = { .d = a };
	u.x[1] = (int)(b * (u.x[1] - 1072632447) + 1072632447);
	u.x[0] = 0;
	return u.d;
}

// volume from 0 to 10, sets the audio sample multiplier.
void
mii_audio_volume(
		mii_audio_source_t *s,
		float volume)
{
	if (volume < 0) volume = 0;
	else if (volume > 10) volume = 10;
	double mul = (fastPow(10.0, volume / 10.0) / 10.0) - 0.09;
	s->vol_multiplier = mul;
	s->volume = volume;
}

void
mii_audio_add_source(
		mii_audio_sink_t *sink,
		mii_audio_source_t *source)
{
	source->sink = sink;
	mii_audio_volume(source, 5);
	SLIST_INSERT_HEAD(&sink->source, source, self);
}


void
mii_audio_source_push(
		mii_audio_source_t *source,
		mii_audio_frame_t *frame)
{
	if (source->sink->drv && source->sink->drv->write)
		source->sink->drv->write(source->sink, source);
}
