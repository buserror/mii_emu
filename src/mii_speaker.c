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

#include "mii.h"
#include "mii_speaker.h"

// one frame of audio per frame of video?
#define MII_SPEAKER_FRAME_SIZE  (MII_SPEAKER_FREQ / 60)

// TODO Make some sort of driver for audio and move alsa code there
#ifdef HAS_ALSA
#include <alsa/asoundlib.h>

#define PCM_DEVICE "default"

static int
_alsa_init(
		mii_speaker_t *s)
{
	int pcm;
	unsigned int rate = 44100, channels = 1;
	snd_pcm_t *alsa;
	snd_pcm_hw_params_t *params;
	snd_pcm_uframes_t frames;

	/* Open the PCM device in playback mode */
	if ((pcm = snd_pcm_open(&alsa, PCM_DEVICE,
					SND_PCM_STREAM_PLAYBACK, 0)) < 0)
		printf("ERROR: Can't open \"%s\" PCM device. %s\n",
					PCM_DEVICE, snd_strerror(pcm));

	/* Allocate parameters object and fill it with default values*/
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_hw_params_any(alsa, params);

	/* Set parameters */
	if ((pcm = snd_pcm_hw_params_set_access(alsa, params,
					SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		printf("ERROR: Can't set interleaved mode. %s\n",
				snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_format(alsa, params,
						SND_PCM_FORMAT_S16_LE)) < 0)
		printf("ERROR: Can't set format. %s\n",
				snd_strerror(pcm));
	if ((pcm = snd_pcm_hw_params_set_channels(alsa, params, channels)) < 0)
		printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if ((pcm = snd_pcm_hw_params_set_rate_near(alsa, params, &rate, 0)) < 0)
		printf("ERROR: Can't set rate. %s\n",
				snd_strerror(pcm));
	frames = MII_SPEAKER_FRAME_SIZE;
	/* Write parameters */
	if ((pcm = snd_pcm_hw_params(alsa, params)) < 0)
		printf("ERROR: Can't set harware parameters. %s\n",
				snd_strerror(pcm));
//	printf("%s frames want %d got %ld\n",
//			__func__, MII_SPEAKER_FRAME_SIZE, frames);

	snd_pcm_sw_params_t *sw_params;
	snd_pcm_sw_params_alloca (&sw_params);
	snd_pcm_sw_params_current (alsa, sw_params);
	snd_pcm_sw_params_set_start_threshold(alsa, sw_params, frames * 4);
	snd_pcm_sw_params_set_avail_min(alsa, sw_params, frames*4);
	snd_pcm_sw_params(alsa, sw_params);

	s->fsize = frames;
	s->alsa_pcm = alsa;
	snd_pcm_prepare(s->alsa_pcm);
	return 0;
}
#endif

// Initialize the speaker with the frame size in samples
void
mii_speaker_init(
		struct mii_t * mii,
		mii_speaker_t *s)
{
	s->mii = mii;
	s->fsize = MII_SPEAKER_FRAME_SIZE;
#ifdef HAS_ALSA
	if (!s->off)
		_alsa_init(s);	// this can/will change fsize
#endif
	s->vol_multiplier = 0.2;
	s->sample = 0x8000;
	s->findex = 0;
	for (int i = 0; i < MII_SPEAKER_FRAME_COUNT; i++)
		s->frame[i].audio = calloc(sizeof(s->frame[i].audio[0]), s->fsize);
	s->frame[0].start = mii->cycles;
}

void
mii_speaker_dispose(
		mii_speaker_t *speaker)
{
#ifdef HAS_ALSA
	if (speaker->alsa_pcm)
		snd_pcm_close(speaker->alsa_pcm);
#endif
	for (int i = 0; i < MII_SPEAKER_FRAME_COUNT; i++) {
		free(speaker->frame[i].audio);
		speaker->frame[i].audio = NULL;
	}
}

// Called when $c030 is touched, place a sample at the 'appropriate' time
void
mii_speaker_click(
		mii_speaker_t *s)
{
	// if CPU speed has changed, recalculate the number of cycles per sample
	if (s->cpu_speed != s->mii->speed) {
		s->cpu_speed = s->mii->speed;
		s->clk_per_sample = ((1000000.0 /* / s->mii->speed */) /
					(float)MII_SPEAKER_FREQ) + 0.5f;
		printf("%s: %d cycles per sample\n", __func__, s->clk_per_sample);
	}
	mii_audio_frame_t *f = &s->frame[s->findex];
	// if we had stopped playing for 2 frames, restart
	if (f->start == 0 ||
			(s->mii->cycles - f->start) > (2 * s->fsize * s->clk_per_sample)) {
	//	printf("Restarting playback\n");
#ifdef HAS_ALSA
		if (s->alsa_pcm)
			snd_pcm_prepare(s->alsa_pcm);
#endif
		f->start = s->mii->cycles - (s->clk_per_sample * 8);
		f->fill = 0;
		// add a small attack to the start of the frame to soften the beeps
		// we are going to flip the sample, so we need to preemptively
		// flip the attack as well
		mii_audio_sample_t attack = s->sample ^ 0xffff;
		for (int i = 8; i >= 1; i--)
			f->audio[f->fill++] = (attack / i) * s->vol_multiplier;
		s->fplay = s->findex; // restart here
	}

	long sample_index = (s->mii->cycles - f->start) / s->clk_per_sample;
	// fill from last sample to here with the current sample
	for (; f->fill < sample_index && f->fill < s->fsize; f->fill++)
		f->audio[f->fill] = s->sample * s->vol_multiplier;

	// if we've gone past the end of the frame, switch to the next one
	if (sample_index >= s->fsize) {
		sample_index = sample_index % s->fsize;
		__uint128_t newstart = s->mii->cycles - (sample_index * s->clk_per_sample);
		s->findex = (s->findex + 1) % MII_SPEAKER_FRAME_COUNT;
		f = &s->frame[s->findex];
		f->start = newstart;
		f->fill = 0;
		// fill from start of this frame to newly calculated sample_index
		for (; f->fill < sample_index && f->fill < s->fsize; f->fill++)
			f->audio[f->fill] = s->sample * s->vol_multiplier;
	}
	s->sample ^= 0xffff;
	f->audio[sample_index] = s->sample * s->vol_multiplier;
}


// Check to see if there's a new frame to send, send it
void
mii_speaker_run(
		mii_speaker_t *s)
{
	mii_audio_frame_t *f = &s->frame[s->fplay];

	// here we check if the frame we want to play is filled, and if it's
	// been long enough since we started filling it to be sure we have
	// enough samples to play.
	// There's also the case were we stopped playing and the last frame
	// wasn't complete, in which case we flush it as well
	if (f->fill && ((s->mii->cycles - f->start) >
					(s->fsize * s->clk_per_sample * 2))) {
		s->fplaying = s->fplay;
		s->fplay = (s->fplay + 1) % MII_SPEAKER_FRAME_COUNT;
		f = &s->frame[s->fplaying];
		if (!s->muted) {
			if (s->debug_fd != -1)
				write(s->debug_fd, f->audio,
							f->fill * sizeof(s->frame[0].audio[0]));
#ifdef HAS_ALSA
			if (s->alsa_pcm) {
				int pcm;
				if ((pcm = snd_pcm_writei(s->alsa_pcm,
							f->audio, f->fill)) == -EPIPE) {
					printf("%s Underrun.\n", __func__);
					snd_pcm_recover(s->alsa_pcm, pcm, 1);
				}
			}
#endif
		}
		f->fill = 0;
	}
}

// this is here so we dont' have to drag in libm math library.
double fastPow(double a, double b) {
	union { double d; int32_t x[2]; } u = { .d = a };
	u.x[1] = (int)(b * (u.x[1] - 1072632447) + 1072632447);
	u.x[0] = 0;
	return u.d;
}

// take the volume from 0 to 10, save it, convert it to a multiplier
void
mii_speaker_volume(
		mii_speaker_t *s,
		float volume)
{
	if (volume < 0) volume = 0;
	else if (volume > 10) volume = 10;
	double mul = (fastPow(10.0, volume / 10.0) / 10.0) - 0.09;
	s->vol_multiplier = mul;
	s->volume = volume;

//	printf("audio: speaker volume set to %.3f (%.4f)\n", volume, mul);
}
