/*
 * mui_sokol_audio.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "mii.h"

#include <fcntl.h>


#define SOKOL

#ifdef SOKOL
#define SOKOL_IMPL
#include "sokol_audio.h"
#endif
#ifdef MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_WAV
#define MA_NO_MP3
#define MA_NO_FLAC
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
//#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#include "miniaudio.h"
#endif

static void
mii_sokol_audio_stop(
		mii_audio_sink_t *sink)
{
#ifdef SOKOL
	saudio_shutdown();
#endif
}

static void
mii_sokol_audio_write(
		mii_audio_sink_t *sink,
		mii_audio_source_t *source)
{
//	mii_audio_buffer_t *fifo = &source->fifo;
//	mii_audio_frame_t *frame = mii_audio_buffer_write_ptr(fifo);
}

/* This is very pedestrian, but it helps with the compiler explorer to
 * see what gets vectorized in here -- as it turns out, -O3 seems to do
 * a slightly better job at it than trying to use the generic gcc vectoring
 * extensions. So I keep the scalar code instead */
unsigned int
_mix_sample_buffer(
		const float volume,
		unsigned int num_frames,
        const float src[num_frames],
		float    dst[num_frames])
{
    #define fmin -1.0f
    #define fmax 1.0f
    while (num_frames) {
        float s = *src;
        float d = *dst;
        float m = s * volume;
        float a = d + m;
        d = a > fmax ? fmax : a < fmin ? fmin : a;
        *dst = d;
        dst++;
        src++;
        num_frames--;
    }
	return num_frames;
}

static void __attribute__((unused))
_sokol_stream_cb(
		float *  buffer,
		int num_frames,
		int num_channels,
		void *user_data)
{
	mii_audio_sink_t *sink = user_data;

	mii_audio_run(sink);

	mii_audio_source_t *s;
	// audio buffer is not zeroed.
	const uint num_samples = num_frames * num_channels;
	memset(buffer, 0, num_samples * sizeof(float));
	uint count = 0;
	float inter[num_samples];
	SLIST_FOREACH(s, &sink->source, self) {
		mii_audio_frame_t *f = &s->fifo;
		uint avail = mii_audio_frame_get_read_size(f);
		if (avail > num_samples)
			avail = num_samples;
		uint dst;
		if (sink->muted) {	// just advance read pointer
			mii_audio_frame_read_offset(f, avail);
		} else {
			/*
			 * Wait for a full buffer of audio available before we start
			 * taking it, otherwise we'd be padding the end of the frame
			 * with zeroes, creating horrible click.
			 * Once the engine is started, we're OK to take whatever is
			 * available, as we'll be padding the end of the frame with
			 * the last sample we read, which is a lot less noticeable.
			 */
			if ((s->last_read == 0 && avail >= num_samples) ||
						((s->last_read > 0 && avail > 0))) {
				dst = mii_audio_frame_read_count(f, avail, inter);
				count += dst;
				_mix_sample_buffer(s->vol_multiplier, dst, inter, buffer);
				s->last_read = dst;
			} else
				s->last_read = 0;
		}
	}
}
#ifdef MINIAUDIO
static void
_ma_stream_cb(
		ma_device* pDevice,
		void* pOutput,
		const void* pInput,
		ma_uint32 frameCount)
{
	_sokol_stream_cb(pOutput, frameCount, 1, pDevice->pUserData);
}
#endif

/*
 * these make sure the audio buffer is aligned to 32 bytes
 */
static void *
_audio_aligned_alloc(
		size_t size,
		void* user_data)
{
	void *ptr = NULL;
	posix_memalign(&ptr, 32, size);
	return ptr;
}

static void
_audio_free(
		void *ptr,
		void *user_data)
{
	free(ptr);
}

static void
mii_sokol_audio_start(
		mii_audio_sink_t *sink)
{
	printf("mii_sokol_audio_start\n");
#ifdef SOKOL
	saudio_setup(&(saudio_desc){
		.stream_userdata_cb = _sokol_stream_cb,
		.user_data = sink,
		.num_channels = 1,
		.sample_rate = 44100,
		.buffer_frames = 1024,
	//	.logger.func = slog_func,
		.allocator.alloc_fn = _audio_aligned_alloc,
		.allocator.free_fn = _audio_free,
	});
#endif
#ifdef MINIAUDIO
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
	config.playback.format   = ma_format_f32;
	config.playback.channels = 1;
	config.sampleRate        = 44100;
	config.dataCallback      = _ma_stream_cb;
	config.pUserData         = sink;
	ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		printf("%s: miniaudio failed to open playback device.\n", __func__);
		return;
	}
	if (ma_device_start(&device) != MA_SUCCESS) {
		printf("%s: miniaudio failed to start playback device.\n", __func__);
		ma_device_uninit(&device);
		return;
	}
#endif
}


static const struct mii_audio_driver_t mii_audio_driver = {
	.start = mii_sokol_audio_start,
	.stop = mii_sokol_audio_stop,
	.write = mii_sokol_audio_write,
};

void
mii_sokol_audio_init(
		mii_t *mii)
{
	mii_audio_sink_t *sink = &mii->audio;
	mii_audio_set_driver(sink, &mii_audio_driver);
}
