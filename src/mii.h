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
#include "mii_audio.h"
#include "mii_speaker.h"
#include "mii_mouse.h"
#include "mii_analog.h"
#include "mii_vcd.h"
#include "mii_rom.h"

#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

enum {
	MII_EMU_IIEE = 0,
	MII_EMU_IIC,
};

enum {
	MII_BANK_MAIN = 0,		// main 48K address space
	MII_BANK_BSR, 			// 0xd000 - 0xffff bank switched RAM 16KB
	MII_BANK_BSR_P2,		// 0xd000 - 0xe000 bank switched RAM aux 4KB
	// this one is the fixed one, used by video
	MII_BANK_AUX_BASE,		// aux 48K address space (80 cols card)
	// these one can 'move' in the block of ramworks ram
	MII_BANK_AUX,			// aux 48K address space (80 cols card)
	MII_BANK_AUX_BSR,		// 0xd000 - 0xffff bank switched RAM aux 16KB
	MII_BANK_AUX_BSR_P2,	// 0xd000 - 0xe000 bank switched RAM aux 4KB (aux bank)

	MII_BANK_ROM,			// 0xc000 - 0xffff 16K ROM
	MII_BANK_CARD_ROM,		// 0xc100 - 0xcfff Card ROM access
	MII_BANK_SW,			// 0xc000 - 0xc0ff Softswitches
	MII_BANK_COUNT,
};

/*
 * A 'trap' is a sequence of 2 special NOPs that are used to trigger
 * a callback. The callback is called with the mii_t * and the trap ID,
 *
 * This is used extensively by the smartport driver to jump back to the
 * emulator code when a disk access is needed.
 */
typedef void (*mii_trap_handler_cb)(
				struct mii_t * mii,
				uint8_t trap);
typedef struct mii_trap_t {
	uint16_t 	map;
	struct {
		mii_trap_handler_cb cb;
	}		trap[16];
} mii_trap_t;

// state of the emulator
enum {
	MII_INIT = 0,
	MII_RUNNING,	// default
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

typedef uint64_t (*mii_timer_p)(
				struct mii_t * mii,
				void * param );

#define MII_SPEED_NTSC 	1.0227271429 	// 14.31818 MHz / 14
#define MII_SPEED_PAL 	1.0178571429	// 14.25 MHz / 14
#define MII_SPEED_TITAN 3.58

/*
 * principal emulator state, for a faceless emulation
 */
typedef struct mii_t {
	// this is the 'emulation' type, IIEE or IIC [currently only IIe works]
	uint 			emu; // MII_EMU_*
	mii_cpu_t 		cpu;
	mii_cpu_state_t	cpu_state;
	/* this is the CPU speed, default to MII_SPEED_NTSC */
	float			speed;
	unsigned int	state;
	/*
	 * These are used as MUX for IRQ requests from drivers. Each driver
	 * can request an IRQ number, and 'raise' and 'clear' it, and the
	 * CPU IRQ line will be set if any of them are raised, and cleared
	 * when none are raised.
	 *
	 * This fixes the problem of multiple drivers trying to raise the
	 * only IRQ line on the CPU. Typically if you have the mouse card and
	 * the serial card, or the mockingboard.
	 */
	struct {
		uint16_t 		map;
		uint16_t 		raised;
		struct {
			const char * name;
			uint8_t 	count;
		}			irq[16];
	}				irq;
	/*
	 * These are 'cycle timers' -- they count down from a set value,
	 * and stop at 0 (or possibly -1 or -2, depending on the instructions)
	 * and call the callback (if present).
	 * The callback returns the number of cycles to wait until the next
	 * call.
	 */
	struct {
		uint64_t 	map;
#if MII_65C02_DIRECT_ACCESS
		uint8_t		last_cycle;
#endif
		struct {
			mii_timer_p 		cb;
			void *				param;
			int64_t 			when;
			const char *		name; // debug
		} timers[64];
	}				timer;
	/*
	 * bank index for each memory page number, this is recalculated
	 * everytime a MMU soft switch is triggered
	 */
	struct  		{
		union {
			struct {
				uint8_t write : 4, read : 4;
			};
			uint8_t 	both;
		};
	} 				mem[256];
	int 			mem_dirty;	// recalculate mem[] on next access
	/*
	 * RAMWORKS card emulation, this is a 16MB address space, with 128
	 * possible 64KB banks. The 'avail' bitfield marks the banks that
	 * are 'possible' (depending on what size of RAMWORKS card is installed).
	 * The 'bank' array is a pointer to the actual memory block.
	 *
	 * These memory blocks replace the main AUX bank when a register is set.
	 */
	struct {
		unsigned __int128	avail;
		uint8_t * 			bank[128];
	}				ramworks;
	/*
	 * These are the 'real' state of the soft switches, as opposed to the
	 * value stores in the C000 page. This is made so they can be easily
	 * manipulated, copied, and restored... use the macros in mii_sw.h
	 */
	uint32_t 		sw_state;	// B_SW* bitfield
	mii_trace_t		trace;
	int				trace_cpu;
	mii_trap_t		trap;
	mii_signal_pool_t sig_pool;	// vcd support
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
	/*
	 * The page c000 can have individual callbacks to override/supplement
	 * existing default behaviour. This is currently used for the titan
	 * accelerator 'card'
	 */
	mii_bank_access_t * soft_switches_override;
	mii_slot_t		slot[7];

	/*
	 * These are all the state of the various subsystems.
	 */
	mii_rom_t *		rom;
	mii_video_t		video;
	mii_speaker_t	speaker;
	mii_mouse_t		mouse;
	mii_dd_system_t	dd;
	mii_analog_t	analog;
	mii_audio_sink_t audio;
} mii_t;

enum {
	MII_INIT_NSC 			= (1 << 0), // Install no slot clock
	MII_INIT_TITAN			= (1 << 1), // Install Titan 'card'
	MII_INIT_SILENT			= (1 << 2), // No audio, ever
	MII_INIT_MOCKINGBOARD	= (1 << 3), // Install mockingboard
	// number of 256KB banks added to the ramworks
//	MII_INIT_RAMWORKS_BIT	= 4, // bit 4 in flags. Can be up to 12

	MII_INIT_FULLSCREEN		= (1 << 8),
	MII_INIT_HIDE_UI		= (1 << 9),

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

/* register a cycle timer. cb will be called when (at least) when
 * cycles have been spent -- the callback returns how many it should
 * spend until the next call */
uint8_t
mii_timer_register(
		mii_t *mii,
		mii_timer_p cb, // this is optional, can be NULL
		void *param,
		int64_t when,
		const char *name);
/* return the cycles left for timer_id (can be negative !)*/
int64_t
mii_timer_get(
		mii_t *mii,
		uint8_t timer_id);
int
mii_timer_set(
		mii_t *mii,
		uint8_t timer_id,
		int64_t when);

uint8_t
mii_irq_register(
		mii_t *mii,
		const char *name );
void
mii_irq_unregister(
		mii_t *mii,
		uint8_t irq_id );
void
mii_irq_raise(
		mii_t *mii,
		uint8_t irq_id );
void
mii_irq_clear(
		mii_t *mii,
		uint8_t irq_id );

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

#define MII_TRAP 0xebfb
/*
 * Request a trap ID for the given callback. Calling code is responsible
 * for setting up the trap using the 2 magic NOPs in sequence.
 * See mii_smartport.c for an example.
 */
uint8_t
mii_register_trap(
		mii_t *mii,
		mii_trap_handler_cb cb);

/*
 * this is used if libmish is active, to register the 'mii' commands
 */

#define MII_MISH_KIND MISH_FCC('m','i','i',' ')
#define MII_MISH(_name,_cmd) \
	MISH_CMD_REGISTER_KIND(_name, _cmd, 0, MII_MISH_KIND)


void
mii_cpu_step(
		mii_t *mii,
		uint32_t count );
void
mii_cpu_next(
		mii_t *mii);
