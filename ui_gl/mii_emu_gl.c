/*
 * mii_emu_gl.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE // for asprintf
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <locale.h>
#include <time.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xatom.h>
#include "mii_mui.h"
#include "mish.h"
#include "mii_thread.h"

#include "mii_mui.h"
#include "minipt.h"
#include "miigl_counter.h"
#define MII_ICON64_DEFINE
#include "mii-icon-64.h"

/*
 * Note: This *assumes* that the GL implementation has support for non-power-of-2
 * textures, which is not a given for older implementations. However, I think
 * (by 2024) that's a safe assumption.
 */
#define WINDOW_WIDTH 		1280
#define WINDOW_HEIGHT		720

#define POWER_OF_TWO 		0

typedef struct mii_gl_tex_t {
//	c2_rect_t 			frame;
	GLuint 				id;
}	mii_gl_tex_t;

typedef struct mii_x11_t {
	mii_mui_t			video;
	pthread_t 			cpu_thread;

	mui_drawable_t 		dr;		// drawable
	uint32_t 			dr_padded_y;

	union {
		struct {
			mii_gl_tex_t		mii_tex, mui_tex;
		};
		mii_gl_tex_t		tex[2];
	};

	c2_rect_t			video_frame; // current video frame
	float				mui_alpha;
	void *				transision_state;
	struct {
		mui_time_t			start, end;
		c2_rect_t 			from, to;
	} 					transition;

	Cursor 				cursor;
	Display *			dpy;
	Window 				win;
	long				last_button_click;
	struct {
		int 			ungrab, grab, grabbed, down;
		c2_pt_t 		pos;
	} 					mouse;
	mui_event_t 		key;

	XVisualInfo *		vis;
	Colormap 			cmap;
	XSetWindowAttributes swa;
	XWindowAttributes 	attr;
	GLXFBConfig 		fbc;
	Atom 				wm_delete_window;
	int 				width, height;
	GLXContext 			glContext;

	miigl_counter_t 	videoc, redrawc, sleepc;
} mii_x11_t;


static int gl_err = 0;
static int gl_error_handler(
		Display *dpy,
		XErrorEvent *ev)
{
	gl_err = true;
	return 0;
}

#define die(_w) { fprintf(stderr,"%s\n",_w); exit(1); }

static int
has_gl_extension(
		const char *string,
		const char *ext)
{
	if (!string || !ext)
		return false;
	int l = strlen(ext);
	while (*string) {
		char * gotit = strstr((const char *)string, ext);
		if (!gotit)
			return false;
		if (gotit && (gotit[l] == ' ' || gotit[l] == '\0'))
			return true;
		string += l;
	}
	return false;
}

c2_rect_t
c2_rect_interpolate(
		c2_rect_t *a,
		c2_rect_t *b,
		float t)
{
	c2_rect_t r = {};
	r.l = 0.5 + a->l + (b->l - a->l) * t;
	r.r = 0.5 + a->r + (b->r - a->r) * t;
	r.t = 0.5 + a->t + (b->t - a->t) * t;
	r.b = 0.5 + a->b + (b->b - a->b) * t;
	return r;
}

static c2_rect_t
_mii_get_video_position(
		mii_x11_t * ui,
		bool ui_visible )
{
	c2_rect_t r = C2_RECT(0, 0, MII_VIDEO_WIDTH, MII_VIDEO_HEIGHT);
	if (ui_visible) {
		float fac = (ui->attr.height - 38) / (float)MII_VIDEO_HEIGHT;
		c2_rect_scale(&r, fac);
		c2_rect_offset(&r,
				(ui->attr.width / 2) - (c2_rect_width(&r) / 2), 36);
	} else {
		float fac = (ui->attr.height) / (float)MII_VIDEO_HEIGHT;
		c2_rect_scale(&r, fac);
		c2_rect_offset(&r,
				(ui->attr.width / 2) - (c2_rect_width(&r) / 2),
				(ui->attr.height / 2) - (c2_rect_height(&r) / 2));
		c2_rect_inset(&r, 10, 10);
	}
	return r;
}

static void
_mii_transition(
		mii_x11_t * ui )
{
	pt_start(ui->transision_state);

	while (ui->video.transition == MII_MUI_TRANSITION_NONE)
		pt_yield(ui->transision_state);

	ui->transition.start = mui_get_time();
	ui->transition.end = ui->transition.start + (MUI_TIME_SECOND / 2);
	ui->transition.from = ui->video_frame;

	switch (ui->video.transition) {
		case MII_MUI_TRANSITION_HIDE_UI:
			ui->transition.to = _mii_get_video_position(ui, false);
			ui->video.mui_visible = true;
			break;
		case MII_MUI_TRANSITION_SHOW_UI:
			ui->transition.to = _mii_get_video_position(ui, true);
			ui->video.mui_visible = true;
			break;
	}
	while (1) {
		mui_time_t now = mui_get_time();
		float t = (now - ui->transition.start) /
						(float)(ui->transition.end - ui->transition.start);
		if (t >= 1.0f)
			break;
		switch (ui->video.transition) {
			case MII_MUI_TRANSITION_HIDE_UI:
				ui->mui_alpha = 1.0f - t;
				break;
			case MII_MUI_TRANSITION_SHOW_UI:
				ui->mui_alpha = t;
				break;
		}
		ui->video_frame = c2_rect_interpolate(
							&ui->transition.from, &ui->transition.to, t);
		pt_yield(ui->transision_state);
	}
	switch (ui->video.transition) {
		case MII_MUI_TRANSITION_HIDE_UI:
			ui->video.mui_visible = false;
			ui->mui_alpha = 0.0f;
			break;
		case MII_MUI_TRANSITION_SHOW_UI:
			ui->mui_alpha = 1.0f;
			break;
	}
	ui->video.transition = MII_MUI_TRANSITION_NONE;

	pt_end(ui->transision_state);
}

/*
 * xmodmap -pke or -pk will print the list of keycodes
 */
static bool
_mii_x11_convert_keycode(
		mii_x11_t *ui,
		KeySym sym,
		mui_event_t *out )
{
	switch (sym) {
		case XK_F1 ... XK_F12:
			out->key.key = MUI_KEY_F1 + (sym - XK_F1);
			break;
		case XK_Escape: out->key.key = MUI_KEY_ESCAPE; break;
		case XK_Left: out->key.key = MUI_KEY_LEFT; break;
		case XK_Up: out->key.key = MUI_KEY_UP; break;
		case XK_Right: out->key.key = MUI_KEY_RIGHT; break;
		case XK_Down: out->key.key = MUI_KEY_DOWN; break;
		// XK_Begin
		case XK_Insert: out->key.key = MUI_KEY_INSERT; break;
		case XK_Home: out->key.key = MUI_KEY_HOME; break;
		case XK_End: out->key.key = MUI_KEY_END; break;
		case XK_Page_Up: out->key.key = MUI_KEY_PAGEUP; break;
		case XK_Page_Down: out->key.key = MUI_KEY_PAGEDOWN; break;

		case XK_Shift_R: out->key.key = MUI_KEY_RSHIFT; break;
		case XK_Shift_L: out->key.key = MUI_KEY_LSHIFT; break;
		case XK_Control_R: out->key.key = MUI_KEY_RCTRL; break;
		case XK_Control_L: out->key.key = MUI_KEY_LCTRL; break;
		case XK_Alt_L: out->key.key = MUI_KEY_LALT; break;
		case XK_Alt_R: out->key.key = MUI_KEY_RALT; break;
		case XK_Super_L: out->key.key = MUI_KEY_LSUPER; break;
		case XK_Super_R: out->key.key = MUI_KEY_RSUPER; break;
		default:
			out->key.key = sym & 0xff;
			break;
	}
//	printf("%s %08x to %04x\n", __func__, sym, out->key.key);
	return true;
}

static int
mii_x11_init(
		struct mii_x11_t *ui )
{
	mui_t * mui = &ui->video.mui;

	if (!setlocale(LC_ALL,"") ||
			!XSupportsLocale() ||
			!XSetLocaleModifiers("@im=none"))
		return -1;

	ui->dpy = XOpenDisplay(NULL);
	if (!ui->dpy)
		die("Failed to open X display\n");
	{
		/* check glx version */
		int glx_major, glx_minor;
		if (!glXQueryVersion(ui->dpy, &glx_major, &glx_minor))
			die("[X11]: Error: Failed to query OpenGL version\n");
		if ((glx_major == 1 && glx_minor < 3) || (glx_major < 1))
			die("[X11]: Error: Invalid GLX version!\n");
	}
	{
		/* find and pick matching framebuffer visual */
		int fb_count;
		static const GLint attr[] = {
			GLX_X_RENDERABLE, True,
			GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
			GLX_RENDER_TYPE, GLX_RGBA_BIT,
			GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
			GLX_RED_SIZE, 8,
			GLX_GREEN_SIZE, 8,
			GLX_BLUE_SIZE, 8,
			GLX_ALPHA_SIZE, 8,
			None
		};
		GLXFBConfig *fbc = glXChooseFBConfig(ui->dpy,
								DefaultScreen(ui->dpy), attr, &fb_count);
		if (!fbc)
			die("[X11]: Error: failed to retrieve framebuffer configuration\n");
		{
			/* pick framebuffer with most samples per pixel */
			int fb_best = -1, best_num_samples = -1;
			for (int i = 0; i < fb_count; ++i) {
				XVisualInfo *vi = glXGetVisualFromFBConfig(ui->dpy, fbc[i]);
				if (vi) {
					int sample_buffer, samples;
					glXGetFBConfigAttrib(ui->dpy, fbc[i],
								GLX_SAMPLE_BUFFERS, &sample_buffer);
					glXGetFBConfigAttrib(ui->dpy, fbc[i],
								GLX_SAMPLES, &samples);
					if ((fb_best < 0) ||
							(sample_buffer && samples > best_num_samples))
						fb_best = i, best_num_samples = samples;
					XFree(vi);
				}
			}
			ui->fbc = fbc[fb_best];
			XFree(fbc);
			ui->vis = glXGetVisualFromFBConfig(ui->dpy, ui->fbc);
		}
	}
	{
		/* create window */
		ui->cmap = XCreateColormap(ui->dpy,
						RootWindow(ui->dpy, ui->vis->screen),
						ui->vis->visual, AllocNone);
		ui->swa.colormap = ui->cmap;
		ui->swa.background_pixmap = None;
		ui->swa.border_pixel = 0;
		ui->swa.event_mask =
			ExposureMask | KeyPressMask | KeyReleaseMask |
			ButtonPress | ButtonReleaseMask | ButtonMotionMask |
			Button1MotionMask | Button3MotionMask |
			Button4MotionMask | Button5MotionMask |
			PointerMotionMask | StructureNotifyMask | FocusChangeMask;
		ui->win = XCreateWindow(ui->dpy,
					RootWindow(ui->dpy, ui->vis->screen), 0, 0,
					WINDOW_WIDTH, WINDOW_HEIGHT, 0, ui->vis->depth, InputOutput,
					ui->vis->visual,
					CWBorderPixel | CWColormap | CWEventMask,
					&ui->swa);
		if (!ui->win)
			die("[X11]: Failed to create window\n");
		XFree(ui->vis);
		{
			char title[128];
			sprintf(title, "MII //e Emulator");
			char *telnet = getenv("MISH_TELNET_PORT");
			if (telnet)
				sprintf(title + strlen(title), " (telnet port %s)", telnet);
			else
				sprintf(title + strlen(title), " (telnet disabled)");
			XStoreName(ui->dpy, ui->win, title);
		}
		{
			Atom net_wm_icon_atom = XInternAtom(ui->dpy, "_NET_WM_ICON", False);
			XChangeProperty(ui->dpy, ui->win, net_wm_icon_atom, XA_CARDINAL,
						32, PropModeReplace,
						(unsigned char *)mii_icon64,
						sizeof(mii_icon64) / sizeof(mii_icon64[0]));
			XFlush(ui->dpy);
		}
		XMapWindow(ui->dpy, ui->win);
		ui->wm_delete_window = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
		XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete_window, 1);
	}
	/* create invisible cursor */
	{
		static XColor dummy; char data[1] = {0};
		Pixmap blank = XCreateBitmapFromData(ui->dpy, ui->win, data, 1, 1);
		if (blank == None) return 0;
		ui->cursor = XCreatePixmapCursor(ui->dpy, blank, blank, &dummy, &dummy, 0, 0);
		XFreePixmap(ui->dpy, blank);
	}
	{
		/* create opengl context */
		typedef GLXContext (*glxCreateContext)(
						Display *, GLXFBConfig, GLXContext, Bool, const int *);
		int (*old_handler)(Display *, XErrorEvent *) =
						XSetErrorHandler(gl_error_handler);
		const char *extensions_str = glXQueryExtensionsString(
						ui->dpy, DefaultScreen(ui->dpy));
		glxCreateContext create_context = (glxCreateContext)
			glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");

		gl_err = false;
		if (!has_gl_extension(extensions_str, "GLX_ARB_create_context") ||
				!create_context) {
			fprintf(stdout, "[X11]: glXCreateContextAttribARB() not found...\n");
			fprintf(stdout, "[X11]: ... using old-style GLX context\n");
			ui->glContext = glXCreateNewContext(ui->dpy, ui->fbc, GLX_RGBA_TYPE, 0, True);
		} else {
			GLint attr[] = {
				GLX_CONTEXT_MAJOR_VERSION_ARB, 2,
				GLX_CONTEXT_MINOR_VERSION_ARB, 2,
				None
			};
			ui->glContext = create_context(ui->dpy, ui->fbc, 0, True, attr);
			XSync(ui->dpy, False);
			if (gl_err || !ui->glContext) {
				attr[1] = 1;
				attr[3] = 0;
				gl_err = false;
				fprintf(stdout, "[X11] Failed to create OpenGL 3.0 context\n");
				fprintf(stdout, "[X11] ... using old-style GLX context!\n");
				ui->glContext = create_context(ui->dpy, ui->fbc, 0, True, attr);
			}
		}

		XSync(ui->dpy, False);
		XSetErrorHandler(old_handler);
		if (gl_err || !ui->glContext)
			die("[X11]: Failed to create an OpenGL context\n");
		glXMakeCurrent(ui->dpy, ui->win, ui->glContext);
	}
	{	// create the MUI 'screen' at the window size
		mui_pixmap_t* pix = &ui->dr.pix;
		pix->size.y = WINDOW_HEIGHT;
		pix->size.x = WINDOW_WIDTH;
		// annoyingly I have to make it a LOT bigger to handle that the
		// non-power-of-2 texture extension is not avialable everywhere
		// textures, which is a bit of a waste of memory, but oh well.

#if POWER_OF_TWO
		int padded_x = 1;
		int padded_y = 1;
		while (padded_x < pix->size.x)
			padded_x <<= 1;
		while (padded_y < pix->size.y)
			padded_y <<= 1;
#else
		int padded_x = pix->size.x;
		int padded_y = pix->size.y;
#endif
		pix->row_bytes = padded_x * 4;
		pix->bpp = 32;

		ui->dr_padded_y = padded_y;
		printf("MUI Padded UI size is %dx%d\n", padded_x, padded_y);

		pix->pixels = malloc(pix->row_bytes * ui->dr_padded_y);
		mui->screen_size = pix->size;
	}
	{
		XGetWindowAttributes(ui->dpy, ui->win, &ui->attr);
		ui->mui_alpha = 1.0f;
		ui->video.mui_visible = true;
		ui->video_frame = _mii_get_video_position(ui, ui->video.mui_visible);
	}
	return 0;
}

static void
mii_x11_update_mouse_card(
		mii_x11_t * ui)
{
	mii_t * mii = &ui->video.mii;
	mui_t * mui = &ui->video.mui;
	/*
	 * We can grab the mouse if it is enabled by the driver, it is in the
	 * video frame, and there is no active MUI windows (or menus).
	 */
	if (mii->mouse.enabled &&
			c2_rect_contains_pt(&ui->video_frame, &ui->mouse.pos) &&
			!(ui->video.mui_visible && mui_has_active_windows(mui))) {
		if (!ui->mouse.grabbed) {
			ui->mouse.grab = 1;
			ui->mouse.grabbed = 1;
		//	printf("Grab mouse\n");
		}
	} else {
		if (ui->mouse.grabbed) {
			ui->mouse.ungrab = 1;
			ui->mouse.grabbed = 0;
		//	printf("Ungrab mouse\n");
		}
	}
	if (!ui->mouse.grabbed)
		return;
	double x = ui->mouse.pos.x - ui->video_frame.l;
	double y = ui->mouse.pos.y - ui->video_frame.t;
	// get mouse button state
	int button = ui->mouse.down;
	// clamp coordinates inside bounds
	double vw = c2_rect_width(&ui->video_frame);
	double vh = c2_rect_height(&ui->video_frame);
	double mw = mii->mouse.max_x - mii->mouse.min_x;
	double mh = mii->mouse.max_y - mii->mouse.min_y;
	// normalize mouse coordinates
	mii->mouse.x = mii->mouse.min_x	+ (x * mw / vw) + 0.5;
	mii->mouse.y = mii->mouse.min_y	+ (y * mh / vh) + 0.5;
	mii->mouse.button = button;
}

static int
mii_x11_handle_event(
		mii_x11_t * ui,
		XEvent *evt)
{
	int quit = 0;

	/* We don't actually 'grab' as in warp the pointer, we just show/hide it
	 * dynamically when we enter/leave the video rectangle */
	if (ui->mouse.grab) {
		XDefineCursor(ui->dpy, ui->win, ui->cursor);
		ui->mouse.grab = 0;
	} else if (ui->mouse.ungrab) {
		XUndefineCursor(ui->dpy, ui->win);
		ui->mouse.ungrab = 0;
	}
	mui_t * mui = &ui->video.mui;
	mii_t * mii = &ui->video.mii;
	switch (evt->type) {
		case FocusIn:
		case FocusOut:
		//	printf("%s focus %d\n", __func__, evt->type == FocusIn);
			/* This prevents 'stale' modifiers keys when the emulator is
			 * in front and we switches window/desktop with a modifier on..
			 * this used to trigger 'phantom' modifiers still being 'on' */
			mui->modifier_keys = 0;
			break;
		case KeyRelease:
		case KeyPress: {
			int ret, down = (evt->type == KeyPress);
			KeySym *code = XGetKeyboardMapping(ui->dpy, (
						KeyCode)evt->xkey.keycode, 1, &ret);
			ui->key.type = down ? MUI_EVENT_KEYDOWN : MUI_EVENT_KEYUP;
			ui->key.key.up = 0;
			bool handled = false;
			bool converted = _mii_x11_convert_keycode(ui, *code, &ui->key);
			bool is_modifier = ui->key.key.key >= MUI_KEY_MODIFIERS &&
						ui->key.key.key <= MUI_KEY_MODIFIERS_LAST;
			if (converted) {
				// convert keycodes into a bitfields of current modifiers
				if (ui->key.key.key >= MUI_KEY_MODIFIERS &&
						ui->key.key.key <= MUI_KEY_MODIFIERS_LAST) {
					if (down)
						mui->modifier_keys |= (1 << (ui->key.key.key - MUI_KEY_MODIFIERS));
					else
						mui->modifier_keys &= ~(1 << (ui->key.key.key - MUI_KEY_MODIFIERS));
				}
				ui->key.modifiers = mui->modifier_keys;
				switch (ui->key.key.key) {
					case MUI_KEY_RSUPER:
					case MUI_KEY_LSUPER: {
						int apple = ui->key.key.key - MUI_KEY_RSUPER;
						mii_bank_t *bank = &mii->bank[MII_BANK_MAIN];
						uint8_t old = mii_bank_peek(bank, 0xc061 + apple);
						mii_bank_poke(bank, 0xc061 + apple, down ? 0x80 : 0);
						if (!!down != !!old) {
						//	printf("Apple %s %s\n", apple ? "Open" : "Close",
						//		down ? "down" : "up");
						}
					}	break;
				}
				handled = mui_handle_event(mui, &ui->key);
				// if not handled and theres a window visible, assume
				// it's a dialog and it OUGHT to eat the key
				if (!handled)
					handled = mui_window_front(mui) != NULL;
			//	printf("%s key handled %d\n", __func__, handled);
			}
			if (!handled && down && !is_modifier) {
				uint16_t mii_key = ui->key.key.key;
                char buf[32] = "";
                KeySym keysym = 0;
                if (XLookupString((XKeyEvent*)evt, buf, 32, &keysym, NULL) != NoSymbol) {
		#if 0
					printf("   lookup sym %04x %d:", (int)keysym, (int)strlen(buf));
					for (int i = 0; i < (int)strlen(buf); i++)
						printf("  %02x", buf[i]);
					printf("\n");
		#endif
					mii_key = buf[0];
                }
				switch (mii_key) {
					case MUI_KEY_DOWN: 	mii_key = 'J' - 'A' + 1; break;
					case MUI_KEY_UP: 	mii_key = 'K' - 'A' + 1; break;
					case MUI_KEY_RIGHT: mii_key = 'U' - 'A' + 1; break;
					case MUI_KEY_LEFT: 	mii_key = 'H' - 'A' + 1; break;
				}
			//	printf("key %04x %4x\n", mii_key, mui->modifier_keys);
				mii_keypress(mii, mii_key);
			}
			XFree(code);
		}	break;
		case ButtonPress:
		case ButtonRelease: {
		//	printf("%s %s button %d grabbed:%d\n", __func__,
		//			evt->type == ButtonPress ? "Down":"Up  ",
		//			evt->xbutton.button, ui->mouse.grabbed);
			switch (evt->xbutton.button) {
				case Button1: {
					ui->mouse.down = evt->type == ButtonPress;
					ui->mouse.pos.x = evt->xbutton.x;
					ui->mouse.pos.y = evt->xbutton.y;
					if (ui->video.mui_visible) {
						mui_event_t ev = {
							.type = ui->mouse.down ?
										MUI_EVENT_BUTTONDOWN :
										MUI_EVENT_BUTTONUP,
							.mouse.where = ui->mouse.pos,
							.modifiers = mui->modifier_keys, // | MUI_MODIFIER_EVENT_TRACE,
						};
						mui_handle_event(mui, &ev);
					}
					mii_x11_update_mouse_card(ui);
				}	break;
				case Button4:
				case Button5: {
				//	printf("%s wheel %d %d\n", __func__,
				//			evt->xbutton.button, ui->video.mui_visible);
					if (ui->video.mui_visible) {
						mui_event_t ev = {
							.type = MUI_EVENT_WHEEL,
							.modifiers = mui->modifier_keys,// | MUI_MODIFIER_EVENT_TRACE,
							.wheel.where = ui->mouse.pos,
							.wheel.delta = evt->xbutton.button == Button4 ? -1 : 1,
						};
						mui_handle_event(mui, &ev);
					}
				}	break;
			}
		}	break;
		case MotionNotify: {
			ui->mouse.pos.x = evt->xmotion.x;
			ui->mouse.pos.y = evt->xmotion.y;
			mii_x11_update_mouse_card(ui);
			if (ui->mouse.grabbed)
				break;
			if (ui->video.mui_visible) {
				mui_event_t ev = {
					.type = MUI_EVENT_DRAG,
					.mouse.where = ui->mouse.pos,
					.modifiers = mui->modifier_keys,
				};
				mui_handle_event(mui, &ev);
			}
		}	break;
		case ClientMessage:
			if ((Atom)evt->xclient.data.l[0] == ui->wm_delete_window)
				quit = 1;
			break;
		case KeymapNotify:
			XRefreshKeyboardMapping(&evt->xmapping);
			break;
	}
	return quit;
}

static void
mii_x11_terminate(
		mii_x11_t *ui)
{
	glXMakeCurrent(ui->dpy, 0, 0);
	glXDestroyContext(ui->dpy, ui->glContext);
	XUnmapWindow(ui->dpy, ui->win);
	XFreeColormap(ui->dpy, ui->cmap);
	XDestroyWindow(ui->dpy, ui->win);
	XCloseDisplay(ui->dpy);
}

void
mii_x11_prepare_textures(
		mii_x11_t *ui)
{
	mii_t * mii = &ui->video.mii;
//	mui_t * mui = &ui->video.mui;
	GLuint tex[2];
	glGenTextures(2, tex);
	for (int i = 0; i < 2; i++) {
		mii_gl_tex_t * t = &ui->tex[i];
		memset(t, 0, sizeof(*t));
		t->id = tex[i];
	}
	glEnable(GL_TEXTURE_2D);
	// bind the mii texture using the GL_ARB_texture_rectangle extension
	glBindTexture(GL_TEXTURE_2D, ui->mii_tex.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// disable the repeat of textures
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			MII_VRAM_WIDTH,
			MII_VRAM_HEIGHT, 0, GL_BGRA,	// note BGRA here, not RGBA
	        GL_UNSIGNED_BYTE,
	        mii->video.pixels);
	// bind the mui texture using the GL_ARB_texture_rectangle as well
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, ui->mui_tex.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			ui->dr.pix.row_bytes / 4,	// already power of two.
			ui->dr_padded_y, 0, GL_RGBA,
			GL_UNSIGNED_BYTE,
			ui->dr.pix.pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

//	printf("%s texture created %d\n", __func__, mii_apple_screen_tex);
// display opengl error
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("Error creating texture: %d\n", err);
	}
}

void
mii_x11_render(
		mii_x11_t *ui)
{
	glClearColor(
		.6f * ui->mui_alpha,
		.6f * ui->mui_alpha,
		.6f * ui->mui_alpha,
		1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_TRANSFORM_BIT);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* setup viewport/project */
	glViewport(0, 0, (GLsizei)ui->attr.width, (GLsizei)ui->attr.height);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0f, ui->attr.width, ui->attr.height, 0.0f, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	// This (was) the recommended way to handle pixel alignment in glOrtho
	// mode, but this seems to have changed -- now it looks like Linear filtering
//	glTranslatef(0.375f, 0.375f, 0.0f);
	{
		/* draw mii texture */
		glColor3f(1.0f, 1.0f, 1.0f);
		glBindTexture(GL_TEXTURE_2D, ui->mii_tex.id);
		glBegin(GL_QUADS);
		c2_rect_t r = ui->video_frame;
		glTexCoord2f(0, 0);
				glVertex2f(r.l, r.t);
		glTexCoord2f(MII_VIDEO_WIDTH / (double)MII_VRAM_WIDTH, 0);
				glVertex2f(r.r, r.t);
		glTexCoord2f(MII_VIDEO_WIDTH / (double)MII_VRAM_WIDTH,
					MII_VIDEO_HEIGHT / (double)MII_VRAM_HEIGHT);
				glVertex2f(r.r, r.b);
		glTexCoord2f(0,
					MII_VIDEO_HEIGHT / (double)MII_VRAM_HEIGHT);
				glVertex2f(r.l, r.b);
		glEnd();
		/* draw mui texture */
		if (ui->mui_alpha > 0.0f) {
			glColor4f(1.0f, 1.0f, 1.0f, ui->mui_alpha);
			glBindTexture(GL_TEXTURE_2D, ui->mui_tex.id);
			glBegin(GL_QUADS);
			glTexCoord2f(0, 0); glVertex2f(0, 0);
			glTexCoord2f(ui->attr.width / (double)(ui->dr.pix.row_bytes / 4), 0);
					glVertex2f(ui->attr.width, 0);
			glTexCoord2f(ui->attr.width / (double)(ui->dr.pix.row_bytes / 4),
						ui->attr.height / (double)(ui->dr_padded_y));
					glVertex2f(ui->attr.width, ui->attr.height);
			glTexCoord2f(0,
						ui->attr.height / (double)(ui->dr_padded_y));
					glVertex2f(0, ui->attr.height);
			glEnd();
		}
	}
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, 0);
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glPopAttrib();
}

// TODO factor this into a single table, this is dupped from mii_mui_settings.c!
static const struct {
	const char * name;
} mii_slot_driver[MII_SLOT_DRIVER_COUNT] = {
	[MII_SLOT_DRIVER_NONE] 		= { "none", },
	[MII_SLOT_DRIVER_SMARTPORT]	= { "smartport", },
	[MII_SLOT_DRIVER_DISK2] 	= { "disk2", },
	[MII_SLOT_DRIVER_MOUSE] 	= { "mouse", },
	[MII_SLOT_DRIVER_SUPERSERIAL] = { "ssc", },
	[MII_SLOT_DRIVER_ROM1MB]	= { "eecard", },
};

void
mii_ui_reconfigure_slot(
		mii_t * mii,
		mii_machine_config_t * config,
		int slot )
{
	printf("%s slot %d\n", __func__, slot);
	int i = slot-1;
	switch (config->slot[i].driver) {
		case MII_SLOT_DRIVER_SMARTPORT: {
			mii_slot_command(mii, slot,
					MII_SLOT_DRIVE_LOAD,
					(void*)config->slot[i].conf.smartport.drive[0].disk);
			mii_slot_command(mii, slot,
					MII_SLOT_DRIVE_LOAD + 1,
					(void*)config->slot[i].conf.smartport.drive[1].disk);
		}	break;
		case MII_SLOT_DRIVER_DISK2: {
			for (int di = 0; di < 2; di++) {
				mii_slot_command(mii, slot,
						MII_SLOT_DRIVE_LOAD + di,
						(void*)config->slot[i].conf.disk2.drive[di].disk);
				int wp = config->slot[i].conf.disk2.drive[di].wp;
				mii_slot_command(mii, slot,
						MII_SLOT_DRIVE_WP + di,
						(void*)&wp);
			}
		}	break;
		case MII_SLOT_DRIVER_ROM1MB: {
			mii_slot_command(mii, slot,
					MII_SLOT_DRIVE_LOAD,
					config->slot[i].conf.rom1mb.use_default ?
						"" :
						(void*)config->slot[i].conf.rom1mb.drive.disk);
		}	break;
	}
}

// load the content of the mii_machine_config_t into the initialized
// mii_t struct, similar to what mii_argv_parse does
// TODO: Change argv to modify that struct instead of the mii_t directly
static void
_mii_ui_load_config(
		mii_t * mii,
		mii_machine_config_t * config,
		uint32_t *ioFlags )
{
	if (config->no_slot_clock)
		*ioFlags |= MII_INIT_NSC;
	if (config->titan_accelerator)
		*ioFlags |= MII_INIT_TITAN;
	mii->speaker.muted = config->audio_muted;
	mii->video.color_mode = config->video_mode;
	for (int i = 0; i < 7; i++) {
		if (config->slot[i].driver == MII_SLOT_DRIVER_NONE)
			continue;
		if (config->slot[i].driver >= MII_SLOT_DRIVER_COUNT) {
			printf("%s invalid driver %d\n", __func__,
					config->slot[i].driver);
			continue;
		}
		int slot =  i + 1;
		if (mii_slot_drv_register(mii, slot,
					mii_slot_driver[config->slot[i].driver].name) < 0) {
			printf("%s failed to register driver %s\n", __func__,
					mii_slot_driver[config->slot[i].driver].name);
		}
		mii_ui_reconfigure_slot(mii, config, slot);
	}
}

void
mii_x11_reload_config(
	struct mii_x11_t *ui )
{
	mii_t * mii = &ui->video.mii;
	mii_machine_config_t * config = &ui->video.config;
	uint32_t flags = MII_INIT_DEFAULT;

	if (mii->state != MII_INIT) {
		printf("%s mii is running, terminating thread\n", __func__);
		mii->state = MII_TERMINATE;
		pthread_join(ui->cpu_thread, NULL);
		printf("%s mii terminated\n", __func__);
	}
	mii_mui_menu_slot_menu_update(&ui->video);
	printf("%s (re)loading config\n", __func__);
	mii_init(mii);
	_mii_ui_load_config(mii, config, &flags);
	mii_prepare(mii, flags);
	mii_reset(mii, true);

	/* start the CPU/emulator thread */
	ui->cpu_thread = mii_threads_start(mii);
}

mii_x11_t g_mii = {};

int
main(
		int argc,
		const char *argv[])
{
	char * conf_path;
	asprintf(&conf_path, "%s/.local/share/mii", getenv("HOME"));
	mkdir(conf_path, 0755);

	mii_x11_t * ui = &g_mii;
	mii_t * mii = &g_mii.video.mii;
	bool no_config_found = false;

	if (mii_settings_load(
					&ui->video.cf, getcwd(NULL,0), ".mii_emu_config") < 0 &&
			mii_settings_load(
					&ui->video.cf, conf_path, "mii_emu_gl") < 0) {
		asprintf(&ui->video.cf.path, "%s/.local/share/mii/mii_emu_gl",
					getenv("HOME"));
		printf("%s failed to load settings\n", argv[0]);
		ui->video.config.load_defaults = true;
		no_config_found = true;
	} else {
		mii_emu_load(&ui->video.cf, &ui->video.config);
	}
	{
		mii_init(mii);
		int idx = 1;
		uint32_t flags = MII_INIT_DEFAULT;

		if (!no_config_found) {
			// load the config we have in the UI config struct into the
			// mii config struct
			_mii_ui_load_config(mii, &ui->video.config, &flags);
		}
		int r = mii_argv_parse(mii, argc, argv, &idx, &flags);
		if (r == 0) {
			printf("mii: Invalid argument %s, skipped\n", argv[idx]);
		} else if (r == -1)
			exit(1);
		mii_prepare(mii, flags);
	}
	{
		mish_prepare(1);
		mish_set_command_parameter(MISH_FCC('m','i','i',' '), mii);
		printf("MISH_TELNET_PORT = %s\n", getenv("MISH_TELNET_PORT"));
	}
	mii_x11_init(ui);
	mui_t * mui = &ui->video.mui; // to move to a function later
	mui_init(mui);
	mui->color.clear.value = 0;
	asprintf(&mui->pref_directory, "%s/.local/share/mii", getenv("HOME"));

	mii_mui_menus_init((mii_mui_t*)ui);
	ui->video.mui_visible = 1;
	mii_mui_menu_slot_menu_update(&ui->video);

	mii_x11_prepare_textures(ui);

	// use a 60fps timerfd here as well
	int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (timerfd < 0) {
		perror(__func__);
		goto cleanup;
	}
	mii_thread_set_fps(timerfd, 60);
	/*
	 * If it is the first run, theres no config files, open the
	 * slot dialog to let the user configure some stuff himself
	 */
	if (no_config_found) {
		ui->video.config.reboot_on_load = true;
		mii_config_open_slots_dialog(&ui->video);
	}
	/* start the CPU/emulator thread */
	ui->cpu_thread = mii_threads_start(mii);

	while (mii->state != MII_INIT) {
		/* Input */
		XEvent evt;
		while (XPending(ui->dpy)) {
			XNextEvent(ui->dpy, &evt);
			if (evt.type == ClientMessage)
				goto cleanup;
			if (XFilterEvent(&evt, ui->win))
				continue;
			mii_x11_handle_event(ui, &evt);
		}
		mui_run(mui);
		bool draw = false;
		if (pixman_region32_not_empty(&mui->inval)) {
			draw = true;
			mui_draw(mui, &ui->dr, 0);
			glBindTexture(GL_TEXTURE_2D, ui->mui_tex.id);

			pixman_region32_intersect_rect(&mui->redraw, &mui->redraw,
					0, 0, ui->dr.pix.size.x, ui->dr.pix.size.y);
			int rc = 0;
			c2_rect_t *ra = (c2_rect_t*)pixman_region32_rectangles(&mui->redraw, &rc);
		//	rc = 1; ra = &C2_RECT(0, 0, mui->screen_size.x, mui->screen_size.y);
			if (rc) {
		//		printf("GL: %d rects to redraw\n", rc);
				for (int i = 0; i < rc; i++) {
					c2_rect_t r = ra[i];
		//			printf("GL: %d,%d %dx%d\n", r.l, r.t, c2_rect_width(&r), c2_rect_height(&r));
					glPixelStorei(GL_UNPACK_ROW_LENGTH, ui->dr.pix.row_bytes / 4);
					glTexSubImage2D(GL_TEXTURE_2D, 0, r.l, r.t,
							c2_rect_width(&r), c2_rect_height(&r),
							GL_BGRA, GL_UNSIGNED_BYTE,
							ui->dr.pix.pixels + (r.t * ui->dr.pix.row_bytes) + (r.l * 4));
				}
			}
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			pixman_region32_clear(&mui->redraw);
		}
		uint32_t current_frame = mii->video.frame_count;
		if (current_frame != mii->video.frame_drawn) {
			miigl_counter_tick(&ui->videoc, miigl_get_time());
			draw = true;
			mii->video.frame_drawn = current_frame;
			// update the whole texture
			glBindTexture(GL_TEXTURE_2D, ui->mii_tex.id);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					MII_VRAM_WIDTH,
					MII_VIDEO_HEIGHT, GL_RGBA,
					GL_UNSIGNED_BYTE,
					mii->video.pixels);
		}
		/* Draw */
		if (draw) {
			miigl_counter_tick(&ui->redrawc, miigl_get_time());
			XGetWindowAttributes(ui->dpy, ui->win, &ui->attr);
			glViewport(0, 0, ui->width, ui->height);
			_mii_transition(ui);
			mii_x11_render(ui);
			glFlush();
			glXSwapBuffers(ui->dpy, ui->win);
		}
		/* Wait for next frame */
		uint64_t timerv;
		if (read(timerfd, &timerv, sizeof(timerv)) < 0) {
			perror(__func__);
			goto cleanup;
		}
	#if 0
		miigl_counter_tick(&ui->sleepc, miigl_get_time());
		if (!(current_frame % 60))
			printf("VID: %3d Draw:%3d sleep:%3d\n",
					miigl_counter_get_read_size(&ui->videoc),
					miigl_counter_get_read_size(&ui->redrawc),
					miigl_counter_get_read_size(&ui->sleepc));
	#endif
	}
cleanup:
	mii_emu_save(&ui->video.cf, &ui->video.config);
	// this was already done in the mii_thread
//    mii_dispose(mii);
	mii_x11_terminate(ui);
	return 0;
}
