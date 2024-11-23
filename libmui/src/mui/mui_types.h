/*
 * mui_types.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "c2_arrays.h"

#ifdef __wasm__
typedef unsigned int uint;
#endif

#if 0 // only use to debug queue macros; do not enable
#define _KERNEL
#define INVARIANTS
#define	QUEUE_MACRO_DEBUG_TRACE
#define panic(...) \
	do { \
		fprintf(stderr, "PANIC: %s:%d\n", __func__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		*((int*)0) = 0xdead; \
	} while(0)
#endif

#include "bsd_queue.h"
#include "stb_ttc.h"

/* Four Character Constants are used everywhere. Wish this had become a standard,
 * as it is so handy -- but nope, thus the macro. Annoyingly, the little-
 * endianess of them makes it a pain to do a printf() with them, this is why
 * the values are reversed here.
 */
#include <ctype.h>
#define FCC(_a,_b,_c,_d) (((_d)<<24)|((_c)<<16)|((_b)<<8)|(_a))
/* These are made to allow FCC to have a numerical index, this is
 * mostly used for radio button, menu items and so on */
#define FCC_MASK		FCC(0xff,0xff,0xff,0)
/* number of bits to shift to get the fourth character of _fcc */
#define FCC_SHIFT(_fcc) ((_fcc)>>(ffs(~FCC_MASK)-1) & 0xff)
/* extract the index number of a fcc of type abcX where X is '0' - '9' */
#define FCC_INDEX(_fcc) (isdigit(FCC_SHIFT(_fcc)) ? \
							((FCC_SHIFT(_fcc)) - '0') : 0)
#define FCC_INDEXED(_fcc, _idx) \
			((_fcc & FCC_MASK) | ('0'+((_idx) & 0xff)) << (ffs(~FCC_MASK)-1))

typedef enum mui_event_e {
	MUI_EVENT_KEYUP = 0,
	MUI_EVENT_KEYDOWN,
	MUI_EVENT_BUTTONUP,
	MUI_EVENT_BUTTONDOWN,
	MUI_EVENT_BUTTONDBL,	// double click
	MUI_EVENT_WHEEL,
	MUI_EVENT_DRAG,
	// the following ones aren't supported yet
	MUI_EVENT_TEXT,			// UTF8 sequence [TODO]
	MUI_EVENT_MOUSEENTER,
	MUI_EVENT_MOUSELEAVE,
	MUI_EVENT_RESIZE,
	MUI_EVENT_CLOSE,

	MUI_EVENT_COUNT,
	// left, middle, right buttons for clicks
	MUI_EVENT_BUTTON_MAX = 3,
} mui_event_e;

typedef enum mui_key_e {
	// these are ASCII
	MUI_KEY_ESCAPE 	= 0x1b,
	MUI_KEY_SPACE 	= 0x20,
	MUI_KEY_RETURN 	= 0x0d,
	MUI_KEY_TAB 	= 0x09,
	MUI_KEY_BACKSPACE = 0x08,
	// these are not ASCII
	MUI_KEY_LEFT 	= 0x80,	MUI_KEY_UP,	MUI_KEY_RIGHT,	MUI_KEY_DOWN,
	MUI_KEY_INSERT,		MUI_KEY_DELETE,
	MUI_KEY_HOME,		MUI_KEY_END,
	MUI_KEY_PAGEUP,		MUI_KEY_PAGEDOWN,
	MUI_KEY_MODIFIERS = 0x90,
	MUI_KEY_LSHIFT 	= MUI_KEY_MODIFIERS,
	MUI_KEY_RSHIFT,
	MUI_KEY_LCTRL,		MUI_KEY_RCTRL,
	MUI_KEY_LALT,		MUI_KEY_RALT,
	MUI_KEY_LSUPER,		MUI_KEY_RSUPER,
	MUI_KEY_CAPSLOCK,
	MUI_KEY_MODIFIERS_LAST,
	MUI_KEY_F1 = 0x100,	MUI_KEY_F2,	MUI_KEY_F3,	MUI_KEY_F4,
	MUI_KEY_F5,			MUI_KEY_F6,	MUI_KEY_F7,	MUI_KEY_F8,
	MUI_KEY_F9,			MUI_KEY_F10,MUI_KEY_F11,MUI_KEY_F12,
} mui_key_e;

typedef enum mui_modifier_e {
	MUI_MODIFIER_LSHIFT 	= (1 << (MUI_KEY_LSHIFT - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_RSHIFT 	= (1 << (MUI_KEY_RSHIFT - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_LCTRL 		= (1 << (MUI_KEY_LCTRL - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_RCTRL 		= (1 << (MUI_KEY_RCTRL - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_LALT 		= (1 << (MUI_KEY_LALT - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_RALT 		= (1 << (MUI_KEY_RALT - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_RSUPER 	= (1 << (MUI_KEY_RSUPER - MUI_KEY_MODIFIERS)),
	MUI_MODIFIER_LSUPER 	= (1 << (MUI_KEY_LSUPER - MUI_KEY_MODIFIERS)),

	// special flag, trace events handling for this event
	MUI_MODIFIER_EVENT_TRACE= (1 << 15),
	MUI_MODIFIER_SHIFT 		= (MUI_MODIFIER_LSHIFT | MUI_MODIFIER_RSHIFT),
	MUI_MODIFIER_CTRL 		= (MUI_MODIFIER_LCTRL | MUI_MODIFIER_RCTRL),
	MUI_MODIFIER_ALT 		= (MUI_MODIFIER_LALT | MUI_MODIFIER_RALT),
	MUI_MODIFIER_SUPER 		= (MUI_MODIFIER_LSUPER | MUI_MODIFIER_RSUPER),
} mui_modifier_e;

/*
 * The following constants are in UTF8 format, and relate to glyphs in
 * the TTF fonts
 */
/* These are from the icon font */
#define MUI_ICON_FOLDER 		""
#define MUI_ICON_FOLDER_OPEN 	""
#define MUI_ICON_ROOT 			""
#define MUI_ICON_FILE 			""
#define MUI_ICON_POPUP_ARROWS	""
#define MUI_ICON_HOME			""
#define MUI_ICON_SBAR_UP		""
#define MUI_ICON_SBAR_DOWN		""
#define MUI_ICON_FLOPPY5		""
#define MUI_ICON_HARDDISK		""

/* These are specific to our custom version of the Charcoal System font */
#define MUI_GLYPH_APPLE 		""	// solid apple
#define MUI_GLYPH_OAPPLE 		""	// open apple
#define MUI_GLYPH_COMMAND 		""
#define MUI_GLYPH_OPTION 		""
#define MUI_GLYPH_CONTROL 		""
#define MUI_GLYPH_SHIFT 		""
#define MUI_GLYPH_TICK			""	// tickmark for menus
#define MUI_GLYPH_SUBMENU		"▶"	// custom, for the hierarchical menus
#define MUI_GLYPH_IIE			""	// custom, IIe glyph
#define MUI_GLYPH_POPMARK		"▼"	// custom, popup menu marker
#define MUI_GLYPH_CLOSEBOX		"☐"	// custom, close box
#define MUI_GLYPH_CLICKBOX		"☒"	// custom, clicked (close, zoom?) box
/* These are also from Charcoal System font (added to the original) */
#define MUI_GLYPH_F1			""
#define MUI_GLYPH_F2			""
#define MUI_GLYPH_F3			""
#define MUI_GLYPH_F4			""
#define MUI_GLYPH_F5			""
#define MUI_GLYPH_F6			""
#define MUI_GLYPH_F7			""
#define MUI_GLYPH_F8			""
#define MUI_GLYPH_F9			""
#define MUI_GLYPH_F10			""
#define MUI_GLYPH_F11			""
#define MUI_GLYPH_F12			""
#define MUI_GLYPH_ESC			"☑"

