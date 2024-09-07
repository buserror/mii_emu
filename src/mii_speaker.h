/*
 * mii_speaker.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <stdint.h>

#include "mii_audio.h"


struct mii_t;

typedef struct mii_speaker_t {
	struct mii_t	  *	mii;
	uint8_t 			timer_id;
	mii_audio_sample_t 	sample; // current value for the speaker output
	mii_audio_source_t 	source;
	uint64_t		   	last_click_cycle, last_fill_cycle;
} mii_speaker_t;

// Initialize the speaker with the frame size in samples
void
mii_speaker_init(
		struct mii_t * mii,
		mii_speaker_t *speaker);
void
mii_speaker_dispose(
		mii_speaker_t *speaker);
// Called when $c030 is touched, place a sample at the 'appropriate' time
void
mii_speaker_click(
		mii_speaker_t *speaker);
