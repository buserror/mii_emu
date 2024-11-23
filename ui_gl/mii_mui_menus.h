/*
 * mui_mui_menus.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "mui.h"

extern mui_menu_item_t m_apple_menu[];
extern mui_menu_item_t m_file_menu[];
extern mui_menu_item_t m_video_menu[];
extern mui_menu_item_t m_audio_menu[];
extern mui_menu_item_t m_machine_menu[];
extern mui_menu_item_t m_cpu_menu[];

#ifdef MII_MUI_MENUS_C
#define MII_MUI_APPLE_LOGO_DEFINE
#include "mii_mui_apple_logo.h"
mui_menu_item_t m_color_apple_menu[] = {
	{ .color_icon = mii_mui_apple_logo, .is_menutitle = 1, },
	{ .title = "About MII…",
			.uid = FCC('a','b','o','t') },
//	{ .title = "-", },
	{ },
};

mui_menu_item_t m_file_menu[] = {
	/* these two don't do anything for the moment */
	{ .title = "No Drives Installed…",
			.disabled = 1,
			.uid = FCC('l','d','i','m'),
			.kcombo = MUI_GLYPH_SHIFT MUI_GLYPH_COMMAND "N" },
	{ .title = "-", },
	{ .title = "Load & Run Binary…",
			.disabled = 1,
			.uid = FCC('l','r','u','n'), },
	{ .title = "-" },
	{ .title = "Quit",
			.uid = FCC('q','u','i','t'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL|MUI_MODIFIER_RSUPER, 'Q'),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_COMMAND "Q" },
	{ },
};
mui_menu_item_t m_video_menu[] = {
	{ .title = "Show/Hide Menus",
			.uid = FCC('s','h','m','b'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL, MUI_KEY_F1),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_F1 },
	{ .title = "Toggle Fullscreen",	// Handled in the X11 layer, not MUI
			.uid = FCC('t','g','l','F'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL|MUI_MODIFIER_RSHIFT, '\x1b'),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_SHIFT MUI_GLYPH_ESC },
	{ .title = "Intl. Font Switch",
			.uid = FCC('a','l','t','f'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL, MUI_KEY_F10),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_F10 },
	{ .title = "Cycle Color Mode",
			.uid = FCC('v','d','C','l'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL, MUI_KEY_F11),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_F11 },
	{ .title = "-", },
	{ .title = "Color NTSC",
			.mark = MUI_GLYPH_TICK,
			.uid = FCC('v','d','c','0') },
	{ .title = "Color NTSC (Alt)",
			.mark = MUI_GLYPH_TICK,
			.uid = FCC('v','d','c','1') },
	{ .title = "Color Mega2",
			.mark = MUI_GLYPH_TICK,
			.uid = FCC('v','d','c','2') },
	{ .title = "Green",
			.uid = FCC('v','d','c','3') },
	{ .title = "Amber",
			.uid = FCC('v','d','c','4') },
	{ },
};
mui_menu_item_t m_audio_menu[] = {
	{ .title = "Mute",
			.uid = FCC('a','u','d','0'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL, MUI_KEY_F10),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_F10 },
	{ .title = "-", },
	{ .title = "Louder",
			.uid = FCC('a','u','d','+'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL|MUI_MODIFIER_RSHIFT, '+'),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_SHIFT "+" },
	{ .title = "Quieter",
			.uid = FCC('a','u','d','-'),
			// shift-minus is _ (underscore)
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL|MUI_MODIFIER_RSHIFT, '_'),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_SHIFT "-" },
	{ },
};
mui_menu_item_t m_machine_menu[] = {
	{ .title = MUI_GLYPH_OAPPLE "-Control-Reset",
			.uid = FCC('c','r','e','s'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL|MUI_MODIFIER_RSHIFT, MUI_KEY_F12),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_SHIFT MUI_GLYPH_F12 },
	{ .title = "Control-Reset",
			.uid = FCC('r','e','s','t'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_RCTRL, MUI_KEY_F12),
			.kcombo = MUI_GLYPH_CONTROL MUI_GLYPH_F12 },
	{ .title = "-", },
	{ .title = "Configure Slots…",
			.uid = FCC('s','l','o','t') },
	{ .title = "Joystick…",
			.disabled = 1,
			.uid = FCC('j','o','y','s') },
	{ .title = "-", },
	{ .title = "Video",
			.kcombo = MUI_GLYPH_SUBMENU,
			.submenu = m_video_menu },
	{ .title = "Audio",
			.kcombo = MUI_GLYPH_SUBMENU,
			.uid = FCC('s','a','u','d'),
			.submenu = m_audio_menu },
	{ },
};
mui_menu_item_t m_cpu_menu[] = {
	{ .title = "Normal: 1MHz",
			.uid = FCC('m','h','z','1'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_LSUPER, MUI_KEY_F2),
			.kcombo = MUI_GLYPH_COMMAND MUI_GLYPH_F2 },
	{ .title = "Fast: 3.5MHz",
			.uid = FCC('m','h','z','3'),
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_LSUPER, MUI_KEY_F3),
			.kcombo = MUI_GLYPH_COMMAND MUI_GLYPH_F3 },
	{ .title = "-", },
	{ .title = "Stop",
			.kcombo = MUI_GLYPH_COMMAND MUI_GLYPH_F4,
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_LSUPER, MUI_KEY_F4),
			.uid = FCC('s','t','o','p') },
	{ .title = "Running",
			.kcombo = MUI_GLYPH_COMMAND MUI_GLYPH_F5,
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_LSUPER, MUI_KEY_F5),
			.uid = FCC('r','u','n',' ') },
	{ .title = "Step",
			.kcombo = MUI_GLYPH_COMMAND MUI_GLYPH_F6,
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_LSUPER, MUI_KEY_F6),
			.uid = FCC('s','t','e','p') },
	{ .title = "Next",
			.kcombo = MUI_GLYPH_COMMAND MUI_GLYPH_F7,
			.key_equ = MUI_KEY_EQU(MUI_MODIFIER_LSUPER, MUI_KEY_F7),
			.uid = FCC('n','e','x','t') },
	{ },
};

#endif
