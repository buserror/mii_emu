/*
 * mii.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "mii_65c02.h"
#include "mii_dd.h"
#include "mii_bank.h"
#include "mii_slot.h"
#include "mii_video.h"
#include "mii_speaker.h"
#include "mii_mouse.h"

enum {
	MII_BANK_MAIN = 0,		// main 48K address space
	MII_BANK_BSR, 			// 0xd000 - 0xffff bank switched RAM 16KB
	MII_BANK_BSR_P2,		// 0xd000 - 0xe000 bank switched RAM aux 4KB

	MII_BANK_AUX,			// aux 48K address space (80 cols card)
	MII_BANK_AUX_BSR,		// 0xd000 - 0xffff bank switched RAM aux 16KB
	MII_BANK_AUX_BSR_P2,	// 0xd000 - 0xe000 bank switched RAM aux 4KB (aux bank)

	MII_BANK_ROM,			// 0xc000 - 0xffff 16K ROM
	MII_BANK_CARD_ROM,		// 0xc100 - 0xcfff Card ROM access
	MII_BANK_COUNT,
};

/*
 * A 'trap' is a sequence of 2 special NOPs that are used to trigger
 * a callback. The callback is called with the mii_t * and the trap ID
 */
typedef void (*mii_trap_handler_cb)(
				mii_t * mii,
				uint8_t trap);
typedef struct mii_trap_t {
	uint16_t 	map;
	struct {
		mii_trap_handler_cb cb;
	}		trap[16];
} mii_trap_t;

// state of the emulator
enum {
	MII_RUNNING = 0,	// default
	MII_STOPPED,
	MII_STEP,
	MII_TERMINATE,
};

enum {
	MII_BP_PC		= (1 << 0),	// breakpoint on PC
	MII_BP_W		= (1 << 1),	// breakpoint on write
	MII_BP_R		= (1 << 2),	// breakpoint on read
	MII_BP_HIT		= (1 << 3),	// breakpoint was hit
	MII_BP_SILENT	= (1 << 4),	// don't dump state (used for the 'next' command)
	MII_BP_STICKY	= (1 << 7), // breakpoint is sticky (rearms itself)
};

#define MII_PC_LOG_SIZE		16

/*
 * this keeps track of the last few PC values, for the debugger
 */
typedef struct mii_trace_t {
	uint16_t	log[MII_PC_LOG_SIZE];
	uint8_t		idx;
	// when in MII_STEP, do not fall back to MII_STOPPED until these
	// run out
	uint32_t 	step_inst;
} mii_trace_t;

/*
 * principal emulator state, for a faceless emulation
 */
typedef struct mii_t {
	unsigned int	state;
	__uint128_t		cycles;
	/* this is the video frame/VBL rate vs 60hz, default to 1.0 */
	float			speed;
	float			speed_current; // calculated speed
	mii_cpu_t 		cpu;
	mii_cpu_state_t	cpu_state;
	/*
	 * bank index for each memory page number, this is recalculated
	 * everytime a soft switch is triggered
	 */
	struct  		{
		uint8_t read : 4, write : 4;
	} 				mem[256];
	int 			mem_dirty;	// recalculate mem[] on next access
	uint32_t 		sw_state;	// B_SW* bitfield
	mii_trace_t		trace;
	int				trace_cpu;
	mii_trap_t		trap;
	/*
	 * Used for debugging only
	 */
	struct {
		uint16_t		bp_map;
		struct {
			uint32_t		kind : 8,
							addr : 16,
							size : 8,
							silent : 1;
		}			bp[16];
	}				debug;
	mii_bank_t		bank[MII_BANK_COUNT];
	// the page c000 can have individual callbacks to override/supplement
	// existing default behaviour. This is currently used for the titan
	// accelerator 'card'
	mii_bank_access_t * soft_switches_override;
	mii_slot_t		slot[7];
	mii_video_t		video;
	mii_speaker_t	speaker;
	mii_mouse_t		mouse;
	mii_dd_system_t	dd;
} mii_t;

enum {
	MII_INIT_NSC 		= (1 << 0), // Install no slot clock
	MII_INIT_TITAN		= (1 << 1), // Install Titan 'card'
	MII_INIT_DEFAULT 	= MII_INIT_NSC,
};

/*
 * Call this first, to initialize the emulator state
 * This doesn't initializes any driver.
 */
void
mii_init(
		mii_t *mii );

/*
 * Call this to prepare the emulator, instantiate and install drivers
 * etc. Presumably after you have used mii_argv_parse or loaded a config
 * file to set up the drivers.
 * flags is a combination of MII_INIT_*
 */
void
mii_prepare(
		mii_t *mii,
		uint32_t flags );
/*
 * Stop the emulator, dispose of everything, free memory etc.
 */
void
mii_dispose(
		mii_t *mii );

/*
 * Parses arguments until in finds one that isn't for mii, and returns
 * the index of that argument in *index.
 * Return value is 0 if there's an argument that wasn't handled or 1
 * if all the arguments were parsed (and *index == argc)
 * mii parameter is the state AFTER mii_init() has been called.
 *
 * ioFlags is a combination of MII_INIT_*, it will be updated with the
 * flags found in the arguments. Pass this to mii_prepare()
 */
int
mii_argv_parse(
		mii_t *mii,
		int argc,
		const char *argv[],
		int *index,
		uint32_t *ioFlags );
/*
 * Locate driver_name, and attempt to register it with slot_id slot.
 * Returns 0 on success, -1 on failure
 */
int
mii_slot_drv_register(
		mii_t *mii,
		uint8_t slot_id,
		const char *driver_name);
/* returns the driver registered for slot slot_id (or NULL) */
mii_slot_drv_t *
mii_slot_drv_get(
		mii_t *mii,
		uint8_t slot_id);

/*
 * Reset the emulator, cold reset if cold is true, otherwise warm reset
 */
void
mii_reset(
		mii_t *mii,
		bool cold);
/* Execute one instruction, respecting breakpoints, runs the video code,
 * Check for traps and other associated debug stuff.
 */
void
mii_run(
		mii_t *mii);
// this one is thread safe, there's a FIFO behind it.
void
mii_keypress(
		mii_t *mii,
		uint8_t key);

/* read a byte as the processor would (ex softwitches!), this will
 * respect the status of slot ROMs, language card, 80 columns etc */
uint8_t
mii_read_one(
		mii_t *mii,
		uint16_t addr);
/* write a byte as the processor would (ex softwitches!), this will
 * respect the status of slot ROMs, language card, 80 columns etc */
void
mii_write_one(
		mii_t *mii,
		uint16_t addr,
		uint8_t d);
/* read a word as the processor would (ex softwitches!), this will
 * respect the status of slot ROMs, language card, 80 columns etc */
uint16_t
mii_read_word(
		mii_t *mii,
		uint16_t addr);
/* write a word as the processor would (ex softwitches!), this will
 * respect the status of slot ROMs, language card, 80 columns etc */
void
mii_write_word(
		mii_t *mii,
		uint16_t addr,
		uint16_t w);

/* lower level call to access memory -- this one can trigger softswitches
 * if specified. Otherwise behaves as the previous ones, one byte at a time
 */
void
mii_mem_access(
		mii_t *mii,
		uint16_t addr,
		uint8_t * byte,
		bool write,
		bool do_sw);
/* register a callback to call when a specific soft switches is hit,
 * this allows overriding/supplementing/tracing access to sw.
 */
void
mii_set_sw_override(
		mii_t *mii,
		uint16_t sw_addr,
		mii_bank_access_cb cb,
		void *param);

void
mii_dump_trace_state(
	mii_t *mii);
void
mii_dump_run_trace(
	mii_t *mii);

extern mii_slot_drv_t * mii_slot_drv_list;

#define MI_DRIVER_REGISTER(_mii_driver)\
		__attribute__((constructor,unused)) \
		static void _mii_register_##_mii_driver() { \
			_mii_driver.next = mii_slot_drv_list; \
			mii_slot_drv_list = &_mii_driver; \
		}

#define MII_TRAP 0xdbfb
/*
 * Request a trap ID for the given callback. Calling code is responsible
 * for setting up the trap using the 2 magic NOPs in sequence.
 * See mii_smartport.c for an example.
 */
uint8_t
mii_register_trap(
		mii_t *mii,
		mii_trap_handler_cb cb);
