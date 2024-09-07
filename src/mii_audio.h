/*
 * mii_audio.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include "fifo_declare.h"
#include "bsd_queue.h"

#define MII_AUDIO_FREQ			(44100)
// circular buffer
#define MII_AUDIO_FRAME_SIZE  	4096

typedef float mii_audio_sample_t;

DECLARE_FIFO(mii_audio_sample_t, mii_audio_frame, MII_AUDIO_FRAME_SIZE);
DEFINE_FIFO(mii_audio_sample_t, mii_audio_frame);

struct mii_audio_sink_t;


enum mii_audio_source_state_e {
	MII_AUDIO_IDLE,
	MII_AUDIO_STARTING,
	MII_AUDIO_PLAYING,
	MII_AUDIO_STOPPING,
};

/*
 * A source of samples. It has a FIFO that source can fill up, and
 * it is attached to a sink that will consume the samples.
 * The state field is filed by the source itself, the audio sink uses
 * it to know when playing starts/stops for padding reasons.
 */
typedef struct mii_audio_source_t {
	struct mii_audio_sink_t *		sink;
	SLIST_ENTRY(mii_audio_source_t) self;
	uint							state;
	// mute (without having to the the volume to zero)
	float			   				volume;			// volume, 0.0 to 10.0
	float			  				vol_multiplier;	// 0.0 to 1.0
	mii_audio_frame_t 				fifo;
	uint 							last_read;
} mii_audio_source_t;

/*
 * Audio sink "pulls" samples from the sources, mix them, and send them to the
 * audio driver.
 */
typedef struct mii_audio_driver_t {
	void (*start)(
				struct mii_audio_sink_t *sink);
	void (*stop)(
				struct mii_audio_sink_t *sink);
	void (*write)(
				struct mii_audio_sink_t *sink,
				mii_audio_source_t *source);
} mii_audio_driver_t;


typedef struct mii_audio_sink_t {
	struct mii_t *					mii;
	mii_audio_driver_t * 			drv;
	uint			   				muted : 1, state;
	SLIST_HEAD(, mii_audio_source_t) source;
	// last CPU speed in MHz, to calculate clk_per_sample
	float			  				cpu_speed;
	// number of cycles per sample (at current CPU speed)
	float			   				clk_per_sample;
} mii_audio_sink_t;

void
mii_audio_init(
		struct mii_t *mii,
		mii_audio_sink_t *sink);

void
mii_audio_dispose(
		mii_audio_sink_t *sink);

void
mii_audio_set_driver(
		mii_audio_sink_t *sink,
		const mii_audio_driver_t *drv);

void
mii_audio_add_source(
		mii_audio_sink_t *sink,
		mii_audio_source_t *source);

void
mii_audio_start(
		mii_audio_sink_t *sink );
void
mii_audio_run(
		mii_audio_sink_t *sink );
// volume from 0 to 10, sets the audio sample multiplier.
void
mii_audio_volume(
		mii_audio_source_t *source,
		float volume);
