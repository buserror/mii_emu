/*
 * mii_sw.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

enum {
	SWKBD 			= 0xc000,
	SW80STOREOFF 	= 0xc000,
	SW80STOREON 	= 0xc001,
	SWRAMRDOFF 		= 0xc002,
	SWRAMRDON 		= 0xc003,
	SWRAMWRTOFF 	= 0xc004,
	SWRAMWRTON 		= 0xc005,
	SWINTCXROMOFF 	= 0xc006,
	SWINTCXROMON 	= 0xc007,
	SWALTPZOFF 		= 0xc008,
	SWALTPZON 		= 0xc009,
	SWSLOTC3ROMOFF 	= 0xc00a,
	SWSLOTC3ROMON 	= 0xc00b,
	SW80COLOFF 		= 0xc00c,
	SW80COLON 		= 0xc00d,
	SWALTCHARSETOFF = 0xc00e,
	SWALTCHARSETON  = 0xc00f,
	SWAKD 			= 0xc010,
	SWBSRBANK2 		= 0xc011,
	SWBSRREADRAM 	= 0xc012,
	SWRAMRD 		= 0xc013,
	SWRAMWRT 		= 0xc014,
	SWINTCXROM 		= 0xc015,
	SWALTPZ 		= 0xc016,
	SWSLOTC3ROM 	= 0xc017,
	SW80STORE 		= 0xc018,
	SWVBL 			= 0xc019,
	SWALTCHARSET 	= 0xc01e,
	SW80COL 		= 0xc01f,
	SWTEXT 			= 0xc01a,
	SWMIXED 		= 0xc01b,
	SWPAGE2 		= 0xc01c,
	SWHIRES 		= 0xc01d,
	SWSPEAKER 		= 0xc030,
	SWTEXTOFF 		= 0xc050,	//  (AKA LORES ON)
	SWTEXTON 		= 0xc051,
	SWMIXEDOFF 		= 0xc052,
	SWMIXEDON 		= 0xc053,
	SWPAGE2OFF 		= 0xc054,
	SWPAGE2ON 		= 0xc055,
	SWHIRESOFF 		= 0xc056,
	SWHIRESON 		= 0xc057,
	// this one is inverted, the ON is the even address
	SWDHIRESON 		= 0xc05e,		// AN3_OFF
	SWDHIRESOFF 	= 0xc05f,		// AN3_ON
	SWAN3 			= 0xc05e,		// AN3 status
	SWAN3_REGISTER 	= 0xc05f,	// AN3 register for video mode
	SWRAMWORKS_BANK	= 0xc073,
	// https://retrocomputing.stackexchange.com/questions/13449/apple-iie-auxiliary-ram-bank-select-register-address-c073-or-c07x
	SWRAMWORKS_ALT1	= 0xc071,
	SWRAMWORKS_ALT5	= 0xc075,
	SWRAMWORKS_ALT7	= 0xc077,
	SWRDDHIRES 		= 0xc07f,
};

/*
 * This is for a bitfield in mii_t that keeps track of the state of the
 * softswitches used for memory mapping and video.
 */
enum {
	B_SW80STORE 	= ( 0),
	B_SWALTCHARSET 	= ( 1),
	B_SW80COL 		= ( 2),
	B_SWTEXT 		= ( 3),
	B_SWMIXED 		= ( 4),
	B_SWPAGE2 		= ( 5),
	B_SWHIRES 		= ( 6),
	B_SWRAMRD 		= ( 7),
	B_SWRAMWRT 		= ( 8),
	B_SWINTCXROM 	= ( 9),
	B_SWALTPZ 		= (10),
	B_SWSLOTC3ROM 	= (11),
	B_BSRWRITE 		= (12),
	B_BSRREAD 		= (13),
	B_BSRPAGE2 		= (14),
	B_BSRPREWRITE 	= (15),
	B_SWDHIRES	 	= (16),
	// this is no 'real' softwitch, but a bit to mention a card has
	// it's secondary rom online in pages c800-cfff
	B_INTC8ROM		= (17),

	M_SW80STORE 	= (1 << B_SW80STORE),
	M_SWALTCHARSET 	= (1 << B_SWALTCHARSET),
	M_SW80COL 		= (1 << B_SW80COL),
	M_SWTEXT 		= (1 << B_SWTEXT),
	M_SWMIXED 		= (1 << B_SWMIXED),
	M_SWPAGE2 		= (1 << B_SWPAGE2),
	M_SWHIRES 		= (1 << B_SWHIRES),
	M_SWRAMRD 		= (1 << B_SWRAMRD),
	M_SWRAMWRT 		= (1 << B_SWRAMWRT),
	M_SWINTCXROM 	= (1 << B_SWINTCXROM),
	M_SWALTPZ 		= (1 << B_SWALTPZ),
	M_SWSLOTC3ROM 	= (1 << B_SWSLOTC3ROM),
	M_BSRWRITE 		= (1 << B_BSRWRITE),
	M_BSRREAD 		= (1 << B_BSRREAD),
	M_BSRPAGE2 		= (1 << B_BSRPAGE2),
	M_BSRPREWRITE 	= (1 << B_BSRPREWRITE),
	M_SWDHIRES	 	= (1 << B_SWDHIRES),
	M_INTC8ROM		= (1 << B_INTC8ROM),
};

#define __unused__ __attribute__((unused))

// unused is to prevent the stupid warnings about unused static stuff
static const char __unused__ *mii_sw_names[] =  {
	"80STORE",
	"ALTCHARSET",
	"80COL",
	"TEXT",
	"MIXED",
	"PAGE2",
	"HIRES",
	"RAMRD",
	"RAMWRT",
	"INTCXROM",
	"ALTPZ",
	"SLOTC3ROM",
	"BSRWRITE",
	"BSRREAD",
	"BSRPAGE2",
	"BSRPREWRITE",
	"DHIRES",
	"INTC8ROM",
	NULL,
} ;

#define SWW_SETSTATE(_bits, _sw, _state) \
	(_bits) = ((_bits) & ~(M_##_sw)) | \
						(!!(_state) << B_##_sw)
#define SWW_GETSTATE(_bits, _sw) \
	(!!((_bits) & M_##_sw))

#define SW_SETSTATE(_mii, _sw, _state) \
	SWW_SETSTATE((_mii)->sw_state, _sw, _state)
#define SW_GETSTATE(_mii, _sw) \
	SWW_GETSTATE((_mii)->sw_state, _sw)

// set bit 8 of a byte to the state of a softswitch
#define SWW_READ(_byte, _bits, _sw) \
	(_byte) = ((_byte) & 0x7f) | (SWW_GETSTATE(_bits, _sw) << 7)
#define SW_READ(_byte, _mii, _sw) \
	SWW_READ(_byte, (_mii)->sw_state, _sw)
