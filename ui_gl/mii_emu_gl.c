/*
 * mii_emu_gl.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * This is the main file for the X11/GLX version of the MII emulator
 */
#define _GNU_SOURCE // for asprintf
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <locale.h>
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
#include "mii_ssc.h"
#include "mii_mui_gl.h"
#include "miigl_counter.h"
#include "mii_sokol_audio.h"
#define MII_ICON64_DEFINE
#include "mii_icon64.h"

/*
 * Note: This *assumes* that the GL implementation has support for
 * non-power-of-2 * textures, which is not a given for older
 * implementations. However, I think (by 2024) that's a safe assumption.
 */
#define WINDOW_WIDTH 		1280
#define WINDOW_HEIGHT		720

#define MII_MUI_GL_POW2 	0

typedef struct mii_x11_t {
	mii_mui_t			video;
	pthread_t 			cpu_thread;

	Cursor 				cursor;
	Display *			dpy;
	Window 				win;

	Atom 				wmState;
	Atom 				wmStateFullscreen;
	Atom 				wm_delete_window;
	int 				width, height;
	GLXContext 			glContext;

//	miigl_counter_t 	videoc, redrawc, sleepc;
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
		case XK_Caps_Lock: out->key.key = MUI_KEY_CAPSLOCK; break;
		default:
			out->key.key = sym & 0xff;
			break;
	}
//	printf("%s %08x to %04x\n", __func__, sym, out->key.key);
	return true;
}


static GLAPIENTRY void
debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
                               GLsizei length, const GLchar* message, const void* userParam) {
    // Filter out notifications if they’re too verbose
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

    printf("OpenGL Debug Message:\n");
    printf("Source: %s\n", source == GL_DEBUG_SOURCE_API ? "API" :
                           source == GL_DEBUG_SOURCE_WINDOW_SYSTEM ? "Window System" :
                           source == GL_DEBUG_SOURCE_SHADER_COMPILER ? "Shader Compiler" :
                           source == GL_DEBUG_SOURCE_THIRD_PARTY ? "Third Party" :
                           source == GL_DEBUG_SOURCE_APPLICATION ? "Application" :
                           source == GL_DEBUG_SOURCE_OTHER ? "Other" : "Unknown");

    printf("Type: %s\n", type == GL_DEBUG_TYPE_ERROR ? "Error" :
                          type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR ? "Deprecated Behavior" :
                          type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR ? "Undefined Behavior" :
                          type == GL_DEBUG_TYPE_PORTABILITY ? "Portability" :
                          type == GL_DEBUG_TYPE_PERFORMANCE ? "Performance" :
                          type == GL_DEBUG_TYPE_MARKER ? "Marker" :
                          type == GL_DEBUG_TYPE_PUSH_GROUP ? "Push Group" :
                          type == GL_DEBUG_TYPE_POP_GROUP ? "Pop Group" :
                          type == GL_DEBUG_TYPE_OTHER ? "Other" : "Unknown");

    printf("Severity: %s\n", severity == GL_DEBUG_SEVERITY_HIGH ? "High" :
                             severity == GL_DEBUG_SEVERITY_MEDIUM ? "Medium" :
                             severity == GL_DEBUG_SEVERITY_LOW ? "Low" :
                             severity == GL_DEBUG_SEVERITY_NOTIFICATION ? "Notification" : "Unknown");

    printf("Message: %s\n", message);
    printf("---------------------------------\n");
}

static int
mii_x11_init(
		struct mii_x11_t *ui )
{
//	mui_t * mui = &ui->video.mui;

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
	GLXFBConfig 		fbc;
	XVisualInfo *		vis = NULL;
	{
		/* find and pick matching framebuffer visual */
		int fb_count;
		static const GLint attr[] = {
			GLX_X_RENDERABLE, True,
			GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
			GLX_RENDER_TYPE, GLX_RGBA_BIT,
			GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
			GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
			GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
			None
		};
		GLXFBConfig *fbc_list = glXChooseFBConfig(ui->dpy,
								DefaultScreen(ui->dpy), attr, &fb_count);
		if (!fbc_list)
			die("[X11]: Error: failed to retrieve framebuffer configuration\n");
		{
			/* pick framebuffer with most samples per pixel */
			int fb_best = -1, best_num_samples = -1;
			for (int i = 0; i < fb_count; ++i) {
				XVisualInfo *vi = glXGetVisualFromFBConfig(ui->dpy, fbc_list[i]);
				if (vi) {
					int sample_buffer, samples;
					glXGetFBConfigAttrib(ui->dpy, fbc_list[i],
								GLX_SAMPLE_BUFFERS, &sample_buffer);
					glXGetFBConfigAttrib(ui->dpy, fbc_list[i],
								GLX_SAMPLES, &samples);
					if ((fb_best < 0) ||
							(sample_buffer && samples > best_num_samples))
						fb_best = i, best_num_samples = samples;
					XFree(vi);
				}
			}
			fbc = fbc_list[fb_best];
			XFree(fbc_list);
			vis = glXGetVisualFromFBConfig(ui->dpy, fbc);
		}
	}
	{
		/* create window */
		XSetWindowAttributes swa = {
			.colormap = XCreateColormap(ui->dpy,
						RootWindow(ui->dpy, vis->screen),
						vis->visual, AllocNone),
			.background_pixmap = None,
			.border_pixel = 0,
			.event_mask =
				ExposureMask | KeyPressMask | KeyReleaseMask |
				ButtonPress | ButtonReleaseMask | ButtonMotionMask |
				Button1MotionMask | Button3MotionMask |
				Button4MotionMask | Button5MotionMask |
				PointerMotionMask | StructureNotifyMask | FocusChangeMask,
		};
		ui->win = XCreateWindow(ui->dpy,
					RootWindow(ui->dpy, vis->screen), 0, 0,
					WINDOW_WIDTH, WINDOW_HEIGHT, 0, vis->depth, InputOutput,
					vis->visual, CWBorderPixel | CWColormap | CWEventMask,
					&swa);
		if (!ui->win)
			die("[X11]: Failed to create window\n");
		XFree(vis);
		XFreeColormap(ui->dpy, swa.colormap);
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
		{
			XSizeHints *hints = XAllocSizeHints();
			hints->flags = PMinSize | PAspect;// | PMaxSize;
			hints->min_width = WINDOW_WIDTH / 2;
			hints->min_height = WINDOW_HEIGHT / 2;
			hints->max_aspect.x = WINDOW_WIDTH;
			hints->max_aspect.y = WINDOW_HEIGHT;
			hints->min_aspect.x = WINDOW_WIDTH;
			hints->min_aspect.y = WINDOW_HEIGHT;
			XSetWMNormalHints(ui->dpy, ui->win, hints);
			XFree(hints);
		}
		XMapWindow(ui->dpy, ui->win);
		ui->wm_delete_window = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
		XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete_window, 1);
		ui->wmState = XInternAtom(ui->dpy, "_NET_WM_STATE", False);
		ui->wmStateFullscreen = XInternAtom(ui->dpy, "_NET_WM_STATE_FULLSCREEN", False);
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
			ui->glContext = glXCreateNewContext(
						ui->dpy, fbc, GLX_RGBA_TYPE, 0, True);
		} else {
			GLint attr[] = {
				GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
				GLX_CONTEXT_MINOR_VERSION_ARB, 0,
				None
			};
			ui->glContext = create_context(ui->dpy, fbc, 0, True, attr);
			XSync(ui->dpy, False);
			if (gl_err || !ui->glContext) {
				attr[1] = 1;
				attr[3] = 0;
				gl_err = false;
				fprintf(stdout, "[X11] Failed to create OpenGL 3.0 context\n");
				fprintf(stdout, "[X11] ... using old-style GLX context!\n");
				ui->glContext = create_context(ui->dpy, fbc, 0, True, attr);
			}
		}
		XSync(ui->dpy, False);
		XSetErrorHandler(old_handler);
		if (gl_err || !ui->glContext)
			die("[X11]: Failed to create an OpenGL context\n");
		glXMakeCurrent(ui->dpy, ui->win, ui->glContext);
	}
	{
		glEnable(GL_DEBUG_OUTPUT);               // Enables the debug output
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);   // Makes sure errors are handled immediately

		typedef void (APIENTRY *PFNGLDEBUGMESSAGECALLBACKPROC)(
				GLDEBUGPROC callback, const void *userParam);
		PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback =
			(PFNGLDEBUGMESSAGECALLBACKPROC)glXGetProcAddressARB(
				(const GLubyte *)"glDebugMessageCallback");

		glDebugMessageCallback(debug_callback, 0); // Register the callback function
	}
	return 0;
}

static void
mui_read_clipboard(
		struct mui_t *mui)
{
	FILE *f = popen("xclip -selection clipboard -o", "r");
	if (!f) {
		perror("popen xclip");
		return;
	}
	mui_utf8_t clip = {};
	char buf[1024];
	size_t r = 0;
	do {
		r = fread(buf, 1, sizeof(buf), f);
		if (r > 0)
			mui_utf8_append(&clip, (uint8_t*)buf, r);
	} while (r > 0);
	pclose(f);
	mui_utf8_free(&mui->clipboard);
	mui->clipboard = clip;
	printf("%s %d bytes\n", __func__, mui->clipboard.count);
}

void
mii_mui_toggle_fullscreen(
	mii_mui_t * _ui )
{
	mii_x11_t * ui = (mii_x11_t*)_ui;

	XEvent xev = {};
	xev.type = ClientMessage;
	xev.xclient.window = ui->win;
	xev.xclient.message_type = ui->wmState;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 2; // _NET_WM_STATE_TOGGLE
	xev.xclient.data.l[1] = ui->wmStateFullscreen;
	xev.xclient.data.l[2] = 0; // No second property
	xev.xclient.data.l[3] = 1; // Normal source indication
	XSendEvent(ui->dpy, DefaultRootWindow(ui->dpy), False,
				SubstructureNotifyMask | SubstructureRedirectMask, &xev);
	XFlush(ui->dpy);
}

static int
mii_x11_handle_event(
		mii_x11_t * ui,
		XEvent *evt)
{
	int quit = 0;

	/* We don't actually 'grab' as in warp the pointer, we just show/hide it
	 * dynamically when we enter/leave the video rectangle */
	if (ui->video.mouse.grab) {
		XDefineCursor(ui->dpy, ui->win, ui->cursor);
		ui->video.mouse.grab = 0;
	} else if (ui->video.mouse.ungrab) {
		XUndefineCursor(ui->dpy, ui->win);
		ui->video.mouse.ungrab = 0;
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
			KeySym *code = XGetKeyboardMapping(ui->dpy,
						(KeyCode)evt->xkey.keycode, 1, &ret);
			ui->video.key.type = down ? MUI_EVENT_KEYDOWN : MUI_EVENT_KEYUP;
			ui->video.key.key.up = 0;
			bool handled = false;
			bool converted = _mii_x11_convert_keycode(ui, *code,
											&ui->video.key);
			bool is_modifier = ui->video.key.key.key >= MUI_KEY_MODIFIERS &&
						ui->video.key.key.key <= MUI_KEY_MODIFIERS_LAST;
			if (converted) {
				// convert keycodes into a bitfields of current modifiers
				if (ui->video.key.key.key >= MUI_KEY_MODIFIERS &&
						ui->video.key.key.key <= MUI_KEY_MODIFIERS_LAST) {
					if (down)
						mui->modifier_keys |= (1 <<
								(ui->video.key.key.key - MUI_KEY_MODIFIERS));
					else
						mui->modifier_keys &= ~(1 <<
								(ui->video.key.key.key - MUI_KEY_MODIFIERS));
				}
				ui->video.key.modifiers = mui->modifier_keys;
				switch (ui->video.key.key.key) {
#if 0
					case MUI_KEY_RALT:
					case MUI_KEY_LALT: {
						int apple = ui->video.key.key.key - MUI_KEY_RALT;
#else
					case MUI_KEY_RSUPER:
					case MUI_KEY_LSUPER: {
						int apple = ui->video.key.key.key - MUI_KEY_LSUPER;
#endif
						mii_bank_t *bank = &mii->bank[MII_BANK_SW];
						uint8_t old = mii_bank_peek(bank, 0xc061 + apple);
						mii_bank_poke(bank, 0xc061 + apple, down ? 0x80 : 0);
						if (!!down != !!old) {
						//	printf("Apple %s %s\n", apple ? "Open" : "Close",
						//		down ? "down" : "up");
						}
					}	break;
				}
				handled = mui_handle_event(mui, &ui->video.key);
				// if not handled and theres a window visible, assume
				// it's a dialog and it OUGHT to eat the key
				if (!handled)
					handled = mui_window_front(mui) != NULL;
			//	printf("%s key handled %d\n", __func__, handled);
			}
			if (!handled && down && !is_modifier) {
				uint16_t mii_key = ui->video.key.key.key;
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
//				printf("key %04x %4x\n", mii_key, mui->modifier_keys);
				/* control shift V is hard coded! */
				if (mii_key == 0x016 &&
						(mui->modifier_keys & MUI_MODIFIER_SHIFT) &&
						(mui->modifier_keys & MUI_MODIFIER_CTRL)) {
					printf("Paste\n");
					mui_read_clipboard(mui);
					if (mui->clipboard.count) {
						mui_utf8_add(&mui->clipboard, 0);
						// convert newlines
						for (uint i = 0; i < mui->clipboard.count; i++)
							if (mui->clipboard.e[i] == '\n')
								mui->clipboard.e[i] = '\r';
						mii_th_signal_t sig = {
							.cmd = SIGNAL_PASTE,
							.ptr = mui->clipboard.e,
						};
						mui->clipboard.e = NULL;
						mui->clipboard.count = mui->clipboard.size = 0;
						mii_th_fifo_write(mii_thread_get_fifo(mii), sig);
					}
				} else if (mii_key == 0x01b &&	/* Toggle full screen too! */
						(mui->modifier_keys & MUI_MODIFIER_SHIFT) &&
						(mui->modifier_keys & MUI_MODIFIER_CTRL)) {

					mii_mui_toggle_fullscreen(&ui->video);
				} else
					mii_keypress(mii, mii_key);
			}
			XFree(code);
		}	break;
		case ButtonPress:
		case ButtonRelease: {
		//	printf("%s %s button %d grabbed:%d\n", __func__,
		//			evt->type == ButtonPress ? "Down":"Up  ",
		//			evt->xbutton.button, ui->video.mouse.grabbed);
			switch (evt->xbutton.button) {
				case Button1: {
					ui->video.mouse.down = evt->type == ButtonPress;
					ui->video.mouse.pos.x = evt->xbutton.x;
					ui->video.mouse.pos.y = evt->xbutton.y;
					if (ui->video.mui_visible) {
						mui_event_t ev = {
							.type = ui->video.mouse.down ?
										MUI_EVENT_BUTTONDOWN :
										MUI_EVENT_BUTTONUP,
							.mouse.where = ui->video.mouse.pos,
							.modifiers = mui->modifier_keys, // | MUI_MODIFIER_EVENT_TRACE,
						};
						mui_handle_event(mui, &ev);
					}
					mii_mui_update_mouse_card(&ui->video);
				}	break;
				case Button3: {
					/* right click force the UI to reappear if it was hidden */
					if (!ui->video.mui_visible &&
								ui->video.transition.state == MII_MUI_TRANSITION_NONE) {
						ui->video.transition.state = MII_MUI_TRANSITION_SHOW_UI;
					}
				}	break;
				case Button4:
				case Button5: {
				//	printf("%s wheel %d %d\n", __func__,
				//			evt->xbutton.button, ui->video.mui_visible);
					if (ui->video.mui_visible) {
						mui_event_t ev = {
							.type = MUI_EVENT_WHEEL,
							.modifiers = mui->modifier_keys,// | MUI_MODIFIER_EVENT_TRACE,
							.wheel.where = ui->video.mouse.pos,
							.wheel.delta = evt->xbutton.button == Button4 ? -1 : 1,
						};
						mui_handle_event(mui, &ev);
					}
				}	break;
			}
		}	break;
		case ConfigureNotify:
			if (evt->xconfigure.width != ui->video.window_size.x ||
				evt->xconfigure.height != ui->video.window_size.y) {
				// Window is being resized
				// Handle the resize event here
				ui->video.window_size.x = evt->xconfigure.width;
				ui->video.window_size.y = evt->xconfigure.height;
				ui->video.video_frame = mii_mui_get_video_position(&ui->video);
				mui_resize(&ui->video.mui, &ui->video.pixels.mui,
							ui->video.window_size);
				ui->video.pixels.mui.texture.size = ui->video.window_size;
				mui_mui_gl_regenerate_ui_texture(&ui->video);
				mii_mui_update_mouse_card(&ui->video);
			}
			break;
		case MotionNotify: {
			ui->video.mouse.pos.x = evt->xmotion.x;
			ui->video.mouse.pos.y = evt->xmotion.y;
			mii_mui_update_mouse_card(&ui->video);
			if (ui->video.mouse.grabbed)
				break;
			if (ui->video.mui_visible) {
				mui_event_t ev = {
					.type = MUI_EVENT_DRAG,
					.mouse.where = ui->video.mouse.pos,
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
	XDestroyWindow(ui->dpy, ui->win);
	XCloseDisplay(ui->dpy);
}


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
		case MII_SLOT_DRIVER_SSC: {
			mii_ssc_setconf_t ssc = {
				.baud = config->slot[i].conf.ssc.baud,
				.bits = config->slot[i].conf.ssc.bits,
				.parity = config->slot[i].conf.ssc.parity,
				.stop = config->slot[i].conf.ssc.stop,
				.handshake = config->slot[i].conf.ssc.hw_handshake,
				.is_device = config->slot[i].conf.ssc.kind == MII_SSC_KIND_DEVICE,
				.is_socket = config->slot[i].conf.ssc.kind == MII_SSC_KIND_SOCKET,
				.is_pty = config->slot[i].conf.ssc.kind == MII_SSC_KIND_PTY,
				.socket_port = config->slot[i].conf.ssc.socket_port,
			};
			strncpy(ssc.device, config->slot[i].conf.ssc.device,
						sizeof(ssc.device)-1);
			mii_slot_command(mii, slot,
					MII_SLOT_SSC_SET_TTY,
					(void*)&ssc);
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
	mii->audio.muted = config->audio_muted;
	mii_audio_volume(&mii->speaker.source, config->audio_volume);
	mii->video.color_mode = config->video_mode;
	mii_video_set_mode(mii, config->video_mode);
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
					mii_slot_driver[config->slot[i].driver].driver) < 0) {
			printf("%s failed to register driver %s\n", __func__,
					mii_slot_driver[config->slot[i].driver].driver);
		}
		mii_ui_reconfigure_slot(mii, config, slot);
	}
}

// I want at least the 'silent' flags to be 'sticky'
uint32_t g_startup_flags = 0;

void
mii_x11_reload_config(
	struct mii_x11_t *ui )
{
	mii_t * mii = &ui->video.mii;
	mii_machine_config_t * config = &ui->video.config;
	uint32_t flags = MII_INIT_DEFAULT | g_startup_flags;

	if (mii->state != MII_INIT) {
		printf("%s mii is running, terminating thread\n", __func__);
		mii->state = MII_TERMINATE;
		pthread_join(ui->cpu_thread, NULL);
		printf("%s mii terminated\n", __func__);
	}
	mii_mui_menu_slot_menu_update(&ui->video);
	printf("%s (re)loading config\n", __func__);
	// if we're silent from the command line, we are *always* silent.
	mii_init(mii);
	_mii_ui_load_config(mii, config, &flags);
	if (g_startup_flags & MII_INIT_SILENT)
		mii->audio.drv = NULL;
	else
		mii_sokol_audio_init(mii);
	mii_prepare(mii, flags);
	mii_reset(mii, true);
	mii_mui_gl_prepare_textures(&ui->video);

	/* start the CPU/emulator thread */
	ui->cpu_thread = mii_threads_start(mii);
}

mii_x11_t g_mii = {};

extern const mii_machine_config_t mii_default_config;

int
main(
		int argc,
		const char *argv[])
{
	char * conf_path;
	asprintf(&conf_path, "%s/.local/share/mii", getenv("HOME"));
	mkdir(conf_path, 0755);

	mii_x11_t * ui = &g_mii;
	mii_t * 	mii = &g_mii.video.mii;
	mui_t * 	mui = &g_mii.video.mui;

	// start with a default config, as some settings might not have been saved
	ui->video.config = mii_default_config;

	bool no_config_found = false;
	char pwd[1024];
	if (!getcwd(pwd, sizeof(pwd))) {
		perror("getcwd");
		exit(1);
	}
	if (mii_settings_load(
					&ui->video.cf, pwd, ".mii_emu_config") < 0 &&
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
		if (!(flags & MII_INIT_SILENT))
			mii_sokol_audio_init(mii);
		else
			printf("Audio disabled\n");
		mii_prepare(mii, flags);
		g_startup_flags = flags;
	}
	{
		mish_prepare(1);
		mish_set_command_parameter(MISH_FCC('m','i','i',' '), mii);
		printf("MISH_TELNET_PORT = %s\n", getenv("MISH_TELNET_PORT"));
	}
	mii_x11_init(ui);
	mii_mui_init(&ui->video, C2_PT(WINDOW_WIDTH, WINDOW_HEIGHT));
	mii_mui_gl_init(&ui->video);

	asprintf(&mui->pref_directory, "%s/.local/share/mii", getenv("HOME"));

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

	if (g_startup_flags & MII_INIT_HIDE_UI)
		ui->video.transition.state = MII_MUI_TRANSITION_HIDE_UI;
	while (mii->state != MII_INIT) {
		/* Input */
		XEvent evt;
		while (XPending(ui->dpy)) {
			XNextEvent(ui->dpy, &evt);
			if (evt.type == ClientMessage) {
				if (evt.xclient.message_type ==
							XInternAtom(ui->dpy, "WM_PROTOCOLS", False) &&
						(Atom)evt.xclient.data.l[0] == ui->wm_delete_window) {
				//	printf("Window close requested!\n");
					goto cleanup;
				}
			}
			if (XFilterEvent(&evt, ui->win))
				continue;
			mii_x11_handle_event(ui, &evt);
		}
		bool draw = mii_mui_gl_run(&ui->video);
		if (draw) {
		//	miigl_counter_tick(&ui->redrawc, miigl_get_time());
		//	XGetWindowAttributes(ui->dpy, ui->win, &ui->attr);
			glViewport(0, 0, ui->video.window_size.x, ui->video.window_size.y);
			mii_mui_showhide_ui_machine(&ui->video);
			mii_mui_gl_render(&ui->video);
			glFlush();
			glXSwapBuffers(ui->dpy, ui->win);
		}
		/* Wait for next frame */
		uint64_t timerv;
		if (read(timerfd, &timerv, sizeof(timerv)) < 0) {
			perror(__func__);
			goto cleanup;
		}
		/* This delay the switch to full screen until the window was drawn */
		if (mii->video.frame_count == 30) {
			if (g_startup_flags & MII_INIT_FULLSCREEN) {
				mii_mui_toggle_fullscreen(&ui->video);
			}
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
