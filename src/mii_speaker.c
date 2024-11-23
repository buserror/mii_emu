/*
 * mii_speaker.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "mii.h"
#include "mii_speaker.h"


#define MII_SPEAKER_BASE_SAMPLE 	0.5f
#define MII_SPEAKER_RAMP_ON 		16
#define MII_SPEAKER_RAMP_OFF 		128

int mii_speaker_debug = 0;
int mii_speaker_debug_fd = -1;

static inline void
_mii_speaker_write(
		mii_audio_frame_t *f,
		mii_audio_sample_t sample,
		bool start, bool stop)
{
	mii_audio_frame_write(f, sample);
	static int mii_speaker_debug_fd = -1;
	if (!mii_speaker_debug) {
		if (mii_speaker_debug_fd != -1)
			close(mii_speaker_debug_fd);
		mii_speaker_debug_fd = -1;
		return;
	}
	if (start) {
		if (mii_speaker_debug_fd != -1)
			close(mii_speaker_debug_fd);
		mii_speaker_debug_fd = open("speaker.raw",
						O_CREAT|O_TRUNC|O_WRONLY, 0644);
	}
	if (mii_speaker_debug_fd != -1)
		write(mii_speaker_debug_fd, &sample, sizeof(sample));
#if 0
	if (stop) {
		close(mii_speaker_debug_fd);
		mii_speaker_debug_fd = -1;
	}
	#endif
}

static void
_mii_speaker_pad(
		mii_speaker_t *s,
		bool click)
{
	mii_audio_frame_t *f = &s->source.fifo;
	uint64_t now 	= s->mii->cpu.total_cycle;

//	printf("pad: %d %d\n", s->play_state, click);
	do {
		switch(s->source.state) {
			case MII_AUDIO_IDLE:
				// nothing to do
				if (click) {
					s->source.state = MII_AUDIO_STARTING;
					continue;
				} else
					break;
			case MII_AUDIO_STARTING: {
				// we are starting, so we need to pad the start of the frame
				// with a small attack to soften the beeps
//				printf("%s started avail W:%5d\n",
//					__func__, mii_audio_frame_get_write_size(f));
				mii_audio_sample_t attack = -s->sample;
				for (int i = MII_SPEAKER_RAMP_ON; i >= 1; i--)
					_mii_speaker_write(f, (attack / i),
							i == MII_SPEAKER_RAMP_ON, false);
				s->source.state = MII_AUDIO_PLAYING;
				s->last_fill_cycle = now;
			}	break;
			case MII_AUDIO_PLAYING: {
				uint64_t last_click = (now - s->last_click_cycle) /
											s->source.sink->clk_per_sample;
				if (last_click > (MII_AUDIO_FREQ / 64)) {
//					printf("%s stopping\n", __func__);
					s->source.state = MII_AUDIO_STOPPING;
			//		continue;
				}
				// write padding
				uint64_t fill_amount = (now - s->last_fill_cycle) /
											s->source.sink->clk_per_sample;
//				printf("play %d pad %5ld/%5ld rds %5d\n", click,
//						fill_amount, last_click,
//						mii_audio_frame_get_read_size(f));
				while (fill_amount > 0 && !mii_audio_frame_isfull(f)) {
					_mii_speaker_write(f, s->sample, false, false);
					fill_amount--;
				}
				s->last_fill_cycle = now;
//				printf("  fifo state W:%4d R:%4d\n",
//						mii_audio_frame_get_write_size(f),
//						mii_audio_frame_get_read_size(f));
			}	break;
			case MII_AUDIO_STOPPING: {
//				printf("%s stopped\n", __func__);
				// we are stopping, so we need to pad the end of the frame
				// with a small tailoff to soften the beeps
				mii_audio_sample_t tail = s->sample;
				for (int i = 1; i <= MII_SPEAKER_RAMP_OFF; i++)
					_mii_speaker_write(f, tail / i,
							false, i == MII_SPEAKER_RAMP_OFF);
				s->source.state = MII_AUDIO_IDLE;
			}	break;
		}
		break;
	} while(1);

	if (click) {
		s->last_click_cycle = now;
		s->sample = -s->sample;
		_mii_speaker_write(f, s->sample, false, false);
	}
}

static uint64_t
_mii_speaker_timer_cb(
		mii_t * mii,
		void * param )
{
	mii_speaker_t *s = (mii_speaker_t*)param;
	_mii_speaker_pad(s, false);
	return s->source.state == MII_AUDIO_IDLE ? 0 :
				(MII_AUDIO_FRAME_SIZE / 2) * s->source.sink->clk_per_sample;
}

// Initialize the speaker with the frame size in samples
void
mii_speaker_init(
		struct mii_t * mii,
		mii_speaker_t *s)
{
	s->mii = mii;
	s->sample = -MII_SPEAKER_BASE_SAMPLE;
	s->source.state = MII_AUDIO_IDLE;
	mii_audio_add_source(&mii->audio, &s->source);
	// disabled at start...
	s->timer_id = mii_timer_register(mii,
			_mii_speaker_timer_cb, s, 0, __func__);
}

void
mii_speaker_dispose(
		mii_speaker_t *s)
{
	mii_timer_set(s->mii, s->timer_id, 0);
}


// Called when $c030 is touched, place a sample at the 'appropriate' time
void
mii_speaker_click(
		mii_speaker_t *s)
{
	_mii_speaker_pad(s, true);
	mii_timer_set(s->mii, s->timer_id,
		(MII_AUDIO_FRAME_SIZE / 2) * s->source.sink->clk_per_sample);
}
