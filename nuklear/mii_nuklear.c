/*
 * mii_nuklear.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_sw.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_BUTTON_TRIGGER_ON_RELEASE
#include "nuklear.h"
#include "nuklear_xlib_gl3.h"
//#include "stb_image_write.h"
#include <GL/gl.h>
#include <GL/glx.h>

#define _NK_RGBA(_r,_g,_b,_a) {.r=_r,.g=_g,.b=_b,.a=_a}
static const struct nk_color style[NK_COLOR_COUNT] = {
	[NK_COLOR_TEXT] = _NK_RGBA(0, 0, 0, 255),
	[NK_COLOR_WINDOW] = _NK_RGBA(175, 175, 175, 255),
	[NK_COLOR_HEADER] = _NK_RGBA(175, 175, 175, 255),
	[NK_COLOR_BORDER] = _NK_RGBA(0, 0, 0, 255),
	[NK_COLOR_BUTTON] = _NK_RGBA(185, 185, 185, 255),
	[NK_COLOR_BUTTON_HOVER] = _NK_RGBA(170, 170, 170, 255),
	[NK_COLOR_BUTTON_ACTIVE] = _NK_RGBA(160, 160, 160, 255),
	[NK_COLOR_TOGGLE] = _NK_RGBA(150, 150, 150, 255),
	[NK_COLOR_TOGGLE_HOVER] = _NK_RGBA(120, 120, 120, 255),
	[NK_COLOR_TOGGLE_CURSOR] = _NK_RGBA(175, 175, 175, 255),
	[NK_COLOR_SELECT] = _NK_RGBA(190, 190, 190, 255),
	[NK_COLOR_SELECT_ACTIVE] = _NK_RGBA(175, 175, 175, 255),
	[NK_COLOR_SLIDER] = _NK_RGBA(190, 190, 190, 255),
	[NK_COLOR_SLIDER_CURSOR] = _NK_RGBA(80, 80, 80, 255),
	[NK_COLOR_SLIDER_CURSOR_HOVER] = _NK_RGBA(70, 70, 70, 255),
	[NK_COLOR_SLIDER_CURSOR_ACTIVE] = _NK_RGBA(60, 60, 60, 255),
	[NK_COLOR_PROPERTY] = _NK_RGBA(175, 175, 175, 255),
	[NK_COLOR_EDIT] = _NK_RGBA(150, 150, 150, 255),
	[NK_COLOR_EDIT_CURSOR] = _NK_RGBA(0, 0, 0, 255),
	[NK_COLOR_COMBO] = _NK_RGBA(175, 175, 175, 255),
	[NK_COLOR_CHART] = _NK_RGBA(160, 160, 160, 255),
	[NK_COLOR_CHART_COLOR] = _NK_RGBA(45, 45, 45, 255),
	[NK_COLOR_CHART_COLOR_HIGHLIGHT] = _NK_RGBA( 255, 0, 0, 255),
	[NK_COLOR_SCROLLBAR] = _NK_RGBA(180, 180, 180, 255),
	[NK_COLOR_SCROLLBAR_CURSOR] = _NK_RGBA(140, 140, 140, 255),
	[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = _NK_RGBA(150, 150, 150, 255),
	[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = _NK_RGBA(160, 160, 160, 255),
	[NK_COLOR_TAB_HEADER] = _NK_RGBA(180, 180, 180, 255),
};

static GLuint screen_texture;
static struct nk_image screen_nk;


#include <time.h>

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

#include <pthread.h>
#include "fifo_declare.h"

static pthread_t mii_thread;
static bool mii_thread_running = false;
//static mii_trace_t mii_trace = {};
float default_fps = 60;

enum {
	SIGNAL_RESET,
	SIGNAL_STOP,
	SIGNAL_STEP,
	SIGNAL_RUN,
};

typedef struct {
	uint8_t cmd;
	uint8_t data;
} signal_t;

DECLARE_FIFO(signal_t, signal_fifo, 16);
DEFINE_FIFO(signal_t, signal_fifo);

signal_fifo_t signal_fifo;

static void *mii_thread_func(void *arg)
{
	mii_t *mii = (mii_t *) arg;
	mii_thread_running = true;
	__uint128_t last_cycles = mii->cycles;
	uint32_t running 	= 1;
	unsigned long target_fps_us = 1000000 / default_fps;
	long sleep_time 	= target_fps_us;

	//mii_time_t base = mii_get_time(NULL);
	uint32_t last_frame = mii->video.frame_count;
	mii_time_t last_frame_stamp = mii_get_time();
	while (mii_thread_running) {
		signal_t sig;
		while (!signal_fifo_isempty(&signal_fifo)) {
			sig = signal_fifo_read(&signal_fifo);
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
				__uint128_t cycles = mii->cycles;
				__uint128_t delta_cycles = cycles - last_cycles;
				last_cycles = cycles;
				mii->speed_current = delta_cycles / (float)target_fps_us;
			}
			usleep(sleep_time);
		}
	}
	return NULL;
}

extern struct nk_font *nk_main_font;
struct nk_font *nk_mono_font = NULL;

extern const unsigned char mii_proggy_data[];
extern const unsigned int mii_proggy_size;
extern const unsigned char mii_droid_data[];
extern const unsigned int mii_droid_size;

void
mii_nuklear_init(
		mii_t *mii,
		struct nk_context *ctx)
{
	nk_style_from_table(ctx, style);

    {
        struct nk_font_atlas *atlas;
        nk_x11_font_stash_begin(&atlas);
		struct nk_font_config cfg = nk_font_config(0);
		#if 0
		nk_rune ranges[] = {
			0x0020, 0x007E,    	/* Ascii */
			0x00A1, 0x00FF,    	/* Symbols + Umlaute */
			0
		};
		#endif
		cfg.range = nk_font_default_glyph_ranges();
		cfg.oversample_h = cfg.oversample_v = 1;
		cfg.pixel_snap = true;
        struct nk_font *nkf = nk_font_atlas_add_from_memory(atlas,
					(void*)mii_proggy_data, mii_proggy_size, 20, &cfg);
        nk_x11_font_stash_end();
		nk_mono_font = nkf;
	}

	glGenTextures(1, &screen_texture);
	glBindTexture(GL_TEXTURE_2D, screen_texture);

	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			MII_VRAM_WIDTH,
			MII_VRAM_HEIGHT, 0, GL_RGBA,
	        GL_UNSIGNED_BYTE,
	        mii->video.pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//	printf("%s texture created %d\n", __func__, screen_texture);
// display opengl error
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("Error creating texture: %d\n", err);
	}
	screen_nk = nk_subimage_id(
			screen_texture, MII_VRAM_WIDTH, MII_VRAM_HEIGHT,
			nk_rect(0, 0, MII_VIDEO_WIDTH, MII_VIDEO_HEIGHT));

	/* start the thread */
	pthread_create(&mii_thread, NULL, mii_thread_func, mii);

}


extern int disk2_debug;
int show_zero_page = 0;

static void
mii_nuklear_handle_input(
		mii_t *mii,
		struct nk_context *ctx)
{
	struct nk_input *in = &ctx->input;
	if (in->keyboard.text_len) {
	//	printf("INPUT %d %s\n", in->keyboard.text_len, in->keyboard.text);
		if (in->keyboard.text_len < 4) {
			mii_keypress(mii, in->keyboard.text[0]);
		} else if (in->keyboard.text_len > 1) {
			uint32_t *raw = ((uint32_t *) in->keyboard.text);
			for (int ki = 0; ki < in->keyboard.text_len / 4; ki ++) {
				uint32_t key = (raw[ki] >> 16) & 0xffff;
				uint8_t down = raw[ki] & 0xff;
				printf("KEY %04x %s\n", key, down ? "down" : "up");
				if (down) {
					if (key == 0xffc9) { // F12
						if (nk_input_is_key_down(in, NK_KEY_CTRL)) {
							signal_t sig = {
								.cmd = SIGNAL_RESET,
								.data = nk_input_is_key_down(in, NK_KEY_SHIFT)
							};
							signal_fifo_write(&signal_fifo, sig);
							printf("RESET\n");
						}
					} else if (key == 0xffc8) { // F11
						if (nk_input_is_key_down(in, NK_KEY_CTRL)) {
							signal_t sig = {
								.cmd = SIGNAL_STOP,
							};
							signal_fifo_write(&signal_fifo, sig);
							printf("STOP\n");
						}
					} else if (key == 0xffc7) { // F10
						if (nk_input_is_key_down(in, NK_KEY_CTRL)) {
							signal_t sig = {
								.cmd = SIGNAL_STEP,
							};
							signal_fifo_write(&signal_fifo, sig);
							printf("STEP\n");
						}
					} else if (key == 0xffc6) { // F9
						if (nk_input_is_key_down(in, NK_KEY_CTRL)) {
							signal_t sig = {
								.cmd = SIGNAL_RUN,
							};
							signal_fifo_write(&signal_fifo, sig);
							printf("RUN\n");
						}
					} else if (key == 0xffc2) { // F5
						mii->speed = 1.0;
						printf("Speed Normal (1MHz)\n");
					} else if (key == 0xffc3) { // F6
						mii->speed = 4;
						printf("Speed Fast (4MHz)\n");
					}
				}
				if (key == 0xffeb || key == 0xffec) {	// super left/right
					key -= 0xffeb;
					mii_bank_t *bank = &mii->bank[MII_BANK_MAIN];
					uint8_t old = mii_bank_peek(bank, 0xc061 + key);
					mii_bank_poke(bank, 0xc061 + key, down ? 0x80 : 0);
					if (!!down != !!old) {
						printf("Apple %s %s\n", key ? "Open" : "Close",
							down ? "down" : "up");
					}
				}
			}
		}
		in->keyboard.text_len = 0;
	} else {
		signal_t sig = {.cmd = -1 };
		if (nk_input_is_key_pressed(in, NK_KEY_ENTER))
			sig.data = 0x0d;
		else if (nk_input_is_key_pressed(in, NK_KEY_BACKSPACE))
			sig.data = 0x08;
		else if (nk_input_is_key_pressed(in, NK_KEY_DEL))
			sig.data = 0x7f;
		else if (nk_input_is_key_pressed(in, NK_KEY_UP))
			sig.data = 'K' - 'A' + 1;
		else if (nk_input_is_key_pressed(in, NK_KEY_DOWN))
			sig.data = 'J' - 'A' + 1;
		else if (nk_input_is_key_pressed(in, NK_KEY_LEFT))
			sig.data = 'H' - 'A' + 1;
		else if (nk_input_is_key_pressed(in, NK_KEY_RIGHT))
			sig.data = 'U' - 'A' + 1;
		else if (nk_input_is_key_pressed(in, NK_KEY_ESCAPE))
			sig.data = 0x1b;
		if (sig.data) {
		//	signal_fifo_write(&signal_fifo, sig);
		//	printf("Key %d\n", sig.data);
			mii_keypress(mii, sig.data);
		}
	}
}

void
mii_nuklear(
		mii_t *mii,
		struct nk_context *ctx)
{
	if (mii->video.frame_count != mii->video.frame_drawn) {
		mii->video.frame_drawn = mii->video.frame_count;
		// update texture with new pixels; we only need 192 lines, the others
		// are padding for the power of 2 texture
		glBindTexture(GL_TEXTURE_2D, screen_texture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
				MII_VRAM_WIDTH,
				MII_VIDEO_HEIGHT, GL_RGBA,
				GL_UNSIGNED_BYTE,
				mii->video.pixels);
	}
	mii_nuklear_handle_input(mii, ctx);
	int height = 720 - 8;
	int width = MII_VIDEO_WIDTH * (height / (float)MII_VIDEO_HEIGHT);
	int xpos = 1280 / 2 - width / 2;
	{
		struct nk_style *s = &ctx->style;
		nk_style_push_color(ctx, &s->window.background,
				nk_rgba(0,0,0, 255));
		nk_style_push_style_item(ctx, &s->window.fixed_background,
				nk_style_item_color(nk_rgba(0,0,0, 255)));
		if (nk_begin(ctx, "Apple //e Enhanced",
				nk_rect(xpos, 0, width + 10, height + 20),
				NK_WINDOW_NO_SCROLLBAR)) {
			nk_layout_row_static(ctx, height, width, 1);
			static int was_in = -1;
			if (nk_widget_is_hovered(ctx) && mii->mouse.enabled) {
				if (was_in != 1) {
					was_in = 1;
					ctx->input.mouse.grab = 1;
				//	printf("IN\n");
				}
				struct nk_rect bounds = nk_widget_bounds(ctx);
				// normalize mouse coordinates
				double x = ctx->input.mouse.pos.x - bounds.x;
				double y = ctx->input.mouse.pos.y - bounds.y;
				// get mouse button state
				int button = ctx->input.mouse.buttons[NK_BUTTON_LEFT].down;
				// clamp coordinates inside bounds
				double vw = bounds.w;
				double vh = bounds.h;
				double mw = mii->mouse.max_x - mii->mouse.min_x;
				double mh = mii->mouse.max_y - mii->mouse.min_y;
				mii->mouse.x = mii->mouse.min_x	+ (x * mw / vw) + 0.5;
				mii->mouse.y = mii->mouse.min_y	+ (y * mh / vh) + 0.5;
				mii->mouse.button = button;
			//	printf("Mouse is %d %d\n", (int)mii->mouse.x, (int)mii->mouse.y);
			} else {
				if (was_in == 1) {
					was_in = 0;
					ctx->input.mouse.ungrab = 1;
				//	printf("OUT\n");
				}
			}
			nk_image(ctx, screen_nk);
			nk_end(ctx);
			nk_style_pop_color(ctx);
			nk_style_pop_style_item(ctx);
		}
	}

	struct nk_rect bounds = { .x = 0, .y = 0, .w = xpos, .h = height };
//		 nk_window_get_bounds(ctx);
	bool in = nk_input_is_mouse_hovering_rect(&ctx->input, bounds);
	static bool menu_open = false;

	if (in || menu_open) {
		struct nk_style *s = &ctx->style;
		nk_style_push_color(ctx, &s->window.background,
				nk_rgba(175, 175, 175, 255));
		nk_style_push_style_item(ctx, &s->window.fixed_background,
				nk_style_item_color(nk_rgba(175, 175, 175, 255)));

		if (nk_begin(ctx, "Left Bar",
						nk_rect(0, 0, xpos, height + 20),
						NK_WINDOW_NO_SCROLLBAR)) {
#if 0
            nk_menubar_begin(ctx);
            nk_layout_row_begin(ctx, NK_STATIC, 25, 2);
            nk_layout_row_push(ctx, 60);
			bool menu = false;
            if (nk_menu_begin_label(ctx, "MII", NK_TEXT_LEFT,
							nk_vec2(140, 200))) {
                static size_t prog = 40;
                static float slider = 0.5;
                static int check = nk_true;
                nk_layout_row_dynamic(ctx, 25, 1);
                if (nk_menu_item_label(ctx, "Hide", NK_TEXT_LEFT))
                    ;//show_menu = nk_false;
                if (nk_menu_item_label(ctx, "About", NK_TEXT_LEFT))
                    ;//show_app_about = nk_true;
                nk_progress(ctx, &prog, 100, NK_MODIFIABLE);
                nk_slider_float(ctx, 0.01, &slider, 1.0, 0.05);
                nk_checkbox_label(ctx, "Mute", &check);
                nk_menu_end(ctx);
				menu = true;
            }
			menu_open = menu;
            nk_menubar_end(ctx);
			nk_layout_space_end(ctx);
#endif
			int rw = xpos - 8;
		//	nk_layout_row_dynamic(ctx, 4, 1);
		//	nk_spacing(ctx, 1);

			nk_layout_row_static(ctx, 30, rw, 1);
			if (nk_button_label(ctx, "C-RESET"))
				mii_reset(mii, false);
			if (nk_button_label(ctx, "C-OA-RESET"))
				mii_reset(mii, true);
//			nk_layout_row_dynamic(ctx, 0, 1);
			nk_layout_row_begin(ctx, NK_STATIC, 30, 2);
			nk_layout_row_push(ctx, 20);
			nk_label(ctx, "V:", NK_TEXT_LEFT);
			nk_layout_row_push(ctx, rw - 20 - 4);
			const char *video_modes[] = {
				"Color",
				"Green",
				"Amber",
			};
			mii->video.color_mode = nk_combo(ctx,
							video_modes, NK_LEN(video_modes),
							mii->video.color_mode, 25, nk_vec2(80,200));
			nk_layout_space_end(ctx);

			nk_layout_row_begin(ctx, NK_STATIC, 30, 1);
			nk_layout_row_push(ctx, rw);
			const char *speed[] = {
				"1 MHz",
				"Slow",
				"Fast",
			};
			nk_label(ctx, "Speed:", NK_TEXT_LEFT);
			nk_layout_row_push(ctx, 100);
			int spi = 0;
			spi = nk_combo(ctx,
							speed, NK_LEN(speed),
							spi, 25, nk_vec2(80,200));
			nk_layout_space_end(ctx);

		}
		nk_end(ctx);
		nk_style_pop_color(ctx);
		nk_style_pop_style_item(ctx);
	}
	if ( 0 && nk_begin(ctx, "Controls",
			nk_rect(width, 0, 350, 10 + 192 * 3),
			NK_WINDOW_NO_SCROLLBAR)) {
		nk_layout_row_static(ctx, 30, 110, 2);
		if (nk_button_label(ctx, "C-RESET"))
			mii_reset(mii, false);
		if (nk_button_label(ctx, "C-OA-RESET"))
			mii_reset(mii, true);
		#if 0
		if (nk_button_label(ctx, "Screenshot")) {
			stbi_write_png("screen.png",
				MII_VIDEO_WIDTH, MII_VIDEO_HEIGHT, 4, mii->video.pixels,
				MII_VRAM_WIDTH * 4);
			printf("Screenshot taken\n");
		}
		#endif
		nk_layout_row_dynamic(ctx, 30, 4);
		nk_label(ctx, "Speed:", NK_TEXT_CENTERED);
		if (nk_option_label(ctx, "Slow", mii->speed < 0.9)) mii->speed = 0.2;
		if (nk_option_label(ctx, "1 MHz", mii->speed > .95  && mii->speed < 1.05)) mii->speed = 1.0;
		if (nk_option_label(ctx, "Fast", mii->speed > 1.1 && mii->speed < 4.1)) mii->speed = 4.0;

		nk_layout_row_dynamic(ctx, 30, 4);
		nk_label(ctx, "Video:", NK_TEXT_CENTERED);
		if (nk_option_label(ctx, "Color", mii->video.color_mode == MII_VIDEO_COLOR))
				mii->video.color_mode = MII_VIDEO_COLOR;
		if (nk_option_label(ctx, "Green", mii->video.color_mode == MII_VIDEO_GREEN))
				mii->video.color_mode = MII_VIDEO_GREEN;
		if (nk_option_label(ctx, "Amber", mii->video.color_mode == MII_VIDEO_AMBER))
				mii->video.color_mode = MII_VIDEO_AMBER;

#if 0
		nk_layout_row_dynamic(ctx, 20, 1);
        nk_style_set_font(ctx, &nk_mono_font->handle);
		struct nk_color save = ctx->style.window.background;

		ctx->style.window.background = (struct nk_color)_NK_RGBA(0, 0, 0, 255);
		struct nk_color fore = (struct nk_color)_NK_RGBA(0, 255, 0, 255);
		char label[64];
		mii_dasm_t _dasm = {};
		mii_dasm_t *dasm = &_dasm;
		mii_dasm_init(dasm, mii, 0);
		dasm->mii = mii;
		// display the last few cycles up to the PC
		for (int di = 0; di < 3; di++) {
			int pci = (mii_trace.idx + MII_PC_LOG_SIZE - 3 + di) % MII_PC_LOG_SIZE;
			dasm->pc = mii_trace.log[pci];
			mii_dasm_step(dasm, label, sizeof(label));
			if (di == 2)
				label[0] = '*';
	        nk_label_colored(ctx, label, NK_TEXT_LEFT, fore);
		}
		// and the (potentially) next instruction here
		mii_dasm_step(dasm, label, sizeof(label));
        nk_label_colored(ctx, label, NK_TEXT_LEFT, fore);

		sprintf(label, "A:%02X X:%02X Y:%02X S:%02X",
				mii->cpu.A, mii->cpu.X, mii->cpu.Y, mii->cpu.S);
        nk_label_colored(ctx, label, NK_TEXT_CENTERED, fore);
		char n[] = {'C','Z','I','D','B','V','N'};
		label[0] = 0;
		sprintf(label, "%04x ", mii->cpu.PC);
		for (int i = 0; i < 7; i++)
			sprintf(label + strlen(label), "%c%d ", n[i],
					mii->cpu.P.P[i]);
        nk_label_colored(ctx, label, NK_TEXT_CENTERED, fore);

		ctx->style.window.background = save;
		nk_style_set_font(ctx, &nk_main_font->handle);
#endif
        nk_layout_row_static(ctx, 30, 30, 3);

		if (nk_button_symbol(ctx, NK_SYMBOL_RECT_SOLID)) {
			signal_fifo_write(&signal_fifo, (signal_t){.cmd = SIGNAL_STOP});
		}
		if (nk_button_symbol(ctx, NK_SYMBOL_PLUS)) {
			signal_fifo_write(&signal_fifo, (signal_t){.cmd = SIGNAL_STEP});
		}
		if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_RIGHT)) {
			signal_fifo_write(&signal_fifo, (signal_t){.cmd = SIGNAL_RUN});
		}
		nk_layout_row_dynamic(ctx, 20, 3);
		{
			char label[32];
			sprintf(label, "CPU: %.1fMHz", mii->speed_current);
			nk_label(ctx, label, NK_TEXT_CENTERED);
		}
#if 0
		nk_layout_row_dynamic(ctx, 20, 2);
		nk_checkbox_label(ctx, "Disk II Debug", &disk2_debug);
		nk_checkbox_label(ctx, "Slowmo", &mii_SLOW);
//		nk_checkbox_label(ctx, "Zero Page", &show_zero_page);
#endif
		nk_end(ctx);
	}
	if (show_zero_page) {
		if (nk_begin(ctx, "Zero Page",
				nk_rect(0, 10 + 192 * 3, 600, 10 + 16 * 20),
					0 )) {
			nk_layout_row_dynamic(ctx, 20, 1);
			nk_style_set_font(ctx, &nk_mono_font->handle);
			struct nk_color fore = (struct nk_color)_NK_RGBA(0, 255, 0, 255);
			uint8_t *zp = mii->bank[0].mem;
			char label[128];
			for (int ri = 0; ri < (256 / 16); ri++) {
				sprintf(label, "%02x: ", ri * 16);
				for (int ci = 0; ci < 16; ci++) {
					sprintf(label + strlen(label), "%02X ",
						zp[ri * 16 + ci]);
				}
		        nk_label_colored(ctx, label, NK_TEXT_LEFT, fore);
			}
			nk_style_set_font(ctx, &nk_main_font->handle);
			nk_end(ctx);
		}
	}
}
